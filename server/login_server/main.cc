

#include "service/login.hh"
#include <router_id.hh>


int main() {
    login::HttpServer server;
    server.init_http("0.0.0.0", 10000, 4)
        .init_redis({
            .host = "127.0.0.1", // redis ip
            .port = 6379, // redis port
            .db_id = 0, //  redis db
            .keey_alive = true, 
            .thread_count = 1 // redis thread num
        }).init_odb({
            .user = "ltx",
            .password = "544338",
            .db_name = "game_demo",
            .host = "127.0.0.1",
            .charset = "utf8",
            .port = 3306,
            .conn_pool_size = 5
        }).init_login_service("127.0.0.1:2379", ROUTER_ID_BASE, 4 + 1) // 加上主线程
        .start();
    return 0;
}