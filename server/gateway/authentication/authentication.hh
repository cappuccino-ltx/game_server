#pragma once 

#include "concurrent_map.hh"
#include <cstdint>
#include <mutex>
#include <redis.hh>

#include <lockfree/lockfree.hh> 
#include <akcp/channel.hh>
#include <authenticate.hh>

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <condition_variable>



namespace gateway{

struct AuthenticationConfig{
    std::string host = "localhost";
    int port = 6379;
    int db_id = 0;
    bool keey_alive = true;
    int thread_count = 1;
};

class Authentication{
public:
    Authentication(const AuthenticationConfig& config);
    ~Authentication();
    void set_authenticate_callback(const std::function<void(uint64_t, std::shared_ptr<common::PlayerInfo>)> back);
    std::shared_ptr<common::PlayerInfo> authenticate(uint64_t player_id);

    void stop();
    // delete cache
    void delete_cache(uint64_t player_id);

private:
    void thread_loop();

private:
    redis::Redis redis_;
    common::ConcurrentMap<uint64_t, common::PlayerInfo> player_info_;
    std::function<void(uint64_t, std::shared_ptr<common::PlayerInfo>)> authenticate_callback_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_ {false};
    lockfree::concurrent_queue<uint64_t> player_id_queue_ {std::thread::hardware_concurrency(), lockfree::K1};
    std::mutex player_id_queue_mutex_;
    std::condition_variable player_id_queue_cond_;
};   

} // namespace gateway