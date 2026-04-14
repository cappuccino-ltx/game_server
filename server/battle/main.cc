

#include "controller/controller.hh"
#include <router_id.hh>


int main() {
    // add etcd todo ...
    battle::BattleController controller;
    controller.initListen(12345)
        .initNode()
        .initObjectManager(20, {
            .host = "localhost",
            .port = 6379,
            .db_id = 0,
            .keey_alive = true,
        })
        .initRegister("localhost:2379", ROUTER_ID_BATTLE"/1" , "127.0.0.1:12345")
        .start();
    
    return 0;
}
