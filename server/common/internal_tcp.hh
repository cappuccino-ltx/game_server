#pragma once 


#include <boost/asio.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_timer.hpp>
#include <future>
#include <memory>
#include <unordered_set>
#include <lockfree/lockfree.hh>

/*

    基于 asio 库进行的 tcp 服务器和客户端为一体的实现, 
    已解决粘包问题, 且支持多线程访问,暴露的函数都为线程安全的
    已关闭 nagle 算法, 以降低通信延迟
    用无锁队列进行线程间信息传递, 需要指定生产者数量, tcp 线程为 1 个
    
*/
namespace common{
namespace tcp{

namespace asio = boost::asio;
using btcp = asio::ip::tcp;
using  std::placeholders::_1;
using  std::placeholders::_2;
using  std::placeholders::_3;
#define DEFAULT_CHANNEL_BUFFER_SIZE (1024 * 64) // 64kb
#define DEFAULT_SOCKET_BUFFER_SIZE (1024 * 256) // 256kb
#define DEFAULT_WRITE_TIMER_INTERVAL (100) // 100us
#define DEFAULT_WRITE_MAX_PACKET_LIMIT (100) // 100 packet
// protocol 
// 4byte data size + data

class _channel : public std::enable_shared_from_this<_channel>{
    friend class InternalTcp;
public:
    _channel(btcp::socket&& socket,asio::io_context& io_context, int producer_n)
        :io_context_(io_context)
        ,socket_(std::move(socket))
        ,write_timer_(io_context_)
        ,read_buffer_(DEFAULT_CHANNEL_BUFFER_SIZE)
        ,write_buffer_(DEFAULT_CHANNEL_BUFFER_SIZE)
        ,write_queue_(producer_n,lockfree::queue_size::K2) // 可以根据实际情况调整
    {
        // 修改socket缓冲区大小为256kb
        socket_.set_option(asio::socket_base::send_buffer_size(DEFAULT_SOCKET_BUFFER_SIZE));
        socket_.set_option(asio::socket_base::receive_buffer_size(DEFAULT_SOCKET_BUFFER_SIZE));
        // 关闭 nagle
        socket_.set_option(asio::ip::tcp::no_delay(true));
    }
    void disconnect(){
        socket_.close();
    }
    
    void write(std::shared_ptr<std::vector<uint8_t>> data){
        while(!write_queue_.try_put(data));
        uint32_t ret = write_queue_data_size_.fetch_add(1);
        if (ret == 0) {
            io_context_.post([self = shared_from_this()](){
                self->do_write_timer(); 
            });
        }else if (ret == DEFAULT_WRITE_MAX_PACKET_LIMIT - 1){
            io_context_.post([self = shared_from_this()](){
                uint32_t size = self->write_queue_data_size_.load(std::memory_order_acquire);
                if (size >= 100 && self->write_timer_registered_){
                    self->write_timer_.cancel();
                }
            });
        }
    }

    
    btcp::endpoint endpoint(){
        return socket_.remote_endpoint();
    }
private:
    void do_close(){
        if (connection_callback_) {
            connection_callback_(shared_from_this(),false);
        }
        socket_.close();
        remove_callback_(shared_from_this());
    }
    void do_write_timer(){
        if (write_timer_registered_){
            return;
        }
        write_timer_registered_ = true;
        write_timer_.expires_from_now(asio::chrono::microseconds(DEFAULT_WRITE_TIMER_INTERVAL));
        write_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec){
            if(self->handler_write_queue()){
                self->do_write_timer();
            }
        });
    }

    bool handler_write_queue(){
        write_timer_registered_ = false;
        uint32_t size = write_queue_data_size_.load(std::memory_order_acquire);
        std::vector<std::shared_ptr<std::vector<uint8_t>>> datas;
        size_t ret = write_queue_.get_all(datas);
        size_t message_size = 0;
        for (auto& data : datas) {
            message_size += data->size();
        }
        // 保证空间足够
        message_size += sizeof(uint32_t) * datas.size();
        if (write_buffer_.size() - write_size_ < message_size){
            write_buffer_.resize(write_buffer_.size() + message_size);
        }
        // 拷贝 数据
        for (auto& data : datas) {
            uint32_t data_size = data->size();
            *(uint32_t*)(write_buffer_.data() + write_size_) = htonl(data_size);
            write_size_ += sizeof(uint32_t);
            std::copy(data->data(),data->data() + data->size(),write_buffer_.data() + write_size_);
            write_size_ += data->size();
        }
        // 发送数据
        if (!write_registered_){
            do_write();
        }
        size = write_queue_data_size_.fetch_sub(ret);
        return size != ret;
    }

