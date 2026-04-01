#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <vector>

namespace gateway{

#define CONCURRENT_MAP_BUCKET_SIZE (std::thread::hardware_concurrency() > 8 ? std::thread::hardware_concurrency() : 8)

template<typename K, typename V>
class ConcurrentMap{
    using Map = std::unordered_map<K, V>;
    struct MapBucket{
        Map map_;
        std::shared_mutex mutex_;
    };
    MapBucket& get_map_bucket(K key){
        return map_buckets_[std::hash<K>()(key) % map_buckets_.size()];
    }
public:
    ConcurrentMap(int num_buckets = CONCURRENT_MAP_BUCKET_SIZE)
        : map_buckets_(num_buckets)
    {}

    V& operator[](K key){
        return get(key);
    }

    V& get(K key){
        auto& bucket = get_map_bucket(key);
        std::shared_lock<std::shared_mutex> lock(bucket.mutex_);
        return bucket.map_[key];
    }

    void insert(K key, V value){
        auto& bucket = get_map_bucket(key);
        std::unique_lock<std::shared_mutex> lock(bucket.mutex_);
        bucket.map_[key] = value;
    }

    void remove(K key){
        auto& bucket = get_map_bucket(key);
        std::unique_lock<std::shared_mutex> lock(bucket.mutex_);
        bucket.map_.erase(key);
    }

private:
    std::vector<MapBucket> map_buckets_;
};
} // namespace gateway
