

#include <controller/gateway_controller.hh>

int main() {
    gateway::Controller controller;
    controller.init_authentication({
        .host = "127.0.0.1", // redis ip
        .port = 6379, // redis port
        .db_id = 0, //  redis db
        .keey_alive = true, 
        .thread_count = 1 // redis thread num
    }).init_udp_service({
        .io_thread_n = 4, // udp io thread num
        .timeout = 10 // s
    }).init_route({
        .listen_port = 10001, // internal tcp port
        .write_thread_n = 4 // 指的是udp 服务的线程数, 必须指定, 内部为初始化优化过的无锁队列,
    }).init_discovery({
        .host = "127.0.0.1:2379",
        .base_dir = ROUTER_ID_INTERNAL_BASE
    }).init_register({
        .host = "127.0.0.1:2379",
        .base_dir = ROUTER_ID_GATEWAY"/register", //
        .register_value = "127.0.0.1:10001" // internal tcp host
    }).start(8080); // foreign udp port
    return 0;
}