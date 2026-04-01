#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <condition_variable>
#include <thread>
#include <utility>
#include <atomic>
#include <vector>

// ANSI 颜色代码
#define RESET "\033[0m"
#define GREEN "\033[32m"
#define CYAN "\033[36m"
#define YELLOW "\033[33m"
#define RED "\033[31m"
#define MAGENTA "\033[35m"


namespace ltx_log{


enum queue_size {
    K003 = 1llu << 5, // 32
    K01 = 1llu << 7,  // 128
    K05 = 1llu << 9,  // 512
    K1 = 1llu << 10,  // 1024
    K2 = 1llu << 11,  // 2048
    K4 = 1llu << 12,  // 4096
    K8 = 1llu << 13,  // 8192
    K16 = 1llu << 14, // 16384
    K32 = 1llu << 15 // 32768
}; // enum queue_size
namespace util{
static inline size_t get_proper_size(std::size_t n){
    if (n <= queue_size::K003) return queue_size::K003;
    else if (n <= queue_size::K01) return queue_size::K01;
    else if (n <= queue_size::K05) return queue_size::K05;
    else if (n <= queue_size::K1) return queue_size::K1;
    else if (n <= queue_size::K2) return queue_size::K2;
    else if (n <= queue_size::K4) return queue_size::K4;
    else if (n <= queue_size::K8) return queue_size::K8;
    else if (n <= queue_size::K16) return queue_size::K16;
    else return queue_size::K32;
}
template<typename T>
class optional {
private:
    alignas(T) unsigned char storage_[sizeof(T)];
    T* ptr() {
        return reinterpret_cast<T*>(storage_);
    }
    const T* ptr() const {
        return reinterpret_cast<const T*>(storage_);
    }

public:
    optional() noexcept = default;
    // reset
    void reset() {
        ptr()->~T();
    }
    void reset(const T& value) {
        new (storage_) T(value);
    }
    void reset(T&& value) {
        new (storage_) T(std::move(value));
    }
    T& value() {
        return *ptr();
    }
    const T& value() const {
        return *ptr();
    }
};
} // namespace util

#define LOCKFREE_QUEUE_SIZE_DEFAULT queue_size::K1

template<class T>
class lfree_queue {
private:
    struct slot{
        std::atomic_uint64_t seqence;
        util::optional<T> data;
    };
public:
    lfree_queue(ltx_log::queue_size size = LOCKFREE_QUEUE_SIZE_DEFAULT)
        :queue_size_(util::get_proper_size(size))
        ,buffer_(queue_size_)
    {
        // init seqence
        for(int i = 0; i < queue_size_; i++) {
            buffer_[i].seqence = i;
        }
    }

    template<typename U>
    bool try_put(U&& data){
        size_t tail = tail_r();
        size_t head = head_r();
        int index = tail % queue_size_;
        size_t seqence = seqence_a(index);
        if (seqence == tail) {
            if (tail_cas(tail)){
                buffer_[index].data.reset(std::forward<U>(data));
                ++seqence;
                seqence_store(index, (seqence));
                return true;
            }
        }
        return false;
    }
    
    bool try_get(T& data) {
        size_t tail = tail_r();
        size_t head = head_r();
        int index = head % queue_size_;
        size_t seqence = seqence_a(index);
        if (seqence == head + 1) {
            if (head_cas(head)){
                data = std::move(buffer_[index].data.value());
                buffer_[index].data.reset();
                seqence = head + queue_size_;
                seqence_store(index, (seqence));
                return true;
            }
        }
        return false;
    }

