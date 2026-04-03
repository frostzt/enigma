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

namespace enigmadb::common {

class TimestampGenerator {
   private:
    std::atomic<uint64_t> logicalCounter_{};
    std::atomic<uint64_t> lastReturned_{};

   public:
    uint64_t next();
};

}  // namespace enigmadb::common

#endif  // ENIGMA_DB_HLC_H
