#pragma once 

#include "concurrent_map/concurrent_map.hh"
#include <cstdint>
#include <mutex>
#include <redis.hh>
#include <redis_store_name.hh>

#include <lockfree/lockfree.hh> 
#include <akcp/channel.hh>

#include <string>
#include <utility>
#include <vector>
#include <unordered_set>
#include <functional>
#include <memory>
#include <thread>
#include <shared_mutex>
#include <condition_variable>



namespace gateway{

struct AuthenticationConfig{
    std::string host = "localhost";
    int port = 6379;
    int db_id = 0;
    bool keey_alive = true;
    int thread_count = 1;
};

struct PlayerInfo{
    uint64_t version;
    uint64_t session_id;
    uint32_t gateway_id;
    uint32_t zone_id;
    uint32_t battle_id;
};

class Authentication{
public:
    Authentication(const AuthenticationConfig& config)
        : redis_(redis::redis_build::build(config.host, config.port, config.db_id, config.keey_alive))
    {
        threads_.reserve(config.thread_count);
        for(int i = 0; i < config.thread_count; ++i){
            threads_.emplace_back(std::bind(&Authentication::thread_loop, this));
        }
    }
    ~Authentication(){
        stop();
        for(auto& thread : threads_){
            if(thread.joinable()){
                thread.join();
            }
        }
    }
    void set_authenticate_callback(const std::function<void(uint64_t,bool)>& back){
        authenticate_callback_ = back;
    }
    bool authenticate(uint64_t player_id){
        bool is_authenticated = false;
        {
            std::shared_lock<std::shared_mutex> lock(player_ids_mutex_);
            is_authenticated = player_ids_.find(player_id) != player_ids_.end();
        }
        if (!is_authenticated){
            // 异步请求redis验证玩家是否登录
            while(!player_id_queue_.try_put(player_id));
            player_id_queue_cond_.notify_one();
        }
        return is_authenticated;
    }

    void stop(){
        stop_.store(true, std::memory_order_release);
    }

private:
    void thread_loop(){
        while(!stop_.load(std::memory_order_acquire)){
            // 异步请求redis验证玩家是否登录
            if (player_id_queue_.size_approx() == 0) {
                std::unique_lock<std::mutex> lock(player_id_queue_mutex_);
                player_id_queue_cond_.wait(lock, [this](){
                    return player_id_queue_.size_approx() > 0;
                });
                continue;
            }
            uint64_t player_id;
            while(player_id_queue_.size_approx() > 0){
                if(!player_id_queue_.try_get(player_id)){
                    continue;
                }
                // 请求redis 进行验证
                std::string key = REDIS_STORE_NAME_SESSION_TOKEN + std::to_string(player_id);
                bool is_authenticated = !redis_.exists(key);
                if(authenticate_callback_){
                    authenticate_callback_(player_id, is_authenticated);
                }
                if(is_authenticated){
                    // write mutex lock
                    std::unique_lock<std::shared_mutex> lock(player_ids_mutex_);
                    player_ids_.insert(player_id);
                }
            }
        }
    }

private:
    redis::Redis redis_;
    // std::unordered_set<uint64_t> player_ids_;
    // std::shared_mutex player_ids_mutex_;
    ConcurrentMap<uint64_t, PlayerInfo> player_info_;
    std::function<void(uint64_t,bool)> authenticate_callback_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_ {false};
    lockfree::concurrent_queue<uint64_t> player_id_queue_ {std::thread::hardware_concurrency(), lockfree::K1};
    std::mutex player_id_queue_mutex_;
    std::condition_variable player_id_queue_cond_;
};   

} // namespace gateway