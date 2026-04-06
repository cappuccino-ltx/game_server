

// #include <etcd.hh>

// int main () {
//     common::Discovery dis;
//     dis.setHost("127.0.0.1:2379")
//         .setBaseDir("/game/demo/gateway")
//         .setUpdateCallback([](const std::string &key, const std::string &value){
//             infolog("host {} {}" ,key, value);
//         }).setRemoveCallback([](const std::string &key, const std::string &value){
//             infolog("host {} {}" ,key, value);
//         }).start();
//     cin.get();
// }
