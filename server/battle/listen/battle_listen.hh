#pragma once

#include "forward.pb.h"

#include "internal_tcp.hh"

#include <cstdint>
#include <functional>
#include <string>
#include <concurrent_map.hh>
namespace battle {
using  std::placeholders::_1;
using  std::placeholders::_2;
using  std::placeholders::_3;
using  std::placeholders::_4;
class BattleListen {
public:
    BattleListen(int port);

    void set_message_callback(std::function<void(std::shared_ptr<mmo::transport::GatewayToServer>)> callback);

    void send_to_gateway(uint16_t id, const std::shared_ptr<std::vector<uint8_t>>& msg);

    void send_to_player(uint64_t player_id, const std::shared_ptr<std::vector<uint8_t>>& msg);

    void start();

private:
    void on_connect(common::tcp::Channel channel, bool linked, const std::string& flag);
    void on_message(common::tcp::Channel channel, void* data, size_t size, const std::string& flag);
    void send_kick_offline(common::tcp::Channel channel, uint64_t player_id);

private:
    common::tcp::InternalTcp _listen;
    common::ConcurrentMap<uint16_t, common::tcp::Channel> _channels;
    common::ConcurrentMap<uint64_t, uint16_t> _auth_channels;
    std::function<void(std::shared_ptr<mmo::transport::GatewayToServer>)> message_callback;
};

}
