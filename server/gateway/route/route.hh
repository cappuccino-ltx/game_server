#pragma once 

#include <functional>
#include <memory>

#include <router_id.hh>
#include <protocol.hh>
#include <authenticate.hh>
#include <internal_tcp.hh>
#include <log.hh>

#include "envelope.pb.h"

#include "concurrent_map.hh"



namespace gateway{
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

struct RouteCenterConfig{
    int listen_port = 0;  // 监听端口
    int write_thread_n; // 最多有几个线程会通过 Channel 发送数据
};

class RouteCenter{
public:
    RouteCenter(const RouteCenterConfig& config);
    void async_start();
    void add_route_target(const std::string& target_id, const std::string& ip, int port);
    void set_send_to_client_callback(const std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)>& callback);
    void set_internal_callback(const std::function<void(uint64_t, uint32_t)>& callback);
    void client_to_route(std::shared_ptr<mmo::transport::Envelope> envelope , std::shared_ptr<common::PlayerInfo> info);
private:
    void on_connection(common::tcp::Channel channel, bool connected, const std::string& target_id);
    void on_message(common::tcp::Channel channel, void* data,size_t size, const std::string& target_id);

private:

    common::tcp::InternalTcp tcp_;
    common::ConcurrentMap<std::string, common::tcp::Channel> route_targets_;
    std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback_;
    std::function<void(uint64_t, uint32_t)> internal_callback_;
};

} // namespace gateway
