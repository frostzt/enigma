#ifndef ENIGMA_DB_LOG_SINK_H
#define ENIGMA_DB_LOG_SINK_H

#include "enigmadb/log.h"

namespace enigmadb {

struct LogSink {
    virtual ~LogSink() = default;
    virtual void submit(const LogRecord&) = 0;
    virtual void flush() = 0;
    void set_formatter(...);
    void set_level(Level);
};

}  // namespace enigmadb

#endif  // ENIGMA_DB_LOG_SINK_H
