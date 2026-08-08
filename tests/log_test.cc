#include "enigmadb/log.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "enigmadb/log_sinks.h"

namespace enigmadb {
namespace {

LogRecord make_record(Level level = Level::Info, std::string message = "msg") {
    LogRecord rec;
    rec.level = level;
    rec.category = Category::General;
    rec.ts = std::chrono::system_clock::now();
    rec.tid = 1;
    rec.file = "log_test.cc";
    rec.line = 1;
    rec.func = "test";
    rec.message = std::move(message);
    return rec;
}

// Counts what reaches it, so tests can assert on delivery.
class CountingSink : public LogSink {
   public:
    void submit(const LogRecord&) override { count.fetch_add(1); }
    void flush() override {}
    std::atomic<int> count{0};
};

TEST(Log, async_sink_clamps_zero_capacity) {
    auto counter = std::make_shared<CountingSink>();
    AsyncSink async({counter}, 0, OverflowPolicy::Block);

    for (int i = 0; i < 100; ++i) async.submit(make_record());
    async.flush();

    EXPECT_EQ(counter->count.load(), 100);
    async.stop();
}

TEST(Log, async_sink_flush_waits_for_records_in_flight) {
    auto counter = std::make_shared<CountingSink>();
    AsyncSink async({counter}, 64, OverflowPolicy::Block);

    for (int i = 0; i < 500; ++i) async.submit(make_record());
    async.flush();

    // flush() must account for the batch the worker already pulled off the
    // queue but has not written out yet.
    EXPECT_EQ(counter->count.load(), 500);
    async.stop();
}

TEST(Log, async_sink_delivers_records_submitted_after_stop) {
    auto counter = std::make_shared<CountingSink>();
    AsyncSink async({counter}, 64, OverflowPolicy::Block);

    async.stop();
    const int before = counter->count.load();
    async.submit(make_record(Level::Fatal, "after shutdown"));

    // No worker remains to drain the queue, so the record has to go straight
    // to the downstream sinks instead of being swallowed.
    EXPECT_EQ(counter->count.load(), before + 1);
}

TEST(Log, async_sink_loses_nothing_when_stop_races_submitters) {
    // A submitter that reads running_ as true just as stop() joins the worker
    // and drains must not leave its record stranded in a dead queue. With a
    // blocking policy and room to spare, every record has to arrive.
    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;

    for (int trial = 0; trial < 25; ++trial) {
        auto counter = std::make_shared<CountingSink>();
        AsyncSink async({counter}, 1024, OverflowPolicy::Block);

        std::vector<std::thread> submitters;
        for (int t = 0; t < kThreads; ++t) {
            submitters.emplace_back([&] {
                for (int i = 0; i < kPerThread; ++i) async.submit(make_record());
            });
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        async.stop();
        for (auto& s : submitters) s.join();

        ASSERT_EQ(counter->count.load(), kThreads * kPerThread)
            << "records lost on trial " << trial;
    }
}

TEST(Log, ring_buffer_dump_survives_concurrent_writers) {
    // A small ring wraps constantly, so writers whose sequence numbers are a
    // full lap apart target the same slot at the same time. Each writer emits
    // one repeated letter, so a record spliced from two of them is visible.
    constexpr size_t kSlots = 4;
    constexpr size_t kMsgLen = 700;
    constexpr int kThreads = 8;

    RingBufferSink ring(kSlots, kMsgLen + 200);
    std::atomic<bool> stop{false};

    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&, t] {
            LogRecord rec =
                make_record(Level::Info, std::string(kMsgLen, 'a' + t));
            while (!stop.load(std::memory_order_relaxed)) {
                rec.ts = std::chrono::system_clock::now();
                ring.submit(rec);
            }
        });
    }

    const std::string path = "./ring_dump_test.txt";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    for (int i = 0; i < 2000; ++i) ring.signal_safe_dump(fd);
    stop.store(true);
    for (auto& w : writers) w.join();
    ::close(fd);

    std::ifstream in(path);
    std::string line;
    long records = 0, spliced = 0;
    while (std::getline(in, line)) {
        auto pos = line.find("test: ");
        if (pos == std::string::npos) continue;  // dump header
        std::string msg = line.substr(pos + 6);
        ++records;
        if (msg.find_first_not_of(msg[0]) != std::string::npos) ++spliced;
    }
    in.close();
    ::unlink(path.c_str());

    EXPECT_GT(records, 0);
    EXPECT_EQ(spliced, 0);
}

TEST(Log, sink_list_survives_concurrent_dispatch_and_add) {
    // dispatch() must not read the sink list while add_sink() mutates it.
    testing::internal::CaptureStderr();

    std::atomic<bool> stop{false};
    std::vector<std::thread> loggers;
    for (int t = 0; t < 4; ++t) {
        loggers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                Logger::instance().dispatch(make_record());
            }
        });
    }

    for (int i = 0; i < 200; ++i) {
        Logger::instance().add_sink(std::make_shared<CountingSink>());
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    stop.store(true);
    for (auto& l : loggers) l.join();

    testing::internal::GetCapturedStderr();
    SUCCEED();  // the point is finishing without corrupting the vector
}

TEST(LogDeathTest, fatal_dumps_ring_and_aborts) {
    EXPECT_DEATH(
        {
            LogConfig cfg;
            cfg.async = true;
            cfg.console = false;
            Logger::instance().init(cfg);
            Logger::instance().dispatch(make_record(Level::Fatal, "boom"));
        },
        "CRASH RING BUFFER DUMP");
}

TEST(LogDeathTest, fatal_after_shutdown_still_dumps) {
    // shutdown() joins the async worker; a fatal record logged afterwards must
    // still reach the crash ring rather than be stranded in a dead queue.
    EXPECT_DEATH(
        {
            LogConfig cfg;
            cfg.async = true;
            cfg.console = false;
            Logger::instance().init(cfg);
            Logger::instance().shutdown();
            Logger::instance().dispatch(make_record(Level::Fatal, "late boom"));
        },
        "late boom");
}

}  // namespace
}  // namespace enigmadb
