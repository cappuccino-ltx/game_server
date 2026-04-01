#pragma once 


#include <unordered_map>
#include <vector>


namespace gateway{
template<typename K, typename V>
class ThreadLocalStore{
public:
    ThreadLocalStore()
    {}
    static std::vector<V>& get(K key){
        return store_[key];
    }
    static bool has(K key){
        return store_.find(key) != store_.end();
    }
    static void put(K key, V value){
        store_[key].push_back(value);
    }
    static void erase(K key){
        store_.erase(key);
    }
private:
    inline static thread_local std::unordered_map<K, std::vector<V>> store_;
}; // template class ThreadLocalStore
} // namespace gateway
