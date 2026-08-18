#ifndef ENIGMA_DB_MANIFEST_WRITER_H
#define ENIGMA_DB_MANIFEST_WRITER_H

#include "enigmadb/base.h"

namespace enigmadb {

class ManifestWriter {
   public:
    Result<void> write();

   private:
};

}  // namespace enigmadb

#endif  // ENIGMA_DB_MANIFEST_WRITER_H
