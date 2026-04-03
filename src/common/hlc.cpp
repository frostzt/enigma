#include "enigmadb/common/hlc.h"

#include <chrono>

namespace enigmadb::common {

uint64_t TimestampGenerator::next() {
    // Generate milliseconds from the current epoch with the system clock
    const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();
    const long long millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count();

    // Take Wall Clock's 48 Bits and Lamport Counters 16 bits
    const uint64_t wallTime48 = (static_cast<uint64_t>(millis) & 0xFFFFFFFFFFFF)
                                << 16;
    uint64_t candidate;
    uint64_t prev;

    // Account for clock rebalance
    do {
        prev = lastReturned_.load();
        candidate = wallTime48 | (logicalCounter_++ & 0xFFFF);
        if (candidate <= prev) {
            candidate = prev + 1;
        }
    } while (!lastReturned_.compare_exchange_weak(prev, candidate));
    return candidate;
}

}  // namespace enigmadb::common
