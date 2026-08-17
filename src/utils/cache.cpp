#include "enigmadb/utils/cache.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include "enigmadb/hash.h"

namespace enigmadb::utils {

namespace {

struct LRUHandle {
    void* value;
    void (*deleter)(const std::string_view key, void* value);
    LRUHandle* next_hash;
    LRUHandle* next;
    LRUHandle* prev;
    size_t charge;
    size_t key_len;
    bool in_cache;

    /// Active references
    uint32_t refs;

    /// Key hash for sharding
    uint32_t hash;

    /// Beginning of the key
    char key_data[1];

    std::string_view key() const { return std::string_view(key_data, key_len); }
};

class HashMap {
   public:
    HashMap() : len_(0), elems_(0), list_(nullptr) { resize(); }
    ~HashMap() { delete[] list_; }

    LRUHandle* lookup(const std::string_view key, uint32_t hash) { return *find_pointer(key, hash); }

    LRUHandle* insert(LRUHandle* e) {
        auto ptr = find_pointer(e->key(), e->hash);
        auto old = *ptr;
        e->next_hash = (old == nullptr ? nullptr : old->next_hash);
        *ptr = e;
        if (old == nullptr) {
            ++elems_;
            if (elems_ > len_) {
                resize();
            }
        }
        return old;
    }

    LRUHandle* remove(const std::string_view key, uint32_t hash) {
        auto ptr = find_pointer(key, hash);
        auto result = *ptr;
        if (result != nullptr) {
            *ptr = result->next_hash;
            --elems_;
        }
        return result;
    }

   private:
    uint32_t len_;
    uint32_t elems_;
    LRUHandle** list_;

    LRUHandle** find_pointer(const std::string_view key, uint32_t hash) {
        auto ptr = &list_[hash & (len_ - 1)];
        while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key())) {
            ptr = &(*ptr)->next_hash;
        }
        return ptr;
    }

    void resize() {
        uint32_t new_length = 4;
        while (new_length < elems_) {
            new_length *= 2;
        }
        auto new_list = new LRUHandle*[new_length];
        std::memset(new_list, 0, sizeof(new_list[0]) * new_length);
        uint32_t count = 0;
        for (uint32_t i = 0; i < len_; i++) {
            auto h = list_[i];
            while (h != nullptr) {
                auto next = h->next_hash;
                uint32_t hash = h->hash;
                auto ptr = &new_list[hash & (new_length - 1)];
                h->next_hash = *ptr;
                *ptr = h;
                h = next;
                count++;
            }
        }
        assert(elems_ == count);
        delete[] list_;
        list_ = new_list;
        len_ = new_length;
    }
};

class LRUCache {
   public:
    LRUCache() : capacity_(0), usage_(0) {
        lru_.next = &lru_;
        lru_.prev = &lru_;
        in_use_.next = &in_use_;
        in_use_.prev = &in_use_;
    }

    ~LRUCache() {
        assert(in_use_.next == &in_use_);
        for (auto e = lru_.next; e != &lru_;) {
            auto next = e->next;
            assert(e->in_cache);
            e->in_cache = false;
            assert(e->refs == 1);
            unref(e);
            e = next;
        }
    }

    void set_capacity(size_t capacity) { capacity_ = capacity; };

    Cache::Handle* insert(const std::string_view key, uint32_t hash, void* value, size_t charge,
                          void (*deleter)(const std::string_view key, void* value));
    Cache::Handle* lookup(const std::string_view key, uint32_t hash);
    void release(Cache::Handle* handle);
    void erase(const std::string_view key, uint32_t hash);
    void prune();
    size_t total_charge() const {
        std::lock_guard<std::mutex> lock(mu_);
        return usage_;
    }

    Cache::Stats stats_snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return stats_;
    }

   private:
    void LRU_remove(LRUHandle* e);
    void LRU_append(LRUHandle* list, LRUHandle* e);
    void ref(LRUHandle* e);
    void unref(LRUHandle* e);
    bool finish_erase(LRUHandle* e);

    size_t capacity_;

    mutable std::mutex mu_;
    size_t usage_;

    LRUHandle lru_;
    LRUHandle in_use_;

    HashMap table_;
    Cache::Stats stats_;
};

void LRUCache::ref(LRUHandle* e) {
    if (e->refs == 1 && e->in_cache) {
        LRU_remove(e);
        LRU_append(&in_use_, e);
    }
    e->refs++;
}

void LRUCache::unref(LRUHandle* e) {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
        assert(!e->in_cache);
        (*e->deleter)(e->key(), e->value);
        free(e);
    } else if (e->in_cache && e->refs == 1) {
        LRU_remove(e);
        LRU_append(&lru_, e);
    }
}

void LRUCache::LRU_remove(LRUHandle* e) {
    e->next->prev = e->prev;
    e->prev->next = e->next;
}

void LRUCache::LRU_append(LRUHandle* list, LRUHandle* e) {
    e->next = list;
    e->prev = list->prev;
    e->prev->next = e;
    e->next->prev = e;
}

