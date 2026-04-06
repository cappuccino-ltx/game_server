

#include "gateway_controller.hh"

namespace gateway{

    Controller& Controller::init_udp_service(const gateway_service_config& config){
        udp_service = std::make_shared<GatewayService>(config);
        udp_service->set_message_callback(std::bind(&Controller::udp_service_message,this,_1,_2));
        return *this;
    }
    Controller& Controller::init_authentication(const AuthenticationConfig& config){
        authentication_ = std::make_shared<Authentication>(config);
        authentication_->set_authenticate_callback(std::bind(&Controller::on_player_authenticated,this,_1,_2));
        return *this;
    }
    Controller& Controller::init_route(const RouteCenterConfig& config){
        route_ = std::make_shared<RouteCenter>(config);
        route_->set_send_to_client_callback(std::bind(&Controller::send_to_client,this,_1,_2));
        route_->set_internal_callback(std::bind(&Controller::on_internal_message,this,_1,_2));
        route_->async_start();
        return *this;
    }
    Controller& Controller::init_discovery(const DiscoveryAndRegisterConfig& config){
        discovery_ = std::make_shared<common::Discovery>();
        discovery_->setHost(config.host)
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
    Controller& Controller::init_register(const DiscoveryAndRegisterConfig& config){
        register_ = std::make_shared<common::Register>(config.host);
        register_->registory(config.base_dir, config.register_value);
        return *this;
    }
    void Controller::start(int udp_port){
        udp_service->start(udp_port);
    }

    void Controller::udp_service_message(kcp::channel_view channel, std::shared_ptr<std::vector<uint8_t>> message){
        std::shared_ptr<mmo::transport::Envelope> envelope = memory_reuse::get_object<mmo::transport::Envelope>();
        envelope->ParseFromArray(message->data(), message->size());
        auto info = authentication_->authenticate(envelope->header().player_id());
        if(info){
            if (envelope->header().token() != std::to_string(info->session_id)){
                return;
            }
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
    void Controller::on_player_authenticated(uint64_t player_id, std::shared_ptr<common::PlayerInfo> info){
        if(info){
            // add player id to local store
            player_channels_[player_id] = authenticating_[player_id];
            authenticating_.remove(player_id);
            // notify channel
            player_channels_[player_id]->timer_task([this,player_id,info](){
                // 从本地存储中获取所有待交付的包
                auto& packages = packages_thread_local_.get(player_id);
                for(auto& package : packages){
                    // 验证 token 是否匹配
                    if (package->header().token() != std::to_string(info->session_id)){
                        continue;
                    }
                    route_->client_to_route(package, info);
                }
                packages_thread_local_.erase(player_id);
            }, 0);
        }else {
            packages_thread_local_.erase(player_id);
        }
    }
    // route callback
    void Controller::send_to_client(uint64_t player_id, std::shared_ptr<std::vector<uint8_t>> message){
        kcp::channel_view channel = player_channels_[player_id];
        if (channel){
            channel->send(message);
        }
    }
    // internal callback
    void Controller::on_internal_message(uint64_t player_id, uint32_t action){
        infolog("internal message: {} {}" ,player_id, action);
        if (action == mmo::ids::InternalAction::IA_DELETE_CACHE_REQ){
            // delete cache
            authentication_->delete_cache(player_id);
        }
    }
}
