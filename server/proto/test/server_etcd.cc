

#include <etcd.hh>

int main () {
    common::Register reg("127.0.0.1:2379");
    reg.registory("/game/demo/logic", "192.168.1.100:8080");
    reg.registory("/game/demo/battle", "192.168.1.100:8081");
    cin.get();
}
