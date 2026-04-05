

#include <chrono>
#include <internal_tcp.hh>
#include <log.hh>
#include <memory_reuse.hh>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        errorlog("{} client/server port", argv[0]);
        return 1;
    }
    bool is_server = strcmp(argv[1],"server") == 0;
    bool is_client = strcmp(argv[1],"client") == 0;
    int port = atoi(argv[2]);
    int target_port = 0;
    if (is_client) {
        target_port = atoi(argv[3]);
    }

    common::tcp::InternalTcp tcp(port,1);
    tcp.set_connection_callback([is_client](common::tcp::Channel channel, bool connected,const std::string& flag){
        if (connected) {
            infolog("connected to {}", flag);
            if (is_client) {
                auto cur = std::chrono::system_clock::now().time_since_epoch().count();
                std::string str = std::to_string(cur);
                uint32_t size = str.size();
                auto buffer = memory_reuse::get_buffer<uint8_t>(size);
                buffer->resize(size);
                memcpy(buffer->data(),str.c_str(),size);
                channel->write(buffer);
            }
        } else {
            infolog("disconnected from {}", flag);
        }
    });
    tcp.set_message_callback([is_server,is_client](common::tcp::Channel channel,void* data,uint32_t size,const std::string& flag){
        infolog("read {} bytes : {}", size, (char*)data);
        if(is_server){
            auto buffer = memory_reuse::get_buffer<uint8_t>(size);
            buffer->resize(size);
            memcpy(buffer->data(),data,size);
            channel->write(buffer);
        }else if (is_client){
            auto cur = std::chrono::system_clock::now().time_since_epoch().count();
            std::string str = std::to_string(cur);
            infolog("current time {}", str);
            std::string read_str = std::string((char*)data,size);
            long send_time = std::stol(read_str);
            infolog("send time {}", send_time);
            infolog("latency {}", cur - send_time);
            channel->disconnect();
        }
    });
    if(is_server){
        tcp.sync_start();
    }
    if(is_client){
        tcp.async_start();
        tcp.connect("192.168.1.4", target_port, "gateway");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        tcp.stop();
    }
}
