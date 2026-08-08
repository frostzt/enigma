#include "enigmadb/log.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "enigmadb/log_sinks.h"

namespace enigmadb {

namespace {
// How long the fatal path is willing to wait for sinks to flush before it
// dumps the crash ring and aborts anyway.
constexpr std::chrono::milliseconds kFatalFlushTimeout{2000};

// Runs `fn` on a detached thread and waits at most `timeout` for it to finish.
// Returns false if it did not. Abandoning the thread is deliberate: the only
// caller aborts right after, and abort() runs no destructors that a stranded
// thread could race with.
bool run_with_deadline(std::chrono::milliseconds timeout,
                       std::function<void()> fn) {
    try {
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> finished = done->get_future();

        std::thread([done, fn = std::move(fn)]() mutable {
            try {
                fn();
            } catch (...) {
                // A failing sink must not take the process down before the
                // crash dump; the deadline handles a hung one.
            }
            done->set_value();
        }).detach();

        return finished.wait_for(timeout) == std::future_status::ready;
    } catch (...) {
        // Could not even spawn the helper. Treat it as a missed deadline so
        // the caller moves straight on to the dump.
        return false;
    }
}
}  // namespace

LogConfig::LogConfig() { category_levels.fill(default_level); }

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    LogConfig defaultConfig;
    for (size_t i = 0; i < static_cast<size_t>(Category::_Count); ++i) {
        active_levels_[i].store(defaultConfig.default_level,
                                std::memory_order_relaxed);
    }
    auto initial = std::make_shared<SinkList>();
    initial->push_back(std::make_shared<ConsoleSink>());
    sinks_ = std::move(initial);
}

Logger::~Logger() { shutdown(); }

void Logger::init(LogConfig& config) {
    if (initialized_.exchange(true)) return;

    for (size_t i = 0; i < static_cast<size_t>(Category::_Count); ++i) {
        active_levels_[i].store(config.category_levels[i],
                                std::memory_order_relaxed);
    }

    std::vector<std::shared_ptr<LogSink>> raw_sinks;

    if (config.console) {
        raw_sinks.push_back(std::make_shared<ConsoleSink>());
    }

    if (config.file_path) {
        raw_sinks.push_back(std::make_shared<FileSink>(*config.file_path));
    }

    if (config.ring_slots == 0) {
        config.ring_slots = 1024;
    }
    if (config.ring_slot_bytes < 64) {
        config.ring_slot_bytes = 512;
    }
    if (config.async && config.queue_capacity == 0) {
        config.queue_capacity = 8192;
    }

    auto ring = std::make_shared<RingBufferSink>(config.ring_slots,
                                                 config.ring_slot_bytes);
    ring_sink_ = ring;
    // Publish for the dump only once the ring is fully constructed and owned.
    ring_ptr_.store(ring.get(), std::memory_order_release);
    raw_sinks.push_back(std::move(ring));

    auto next = std::make_shared<SinkList>();
    if (config.async) {
        next->push_back(std::make_shared<AsyncSink>(
            std::move(raw_sinks), config.queue_capacity, config.overflow));
    } else {
        *next = std::move(raw_sinks);
    }
    {
        std::unique_lock<std::shared_mutex> lock(sinks_mutex_);
        sinks_ = std::move(next);
    }

    if (config.enable_crash_handler) {
        install_crash_handlers();
    }

    std::atexit([]() { Logger::instance().shutdown(); });
}

void Logger::shutdown() {
    if (shutdown_.exchange(true)) return;

    auto sinks = snapshot_sinks();
    if (!sinks) return;

    for (auto& sink : *sinks) {
        if (auto async = std::dynamic_pointer_cast<AsyncSink>(sink)) {
            async->stop();
        }
        sink->flush();
    }
}

void Logger::dispatch(LogRecord record) {
    if (shutdown_.load(std::memory_order_relaxed) &&
        record.level != Level::Fatal) {
        return;
    }

    // Work from a snapshot: init() and add_sink() can swap the list at any
    // moment, and our reference keeps this one alive for the whole dispatch.
    auto sinks = snapshot_sinks();

    if (sinks) {
        for (auto& sink : *sinks) {
            if (record.level >= sink->level()) {
                sink->submit(record);
            }
        }
    }

    if (record.level == Level::Fatal) {
        // The crash ring usually sits behind the async worker, so this record
        // may still be queued; flushing hands it over. Every step of that can
        // block forever though -- a worker that never drains, a console or
        // file write that never returns -- so it runs under a deadline. The
        // ring dump and abort() must happen either way.
        run_with_deadline(kFatalFlushTimeout, [sinks] {
            if (!sinks) return;
            for (auto& sink : *sinks) sink->flush();
        });

        dump_ring_buffer_to_stderr();
        std::abort();
    }
}

std::shared_ptr<const Logger::SinkList> Logger::snapshot_sinks() const {
    std::shared_lock<std::shared_mutex> lock(sinks_mutex_);
    return sinks_;
}

void Logger::add_sink(std::shared_ptr<LogSink> sink) {
    std::unique_lock<std::shared_mutex> lock(sinks_mutex_);
    auto next = sinks_ ? std::make_shared<SinkList>(*sinks_)
                       : std::make_shared<SinkList>();
    next->push_back(std::move(sink));
    sinks_ = std::move(next);
}

void Logger::set_level(Category cat, Level lvl) noexcept {
    active_levels_[static_cast<size_t>(cat)].store(lvl,
                                                   std::memory_order_relaxed);
}

void Logger::dump_ring_buffer_to_stderr() {
    // Deliberately not ring_sink_: this runs from crash signal handlers, where
    // reading a shared_ptr races init() and is not async-signal-safe.
    if (auto* ring = ring_ptr_.load(std::memory_order_acquire)) {
        ring->signal_safe_dump(STDERR_FILENO);
    }
}

}  // namespace enigmadb
