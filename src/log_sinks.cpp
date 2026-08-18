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

// Largest record a crash dump will emit. The buffer it is copied through lives
// on the stack of whatever thread is crashing, so it stays small; records from
// a ring configured with larger slots are truncated to this.
static constexpr size_t kDumpCopyMax = 1024;

// A dump runs concurrently with live writers, so slot payloads move a word at a
// time through std::atomic_ref instead of memcpy. Relaxed ordering emits the
// same loads and stores a memcpy would -- the slot's generation counter does
// the publishing -- but the overlap is defined behaviour rather than a race.
static_assert(std::atomic_ref<uint64_t>::is_always_lock_free,
              "ring dumps run from signal handlers and must not take a lock");

static void store_slot_bytes(uint64_t* dst, const char* src, size_t len) {
    for (size_t w = 0, done = 0; done < len; ++w, done += 8) {
        uint64_t word = 0;
        std::memcpy(&word, src + done, std::min<size_t>(8, len - done));
        std::atomic_ref<uint64_t>(dst[w]).store(word, std::memory_order_relaxed);
    }
}

static void load_slot_bytes(char* dst, uint64_t* src, size_t len) noexcept {
    for (size_t w = 0, done = 0; done < len; ++w, done += 8) {
        uint64_t word = std::atomic_ref<uint64_t>(src[w]).load(std::memory_order_relaxed);
        std::memcpy(dst + done, &word, std::min<size_t>(8, len - done));
    }
}

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

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(rec.ts - tp_sec).count();

    return std::format("[{}.{:03d}] [{}] [{}] [TID:{}] {}:{} {}: {}\n", time_str, ms, level_to_string(rec.level),
                       category_to_string(rec.category), rec.tid, rec.file, rec.line, rec.func, rec.message);
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
FileSink::FileSink(const std::string& filepath) { file_.open(filepath, std::ios::out | std::ios::app); }

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
      slot_bytes_(slot_bytes < 64 ? 512 : slot_bytes),
      slot_words_((slot_bytes_ + 7) / 8) {
    ring_ = new Slot[slots_cap_];
    for (size_t i = 0; i < slots_cap_; ++i) {
        ring_[i].data = new uint64_t[slot_words_]();
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

    // Enough traffic to wrap the whole ring can arrive while we are copying, in
    // which case the writer that laps us targets this same slot. Take exclusive
    // ownership first: whoever wins the claim writes the payload alone, and the
    // loser drops its record rather than splicing its bytes into ours.
    uint64_t cur = slot.gen.load(std::memory_order_acquire);
    if (cur & kWritingFlag) return;  // still owned by a writer we lapped
    if (!slot.gen.compare_exchange_strong(cur, seq | kWritingFlag, std::memory_order_acq_rel,
                                          std::memory_order_relaxed)) {
        return;  // lost the claim to a concurrent writer
    }

    // The claim marked the slot unreadable, so a dump skips it until we publish
    // the sequence number below.
    size_t copy_len = std::min(formatted.size(), slot_bytes_);
    store_slot_bytes(slot.data, formatted.data(), copy_len);
    slot.len.store(static_cast<uint32_t>(copy_len), std::memory_order_relaxed);
    slot.gen.store(seq, std::memory_order_release);
}

void RingBufferSink::signal_safe_dump(int fd) const noexcept {
    const char* header = "\n--- CRASH RING BUFFER DUMP ---\n";
    ::write(fd, header, std::strlen(header));

    char buf[kDumpCopyMax];

    uint64_t curr_seq = write_seq_.load(std::memory_order_relaxed);
    uint64_t start = (curr_seq > slots_cap_) ? (curr_seq - slots_cap_) : 0;

    for (uint64_t i = start; i < curr_seq; ++i) {
        const Slot& slot = ring_[i % slots_cap_];

        // Whatever this slot holds, not necessarily record i: a writer that
        // lost a claim leaves its sequence number unused, and the older record
        // still sitting here is worth dumping. Acquire pairs with the writer's
        // release below, so a published generation means its payload is
        // visible too.
        uint64_t gen = slot.gen.load(std::memory_order_acquire);
        if (gen == kSlotFree || (gen & kWritingFlag)) continue;

        uint32_t len = slot.len.load(std::memory_order_relaxed);
        if (len == 0) continue;
        bool truncated = len > sizeof(buf);
        if (truncated) len = sizeof(buf);
        load_slot_bytes(buf, slot.data, len);
        // Records carry their own newline; keep one when cutting a long record
        // short so the dump stays one record per line.
        if (truncated) buf[len - 1] = '\n';

        // Re-check: a writer that claimed the slot mid-copy leaves us holding
        // halves of two records, so drop them instead of printing garbage.
        if (slot.gen.load(std::memory_order_acquire) != gen) continue;

        ::write(fd, buf, len);
    }
}

// ---------------- AsyncSink ----------------
AsyncSink::AsyncSink(std::vector<std::shared_ptr<LogSink>> sinks, size_t capacity, OverflowPolicy policy)
    : sinks_(std::move(sinks)),
      capacity_(capacity == 0 ? 8192 : capacity),
      policy_(policy),
      queue_(capacity == 0 ? 8192 : capacity) {
    worker_ = std::thread(&AsyncSink::worker_loop, this);
}

AsyncSink::~AsyncSink() { stop(); }

void AsyncSink::submit(const LogRecord& record) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Checked under the lock, never before it. stop() clears running_ before
    // joining the worker and draining what is left, all of which needs the
    // mutex; a check made outside it can go stale while we wait for the lock,
    // and the record would then land in a queue nobody is left to drain.
    // Delivering inline is what keeps a fatal record from being swallowed.
    if (!running_.load(std::memory_order_relaxed)) {
        lock.unlock();
        deliver(record);
        return;
    }

    if (size_ == capacity_) {
        bool should_block = (policy_ == OverflowPolicy::Block) ||
                            (policy_ == OverflowPolicy::BlockOnWarnPlus && record.level >= Level::Warn);

        if (should_block) {
            cv_produce_.wait(lock, [this] { return size_ < capacity_ || !running_; });

            // Woken by stop() rather than by space opening up: enqueueing now
            // would overwrite a live slot and leave the record undelivered.
            if (size_ == capacity_ || !running_.load(std::memory_order_relaxed)) {
                lock.unlock();
                deliver(record);
                return;
            }
        } else {
            dropped_records_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
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

void AsyncSink::deliver(const LogRecord& record) {
    for (auto& sink : sinks_) {
        if (record.level >= sink->level()) sink->submit(record);
    }
}

void AsyncSink::stop() {
    if (!running_.exchange(false)) return;
    cv_consume_.notify_all();
    cv_produce_.notify_all();
    if (worker_.joinable()) worker_.join();

    // A producer that got past the running_ check just as the worker exited can
    // leave records behind; deliver them here rather than dropping them.
    for (;;) {
        LogRecord rec;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (size_ == 0) break;
            rec = std::move(queue_[head_]);
            head_ = (head_ + 1) % capacity_;
            size_--;
        }
        deliver(rec);
    }
    cv_produce_.notify_all();
}

void AsyncSink::worker_loop() {
    std::vector<LogRecord> batch;
    batch.reserve(256);

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_ && size_ == 0) break;

            cv_consume_.wait_for(lock, std::chrono::milliseconds(100), [this] { return size_ > 0 || !running_; });

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

        for (const auto& rec : batch) deliver(rec);
        batch.clear();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            inflight_ = 0;
        }
        cv_produce_.notify_all();
    }
}

}  // namespace enigmadb
