#pragma once 

#include <functional>
#include <memory>
#include <thread>
// #include <unordered_map>

#include <service/gateway_service.hh>
#include <authentication/authentication.hh>
#include <concurrent_map/concurrent_map.hh>
#include <thread_local_store/thread_local_store.hh>
#include <envelope.pb.h>


namespace gateway{
using std::placeholders::_1;
using std::placeholders::_2;

class Controller{
public:
    Controller()
    {}
    Controller& init_udp_service(gateway_service_config config){
        udp_service = std::make_shared<GatewayService>(config);
        udp_service->set_message_callback(std::bind(&Controller::udp_service_message,this,_1,_2));
        return *this;
    }
    Controller& init_authentication(const AuthenticationConfig& config){
        authentication_ = std::make_shared<Authentication>(config);
        authentication_->set_authenticate_callback(std::bind(&Controller::on_player_authenticated,this,_1,_2));
        return *this;
    }
    Controller& start(int udp_port){
        udp_thread = std::make_shared<std::thread>([this,udp_port](){
            udp_service->start(udp_port);
        });
        return *this;
    }

private:
    void udp_service_message(kcp::channel_view channel, std::shared_ptr<std::vector<uint8_t>> message){
        std::shared_ptr<mmo::transport::Envelope> envelope = std::make_shared<mmo::transport::Envelope>();
        envelope->ParseFromArray(message->data(), message->size());
        bool ret = authentication_->authenticate(envelope->header().player_id());
        if(ret){
            // 认证成功, 向上交付 todo...
            player_channels_[envelope->header().player_id()] = channel;
        }else {
            // 认证失败, 存储到本地, 等待异步认证成功或者失败, 再交付
            packages_thread_local_.put(envelope->header().player_id(), envelope);
            authenticating_[envelope->header().player_id()] = channel;
        }
    }
    // authentication callback
    void on_player_authenticated(uint64_t player_id, bool is_authenticated){
        if(is_authenticated){
            // add player id to local store
            player_channels_[player_id] = authenticating_[player_id];
            authenticating_.remove(player_id);
            // notify channel
            player_channels_[player_id]->timer_task([this,player_id](){
                // 从本地存储中获取所有待交付的包
                auto& packages = packages_thread_local_.get(player_id);
            }, 0);
        }
    }

private:
    std::shared_ptr<GatewayService> udp_service;
    std::shared_ptr<std::thread> udp_thread;
    std::shared_ptr<Authentication> authentication_;
    ConcurrentMap<uint64_t, kcp::channel_view> player_channels_;
    ConcurrentMap<uint64_t, kcp::channel_view> authenticating_;
    ThreadLocalStore<uint64_t, std::shared_ptr<mmo::transport::Envelope>> packages_thread_local_;
};


} // namespace gateway