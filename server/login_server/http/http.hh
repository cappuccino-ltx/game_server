#pragma once
#include <boost/asio.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/version.hpp>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <log.hh>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace web {
    namespace beast = boost::beast;
    namespace asio = boost::asio;
    namespace net = boost::asio::ip;
    namespace http = boost::beast::http;
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;

    using Request = http::request<http::string_body>;
    using Response = http::response<http::string_body>;

    class HttpSession;
    class HttpServer;
    class WebSocketSession;
    class WebSocketServer;

    template <class T, class... Arg> 
    class Dispatcher {
    public:
        using ptr = std::shared_ptr<Dispatcher>;
        Dispatcher() = default;
        void dispatcher(const T &key, Arg...arg) {
            // 检查是否有对应的回调函数
            if (handler.find(key) == handler.end()) {
                return;
            }
            handler[key](arg...);
        }
        void regisory(const T &key, const std::function<void(Arg... arg)> &callback) {
            // 一般是启动服务器之前 由主线程进行的注册工作，所以这里不需要考虑线程的安全问题
            // 如果需要实现运行时添加派发任务的话，这里是需要加锁的
            handler[key] = callback;
        }

    private:
        std::unordered_map<T, std::function<void(Arg... arg)>> handler;
    };

    struct HttpResource {
        std::string basedir;
        Dispatcher<http::verb, HttpSession*, std::shared_ptr<Request>, std::shared_ptr<Response>> dispate;
        std::vector<std::pair<std::regex, std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>, std::shared_ptr<HttpSession>)>>> _get;
        std::vector<std::pair<std::regex, std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>, std::shared_ptr<HttpSession>)>>> _post;
        std::vector<std::pair<std::regex, std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>, std::shared_ptr<HttpSession>)>>> _delete;
        std::vector<std::pair<std::regex, std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>, std::shared_ptr<HttpSession>)>>> _put;
    };
    class HttpSession : public std::enable_shared_from_this<HttpSession> {
        friend class HttpServer;

    public:
        using ptr = std::shared_ptr<HttpSession>;
        HttpSession(std::shared_ptr<net::tcp::socket> s, HttpResource *re) : sock(s), resource(re) {
        }
        std::shared_ptr<net::tcp::socket> socket() {
            return sock;
        }
        void run() {
            do_read();
        }
        // 异步操作执行完了之后的发送函数
        void send(std::shared_ptr<Response> response) {
            // 填充一些必要字段
            response->prepare_payload();
            do_write(response);
        }
        // 获取正则表达式匹配的结果
        std::string getParse(size_t index) {
            if (index < sm.size()) {
                return sm[index].str();
            }
            return std::string("");
        }

    private:
        // 处理读写
        void do_read() {
            auto request = std::make_shared<Request>();
            http::async_read(*sock, buffer, *request, 
                [self = shared_from_this(), request](beast::error_code ec, size_t bytes) {
                if (!ec) {
                    // self->response = http::response<http::string_body>{};
                    self->router(request);
                } else {
                    self->sock.reset();
                }
            });
        }
        void do_write(std::shared_ptr<Response> response) {
            http::async_write(*sock, *response, 
                [self = shared_from_this(), response](beast::error_code ec, size_t bytes) {
                if (!ec) {
                    if (response->keep_alive()) {
                        return self->do_read();
                    }
                }
                self->sock.reset();
            });
        }
        void router(std::shared_ptr<Request> request) {
            static std::unordered_map<std::string, std::string> _mime_msg = {
                {".aac", "audio/aac"},
                {".abw", "application/x-abiword"},
                {".arc", "application/x-freearc"},
                {".avi", "video/x-msvideo"},
                {".azw", "application/vnd.amazon.ebook"},
                {".bin", "application/octet-stream"},
                {".bmp", "image/bmp"},
                {".bz", "application/x-bzip"},
                {".bz2", "application/x-bzip2"},
                {".csh", "application/x-csh"},
                {".css", "text/css"},
                {".csv", "text/csv"},
                {".doc", "application/msword"},
                {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
                {".eot", "application/vnd.ms-fontobject"},
                {".epub", "application/epub+zip"},
                {".gif", "image/gif"},
                {".htm", "text/html"},
                {".html", "text/html"},
                {".ico", "image/vnd.microsoft.icon"},
                {".ics", "text/calendar"},
                {".jar", "application/java-archive"},
                {".jpeg", "image/jpeg"},
                {".jpg", "image/jpeg"},
                {".js", "text/javascript"},
                {".json", "application/json"},
                {".jsonld", "application/ld+json"},
                {".mid", "audio/midi"},
                {".midi", "audio/x-midi"},
                {".mjs", "text/javascript"},
                {".mp3", "audio/mpeg"},
                {".mpeg", "video/mpeg"},
                {".mpkg", "application/vnd.apple.installer+xml"},
                {".odp", "application/vnd.oasis.opendocument.presentation"},
                {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
                {".odt", "application/vnd.oasis.opendocument.text"},
                {".oga", "audio/ogg"},
                {".ogv", "video/ogg"},
                {".ogx", "application/ogg"},
                {".otf", "font/otf"},
                {".png", "image/png"},
                {".pdf", "application/pdf"},
                {".ppt", "application/vnd.ms-powerpoint"},
                {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
                {".rar", "application/x-rar-compressed"},
                {".rtf", "application/rtf"},
                {".sh", "application/x-sh"},
                {".svg", "image/svg+xml"},
                {".swf", "application/x-shockwave-flash"},
                {".tar", "application/x-tar"},
                {".tif", "image/tiff"},
                {".tiff", "image/tiff"},
                {".ttf", "font/ttf"},
                {".txt", "text/plain"},
                {".vsd", "application/vnd.visio"},
                {".wav", "audio/wav"},
                {".weba", "audio/webm"},
                {".webm", "video/webm"},
                {".webp", "image/webp"},
                {".woff", "font/woff"},
                {".woff2", "font/woff2"},
                {".xhtml", "application/xhtml+xml"},
                {".xls", "application/vnd.ms-excel"},
                {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
                {".xml", "application/xml"},
                {".xul", "application/vnd.mozilla.xul+xml"},
                {".zip", "application/zip"},
                {".3gp", "video/3gpp"},
                {".3g2", "video/3gpp2"},
                {".7z", "application/x-7z-compressed"}};
            // 分析请求，并填充一些基础字段
            auto response = std::make_shared<Response>();
            response->result(http::status::ok);
            response->version(request->version());
            response->keep_alive(request->keep_alive());    

            std::string request_path = request->target().to_string();
            if (request_path.back() == '/') {
                request_path += "index.html";
            }
            auto n = request_path.rfind('.');
            if (request->method() == http::verb::get && n != std::string::npos) {
                // 静态资源的请求
                std::string path = request_path;

                // 判断路径是否合法，比如跑出了wwwroot目录
                int count = 0;
                for (size_t index = 0; index < path.size(); index++) {
                    if (path[index] == '/') {
                        count++;
                    } else if (path[index] == '.') {
                        if (index + 2 < path.size() && path[index + 1] == '.' && path[index + 2] == '/') {
                            count--;
                            index += 2;
                        }
                    }
                    if (count < 1) {
                        break;
                    }
                }
                path = resource->basedir + path;
                if (count < 1) {
                    response->result(http::status::bad_request);
                } else {
                    std::ifstream file(path, std::ios::binary);
                    if (!file.is_open()) {
                        // 文件不存在
                        response->result(http::status::not_found);
                    } else {
                        // 读取文件并且写入reponse
                        std::string file_context((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
                        response->body() = file_context;

                        // 获取文件后缀并且填充对应的content-type
                        std::string suffix = request_path.substr(n);
                        response->set(boost::beast::http::field::content_type, _mime_msg[suffix]);
                        response->content_length(file_context.size());
                    }
                }

                if (response->result() != http::status::ok) {
                    // 设置错误相应 todo...
                }
                // 自动设置必要字段
                response->prepare_payload();
                // 发送响应，
                return do_write(response);
            }
            // 自定义的请求处理
            // 构建出一个异步发送响应的结构
            resource->dispate.dispatcher(request->method(), this, request, response);
        }

        void get(std::shared_ptr<Request> request, std::shared_ptr<Response> response) {
            for (auto &e : resource->_get) {
                // 用正则表达式来匹配请求的路径
                std::string path = request->target().to_string();
                if (std::regex_match(path, sm, e.first)) {
                    return e.second(request, response, shared_from_this());
                }
            }
            response->result(http::status::not_found);
            send(response);
        }
        void post(std::shared_ptr<Request> request, std::shared_ptr<Response> response) {
            for (auto &e : resource->_post) {
                // 用正则表达式来匹配请求的路径
                std::string path = request->target().to_string();
                if (std::regex_match(path, sm, e.first)) {
                    return e.second(request, response, shared_from_this());
                }
            }
            response->result(http::status::not_found);
            send(response);
        }
        void delete_(std::shared_ptr<Request> request, std::shared_ptr<Response> response) {
            for (auto &e : resource->_delete) {
                // 用正则表达式来匹配请求的路径
                std::string path = request->target().to_string();
                if (std::regex_match(path, sm, e.first)) {
                    return e.second(request, response, shared_from_this());
                }
            }
            response->result(http::status::not_found);
            send(response);
        }
        void put(std::shared_ptr<Request> request, std::shared_ptr<Response> response) {
            for (auto &e : resource->_put) {
                // 用正则表达式来匹配请求的路径
                std::string path = request->target().to_string();
                if (std::regex_match(path, sm, e.first)) {
                    return e.second(request, response, shared_from_this());
                }
            }
            response->result(http::status::not_found);
            send(response);
        }

    private:
        std::smatch sm;
        HttpResource *resource;
        std::shared_ptr<net::tcp::socket> sock;
        beast::flat_buffer buffer;
        // http::request<http::string_body> request;
        // http::response<http::string_body> response;
    };


    class HttpServer {
    public:
        // hardware_concurrency 这个函数返回当前机器上的核心数，当不能确定的时候，可能返回0，也防止用户传递小于0的数
        HttpServer(const std::string& ip, uint16_t port, int thread_num = std::thread::hardware_concurrency())
            : pool(thread_num < 1 ? 5 : thread_num), accepter(pool.get_executor()) {
            // net::tcp::endpoint point{web::net::make_address(ip), port};
            auto point = web::net::tcp::endpoint{web::net::make_address(ip), port};
            beast::error_code ec;
            beast::error_code ret = accepter.open(point.protocol(), ec);
            if (ec) {
                exit(-1);
            }
            // 开启地址重用
            ret = accepter.set_option(asio::socket_base::reuse_address(true), ec);
            if (ec) {
                exit(-1);
            }
            ret = accepter.bind(point, ec);
            if (ec) {
                exit(-1);
            }
            ret = accepter.listen(10, ec);
            if (ec) {
                exit(-1);
            }
        }

        void start() {
            do_accept();
            if (resource.basedir == "") {
                // 如果没有设置静态资源路径的话，那就设置成当前路径
                resource.basedir = ".";
            }
            // 启动服务器之前，对注册的请求处理函数进行注册，
            resource.dispate.regisory(http::verb::get, std::bind(&HttpSession::get, _1, _2, _3));
            resource.dispate.regisory(http::verb::post, std::bind(&HttpSession::post, _1, _2,_3));
            resource.dispate.regisory(http::verb::delete_, std::bind(&HttpSession::delete_, _1,_2,_3));
            resource.dispate.regisory(http::verb::put, std::bind(&HttpSession::put, _1,_2,_3));
            // 添加主线程到线程池中
            pool.attach();
        }
        void setbasedir(const std::string &path) {
            resource.basedir = path;
            if (resource.basedir.back() == '/') {
                resource.basedir.pop_back();
            }
        }
        void Get(const std::string &path, const std::function<void(const std::shared_ptr<Request>, const std::shared_ptr<Response>, std::shared_ptr<HttpSession>)> &handler) {
            resource._get.push_back(std::make_pair(std::regex(path), handler));
        }
        void Post(const std::string &path, const std::function<void(const std::shared_ptr<Request>, const std::shared_ptr<Response>, std::shared_ptr<HttpSession>)> &handler) {
            resource._post.push_back(std::make_pair(std::regex(path), handler));
        }
        void Put(const std::string &path, const std::function<void(const std::shared_ptr<Request>, const std::shared_ptr<Response>, std::shared_ptr<HttpSession>)> &handler) {
            resource._put.push_back(std::make_pair(std::regex(path), handler));
        }
        void Delete(const std::string &path,
                    const std::function<void(const std::shared_ptr<Request>, const std::shared_ptr<Response>, std::shared_ptr<HttpSession>)> &handler) {
            resource._delete.push_back(std::make_pair(std::regex(path), handler));
        }

    private:
        void do_accept() {
            auto socket = std::make_shared<net::tcp::socket>(pool.get_executor());
            accepter.async_accept(*socket, [this, socket](beast::error_code ec) {
                if (!ec) {
                    // 创建Session
                    std::make_shared<HttpSession>(socket, &resource)->run();
                } else {
                    // std::cout << "监听错误:" << ec.message() << std::endl;
                }
                this->do_accept();
            });
        }

    private:
        HttpResource resource;
        asio::thread_pool pool;
        boost::asio::ip::tcp::acceptor accepter;
    };

} // namespace web


// int main() {
//     std::string web_path = "./wwwroot";
//     web::HttpServer server("0.0.0.0", 8080);
//     server.setbasedir(web_path);

//     server.Get("^/hello$", [&](std::shared_ptr<web::Request> req, std::shared_ptr<web::Response> rsp, std::shared_ptr<web::HttpSession> session) {
//         infolog << "get hello";
//         rsp->body() = "get hello";
//         session->send(rsp);
//     });
//     server.Get(R"(^/hello/(\d+)$)", [&](std::shared_ptr<web::Request> req, std::shared_ptr<web::Response> rsp, std::shared_ptr<web::HttpSession> session) {
//         infolog << "get hello " << session->getParse(1);
//         rsp->body() = "get hello " + session->getParse(1);
//         session->send(rsp);
//     });
//     server.Post("^/user/login$", [&](std::shared_ptr<web::Request> req, std::shared_ptr<web::Response> rsp, std::shared_ptr<web::HttpSession> session) {
//         infolog << "login success";
//         rsp->body() = "login success";
//         session->send(rsp);
//     });
//     server.Put("^/putfile/(.*)$", [&](std::shared_ptr<web::Request> req, std::shared_ptr<web::Response> rsp, std::shared_ptr<web::HttpSession> session) {
//         std::ofstream out(web_path + "/" + session->getParse(1), std::ios::binary);
//         if (out.write(req->body().data(), req->body().size())) {
//             infolog << "putfile success";
//             rsp->body() = "putfile success";
//         } else {
//             infolog << "putfile failed";
//             rsp->body() = "putfile failed";
//         }
//         session->send(rsp);
//     });
//     server.Delete("^/deletefile/(.*)$", [&](std::shared_ptr<web::Request> req, std::shared_ptr<web::Response> rsp, std::shared_ptr<web::HttpSession> session) {
//         std::string file = web_path + "/" + session->getParse(1);
//         int n = unlink(file.c_str());
//         if (n == 0) {
//             infolog << "deletefile success";
//             rsp->body() = "deletefile success";
//         } else {
//             infolog << "deletefile failed";
//             rsp->body() = "deletefile failed";
//         }
//         session->send(rsp);
//     });

//     server.start();
//     return 0;
// }