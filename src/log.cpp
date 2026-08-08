#include "enigmadb/log.h"

#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <vector>

#include "enigmadb/log_sinks.h"

namespace enigmadb {

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

void Logger::init(const LogConfig& config) {
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

    raw_sinks.push_back(std::make_shared<RingBufferSink>(
        config.ring_slots, config.ring_slot_bytes));

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
        dump_ring_buffer_to_stderr();
        shutdown();
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
    for (auto& sink : sinks_) {
        if (auto ring = std::dynamic_pointer_cast<RingBufferSink>(sink)) {
            ring->signal_safe_dump(STDERR_FILENO);
            return;
        }
    }
}

}  // namespace enigmadb
