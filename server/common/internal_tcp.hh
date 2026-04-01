#pragma once 


#include <boost/asio.hpp>
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
    void do_write(std::shared_ptr<std::vector<uint8_t>> data){
        if (data) {
            std::copy(data->data(),data->data() + data->size(),write_buffer_.data() + write_size_);
            write_size_ += data->size();
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
                if (write_size_ == 0){
                    do_write(nullptr);
                }
            }else {
                // 处理写入失败
                do_close();
            }
        });
    }
private:
    void do_close(){
        if (close_callback_) {
            close_callback_(shared_from_this());
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
    std::function<int32_t(std::shared_ptr<_channel>,void*,uint32_t)> read_callback_;
    std::function<void(std::shared_ptr<_channel>)> close_callback_;
    std::function<void(std::shared_ptr<_channel>)> remove_callback_;
}; // class _channel
using Channel = std::shared_ptr<_channel>;

class InternalTcp{
public:
    InternalTcp(int port)
    :io_context_(),
    acceptor_(io_context_, btcp::v4(),port)
    {
        
    }
    ~InternalTcp() = default;

private:
    void do_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, btcp::socket socket){
            if(!ec){
                Channel channel = std::make_shared<_channel>(std::move(socket));
                channel->remove_callback_ = std::bind(&InternalTcp::close_channel,this,std::placeholders::_1);
                channel->read_callback_ = read_callback_;
                channel->close_callback_ = close_callback_;
                channels_.insert(channel);
                channel->do_read();
            }
            do_accept();
        });
    }

private:
    void close_channel(Channel channel){
        channels_.erase(channel);
    }

private:
    asio::io_context io_context_;
    btcp::acceptor acceptor_;
    std::unordered_set<Channel> channels_;
    std::function<int32_t(std::shared_ptr<_channel>,void*,uint32_t)> read_callback_;
    std::function<void(std::shared_ptr<_channel>)> close_callback_;
}; // class InternalTcp
} // namespace tcp
} // namespace common