Cache::Handle* LRUCache::lookup(const std::string_view key, uint32_t hash) {
    std::lock_guard<std::mutex> lock(mu_);
    auto e = table_.lookup(key, hash);
    if (e != nullptr) {
        stats_.total_hits++;
        ref(e);
    } else {
        stats_.total_misses++;
    }
    return reinterpret_cast<Cache::Handle*>(e);
}

void LRUCache::release(Cache::Handle* handle) {
    std::lock_guard<std::mutex> lock(mu_);
    unref(reinterpret_cast<LRUHandle*>(handle));
}

Cache::Handle* LRUCache::insert(const std::string_view key, uint32_t hash, void* value, size_t charge,
                                void (*deleter)(const std::string_view key, void* value)) {
    std::lock_guard<std::mutex> lock(mu_);
    auto e = reinterpret_cast<LRUHandle*>(malloc(sizeof(LRUHandle) - 1 + key.size()));
    e->value = value;
    e->deleter = deleter;
    e->charge = charge;
    e->key_len = key.size();
    e->hash = hash;
    e->in_cache = false;
    e->refs = 1;
    std::memcpy(e->key_data, key.data(), key.size());

    if (capacity_ > 0) {
        e->refs++;
        e->in_cache = true;
        LRU_append(&in_use_, e);
        usage_ += charge;
        finish_erase(table_.insert(e));
        stats_.total_inserts++;
    } else {
        e->next = nullptr;
    }

    while (usage_ > capacity_ && lru_.next != &lru_) {
        LRUHandle* old = lru_.next;
        assert(old->refs == 1);
        if (finish_erase(table_.remove(old->key(), old->hash))) {
            stats_.total_evictions++;
        }
    }

    return reinterpret_cast<Cache::Handle*>(e);
}

bool LRUCache::finish_erase(LRUHandle* e) {
    if (e != nullptr) {
        assert(e->in_cache);
        LRU_remove(e);
        e->in_cache = false;
        usage_ -= e->charge;
        unref(e);
    }
    return e != nullptr;
}

void LRUCache::erase(const std::string_view key, uint32_t hash) {
    std::lock_guard<std::mutex> lock(mu_);
    finish_erase(table_.remove(key, hash));
}

void LRUCache::prune() {
    std::lock_guard<std::mutex> lock(mu_);
    while (lru_.next != &lru_) {
        auto e = lru_.next;
        assert(e->refs == 1);
        bool erased = finish_erase(table_.remove(e->key(), e->hash));
        if (!erased) assert(erased);
    }
}

class ShardedLRUCache : public Cache {
   private:
    size_t num_shards_;
    std::unique_ptr<LRUCache[]> shard_;
    std::mutex id_mu_;
    uint64_t last_id_;

    static inline uint32_t hash_sv(const std::string_view k) { return Hash(k.data(), k.size(), 0); }

    uint32_t shard(uint32_t hash) { return hash % num_shards_; };

   public:
    explicit ShardedLRUCache(size_t capacity, size_t num_shards = 4) : num_shards_(num_shards), last_id_(0) {
        shard_ = std::make_unique<LRUCache[]>(num_shards_);
        const size_t per_shard = (capacity + (num_shards_ - 1)) / num_shards_;
        for (size_t i = 0; i < num_shards_; i++) {
            shard_[i].set_capacity(per_shard);
        }
    }

    ~ShardedLRUCache() override {}

    Cache::Handle* insert(const std::string_view key, void* value, size_t charge,
                          void (*deleter)(const std::string_view key, void* value)) override {
        const uint32_t hash = hash_sv(key);
        return shard_[shard(hash)].insert(key, hash, value, charge, deleter);
    }

    Cache::Handle* lookup(const std::string_view key) override {
        const uint32_t hash = hash_sv(key);
        return shard_[shard(hash)].lookup(key, hash);
    }

    void release(Handle* handle) override {
        auto h = reinterpret_cast<LRUHandle*>(handle);
        shard_[shard(h->hash)].release(handle);
    }

    void erase(const std::string_view key) override {
        const uint32_t hash = hash_sv(key);
        shard_[shard(hash)].erase(key, hash);
    }

    void* value(Handle* handle) override { return reinterpret_cast<LRUHandle*>(handle)->value; }

    uint64_t new_id() override {
        std::lock_guard<std::mutex> lock(id_mu_);
        return ++(last_id_);
    }

    void prune() override {
        for (size_t i = 0; i < num_shards_; i++) {
            shard_[i].prune();
        }
    }

    size_t total_charge() const override {
        size_t total = 0;
        for (size_t i = 0; i < num_shards_; i++) {
            total += shard_[i].total_charge();
        }
        return total;
    }

    Cache::Stats get_stats() const override {
        Stats total;
        for (size_t i = 0; i < num_shards_; i++) {
            auto s = shard_[i].stats_snapshot();
            total.total_evictions += s.total_evictions;
            total.total_inserts += s.total_inserts;
            total.total_misses += s.total_misses;
            total.total_hits += s.total_hits;
        }
        return total;
    }
};

}  // end namespace

std::unique_ptr<Cache> NewLRUCache(size_t capacity, size_t num_shards) {
    return std::make_unique<ShardedLRUCache>(capacity, num_shards);
};

}  // namespace enigmadb::utils
