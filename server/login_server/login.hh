#pragma once

#include <atomic>
#include <cstdint>
// #include <etcd/Client.hpp>
// #include <etcd/SyncClient.hpp>
#include <memory>
#include <odb/mysql/database.hxx>
#include <string>
#include <vector>
#include "redis.hh"
#include "mysql.hh"
#include "login.pb.h"
#include "user_operator.hh"
#include "util.hh"
#include "etcd.hh"
#include "redis_store_name.hh"
#include "http/http.hh"
#include <memory_reuse.hh>


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

std::vector<GatewayInfo> gws = {
    {1, "localhost", 9001, 0},
    {2, "localhost", 9002, 0},
    {3, "localhost", 9003, 0},
};


class GatewaySelector {
public:
    using ptr = std::shared_ptr<GatewaySelector>;
    GatewaySelector(const std::string& host, const std::string& base_dir) {
        discovery_ = std::make_shared<common::Discovery>();

        discovery_->setHost(host)
            .setBaseDir(base_dir)
            .setUpdateCallback([this](const std::string &key, const std::string &value){
                infolog("host {} {}" ,key, value);
                std::unique_lock<std::mutex> lock;
                gateways_.push_back({key, value});
            }).setRemoveCallback([this](const std::string &key, const std::string &value){
                infolog("host {} {}" ,key, value);
                std::unique_lock<std::mutex> lock;
                for(auto it = gateways_.begin(); it != gateways_.end(); ++it){
                    if (it->first == key) {
                        gateways_.erase(it);
                        return;
                    }
                }
            }).start();
    }

    std::pair<std::string, std::string> pick_one() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gateways_.empty()) {
            return {};
        }
        // 轮询
        int idx = rand() % gateways_.size();
        return gateways_[idx];
    }

private:
    std::vector<std::pair<std::string,std::string>> gateways_;
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
        const std::string& base_dir
    ):
        odb_(std::make_shared<UserData>(odb)),
        redis_(redis),
        select_(std::make_shared<GatewaySelector>(etcd_host,base_dir))
    {}
    // 登录逻辑

    void login(std::shared_ptr<mmo::login::C2L_LoginReq> req, std::shared_ptr<mmo::login::L2C_LoginRsp> rsp){
        auto handler = [&](const bool ok,const std::string& reason ){
            rsp->set_ok(ok);
            rsp->set_reason(reason);
            return;
        };
        // 从数据库中校验账号密码
        std::string reason;
        if (!CheckAccount(req->account(), req->password(),reason)) {
            return handler(false,reason);
        }
        // 玩家id
        uint64_t player_id = generate_id();
        // 登录id
        uint64_t session_id = generate_id();
        // 网关选择 -> etcd
        auto gateway = select_->pick_one();
        size_t pos = gateway.first.rfind('/') + 1;
        std::string id_str = gateway.first.substr(pos);
        int gateway_id = std::stoi(id_str);
        // 写入redis PlayerInfo->redis
        std::string key = REDIS_STORE_NAME_SESSION_TOKEN + std::to_string(player_id);

        std::map<std::string, std::string> fields;
        fields["version"] = "1";
        fields["session_id"] = std::to_string(session_id);
        fields["gateway_id"] = gateway_id;
        fields["zone_id"] = "1";
        fields["battle_id"] = "0";

        redis_.hsetall(key, fields);

        // 返回登录结果
        rsp->set_ok(true);
        rsp->set_reason("登录成功");
        rsp->set_session_id(session_id);
        rsp->set_player_id(player_id);
        rsp->set_gateway_host(gateway.second);
    }
private:
    // token生成
    std::string generate_token(){
        return Generate_UUID::generate_uuid();
    }
    // PlayerID生成
    uint64_t generate_id(){
        return Generate_ID::generate_id();
    }
    // 网关选择
    GatewayInfo select_gateway(){
        size_t idx = index_++ % gws.size();
        gws[idx].load++;
        return gws[idx];
    }
    // 从数据库中校验账号
    bool CheckAccount(const std::string& act, const std::string& password,std::string& reason){
        return odb_->exists_by_account(act, password, reason);
    }

private:
    UserData::ptr odb_;
    redis::Redis redis_;
    uint32_t index_ {0};
    common::Discovery::ptr client_;
    GatewaySelector::ptr select_;
    std::string dir_;
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
    HttpServer& init_http(const std::string ip, uint16_t port){
        http_ = std::make_shared<web::HttpServer>(ip, port);
        http_->Post("^/user/login$", std::bind(&HttpServer::login, this,web::_1,web::_2,web::_3));
    }
    HttpServer& init_odb(const odb_config& config){
        odb_ = mysql::mysql_build::build(
            config.user,
            config.password,
            config.db_name,
            config.host,
            config.port,
            config.charset,
            config.conn_pool_size
        );
    }
    HttpServer& init_redis(const AuthenticationConfig_& config){
        redis_ = redis::redis_build::build(
            config.host, 
            config.port, 
            config.db_id, 
            config.keey_alive);
    }
    HttpServer& init_login_service(const std::string& etcd_host, const std::string& path){
        if (!odb_ || !redis_) {
            errorlog("odb or redis client uninitalizer");
            exit(-1);
        }
        login_ = std::make_shared<LoginService>(
            odb_,
            redis_,
            etcd_host,
            path
        );
    }

    void start(){
        http_->start();
    }

private:
    void login(std::shared_ptr<web::Request> req, 
            std::shared_ptr<web::Response> rsp, 
            std::shared_ptr<web::HttpSession> session)
    {
        auto& body = rsp->body();
        auto req_pro = memory_reuse::get_object<mmo::login::C2L_LoginReq>();
        auto rsp_pro = memory_reuse::get_object<mmo::login::L2C_LoginRsp>();
        req_pro->Clear();
        rsp_pro->Clear();
        // 反序列化
        if(!req_pro->ParseFromArray(body.data(), body.size())){
            // 反序列化失败
            session->send(rsp);
            return ;
        }
        // 处理登录逻辑
        login_->login(req_pro, rsp_pro);
        // 序列化
        size_t size = rsp_pro->ByteSizeLong();
        rsp->body().resize(size);
        rsp_pro->SerializeToArray(rsp->body().data(), rsp->body().size());
        // 手动触发send
        session->send(rsp);
    }
private:
    std::shared_ptr<LoginService> login_;
    std::shared_ptr<odb::mysql::database> odb_;
    std::shared_ptr<sw::redis::Redis> redis_;
    std::shared_ptr<web::HttpServer> http_;
};