
#include "gateway_listen.hh"
#include "memory_reuse.hh"
#include <cstdint>


namespace gateway {

GatewayService::GatewayService(gateway_service_config config){
    server.enable_muliti_thread(config.io_thread_n);
    server.set_connect_callback(std::bind(&GatewayService::on_connect,this,kcp::_1,kcp::_2));
    server.set_message_callback(std::bind(&GatewayService::on_message,this,kcp::_1,kcp::_2));
    server.set_connection_timeout(config.timeout);
    server.set_buffer_pool([](size_t size){
        return memory_reuse::get_buffer<uint8_t>(size);
    });
}

void GatewayService::set_message_callback(const std::function<void(kcp::channel_view, kcp::packet)>& callback){
    message_callback = callback;
}
void GatewayService::set_disconnect_callback(const std::function<void(kcp::channel_view)>& callback){
    disconnect_callback = callback;
}

void GatewayService::start(int port,int core_begin, int core_end ){
    debuglog("The server has been started on UDP port {}", port);
    server.start(port, core_begin, core_end);
}

void GatewayService::on_connect(kcp::channel_view channel, bool linked){
    if (linked) {
        infolog("Link incoming, channel address : {}", channel.get());
    }else {
        // disconnect
        if (disconnect_callback) disconnect_callback(channel);
    }
}
void GatewayService::on_message(kcp::channel_view channel, kcp::packet packet){
    // message
    if (message_callback) {
        message_callback(channel, packet);
    }
}

} // namespace service