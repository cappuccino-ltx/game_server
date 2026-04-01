#pragma once 


#include <boost/asio.hpp>
#include <future>
#include <memory>
#include <unordered_set>

namespace common{
namespace tcp{

namespace asio = boost::asio;
using btcp = asio::ip::tcp;
#define DEFAULT_READ_BUFFER_SIZE 1024

class _channel : public std::enable_shared_from_this<_channel>{
    friend class InternalTcp;
public:
    _channel(btcp::socket&& socket)
        :socket_(std::move(socket))
    {}
    void do_read(){
        read_buffer_.resize(read_size_ + DEFAULT_READ_BUFFER_SIZE);
        socket_.async_read_some(asio::buffer(read_buffer_.data() + read_size_, DEFAULT_READ_BUFFER_SIZE),[this](boost::system::error_code ec, std::size_t bytes_transferred){
            if(!ec){
                // 处理读取到的数据
                read_size_ += bytes_transferred;
                if (read_callback_) {
                    uint32_t handler_size = read_callback_(shared_from_this(),read_buffer_.data(),read_size_);
                    if(handler_size > 0){
                        std::copy(read_buffer_.data() + handler_size,read_buffer_.data() + read_size_,read_buffer_.data());
                        read_size_ -= handler_size;
                    }
                }else {
                    read_buffer_.clear();
                }
            }else {
                // 处理读取失败
                do_close();
                return;
            }
            do_read();
        });
    }
    // thread not safe
    void do_write(const std::string& data){
        do_write(data.c_str(),data.size());
    }
    void do_write(const void* data, size_t size){
        write_buffer_.resize(write_size_ + size);
        if (data) {
            std::copy((char*)data,(char*)data + size,write_buffer_.data() + write_size_);
            write_size_ += size;
        }
        asio::async_write(
            socket_, 
            asio::buffer(write_buffer_.data(), write_size_), 
            [this, data](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if(!ec){    
                // 处理写入完成
                std::copy(write_buffer_.data() + bytes_transferred,write_buffer_.data() + write_size_,write_buffer_.data());
                write_size_ -= bytes_transferred;
                if (write_size_ > 0){
                    do_write(nullptr);
                }
            }else {
                // 处理写入失败
                do_close();
            }
        });
    }
    btcp::endpoint endpoint(){
        return socket_.remote_endpoint();
    }
private:
    void do_close(){
        if (connection_callback_) {
            connection_callback_(shared_from_this(),false,"");
        }
        socket_.close();
        remove_callback_(shared_from_this());
    }

private:
    btcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    uint32_t read_size_{0};
    uint32_t write_size_{0};
    std::function<uint32_t(std::shared_ptr<_channel>,void*,uint32_t)> read_callback_;
    std::function<void(std::shared_ptr<_channel>,bool,const std::string&)> connection_callback_;
    std::function<void(std::shared_ptr<_channel>)> remove_callback_;
}; // class _channel
using Channel = std::shared_ptr<_channel>;

class InternalTcp{
public:
    InternalTcp(int port)
    :io_context_(),
    acceptor_(io_context_, btcp::endpoint(btcp::v4(),port))
    {
        do_accept();
    }
    ~InternalTcp() = default;
    void sync_start(){
        io_context_.run();
    }
    void async_start(){
        thread_ = std::make_shared<std::thread>([this](){
            io_context_.run();
            end_.set_value(true);
        });
        thread_->detach();
    }
    void connect(const std::string& ip,int port,const std::string& flag = ""){
        io_context_.post([this,ip,port,flag](){
            do_connect(ip,port,flag);
        });
    }
    void disconnect(Channel channel){
        channel->socket_.cancel();
    }
    void stop(){
        for(auto& channel : channels_){
            channel->socket_.cancel();
        }
        io_context_.stop();
        end_.get_future().get();
    }
    void set_read_callback(std::function<uint32_t(std::shared_ptr<_channel>,void*,uint32_t)> callback){
        read_callback_ = callback;
    }
    void set_connection_callback(std::function<void(std::shared_ptr<_channel>,bool,const std::string&)> callback){
        connection_callback_ = callback;
    }

private:
    void do_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, btcp::socket socket){
            if(!ec){
                Channel channel = std::make_shared<_channel>(std::move(socket));
                channel_init(channel);
                channels_.insert(channel);
                if (connection_callback_) {
                    connection_callback_(channel,true,"");
                }
                channel->do_read();
            }
            do_accept();
        });
    }
    void do_connect(const std::string& ip,int port,const std::string& flag = ""){
        asio::ip::address address = asio::ip::address::from_string(ip);
        btcp::endpoint endpoint(address,port);
        std::shared_ptr<btcp::socket> socket = std::make_shared<btcp::socket>(io_context_);
        socket->async_connect(endpoint,[this,socket,flag](boost::system::error_code ec){
            if(!ec){
                Channel channel = std::make_shared<_channel>(std::move(*socket));
                channel_init(channel);
                channels_.insert(channel);
                if (connection_callback_) {
                    connection_callback_(channel,true,flag);
                }
                channel->do_read();
            }
        });
    }
    void channel_init(Channel channel){
        channel->read_callback_ = read_callback_;
        channel->connection_callback_ = connection_callback_;
        channel->remove_callback_ = std::bind(&InternalTcp::close_channel,this,std::placeholders::_1);
    }

    void close_channel(Channel channel){
        channels_.erase(channel);
    }

private:
    asio::io_context io_context_;
    btcp::acceptor acceptor_;
    std::unordered_set<Channel> channels_;
    std::function<uint32_t(std::shared_ptr<_channel>,void*,uint32_t)> read_callback_;
    std::function<void(std::shared_ptr<_channel>,bool,const std::string&)> connection_callback_;
    std::shared_ptr<std::thread> thread_;
    std::promise<bool> end_;
}; // class InternalTcp
} // namespace tcp
} // namespace common