#pragma once 

#include "listen/battle_listen.hh"
#include "node_module/node.hh"
#include "object_manager/object_manager.hh"
#include "node_module/node.hh"
#include <etcd.hh>
#include <memory>


namespace battle{

class BattleController{
public:
    BattleController() = default;
    ~BattleController() = default;

    BattleController& initListen(int port);
    BattleController& initNode();
    BattleController& initObjectManager(int tick, const redis::RedisConfig& redis_config);
    BattleController& initRegister(const std::string& etcd_host, const std::string& register_path, const std::string& register_value);

    void start();

private:


private:
    std::shared_ptr<BattleListen> listen_;
    std::shared_ptr<node_interface> node_;
    std::shared_ptr<ObjectManager> object_manager_;
    //  etcd_;
    std::shared_ptr<common::Register> register_;
    
};


} // namespace battle