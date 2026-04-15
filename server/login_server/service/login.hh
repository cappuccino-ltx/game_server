#pragma once

#include <atomic>
#include <cstdint>
// #include <etcd/Client.hpp>
// #include <etcd/SyncClient.hpp>
#include <functional>
#include <memory>
#include <odb/mysql/database.hxx>
#include <string>
#include <unordered_map>
#include <vector>
#include "redis.hh"
#include "authenticate.hh"
#include "mysql.hh"
#include "login.pb.h"
#include "internal.pb.h"
#include "user/user_operator.hh"
#include "util.hh"
#include "etcd.hh"
#include "http/http.hh"
#include "router_id.hh"
#include "internal_tcp.hh"
#include <memory_reuse.hh>

namespace login{

struct AuthenticationConfig_{
    std::string host = "localhost";
    int port = 6379;
    int db_id = 0;
    bool keey_alive = true;
    int thread_count = 1;
};

struct GatewayInfo{
    int id;
    std::string host;
    int port;
    int load;
};



class GatewaySelector {
public:
    using ptr = std::shared_ptr<GatewaySelector>;
    GatewaySelector(
        const std::string& host, 
        const std::string& base_dir, 
        const std::function<void(uint32_t,const std::string&)>& battle_online_notify);

    std::pair<uint32_t, std::string> pick_one();

    std::string get_battle_server(uint32_t id);
    
private:
    std::vector<std::pair<uint32_t,std::string>> gateways_;
    std::unordered_map<uint32_t, std::string> battles_;
    std::mutex mutex_;
    common::Discovery::ptr discovery_;
};

class LoginService{
public:
    using ptr = std::shared_ptr<LoginService>;
    LoginService(
        const std::shared_ptr<odb::mysql::database>& odb, 
        const std::shared_ptr<sw::redis::Redis>& redis,
        const std::string& etcd_host,
        const std::string& base_dir,
        int http_thread_num
    );
    // 登录逻辑
    void login(std::shared_ptr<mmo::login::C2L_LoginReq> req, std::shared_ptr<mmo::login::L2C_LoginRsp> rsp);
private:
    // token生成
    std::string generate_token();
    // PlayerID生成
    uint64_t generate_id();
    // 从数据库中校验账号
    bool CheckAccount(const std::string& act, const std::string& password,std::string& reason);
    // 用 player_id 计算或者查询 battle_id
    uint32_t get_battle_id(uint64_t player_id);
    // battle 上线
    void battle_online(uint32_t id, const std::string& host);
    void on_connect(common::tcp::Channel channel,bool linked,const std::string& flag);
    common::tcp::Channel get_battle_server(uint32_t id);
private:
    std::mutex mtx;
    std::unordered_map<uint32_t, common::tcp::Channel> battle_servers_;
    common::tcp::InternalTcp internal_tcp_;
    UserData::ptr odb_;
    redis::Redis redis_;
    GatewaySelector::ptr select_;
    
};

struct odb_config{
    std::string user;
    std::string password;
    std::string db_name;
    std::string host;
    std::string charset;
    int port;
    size_t conn_pool_size;
};

class HttpServer{
public:
    HttpServer(){}
    HttpServer& init_http(const std::string ip, uint16_t port, int thread_num);
    HttpServer& init_odb(const odb_config& config);
    HttpServer& init_redis(const AuthenticationConfig_& config);
    HttpServer& init_login_service(const std::string& etcd_host, const std::string& path, int http_thread_num);

    void start();

private:
    void login(std::shared_ptr<web::Request> req, 
            std::shared_ptr<web::Response> rsp, 
            std::shared_ptr<web::HttpSession> session);
private:
    std::shared_ptr<LoginService> login_;
    std::shared_ptr<odb::mysql::database> odb_;
    std::shared_ptr<sw::redis::Redis> redis_;
    std::shared_ptr<web::HttpServer> http_;
};

}