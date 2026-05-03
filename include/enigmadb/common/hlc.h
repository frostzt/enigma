/**
 * @file hlc.h
 * @brief Hybrid logical clock implementation.
 *
 * @author frostzt
 * @date 2026-04-03
 */

#ifndef ENIGMA_DB_HLC_H
#define ENIGMA_DB_HLC_H

#include <atomic>
#include <memory>

namespace enigmadb::common {

class TimestampGenerator {
   private:
    struct State {
        std::atomic<uint64_t> logicalCounter_{};
        std::atomic<uint64_t> lastReturned_{};
    };
    std::unique_ptr<State> state_;

   public:
    TimestampGenerator() : state_(std::make_unique<State>()) {}

    // Movable
    TimestampGenerator(TimestampGenerator&&) noexcept = default;
    TimestampGenerator& operator=(TimestampGenerator&&) noexcept = default;

    // Non-copyable
    TimestampGenerator(const TimestampGenerator&) = delete;
    TimestampGenerator& operator=(const TimestampGenerator&) = delete;

    uint64_t next();
};

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_HLC_H#endif  // ENIGMA_DB_HLC_H
