#pragma once

#include <atomic>
#include <memory>
#include <odb/mysql/database.hxx>
#include <string>
#include <vector>

#include "redis.hh"
#include "mysql.hh"
#include "login.pb.h"
#include "user_operator.hh"
#include "util.hh"
#include "user_redis.h"


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



class LoginService{
public:
    using ptr = std::shared_ptr<LoginService>;
    LoginService(const std::shared_ptr<odb::mysql::database>& odb
        , const std::shared_ptr<sw::redis::Redis>& redis):
        odb_(std::make_shared<UserData>(odb)),
        redis_(std::make_shared<UserRedis>(redis))
    {}
    // 登录逻辑
    void login(const mmo::login::C2L_LoginReq& req,mmo::login::L2C_LoginRsp& rsp){
        auto handler = [&](const bool ok,const std::string& reason ){
            rsp.set_ok(ok);
            rsp.set_reason(reason);
            return;
        };
        // 1. 从数据库中校验账号密码
        std::string reason;
        if (!CheckAccount(req.account(), req.password(),reason)) {
            return handler(false,reason);
        }
        // 2. 从redis中查询是否已经登录过，如果已经登录过，直接返回之前的token和网关信息
        if (token_exists(req.token())) {
            redis_->RefreshToken(req.token(), 1800); 
            auto player_id = redis_->GetPlayerId(req.token());
            if (player_id) {
                rsp.set_token(req.token());
                rsp.set_player_id(*player_id);
                GatewayInfo gws = select_gateway();
                rsp.set_gateway_ip(gws.host);
                rsp.set_gateway_port(gws.port);
                return handler(true, "登录成功");
            }
        }
        std::string token = generate_token();
        // 3. 网关选择
        GatewayInfo gateway = select_gateway();
        // 4. 写入redis
        write_to_redis(req.account(), token, gateway);
        // 5. 返回登录结果
        rsp.set_ok(true);
        rsp.set_reason("登录成功");
        rsp.set_token(token);
        // rsp.mutable_gateway()->set_host(gateway.host);
        // rsp.mutable_gateway()->set_port(gateway.port);
    }
private:
    // token生成
    std::string generate_token(){
        return Generate_UUID::generate_uuid();
    }
    // 网关选择
    GatewayInfo select_gateway(){
        // 简单的轮询算法
        size_t idx = index++ % gws.size();
        gws[idx].load++; // 增加负载
        return gws[idx];
    }
    // 查询token是否存在
    bool token_exists(const std::string& token){
        return redis_->exists(token);
    }
    // 写入redis
    void write_to_redis(const std::string& account, const std::string& token
        , const GatewayInfo& gateway){
        // 构建登录信息
        // mmo::login::LoginInfo login_info;
        // login_info.set_account(account);
        // login_info.set_token(token);
        // login_info.mutable_gateway()->set_host(gateway.host);
        // login_info.mutable_gateway()->set_port(gateway.port);
        // // 序列化
        // std::string value;
        // login_info.SerializeToString(&value);
        // // 写入redis，设置过期时间为30分钟
        // redis_.setex("login:" + account, value, 1800);
    }
    // 从数据库中校验账号
    bool CheckAccount(const std::string& username, const std::string& password,std::string& reason){

    }

private:
    UserData::ptr odb_;
    UserRedis::ptr redis_;
    std::atomic<size_t> index{0};
};



class LoginServiceBuild{
public:
    LoginServiceBuild(){}
    
    LoginServiceBuild& make_odb_client(
        const std::string& user,
        const std::string& passwd,
        const std::string& db_name,
        const std::string& host,
        size_t port,
        const std::string& charset,
        size_t conn_pool_num
    ) {
        _odb = mysql::mysql_build::build(user, passwd, db_name, host, port, charset, conn_pool_num);
        return *this;
    }
    LoginServiceBuild& make_redis_client(const std::string& host,int port, int db_id,bool keepalive = true) {
        _redis = redis::redis_build::build(host, port, db_id,keepalive);
        return *this;
    }
    LoginService::ptr build() {
    if (!_odb) {
        errorlog << "failed to _odb uninitizalier";
        abort();
    }
    if (!_redis) {
        errorlog << "failed to _redis uninitizalier";
        abort();
    }
    return std::shared_ptr<LoginService>(new LoginService(_odb, _redis));
    }

private:
    shared_ptr<odb::mysql::database> _odb;
    std::shared_ptr<sw::redis::Redis> _redis ;
};