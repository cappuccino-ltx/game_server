
#include "battle_listen.hh"
#include "ids.pb.h"
#include "memory_reuse.hh"
#include "protocol.hh"

namespace battle {

    BattleListen::BattleListen(int port)
        :_listen(port)
    {
        _listen.set_connection_callback(std::bind(&BattleListen::on_connect,this,_1,_2,_3));
        _listen.set_message_callback(std::bind(&BattleListen::on_message,this,_1,_2,_3,_4));
    }

    void BattleListen::set_message_callback(std::function<void(std::shared_ptr<mmo::transport::GatewayToServer>)> callback){
        message_callback = callback;
    }

    void BattleListen::send_to_gateway(uint16_t id, const std::shared_ptr<std::vector<uint8_t>>& msg){
        auto channel = _channels.get(id);
        if (channel){
            channel->write(msg);
        }
    }

    void BattleListen::send_to_player(uint64_t player_id, const std::shared_ptr<std::vector<uint8_t>>& msg){
        auto id = _auth_channels.get(player_id);
        auto channel = _channels.get(id);
        if (channel){
            channel->write(msg);
        }
    }

    void BattleListen::on_connect(common::tcp::Channel channel, bool linked, const std::string& flag){
        if (linked) {
            infolog("Link incoming, channel address : {}", channel->endpoint().address().to_string());
        }else {
            // disconnect
            infolog("Link outgoing, channel address : {}", channel->endpoint().address().to_string());
            // _channels.erase(channel->id());
            _channels.remove(channel->id());
        }
    }
    void BattleListen::on_message(common::tcp::Channel channel, void* data, size_t size, const std::string& flag){
        // message
        auto msg = memory_reuse::get_object<mmo::transport::GatewayToServer>();
        msg->Clear();
        if (!msg->ParseFromArray(data, size)) {
            warninglog("parse GatewayToServer failed");
            return ;
        }
        // check channel init
        if (channel->id() == (uint16_t)-1){
            // init channel
            channel->id() = msg->header().battle_id();
            _channels.insert( channel->id(), channel);
        }
        auto cmd = msg->header().cmd();
        auto direction = common::protocol::util::get_direction(cmd);
        auto module = common::protocol::util::get_module(cmd);
        auto action = common::protocol::util::get_action(cmd);
        if (direction != mmo::ids::GW2BATTLE){
            warninglog("direction not GW2BATTLE {} : {}, {}, {}",channel->endpoint().port(), direction, module, action);
            return ;
        }
        if (module == mmo::ids::AUTH){
            // player_id -> channel init
            if (action == mmo::ids::AuthAction::AUTH_BIND_REQ){
                // auth bind req
                // check auth channel
                auto exist = _auth_channels.find(msg->header().player_id());
                auto channel_old = _auth_channels.get(msg->header().player_id());
                if (exist && !channel_old){
                    // 顶号请求或者断网重连
                    debuglog("auth bind req, player id {} already bind, switch to new channel", msg->header().player_id());
                    // 发送清理前一个 channel 的消息
                    send_kick_offline(_channels.get(exist),msg->header().player_id());
                }
                _auth_channels.insert(msg->header().player_id(), channel->id());
            }
        }
        if (message_callback) {
            message_callback(msg);
        }
    }
    void BattleListen::send_kick_offline(common::tcp::Channel channel, uint64_t player_id){
        // 发送清理前一个 channel 的消息
        auto msg = memory_reuse::get_object<mmo::transport::ServerToGateway>();
        msg->Clear();
        // set field
        msg->set_player_id(player_id);
        uint32_t cmd = 0;
        common::protocol::util::set_direction(cmd, mmo::ids::BATTLE2GW);
        common::protocol::util::set_module(cmd, mmo::ids::INTERNAL);
        common::protocol::util::set_action(cmd, mmo::ids::AuthAction::KICK_NTF);
        msg->set_cmd(cmd);
        std::string body = "Log in from another location";
        msg->set_body(body);
        // serializer message
        int size = msg->ByteSizeLong();
        auto buffer = memory_reuse::get_buffer<uint8_t>(size);
        buffer->resize(size);
        msg->SerializeToArray(buffer->data(), size);
        msg->Clear();
        channel->write(buffer);
    }
    void BattleListen::start(){
        _listen.sync_start();
    }


}
