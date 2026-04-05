#pragma once 

#include <functional>
#include <memory>

#include <router_id.hh>
#include <protocol.hh>
#include <authenticate.hh>
#include <internal_tcp.hh>
#include <log.hh>

#include "envelope.pb.h"

#include "concurrent_map/concurrent_map.hh"
#include "forward.pb.h"
#include "ids.pb.h"
#include "memory_reuse.hh"


namespace gateway{
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

struct RouteCenterConfig{
    int listen_port = 0;  // 监听端口
    int write_thread_n; // 最多有几个线程会通过 Channel 发送数据
};

class RouteCenter{
public:
    RouteCenter(const RouteCenterConfig& config)
    : tcp_(config.listen_port, config.write_thread_n)
    {
        tcp_.set_connection_callback(std::bind(&RouteCenter::on_connection, this, _1, _2, _3));
        tcp_.set_message_callback(std::bind(&RouteCenter::on_message, this, _1, _2, _3, _4));
    }
    void async_start(){
        tcp_.async_start();
    }

    void add_route_target(const std::string& target_id, const std::string& ip, int port){
        tcp_.connect(ip, port,target_id);
    }
    void set_send_to_client_callback(std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback){
        callback_ = callback;
    }

    void client_to_route(std::shared_ptr<mmo::transport::Envelope> envelope , std::shared_ptr<common::PlayerInfo> info){
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
                auto channel = route_targets_.get(ROUTER_ID_LOGIC);
                if (!channel) {
                    errorlog("logic server not connected");
                    return;
                }
                uint32_t cmd = envelope->header().cmd();
                common::protocol::util::set_direction(cmd, mmo::ids::Direction::GW2LOGIC);
                transport_packet->mutable_header()->set_cmd(cmd);
                size_t size = transport_packet->ByteSizeLong();
                auto buffer = memory_reuse::get_buffer<uint8_t>(size);
                buffer->resize(size);
                transport_packet->SerializeToArray(buffer->data(), size);
                channel->write(buffer);
                break;
            }
            case mmo::ids::Module::MOVE:
            case mmo::ids::Module::STATE:
            case mmo::ids::Module::SKILL:
            // route to battle server
            {
                auto channel = route_targets_.get(ROUTER_ID_BATTLE);
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

private:
    void on_connection(common::tcp::Channel channel, bool connected, const std::string& target_id){
        if (connected){
            route_targets_.insert(target_id, channel);
        }else {
            route_targets_.remove(target_id);
        }
    }
    void on_message(common::tcp::Channel channel, void* data,size_t size, const std::string& target_id){
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
            // internal message ...

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

private:

    common::tcp::InternalTcp tcp_;
    ConcurrentMap<std::string, common::tcp::Channel> route_targets_;
    std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback_;
};

} // namespace gateway
