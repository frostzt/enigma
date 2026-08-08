#include "enigmadb/log.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <vector>

#include "enigmadb/log_sinks.h"

namespace enigmadb {

namespace {
// How long the fatal path is willing to wait on an async worker before it
// dumps the crash ring and aborts anyway.
constexpr std::chrono::milliseconds kFatalDrainTimeout{2000};
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
    sinks_.push_back(std::make_shared<ConsoleSink>());
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
    raw_sinks.push_back(std::move(ring));
    sinks_.clear();
    if (config.async) {
        sinks_.push_back(std::make_shared<AsyncSink>(
            std::move(raw_sinks), config.queue_capacity, config.overflow));
    } else {
        sinks_ = std::move(raw_sinks);
    }

    if (config.enable_crash_handler) {
        install_crash_handlers();
    }

    std::atexit([]() { Logger::instance().shutdown(); });
}

void Logger::shutdown() {
    if (shutdown_.exchange(true)) return;

    for (auto& sink : sinks_) {
        if (auto async = std::dynamic_pointer_cast<AsyncSink>(sink)) {
            async->stop();
        }
        sink->flush();
    }
}

void Logger::dispatch(LogRecord record) {
    if (shutdown_.load(std::memory_order_relaxed)) return;

    for (auto& sink : sinks_) {
        if (record.level >= sink->level()) {
            sink->submit(record);
        }
    }

    if (record.level == Level::Fatal) {
        // The crash ring usually sits behind the async worker, so this record
        // may still be queued. Give the worker a bounded window to hand it
        // over; a blocked sink must not keep us from dumping and aborting.
        for (auto& sink : sinks_) {
            if (auto async = std::dynamic_pointer_cast<AsyncSink>(sink)) {
                async->drain_for(kFatalDrainTimeout);
            } else {
                sink->flush();
            }
        }

        dump_ring_buffer_to_stderr();
        std::abort();
    }
}

void Logger::add_sink(std::shared_ptr<LogSink> sink) {
    sinks_.push_back(std::move(sink));
}

void Logger::set_level(Category cat, Level lvl) noexcept {
    active_levels_[static_cast<size_t>(cat)].store(lvl,
                                                   std::memory_order_relaxed);
}

void Logger::dump_ring_buffer_to_stderr() {
    if (ring_sink_) {
        ring_sink_->signal_safe_dump(STDERR_FILENO);
    }
}

}  // namespace enigmadb
