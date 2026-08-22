#ifndef ENIGMA_DB_BASE_H
#define ENIGMA_DB_BASE_H

#include "error.h"
#include "result.h"

namespace enigmadb {

template <typename T>
using Result = ExpectResult<T, Error>;

}  // namespace enigmadb

#endif  // ENIGMA_DB_BASE_H
