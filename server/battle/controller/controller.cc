

#include "controller.hh"
#include "listen/battle_listen.hh"
#include "log.hh"
#include "node_module/grid_aoi_node.hh"

namespace battle{

BattleController& BattleController::initListen(int port)
{
    listen_ = std::make_shared<BattleListen>(port);
    listen_->set_message_callback([this](const std::shared_ptr<mmo::transport::GatewayToServer>& msg)
    {
        // 处理消息
        if (object_manager_) {
            object_manager_->push_msg(msg);
        }
    });
    return *this;
}

BattleController& BattleController::initNode()
{
    node_ = std::make_shared<GridAoiNode>();
    return *this;
}

BattleController& BattleController::initObjectManager(int tick, const redis::RedisConfig& redis_config)
{
    if (!node_ || !listen_)
    {
        errorlog("node_ or listen_ is not initialized");
        exit(1);
    }
    object_manager_ = std::make_shared<ObjectManager>(
        tick, 
        redis::redis_build::build(
            redis_config.host, 
            redis_config.port, 
            redis_config.db_id, 
            redis_config.keey_alive
        )
    );
    object_manager_->set_node(node_);
    auto send_player = bind(&BattleListen::send_to_player, listen_.get(), std::placeholders::_1, std::placeholders::_2);
    object_manager_->set_send_message_to_player_callback(send_player);
    // send message to gateway  ... 
    return *this;
}
BattleController& BattleController::initRegister(const std::string& etcd_host, const std::string& register_path, const std::string& register_value)
{
    register_ = std::make_shared<common::Register>(etcd_host);
    register_->registory(register_path, register_value);
    return *this;
}



void BattleController::start()
{
    listen_->start();
}

} // namespace battle
