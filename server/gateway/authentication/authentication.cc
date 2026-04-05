

#include "authentication.hh"

namespace gateway{
    

Authentication::Authentication(const AuthenticationConfig& config)
    : redis_(redis::redis_build::build(config.host, config.port, config.db_id, config.keey_alive))
{
    threads_.reserve(config.thread_count);
    for(int i = 0; i < config.thread_count; ++i){
        threads_.emplace_back(std::bind(&Authentication::thread_loop, this));
    }
}
Authentication::~Authentication(){
    stop();
    for(auto& thread : threads_){
        if(thread.joinable()){
            thread.join();
        }
    }
}
void Authentication::set_authenticate_callback(const std::function<void(uint64_t, std::shared_ptr<common::PlayerInfo>)> back){
    authenticate_callback_ = back;
}
std::shared_ptr<common::PlayerInfo> Authentication::authenticate(uint64_t player_id){
    bool is_authenticated = player_info_.find(player_id);
    if (!is_authenticated){
        // 异步请求redis验证玩家是否登录
        while(!player_id_queue_.try_put(player_id));
        player_id_queue_cond_.notify_one();
        return nullptr;
    }
    return std::make_shared<common::PlayerInfo>(player_info_.get(player_id));
}

void Authentication::stop(){
    stop_.store(true, std::memory_order_release);
}
// delete cache
void Authentication::delete_cache(uint64_t player_id){
    player_info_.remove(player_id);
}

void Authentication::thread_loop(){
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
            std::unordered_map<std::string, std::string> info_map;
            bool ret = redis_.hgetall(key, info_map);
            if (ret) {
                // 解析玩家信息
                std::shared_ptr<common::PlayerInfo> info = std::make_shared<common::PlayerInfo>();
                info->version = std::stoull(info_map[PLAYER_INFO_VERSION]);
                info->session_id = std::stoull(info_map[PLAYER_INFO_SESSION_ID]);
                info->gateway_id = std::stoi(info_map[PLAYER_INFO_GATEWAY_ID]);
                info->zone_id = std::stoi(info_map[PLAYER_INFO_ZONE_ID]);
                info->battle_id = std::stoi(info_map[PLAYER_INFO_BATTLE_ID]);
                // 存储玩家信息
                player_info_.insert(player_id, *info);
                if(authenticate_callback_){
                    authenticate_callback_(player_id, info);
                }
            }else{
                // 玩家未登录
                if(authenticate_callback_){
                    authenticate_callback_(player_id, nullptr);
                }
            }
        }
    }
}
}