    size_t size_approx(){
        size_t h = head_r();
        size_t t = tail_r();
        return t - h;
    }

private:
    inline size_t head_r(){
        return head_.load(std::memory_order_relaxed);
    }
    inline size_t head_a(){
        return head_.load(std::memory_order_acquire);
    }
    inline bool head_cas(size_t& h){
        return head_.compare_exchange_strong(h, h + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }
    inline size_t tail_r(){
        return tail_.load(std::memory_order_relaxed);
    }
    inline size_t tail_a(){
        return tail_.load(std::memory_order_acquire);
    }
    inline bool tail_cas(size_t& t){
        return tail_.compare_exchange_strong(t, t + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }
    inline uint64_t seqence_r(int index){
        return buffer_[index].seqence.load(std::memory_order_relaxed);
    }
    inline uint64_t seqence_a(int index){
        return buffer_[index].seqence.load(std::memory_order_acquire);
    }
    inline void seqence_store(int index, uint64_t value){
        buffer_[index].seqence.store(value, std::memory_order_release);
    }

private:
    alignas(uint64_t) std::atomic_uint64_t head_ { 0 };
    alignas(uint64_t) std::atomic_uint64_t tail_ { 0 };
    int queue_size_;
    std::vector<slot> buffer_;
}; // template class lfree_queue


class Logger {
public:
    
    enum Level { INFO = 0, DEBUG, WARNING, ERROR, FATAL };

    template <typename T>
    struct InternalConfig{
        // 设置最低打印等级
        static Level log_level; 
        // 决定了日志输出的目标，""默认是co
        // #define default_out_stream ""ut， "out.txt"表示输出到文件，
        static std::string default_out_stream;
    };
    using Config = InternalConfig<void>;

    static void init_log(const std::string& file, Level level){
        Config::default_out_stream = file;
        Config::log_level = level;
    }

    class OutStream{
    public:
        using ptr = std::shared_ptr<OutStream>;
        static OutStream::ptr get(){
            static std::mutex mtx;
            static OutStream::ptr stance;
            if (stance.get() == nullptr) {
                std::unique_lock<std::mutex> lock(mtx);
                if (stance.get() == nullptr) {
                    stance = std::make_shared<OutStream>();
                }
            }
            return stance;
        }

        inline std::ostream& outfile() {
            if (out.is_open()) return out;
            return std::cout;
        }
        
        OutStream() {
            std::string file = Config::default_out_stream;
            if (file != "") {
                out.open(file,std::ios::app);
                if (!out.is_open()) {
                    std::cout << "failed to open file of log. please check file name of log";
                    exit(1);
                }
            }

            log_thread = new std::thread([this](){
                while (1) {
                    if (quit && task.size_approx() == 0) {
                        break;
                    }
                    std::string task_str;
                    bool ret = task.try_get(task_str);
                    if (ret){
                        outfile() << task_str;
                    }else if (task.size_approx() == 0){
                        std::unique_lock<std::mutex> lock(mtx);
                        cond.wait(lock);
                    }
                }
            });
        }
        ~OutStream() {
            quit = true;
            cond.notify_all();
            log_thread->join();
        }

        void push(const std::string& mas) {
            while(!task.try_put(mas)){
                std::this_thread::yield();
            }
            cond.notify_one();
        }
        
    private:
        bool quit = false;
        std::ofstream out;
        std::thread* log_thread = nullptr;
        lfree_queue<std::string> task { queue_size::K05 };
        std::mutex mtx;
        std::condition_variable cond;
    };

    class LogStream {
        friend class Logger;

    private:
        std::stringstream ss;
        const char       *file;
        int               line;
        Level             lvl;

        inline std::string getColor() const {
            if (Config::default_out_stream != ""){
                return "";
            }
            switch (lvl) {
            case INFO:
                return GREEN;
            case DEBUG:
                return CYAN;
            case WARNING:
                return YELLOW;
            case ERROR:
                return RED;
            case FATAL:
                return MAGENTA;
            }
            return RESET;
        }
        inline std::string getReset() const {
            if (Config::default_out_stream != ""){
                return "";
            }
            return RESET;
        }
        void format(std::stringstream& s) {
            char c;
            while(s.get(c)){
                ss << c;
            }
            return ;
        }
        template<class K, class... Args>
        void format(std::stringstream& s,K&& arg, Args&&... args) {
            char c = 0;
            bool found_left = false;
            while(s.get(c)){
                if (c == '{') {
                    found_left = true;
                    break;
                }
                ss << c;
            }
            if (found_left == false) {
                return;
            }
            found_left = false;
            s.get(c);
            if (c == '}') {
                found_left = true;
            }
            if (found_left == false) {
                ss << c;
                format(s,std::forward<K>(arg),std::forward<Args>(args)...);
                return ;
            }
            ss << std::forward<K>(arg);
            format(s,std::forward<Args>(args)...);
            return ;
        }

    public:
        LogStream(const char *file, int line, Level level)
            : file(file), line(line), lvl(level) {
        }
        LogStream(const LogStream &log)
            : file(log.file), line(log.line), lvl(log.lvl) {
        }

        template <typename T> LogStream &operator<<(const T &value) {
            ss << value;
            return *this;
        }
        std::string to_string_int128(__int128 v) {
            if (v == 0) return "0";
            bool neg = v < 0;
            unsigned __int128 x = neg ? (unsigned __int128)(-v) : (unsigned __int128)v;

            std::string s;
            while (x > 0) {
                int digit = (int)(x % 10);
                s.push_back(char('0' + digit));
                x /= 10;
            }
            if (neg) s.push_back('-');
            std::reverse(s.begin(), s.end());
            return s;
        }

        std::string to_string_uint128(unsigned __int128 v) {
            if (v == 0) return "0";
            std::string s;
            while (v > 0) {
                int digit = (int)(v % 10);
                s.push_back(char('0' + digit));
                v /= 10;
            }
            std::reverse(s.begin(), s.end());
            return s;
        }
        LogStream &operator<<(__int128 value) {
            ss << to_string_int128(value);
            return *this;
        }
        LogStream &operator<<(unsigned __int128 value) {
            ss << to_string_uint128(value);
            return *this;
        }
        LogStream &operator<<(std::ostream& (*manip)(std::ostream&)) {
            manip(ss);
            return *this;
        }
        
        template<class... Args>
        LogStream & operator()(const std::string& fmt,Args&&... args) {
            std::stringstream s(fmt);
            format(s,std::forward<Args>(args)...);
            return *this;
        }


        ~LogStream() {
            if (lvl >= Config::log_level) {
                std::stringstream out;
                std::time_t now = std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::now());
                out << getColor() << "["
                          << std::put_time(std::localtime(&now),
                                           "%Y-%m-%d %H:%M:%S")
                          << "] [";

                switch (lvl) {
                case INFO:
                    out << "INFO";
                    break;
                case DEBUG:
                    out << "DEBUG";
                    break;
                case WARNING:
                    out << "WARNING";
                    break;
                case ERROR:
                    out << "ERROR";
                    break;
                case FATAL:
                    out << "FATAL";
                    break;
                }

                out << "] [" << file << ":" << line << "] " << ss.str()
                          << getReset() << std::endl;
                OutStream::get()->push(out.str());
            }
        }
    };

    static LogStream info(const char *file, int line) {
        return LogStream(file, line, INFO);
    }
    static LogStream debug(const char *file, int line) {
        return LogStream(file, line, DEBUG);
    }
    static LogStream warning(const char *file, int line) {
        return LogStream(file, line, WARNING);
    }
    static LogStream error(const char *file, int line) {
        return LogStream(file, line, ERROR);
    }
    static LogStream fatal(const char *file, int line) {
        return LogStream(file, line, FATAL);
    }
}; // class __Logger__

template <typename T>
Logger::Level Logger::InternalConfig<T>::log_level = INFO;
template <typename T>
std::string Logger::InternalConfig<T>::default_out_stream = "";

} // namespace ltx_log

// 宏定义，用于简化使用
#define infolog ltx_log::Logger::info(__FILE__, __LINE__)
#define debuglog ltx_log::Logger::debug(__FILE__, __LINE__)
#define warninglog ltx_log::Logger::warning(__FILE__, __LINE__)
#define errorlog ltx_log::Logger::error(__FILE__, __LINE__)
#define fatallog ltx_log::Logger::fatal(__FILE__, __LINE__)

#define log_init(file, level) ltx_log::Logger::init_log(file, level)
#define log_level_info ltx_log::Logger::Level::INFO
#define log_level_debug ltx_log::Logger::Level::DEBUG
#define log_level_warn ltx_log::Logger::Level::WARNING
#define log_level_error ltx_log::Logger::Level::ERROR
#define log_level_fatal ltx_log::Logger::Level::FATAL
#define log_int_to_level(level) ((ltx_log::Logger::Level)(level))

