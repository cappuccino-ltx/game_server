

#include "login.hh"
#include <router_id.hh>


int main() {
    HttpServer server;
    server.init_http("0.0.0.0", 8080)
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
        }).init_login_service("127.0.0.1:2379", ROUTER_ID_GATEWAY)
        .start();
    return 0;
}