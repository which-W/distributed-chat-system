#include "ResourceService.h"
#include "GrpcTlsSupport.h"
#include "InternalRpcAuth.h"
#include "KeyedExecutor.h"
#include "ChatLogger.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <csignal>

namespace resource {
namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;
class Connection : public std::enable_shared_from_this<Connection> {
    beast::tcp_stream stream_; beast::flat_buffer buffer_{8192};
    http::request_parser<http::string_body> parser_;
    Service& service_; chat::runtime::KeyedExecutor& workers_;
    http::response<http::string_body> response_;
    std::unique_ptr<http::response_serializer<http::string_body>> serializer_;
    Response data_; std::vector<unsigned char> chunk_; std::uint64_t offset_ = 0, remaining_ = 0;
    static inline std::atomic<unsigned> active_{0};
public:
    Connection(tcp::socket socket, Service& service, chat::runtime::KeyedExecutor& workers)
        : stream_(std::move(socket)), service_(service), workers_(workers) { ++active_; parser_.header_limit(8192); parser_.body_limit(5 * 1024 * 1024); }
    ~Connection() { --active_; }
    void start() {
        if (active_ > 128) return;
        stream_.expires_after(std::chrono::seconds(30));
        http::async_read_header(stream_, buffer_, parser_, [self=shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) return;
            const auto target = self->parser_.get().target();
            const std::uint64_t limit = target == "/api/resources/v1/users/me/avatar" ? 5 * 1024 * 1024 : 256 * 1024;
            if ((self->parser_.content_length() && *self->parser_.content_length() > limit) || self->parser_.chunked()) {
                self->error(413, "bounded Content-Length required"); return;
            }
            self->parser_.body_limit(limit);
            http::async_read(self->stream_, self->buffer_, self->parser_, [self](beast::error_code err, std::size_t) {
                if (err) return;
                if (!self->workers_.post(reinterpret_cast<std::size_t>(self.get()), [self] {
                    Response data;
                    try { data = self->service_.handle(self->parser_.get()); }
                    catch (const Error& e) { data.status=e.status; Json::Value v=e.extra; v["error"]=e.status; v["message"]=e.what(); data.body=json(v); }
                    catch (...) { data.status=503; data.body="{\"error\":503,\"message\":\"resource dependency unavailable\"}"; }
                    net::post(self->stream_.get_executor(), [self,data=std::move(data)]() mutable { self->send(std::move(data)); });
                })) self->error(503, "resource worker queue full");
            });
        });
    }
private:
    void error(unsigned code, const char* message) { Response r; r.status=code; Json::Value v; v["error"]=code; v["message"]=message; r.body=json(v); send(std::move(r)); }
    void send(Response r) {
        chat::observability::log(chat::observability::Level::Info,"resource.http","resource request handled",
            {{"status",std::uint64_t(r.status)},{"bytes",std::uint64_t(r.stream_id.empty()?r.body.size():r.length)}});
        data_=std::move(r); response_.version(11); response_.result(data_.status); response_.keep_alive(false);
        response_.set(http::field::content_type, data_.content_type); response_.set("X-Content-Type-Options", "nosniff");
        response_.set("X-Request-Id", uuid()); response_.set(http::field::cache_control, "private, no-store");
        for (auto& h : data_.headers) response_.set(h.first,h.second);
        stream_.expires_after(std::chrono::seconds(30));
        if (data_.stream_id.empty()) {
            response_.body()=std::move(data_.body); response_.prepare_payload();
            http::async_write(stream_,response_,[self=shared_from_this()](beast::error_code,std::size_t) { self->close(); });
        } else {
            response_.content_length(data_.length); offset_=data_.begin; remaining_=data_.length;
            serializer_=std::make_unique<http::response_serializer<http::string_body>>(response_);
            http::async_write_header(stream_,*serializer_,[self=shared_from_this()](beast::error_code ec,std::size_t) { if (!ec) self->next(); });
        }
    }
    void next() {
        if (!remaining_) { close(); return; }
        auto self=shared_from_this();
        if (!workers_.post(reinterpret_cast<std::size_t>(this),[self] {
            try {
                const auto base=self->offset_/chat::files::PlainChunkBytes*chat::files::PlainChunkBytes;
                auto bytes=self->service_.read(self->data_.stream_id,base);
                const auto skip=static_cast<std::size_t>(self->offset_-base);
                if (skip>=bytes.size()) throw std::runtime_error("truncated attachment");
                const auto count=std::min<std::uint64_t>(bytes.size()-skip,self->remaining_);
                self->chunk_.assign(bytes.begin()+skip,bytes.begin()+skip+count);
                net::post(self->stream_.get_executor(),[self] {
                    self->stream_.expires_after(std::chrono::seconds(30));
                    net::async_write(self->stream_,net::buffer(self->chunk_),[self](beast::error_code ec,std::size_t n) {
                        if (ec) return; self->offset_+=n; self->remaining_-=n; self->next();
                    });
                });
            } catch (...) { net::post(self->stream_.get_executor(),[self] { self->close(); }); }
        })) close();
    }
    void close() { beast::error_code ec; stream_.socket().shutdown(tcp::socket::shutdown_both,ec); stream_.socket().close(ec); }
};
}
int main() {
    try {
        using namespace resource;
        chat::observability::initialize("resource_server");
        auto& cfg=ConfigMgr::Inst(); auto tls=chat::grpc_tls::from_config(cfg);
        auto host=setting("Resource","Host","127.0.0.1");
        chat::internal_rpc::validate_server_configuration(host,setting("InternalRpc","PeerToken"),tls.mode);
        Service service; net::io_context io; chat::runtime::KeyedExecutor workers(8,16);
        grpc::ServerBuilder builder; builder.AddListeningPort(host+":"+setting("Resource","RpcPort","50065"),chat::grpc_tls::server_credentials(tls));
        builder.RegisterService(&service); auto rpc=builder.BuildAndStart(); if (!rpc) throw std::runtime_error("resource RPC bind failed");
        tcp::acceptor acceptor(io,{net::ip::make_address(host),static_cast<unsigned short>(number(setting("Resource","HttpPort","8085")))});
        std::function<void()> accept; accept=[&] { acceptor.async_accept(net::make_strand(io),[&](beast::error_code ec,tcp::socket socket) {
            if (!ec) std::make_shared<Connection>(std::move(socket),service,workers)->start(); if (acceptor.is_open()) accept();
        }); }; accept();
        std::atomic<bool> stopping{false}; std::thread maintenance([&] {
            while (!stopping) { try { service.maintenance(); } catch (...) {} for (int i=0;i<10 && !stopping;++i) std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
        });
        net::signal_set signals(io,SIGINT,SIGTERM); signals.async_wait([&](beast::error_code,int) { acceptor.close(); stopping=true; io.stop(); });
        io.run(); stopping=true; maintenance.join(); service.stopNotifications(); rpc->Shutdown(); workers.stop(true); return 0;
    } catch (const std::exception& e) { std::cerr << "resource startup failed: " << e.what() << '\n'; return 1; }
}
