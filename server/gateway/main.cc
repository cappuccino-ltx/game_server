
#include <internal_tcp.hh>
#include <iostream>

int main() {

    common::tcp::InternalTcp tcp(8080);
    tcp.set_read_callback([](common::tcp::Channel channel,void* data,uint32_t size){
        std::cout << "read " << size << " bytes" << std::endl;
        channel->do_write(data,size);
        return size;
    });
    tcp.set_connection_callback([](common::tcp::Channel channel,bool connected,const std::string& flag){
        if(connected){
            std::cout << "connection established" << std::endl;
        }else {
            std::cout << "connection closed" << std::endl;
        }
    });
    tcp.sync_start();
    return 0;
}