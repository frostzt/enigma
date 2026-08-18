#include "enigmadb/hlc.h"

#include <chrono>

namespace enigmadb {

uint64_t TimestampGenerator::next() {
    const auto now = std::chrono::system_clock::now();
    const long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    const uint64_t wallTime48 = (static_cast<uint64_t>(millis) & 0xFFFFFFFFFFFF) << 16;

    uint64_t candidate;
    uint64_t prev;

    do {
        prev = state_->lastReturned_.load();
        candidate = wallTime48 | (state_->logicalCounter_++ & 0xFFFF);
        if (candidate <= prev) {
            candidate = prev + 1;
        }
    } while (!state_->lastReturned_.compare_exchange_weak(prev, candidate));

    return candidate;
}

}  // namespace enigmadb
