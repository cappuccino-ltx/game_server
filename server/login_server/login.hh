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
        // 从数据库中校验账号密码
        std::string reason;
        if (!CheckAccount(req.account(), req.password(),reason)) {
            return handler(false,reason);
        }
        // 从redis中查询是否已经登录过
        if (token_exists(req.session_id())) {
            redis_->Refresh(to_string(req.session_id()), 1800); 
            redis_->Refresh(req.account(), 1800);
            auto player_id = redis_->GetPlayerId(to_string(req.session_id()));
            if (player_id) {
                rsp.set_session_id(req.session_id());
                rsp.set_player_id(*player_id);
                GatewayInfo gws = select_gateway();
                rsp.set_gateway_ip(gws.host);
                rsp.set_gateway_port(gws.port);
                return handler(true, "登录成功");
            }
        }
        // 玩家id
        std::string player_id = generate_token();
        // 登录id
        std::string session_id = generate_token();
        // 网关选择
        GatewayInfo gateway = select_gateway();
        // 写入redis
        redis_->Set(session_id, req.account(), 1800);
        redis_->Set(player_id, req.account(), 1800);
        // 返回登录结果
        rsp.set_ok(true);
        rsp.set_reason("登录成功");
        rsp.set_session_id(stoi(session_id));
        rsp.set_player_id(player_id);
        rsp.set_gateway_ip(gateway.host);
        rsp.set_gateway_port(gateway.port);
    }
private:
    // token生成
    std::string generate_token(){
        return Generate_UUID::generate_uuid();
    }
    // 网关选择
    GatewayInfo select_gateway(){
        size_t idx = index++ % gws.size();
        gws[idx].load++;
        return gws[idx];
    }
    // 查询token是否存在
    bool token_exists(const uint64_t& session_id){
        return redis_->exists(std::to_string(session_id));
    }
    // 从数据库中校验账号
    bool CheckAccount(const std::string& act, const std::string& password,std::string& reason){
        return odb_->exists_by_account(act, password, reason);
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