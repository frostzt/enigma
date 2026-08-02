/**
 * @file internal_iterator.h
 *
 * @author frostzt
 * @date 2026-06-03
 */

#ifndef ENIGMA_DB_DAZZLE_INTERNAL_ITERATOR_H
#define ENIGMA_DB_DAZZLE_INTERNAL_ITERATOR_H

#include "enigmadb/base.h"
#include "enigmadb/storage/dazzle_db/internal_value.h"
#include "enigmadb/storage/key.h"

namespace enigmadb::dazzle {

class InternalIterator {
   public:
    virtual ~InternalIterator() = default;

    virtual bool valid() const = 0;
    virtual void seek_to_first() = 0;
    virtual void next() = 0;
    virtual const storage::Key& key() const = 0;
    virtual const InternalValue& value() const = 0;
    virtual Result<void> status() const = 0;
};

};  // namespace enigmadb::dazzle

#endif  // ENIGMA_DB_DAZZLE_INTERNAL_ITERATOR_H
