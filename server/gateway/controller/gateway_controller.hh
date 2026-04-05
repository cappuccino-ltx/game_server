#pragma once 
// std 
#include <functional>
#include <memory>
// common
#include <etcd.hh>
// gateway
#include <service/gateway_service.hh>
#include <authentication/authentication.hh>
#include <concurrent_map/concurrent_map.hh>
#include <thread_local_store/thread_local_store.hh>
#include <route/route.hh>
// pb
#include <envelope.pb.h>


namespace gateway{
using std::placeholders::_1;
using std::placeholders::_2;

struct DiscoveryConfig{
    std::string host;
    std::string base_dir;
};

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
    Controller& init_route(const RouteCenterConfig& config){
        route_ = std::make_shared<RouteCenter>(config);
        route_->set_send_to_client_callback(std::bind(&Controller::send_to_client,this,_1,_2));
        route_->async_start();
        return *this;
    }
    Controller& init_discovery(const DiscoveryConfig& config){
        discovery_.setHost(config.host)
            .setBaseDir(config.base_dir)
            .setUpdateCallback([this](const std::string &key, const std::string &value){
                infolog("server online : {} {}" ,key, value);
                int pos = value.find(":");
                std::string ip = value.substr(0, pos);
                int port = std::stoi(value.substr(pos + 1));
                route_->add_route_target(key, ip, port);
            }).setRemoveCallback([](const std::string &key, const std::string &value){
                errorlog("server offline : {} {}" ,key, value);
            }).start();
        return *this;
    }
    void start(int udp_port){
        udp_service->start(udp_port);
    }

private:
    void udp_service_message(kcp::channel_view channel, std::shared_ptr<std::vector<uint8_t>> message){
        std::shared_ptr<mmo::transport::Envelope> envelope = memory_reuse::get_object<mmo::transport::Envelope>();
        envelope->ParseFromArray(message->data(), message->size());
        auto info = authentication_->authenticate(envelope->header().player_id());
        if(info){
            // 认证成功, 向上交付 todo...
            player_channels_[envelope->header().player_id()] = channel;
            route_->client_to_route(envelope, info);
        }else {
            // 认证失败, 存储到本地, 等待异步认证成功或者失败, 再交付
            packages_thread_local_.put(envelope->header().player_id(), envelope);
            authenticating_[envelope->header().player_id()] = channel;
        }
    }
    // authentication callback 
    void on_player_authenticated(uint64_t player_id, std::shared_ptr<common::PlayerInfo> info){
        if(info){
            // add player id to local store
            player_channels_[player_id] = authenticating_[player_id];
            authenticating_.remove(player_id);
            // notify channel
            player_channels_[player_id]->timer_task([this,player_id,info](){
                // 从本地存储中获取所有待交付的包
                auto& packages = packages_thread_local_.get(player_id);
                for(auto& package : packages){
                    route_->client_to_route(package, info);
                }
                packages_thread_local_.erase(player_id);
            }, 0);
        }else {
            packages_thread_local_.erase(player_id);
        }
    }
    // route callback
    void send_to_client(uint64_t player_id, std::shared_ptr<std::vector<uint8_t>> message){
        kcp::channel_view channel = player_channels_[player_id];
        if (channel){
            channel->send(message);
        }
    }

private:
    std::shared_ptr<GatewayService> udp_service;
    common::Discovery discovery_;
    std::shared_ptr<RouteCenter> route_;
    std::shared_ptr<Authentication> authentication_;
    ConcurrentMap<uint64_t, kcp::channel_view> player_channels_;
    ConcurrentMap<uint64_t, kcp::channel_view> authenticating_;
    ThreadLocalStore<uint64_t, std::shared_ptr<mmo::transport::Envelope>> packages_thread_local_;
};


} // namespace gateway