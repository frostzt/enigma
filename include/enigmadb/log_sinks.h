#ifndef ENIGMA_DB_LOG_SINK_H
#define ENIGMA_DB_LOG_SINK_H

#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "log.h"

namespace enigmadb {

class LogSink {
   public:
    virtual ~LogSink() = default;
    virtual void submit(const LogRecord& record) = 0;
    virtual void flush() = 0;

    void set_level(Level lvl) noexcept {
        sink_level_.store(lvl, std::memory_order_relaxed);
    }

    [[nodiscard]] Level level() const noexcept {
        return sink_level_.load(std::memory_order_relaxed);
    }

   protected:
    static std::string format_record(const LogRecord& record);

   private:
    std::atomic<Level> sink_level_{Level::Trace};
};

class ConsoleSink : public LogSink {
   public:
    explicit ConsoleSink(bool use_color = true);
    void submit(const LogRecord& record) override;
    void flush() override;

   private:
    bool is_tty_;
    std::mutex mutex_;
};

class FileSink : public LogSink {
   public:
    explicit FileSink(const std::string& filepath);
    ~FileSink() override;
    void submit(const LogRecord& record) override;
    void flush() override;

   private:
    std::ofstream file_;
    std::mutex mutex_;
};

class RingBufferSink : public LogSink {
   public:
    RingBufferSink(size_t slots = 1024, size_t slot_bytes = 512);
    ~RingBufferSink() override;

    void submit(const LogRecord& record) override;
    void flush() override {}

    void signal_safe_dump(int fd) const noexcept;

   private:
    struct Slot {
        std::atomic<uint32_t> len{0};
        char* data{nullptr};
    };

    size_t slots_cap_;
    size_t slot_bytes_;
    Slot* ring_;
    std::atomic<uint64_t> write_seq_{0};
};

class AsyncSink : public LogSink {
   public:
    AsyncSink(std::vector<std::shared_ptr<LogSink>> sinks,
              size_t capacity = 8192,
              OverflowPolicy policy = OverflowPolicy::BlockOnWarnPlus);
    ~AsyncSink() override;

    void submit(const LogRecord& record) override;
    // Waits for the queue to drain, then flushes the downstream sinks. Both
    // halves can block indefinitely on a wedged sink, so callers that must not
    // hang (the fatal path) are responsible for bounding the call.
    void flush() override;
    void stop();

   private:
    void worker_loop();

    std::vector<std::shared_ptr<LogSink>> sinks_;
    size_t capacity_;
    OverflowPolicy policy_;

    std::vector<LogRecord> queue_;
    size_t head_{0};
    size_t tail_{0};
    size_t size_{0};
    // Records the worker pulled off the queue but has not written out yet.
    size_t inflight_{0};

    std::mutex mutex_;
    std::condition_variable cv_produce_;
    std::condition_variable cv_consume_;
    std::atomic<bool> running_{true};
    std::thread worker_;
    std::atomic<uint64_t> dropped_records_{0};
};

}  // namespace enigmadb

#endif  // ENIGMA_DB_LOG_SINK_H
