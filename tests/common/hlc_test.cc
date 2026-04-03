#include "enigmadb/common/hlc.h"

#include <gtest/gtest.h>

#include <thread>
#include <unordered_set>
#include <vector>

using namespace enigmadb::common;

TEST(HybridLogicalClock, generator) {
    TimestampGenerator tsGen;

    uint64_t last = 0;
    for (size_t i = 0; i < 1000; i++) {
        const auto ts = tsGen.next();
        ASSERT_TRUE(ts > last);
        last = ts;
    }
};

TEST(HybridLogicalClock, concurrent_generate) {
    TimestampGenerator tsGen;

    constexpr int threadCount = 10;
    constexpr int perThreadTsCount = 1000;

    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t> > threadResults(threadCount);

    threads.reserve(threadCount);

    for (size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back([&tsGen, &threadResults, i]() {
            for (size_t j = 0; j < perThreadTsCount; ++j) {
                uint64_t ts = tsGen.next();
                threadResults[i].push_back(ts);
            }
        });
    }

    for (auto& t : threads) t.join();

    std::unordered_set<uint64_t> allTimestamps;
    for (const auto& vec : threadResults) {
        for (uint64_t ts : vec) {
            ASSERT_TRUE(allTimestamps.insert(ts).second);
        }
    }

    ASSERT_TRUE(allTimestamps.size() == threadCount * perThreadTsCount);
};