    // thread not safe
    void do_write(){
        if(write_size_ == 0){
            return;
        }
        write_registered_ = true;
        asio::async_write(
            socket_, 
            asio::buffer(write_buffer_.data(), write_size_), 
            [self = shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if(!ec){    
                // 处理写入完成
                std::copy(self->write_buffer_.data() + bytes_transferred,self->write_buffer_.data() + self->write_size_,self->write_buffer_.data());
                self->write_size_ -= bytes_transferred;
                if (self->write_size_ > 0){
                    self->do_write();
                }else {
                    self->write_registered_ = false;
                }
            }else {
                // 处理写入失败
                self->do_close();
            }
        });
    }
    uint32_t on_message(void* data, uint32_t size){
        uint8_t* read_index = (uint8_t*)data;
        uint32_t head_size = sizeof(uint32_t);
        uint32_t ret = 0;
        for(;;){
            if (size < head_size) {
                return ret;
            }
            uint32_t msg_len = ntohl(*(uint32_t*)(read_index));
            if (size - head_size < msg_len) {
                return ret;
            }
            read_index += head_size;
            size -= head_size;
            if (read_callback_) {
                read_callback_(shared_from_this(),read_index,msg_len);
            }
            ret += head_size + msg_len;
            read_index += msg_len;
            size -= msg_len;
        }
        return ret;
    }
    void do_read(){
        socket_.async_read_some(
        asio::buffer(read_buffer_.data() + read_size_, DEFAULT_CHANNEL_BUFFER_SIZE - read_size_),
        [self = shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if(!ec){
                // 处理读取到的数据
                self->read_size_ += bytes_transferred;
                if (self->read_callback_) {
                    uint32_t handler_size = self->on_message(self->read_buffer_.data(),self->read_size_);
                    if(handler_size > 0 && self->read_size_ - handler_size > 0){
                        std::copy(self->read_buffer_.data() + handler_size,self->read_buffer_.data() + self->read_size_,self->read_buffer_.data());
                        self->read_size_ -= handler_size;
                    }
                }else {
                    // read_buffer_.clear();
                    self->read_size_ = 0;
                }
            }else {
                // 处理读取失败
                self->do_close();
                return;
            }
            self->do_read();
        });
    }

private:
    asio::io_context& io_context_;
    btcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    uint32_t read_size_{0};
    uint32_t write_size_{0};
    std::function<void(std::shared_ptr<_channel>,void*,uint32_t)> read_callback_;
    std::function<void(std::shared_ptr<_channel>,bool)> connection_callback_;
    std::function<void(std::shared_ptr<_channel>)> remove_callback_;
    lockfree::lfree_queue_mpsc<std::shared_ptr<std::vector<uint8_t>>> write_queue_;
    asio::system_timer write_timer_;
    std::atomic_uint32_t write_queue_data_size_ { 0 };
    bool write_timer_registered_{false};
    bool write_registered_{false};
}; // class _channel
using Channel = std::shared_ptr<_channel>;

class InternalTcp{
public:
    InternalTcp(int port, int producer_n)
    :io_context_(),
    work_(asio::make_work_guard(io_context_)),
    producer_n_(producer_n)
    {
        if (port > 0) {
            acceptor_ = std::make_shared<btcp::acceptor>(io_context_,btcp::endpoint(btcp::v4(),port));
            do_accept();
        }
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
    void post(const std::function<void()>& callback){
        io_context_.post(callback);
    }
    void stop(){
        for(auto& channel : channels_){
            channel->socket_.cancel();
        }
        work_.reset();
        io_context_.stop();
        end_.get_future().get();
    }
    void set_message_callback(std::function<void(std::shared_ptr<_channel>,void*,uint32_t,const std::string&)> callback){
        message_callback_ = callback;
    }
    void set_connection_callback(std::function<void(std::shared_ptr<_channel>,bool,const std::string&)> callback){
        connection_callback_ = callback;
    }

private:
    void do_accept(){
        acceptor_->async_accept([this](boost::system::error_code ec, btcp::socket socket){
            if(!ec){
                Channel channel = std::make_shared<_channel>(std::move(socket),io_context_,producer_n_);
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
                Channel channel = std::make_shared<_channel>(std::move(*socket),io_context_,producer_n_);
                channel_init(channel,flag);
                channels_.insert(channel);
                if (connection_callback_) {
                    connection_callback_(channel,true,flag);
                }
                channel->do_read();
            }
        });
    }
    void channel_init(Channel channel,const std::string& flag = ""){
        channel->read_callback_ = std::bind(message_callback_, _1,_2,_3,flag);
        channel->connection_callback_ = std::bind(connection_callback_, _1,_2,flag);
        channel->remove_callback_ = std::bind(&InternalTcp::close_channel,this,std::placeholders::_1);
    }

    void close_channel(Channel channel){
        channels_.erase(channel);
    }

private:
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type>  work_;
    std::shared_ptr<btcp::acceptor> acceptor_;
    std::unordered_set<Channel> channels_;
    std::function<void(std::shared_ptr<_channel>,void*,uint32_t,const std::string&)> message_callback_;
    std::function<void(std::shared_ptr<_channel>,bool,const std::string&)> connection_callback_;
    std::shared_ptr<std::thread> thread_;
    std::promise<bool> end_;
    int producer_n_;
}; // class InternalTcp

} // namespace tcp

} // namespace common