#include "enigmadb/log.h"

#include <atomic>

namespace enigmadb {

std::atomic<Level> g_levels[static_cast<size_t>(Category::_Count)];

}  // namespace enigmadb
