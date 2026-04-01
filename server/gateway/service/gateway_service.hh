#pragma once

#include <akcp/server.hh>
#include <log.hh>
#include <functional>


namespace gateway {

struct gateway_service_config{
    int io_thread_n;
    int timeout;
};




class GatewayService{
public:
    GatewayService(gateway_service_config config);

    void set_message_callback(const std::function<void(kcp::channel_view, kcp::packet)>& callback);
    void set_disconnect_callback(const std::function<void(kcp::channel_view)>& callback);
    void start(int port,int core_begin = -1, int core_end = -1);

private:
    void on_connect(kcp::channel_view channel, bool linked);
    void on_message(kcp::channel_view channel, kcp::packet packet);

private:
    kcp::server server;
    std::function<void(kcp::channel_view, kcp::packet)> message_callback;
    std::function<void(kcp::channel_view)> disconnect_callback;
}; // class BattleService

} // namespace service