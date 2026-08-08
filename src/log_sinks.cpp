#include "enigmadb/log_sinks.h"

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <format>
#include <iostream>
#include <mutex>

#include "enigmadb/log.h"

namespace enigmadb {

static const char* level_to_string(Level lvl) {
    // clang-format off
    switch (lvl) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
        default:           return "UNKNOW";
    }
    // clang-format on
}

static const char* category_to_string(Category cat) {
    // clang-format off
    switch (cat) {
        case Category::General:   return "general";
        case Category::Wal:       return "wal";
        case Category::SSTable:   return "sstable";
        case Category::Memtable:  return "memtable";
        case Category::Compaction: return "compaction";
        case Category::IO:        return "io";
        default:                  return "unknown";
    }
    // clang-format on
}

std::string LogSink::format_record(const LogRecord& rec) {
    static thread_local std::time_t last_sec = 0;
    static thread_local char time_str[20] = {0};

    auto tp_sec = std::chrono::time_point_cast<std::chrono::seconds>(rec.ts);
    std::time_t sec = std::chrono::system_clock::to_time_t(tp_sec);

    if (sec != last_sec) {
        std::tm tm_buf;
        localtime_r(&sec, &tm_buf);
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
        last_sec = sec;
    }

    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(rec.ts - tp_sec)
            .count();

    return std::format("[{}.{:03d}] [{}] [{}] [TID:{}] {}:{} {}: {}\n",
                       time_str, ms, level_to_string(rec.level),
                       category_to_string(rec.category), rec.tid, rec.file,
                       rec.line, rec.func, rec.message);
}

// ---------------- ConsoleSink ----------------
ConsoleSink::ConsoleSink(bool) : is_tty_(::isatty(STDERR_FILENO)) {}

void ConsoleSink::submit(const LogRecord& record) {
    std::string formatted = format_record(record);
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_tty_) {
        const char* color = "";
        switch (record.level) {
            case Level::Warn:
                color = "\033[33m";
                break;
            case Level::Error:
                color = "\033[31m";
                break;
            case Level::Fatal:
                color = "\033[1;31m";
                break;
            default:
                break;
        }
        if (*color) {
            std::cerr << color << formatted << "\033[0m";
            return;
        }
    }
    std::cerr << formatted;
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr.flush();
}

// ---------------- FileSink ----------------
FileSink::FileSink(const std::string& filepath) {
    file_.open(filepath, std::ios::out | std::ios::app);
}

FileSink::~FileSink() {
    if (file_.is_open()) file_.close();
}

void FileSink::submit(const LogRecord& record) {
    std::string formatted = format_record(record);
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << formatted;
    }
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.flush();
}

// ---------------- RingBufferSink ----------------
RingBufferSink::RingBufferSink(size_t slots, size_t slot_bytes)
    : slots_cap_(slots == 0 ? 1024 : slots),
      slot_bytes_(slot_bytes < 64 ? 512 : slot_bytes) {
    ring_ = new Slot[slots_cap_];
    for (size_t i = 0; i < slots_cap_; ++i) {
        ring_[i].data = new char[slot_bytes_];
    }
}

RingBufferSink::~RingBufferSink() {
    for (size_t i = 0; i < slots_cap_; ++i) {
        delete[] ring_[i].data;
    }
    delete[] ring_;
}

void RingBufferSink::submit(const LogRecord& record) {
    std::string formatted = format_record(record);
    uint64_t seq = write_seq_.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = ring_[seq % slots_cap_];

    size_t copy_len = std::min(formatted.size(), slot_bytes_ - 1);
    std::memcpy(slot.data, formatted.data(), copy_len);
    slot.data[copy_len] = '\0';
    slot.len.store(static_cast<uint32_t>(copy_len), std::memory_order_release);
}

void RingBufferSink::signal_safe_dump(int fd) const noexcept {
    const char* header = "\n--- CRASH RING BUFFER DUMP ---\n";
    ::write(fd, header, std::strlen(header));

    uint64_t curr_seq = write_seq_.load(std::memory_order_relaxed);
    uint64_t start = (curr_seq > slots_cap_) ? (curr_seq - slots_cap_) : 0;

    for (uint64_t i = start; i < curr_seq; ++i) {
        const Slot& slot = ring_[i % slots_cap_];
        uint32_t len = slot.len.load(std::memory_order_acquire);
        if (len > 0) {
            ::write(fd, slot.data, len);
        }
    }
}

// ---------------- AsyncSink ----------------
AsyncSink::AsyncSink(std::vector<std::shared_ptr<LogSink>> sinks,
                     size_t capacity, OverflowPolicy policy)
    : sinks_(std::move(sinks)),
      capacity_(capacity == 0 ? 8192 : capacity),
      policy_(policy),
      queue_(capacity == 0 ? 8192 : capacity) {
    worker_ = std::thread(&AsyncSink::worker_loop, this);
}

AsyncSink::~AsyncSink() { stop(); }

void AsyncSink::submit(const LogRecord& record) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (size_ == capacity_) {
        bool should_block = (policy_ == OverflowPolicy::Block) ||
                            (policy_ == OverflowPolicy::BlockOnWarnPlus &&
                             record.level >= Level::Warn);

        if (should_block) {
            cv_produce_.wait(lock,
                             [this] { return size_ < capacity_ || !running_; });
        } else {
            dropped_records_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // Still full means the wait above was released by stop(), not by space
    // opening up; enqueueing anyway would overwrite a live slot.
    if (size_ == capacity_) {
        dropped_records_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    queue_[tail_] = record;
    tail_ = (tail_ + 1) % capacity_;
    size_++;
    cv_consume_.notify_one();
}

void AsyncSink::flush() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_produce_.wait(lock, [this] { return size_ == 0 && inflight_ == 0; });
    }
    for (auto& sink : sinks_) sink->flush();
}

void AsyncSink::stop() {
    if (!running_.exchange(false)) return;
    cv_consume_.notify_all();
    cv_produce_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AsyncSink::worker_loop() {
    std::vector<LogRecord> batch;
    batch.reserve(256);

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_ && size_ == 0) break;

            cv_consume_.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return size_ > 0 || !running_; });

            while (size_ > 0 && batch.size() < 256) {
                batch.push_back(std::move(queue_[head_]));
                head_ = (head_ + 1) % capacity_;
                size_--;
            }
            // Batch is off the queue but not written yet, so flushers must
            // keep waiting until the downstream sinks have actually seen it.
            inflight_ = batch.size();
            if (!batch.empty()) cv_produce_.notify_all();
        }

        for (const auto& rec : batch) {
            for (auto& sink : sinks_) {
                if (rec.level >= sink->level()) sink->submit(rec);
            }
        }
        batch.clear();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            inflight_ = 0;
        }
        cv_produce_.notify_all();
    }
}

}  // namespace enigmadb
