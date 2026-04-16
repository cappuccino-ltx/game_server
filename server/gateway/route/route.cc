

#include "route.hh"
#include "forward.pb.h"
#include "ids.pb.h"
#include "log.hh"
#include "memory_reuse.hh"

namespace gateway{

RouteCenter::RouteCenter(const RouteCenterConfig& config)
: tcp_(config.listen_port, config.write_thread_n)
{
    tcp_.set_connection_callback(std::bind(&RouteCenter::on_connection, this, _1, _2, _3));
    tcp_.set_message_callback(std::bind(&RouteCenter::on_message, this, _1, _2, _3, _4));
}
void RouteCenter::async_start(){
    tcp_.async_start();
}

void RouteCenter::add_route_target(const std::string& target_id, const std::string& ip, int port){
    tcp_.connect(ip, port, target_id);
}
void RouteCenter::set_send_to_client_callback(const std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)>& callback){
    callback_ = callback;
}
void RouteCenter::set_internal_callback(const std::function<void(uint64_t, uint32_t)>& callback){
    internal_callback_ = callback;
}

void RouteCenter::client_to_route(std::shared_ptr<mmo::transport::Envelope> envelope , std::shared_ptr<common::PlayerInfo> info){
    uint32_t direction = common::protocol::util::get_direction(envelope->header().cmd());
    uint32_t module = common::protocol::util::get_module(envelope->header().cmd());
    if (direction != mmo::ids::Direction::C2G){
        return ;
    }
    auto transport_packet = common::protocol::util::make_gateway_to_server(envelope, info);
    
    switch (module){
        case mmo::ids::Module::AUTH:
        case mmo::ids::Module::SESSION:
        case mmo::ids::Module::LOGIC:
        // route to logic server
        {
            auto channel = route_targets_.get(ROUTER_ID_LOGIC"/" + std::to_string(get_logic_id(envelope->header().player_id())));
            if (!channel) {
                // errorlog("logic server not connected");
            }else {
                uint32_t cmd = envelope->header().cmd();
                common::protocol::util::set_direction(cmd, mmo::ids::Direction::GW2LOGIC);
                transport_packet->mutable_header()->set_cmd(cmd);
                size_t size = transport_packet->ByteSizeLong();
                auto buffer = memory_reuse::get_buffer<uint8_t>(size);
                buffer->resize(size);
                transport_packet->SerializeToArray(buffer->data(), size);
                channel->write(buffer);
            }
            if (module != mmo::ids::Module::AUTH){
                break;
            }
        }
        case mmo::ids::Module::MOVE:
        case mmo::ids::Module::STATE:
        case mmo::ids::Module::SKILL:
        // route to battle server
        {
            auto channel = route_targets_.get(ROUTER_ID_BATTLE"/" + std::to_string(get_battle_id(envelope->header().player_id())));
            if (!channel) {
                errorlog("battle server not connected");
                return;
            }
            uint32_t cmd = envelope->header().cmd();
            common::protocol::util::set_direction(cmd, mmo::ids::Direction::GW2BATTLE);
            transport_packet->mutable_header()->set_cmd(cmd);
            size_t size = transport_packet->ByteSizeLong();
            auto buffer = memory_reuse::get_buffer<uint8_t>(size);
            buffer->resize(size);
            transport_packet->SerializeToArray(buffer->data(), size);
            channel->write(buffer);
            break;   
        }
        default:
            warninglog("unknown module: %d", module);
            break;
    }
    transport_packet->Clear();
}

void RouteCenter::on_connection(common::tcp::Channel channel, bool connected, const std::string& target_id){
    if (connected){
        route_targets_.insert(target_id, channel);
    }else {
        route_targets_.remove(target_id);
    }
}
void RouteCenter::on_message(common::tcp::Channel channel, void* data,size_t size, const std::string& target_id){
    auto transport_packet = memory_reuse::get_object<mmo::transport::ServerToGateway>();
    transport_packet->ParseFromArray(data, size);
    uint32_t cmd = transport_packet->cmd();
    uint64_t player_id = transport_packet->player_id();
    uint32_t direction = common::protocol::util::get_direction(cmd);
    uint32_t module = common::protocol::util::get_module(cmd);
    if (direction != mmo::ids::Direction::BATTLE2GW && direction != mmo::ids::Direction::LOGIC2GW){
        return;
    }
    if (module == mmo::ids::Module::INTERNAL || module == mmo::ids::Module::GATEWAY){
        uint32_t action = common::protocol::util::get_action(cmd);
        // internal message ...
        if (internal_callback_){
            internal_callback_(player_id, action);
        }
        return ;
    }else {
        // route to client
        auto envelope = common::protocol::util::make_envelope(transport_packet);
        int size = envelope->ByteSizeLong();
        auto buffer = memory_reuse::get_buffer<uint8_t>(size);
        buffer->resize(size);
        envelope->SerializeToArray(buffer->data(),size);
        envelope->Clear();
        // callback
        if (callback_){
            callback_(player_id, buffer);
        }
        return;
    }
}

}