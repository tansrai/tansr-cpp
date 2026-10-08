#include "tansr/runtime.hpp"
#include <curl/curl.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace tansr;
namespace {
std::size_t checks = 0, cases = 0;
constexpr const char *authorization = "Authorization: Bearer synthetic-tls-fixture-token";
void require(bool condition, const std::string &message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void passed(const char *name) {
    ++cases;
    std::cout << "PASS " << name << '\n';
}
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
template <class Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds duration = 2s) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket value) { closesocket(value); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket value) { close(value); }
#endif
struct SocketOwner {
    Socket value{invalid_socket};
    ~SocketOwner() {
        if (value != invalid_socket)
            close_socket(value);
    }
    SocketOwner() = default;
    explicit SocketOwner(Socket socket) : value(socket) {}
    SocketOwner(const SocketOwner &) = delete;
    SocketOwner &operator=(const SocketOwner &) = delete;
};
class SocketEnvironment {
  public:
    SocketEnvironment() {
#ifdef _WIN32
        WSADATA data{};
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "fixture WSAStartup");
#else
        // OpenSSL socket BIO 使用 write；隔离测试进程内屏蔽拒绝握手后的 SIGPIPE。
        previous_ = std::signal(SIGPIPE, SIG_IGN);
        require(previous_ != SIG_ERR, "fixture SIGPIPE setup");
#endif
    }
    ~SocketEnvironment() {
#ifdef _WIN32
        WSACleanup();
#else
        std::signal(SIGPIPE, previous_);
#endif
    }

  private:
#ifndef _WIN32
    using SignalHandler = void (*)(int);
    SignalHandler previous_{};
#endif
};
bool nonblocking(Socket socket) {
#ifdef _WIN32
    unsigned long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
bool readable(Socket socket, bool write = false) {
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(socket, &descriptors);
    timeval timeout{};
    timeout.tv_usec = 10000;
#ifdef _WIN32
    const int count = select(0, write ? nullptr : &descriptors, write ? &descriptors : nullptr,
                             nullptr, &timeout);
#else
    const int count = select(socket + 1, write ? nullptr : &descriptors,
                             write ? &descriptors : nullptr, nullptr, &timeout);
#endif
    return count > 0;
}
int receive(Socket socket, char *data, int size) {
    return static_cast<int>(recv(socket, data, size, 0));
}
bool retry_socket() {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
bool disconnected_socket() {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAECONNRESET || error == WSAECONNABORTED || error == WSAESHUTDOWN ||
           error == WSAENOTCONN;
#else
    return errno == ECONNRESET || errno == ENOTCONN;
#endif
}
bool send_plain(Socket socket, const std::string &bytes, const std::atomic<bool> &stop) {
    std::size_t offset = 0;
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (offset < bytes.size() && !stop.load() && std::chrono::steady_clock::now() < end) {
        if (!readable(socket, true))
            continue;
        const auto remaining =
            static_cast<int>((std::min)(bytes.size() - offset, std::size_t{16384}));
        const auto count = send(socket, bytes.data() + offset, remaining, 0);
        if (count < 0 && retry_socket())
            continue;
        if (count <= 0)
            return false;
        offset += static_cast<std::size_t>(count);
    }
    return offset == bytes.size();
}
struct Observed {
    std::atomic<int> accepted{0}, closed{0}, requests{0}, authorized{0}, client_hello{0},
        peer_closed{0}, polls{0};
};
class LoopbackServer {
  public:
    using Handler = std::function<void(Socket, const std::atomic<bool> &, Observed &)>;
    explicit LoopbackServer(Handler handler) : handler_(std::move(handler)) {
        listener_.value = socket(AF_INET, SOCK_STREAM, 0);
        require(listener_.value != invalid_socket, "fixture socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(listener_.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
                "fixture bind");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        require(getsockname(listener_.value, reinterpret_cast<sockaddr *>(&address), &length) == 0,
                "fixture getsockname");
        port_ = ntohs(address.sin_port);
        require(listen(listener_.value, 16) == 0 && nonblocking(listener_.value), "fixture listen");
        listener_thread_ = std::thread([this] {
            while (!stop_.load()) {
                if (!readable(listener_.value)) {
                    ++observed.polls;
                    continue;
                }
                const auto client = accept(listener_.value, nullptr, nullptr);
                if (client == invalid_socket) {
                    ++observed.polls;
                    continue;
                }
                if (!nonblocking(client)) {
                    close_socket(client);
                    fixture_error_.store(true);
                    ++observed.polls;
                    continue;
                }
                ++observed.accepted;
                workers_.emplace_back([this, client] {
                    {
                        SocketOwner owner(client);
                        try {
                            handler_(client, stop_, observed);
                        } catch (...) {
                            fixture_error_.store(true);
                        }
                    }
                    ++observed.closed;
                });
                ++observed.polls;
            }
        });
    }
    ~LoopbackServer() {
        stop_.store(true);
        if (listener_thread_.joinable())
            listener_thread_.join();
        for (auto &worker : workers_)
            if (worker.joinable())
                worker.join();
    }
    std::string url(const char *path = "/", const char *scheme = "https") const {
        return std::string(scheme) + "://127.0.0.1:" + std::to_string(port_) + path;
    }
    void quiescent() {
        require(eventually([&] { return observed.accepted.load() == observed.closed.load(); }),
                "fixture connections not reclaimed");
        require(!fixture_error_.load(), "fixture worker failed");
    }
    void settle_listener() {
        const auto before = observed.polls.load();
        require(eventually([&] { return observed.polls.load() >= before + 3; }),
                "fixture listener did not drain pending connections");
    }
    Observed observed;

  private:
    Handler handler_;
    SocketOwner listener_;
    unsigned short port_{0};
    std::atomic<bool> stop_{false}, fixture_error_{false};
    std::thread listener_thread_;
    std::vector<std::thread> workers_;
};

template <class T, void (*Destroy)(T *)> using Ossl = std::unique_ptr<T, decltype(Destroy)>;
using Key = Ossl<EVP_PKEY, EVP_PKEY_free>;
using Certificate = Ossl<X509, X509_free>;
struct Identity {
    Key key{nullptr, EVP_PKEY_free};
    Certificate certificate{nullptr, X509_free};
};
void extension(X509 *certificate, X509 *issuer, int nid, const std::string &value) {
    X509V3_CTX context{};
    X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
    Ossl<X509_EXTENSION, X509_EXTENSION_free> item(
        X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str()), X509_EXTENSION_free);
    require(item && X509_add_ext(certificate, item.get(), -1) == 1, "synthetic X509 extension");
}
Identity identity(const char *name, const std::string &crl_url, const Identity *issuer = nullptr,
                  bool wrong_host = false, bool expired = false) {
    Ossl<EVP_PKEY_CTX, EVP_PKEY_CTX_free> generator(
        EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    require(generator && EVP_PKEY_keygen_init(generator.get()) == 1 &&
                EVP_PKEY_CTX_set_rsa_keygen_bits(generator.get(), 2048) == 1,
            "synthetic RSA generator");
    EVP_PKEY *generated = nullptr;
    require(EVP_PKEY_generate(generator.get(), &generated) == 1, "synthetic RSA key");
    Identity result;
    result.key.reset(generated);
    result.certificate.reset(X509_new());
    require(static_cast<bool>(result.certificate), "synthetic X509 allocation");
    auto *certificate = result.certificate.get();
    static long serial = 1;
    require(X509_set_version(certificate, 2) == 1 &&
                ASN1_INTEGER_set(X509_get_serialNumber(certificate), serial++) == 1,
            "synthetic X509 serial");
    require(X509_gmtime_adj(X509_getm_notBefore(certificate),
                            issuer ? (expired ? -7200L : -300L) : -604800L) != nullptr &&
                X509_gmtime_adj(X509_getm_notAfter(certificate),
                                issuer ? (expired ? -3600L : 86400L) : 604800L) != nullptr,
            "synthetic X509 validity");
    require(X509_set_pubkey(certificate, result.key.get()) == 1, "synthetic X509 public key");
    auto *subject = X509_get_subject_name(certificate);
    require(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                       reinterpret_cast<const unsigned char *>(name), -1, -1,
                                       0) == 1,
            "synthetic X509 subject");
    auto *issuer_certificate = issuer ? issuer->certificate.get() : certificate;
    require(X509_set_issuer_name(certificate, X509_get_subject_name(issuer_certificate)) == 1,
            "synthetic X509 issuer");
    extension(certificate, issuer_certificate, NID_basic_constraints,
              issuer ? "critical,CA:FALSE" : "critical,CA:TRUE,pathlen:0");
    extension(certificate, issuer_certificate, NID_key_usage,
              issuer ? "critical,digitalSignature,keyEncipherment"
                     : "critical,keyCertSign,cRLSign");
    extension(certificate, issuer_certificate, NID_subject_key_identifier, "hash");
    extension(certificate, issuer_certificate, NID_authority_key_identifier, "keyid:always");
    extension(certificate, issuer_certificate, NID_crl_distribution_points, "URI:" + crl_url);
    if (issuer) {
        extension(certificate, issuer_certificate, NID_ext_key_usage, "serverAuth");
        extension(certificate, issuer_certificate, NID_subject_alt_name,
                  wrong_host ? "DNS:wrong-host.invalid" : "IP:127.0.0.1");
    }
    require(X509_sign(certificate, issuer ? issuer->key.get() : result.key.get(), EVP_sha256()) > 0,
            "synthetic X509 signature");
    return result;
}
std::string crl_bytes(const Identity &issuer) {
    Ossl<X509_CRL, X509_CRL_free> crl(X509_CRL_new(), X509_CRL_free);
    Ossl<ASN1_TIME, ASN1_TIME_free> before(ASN1_TIME_adj(nullptr, std::time(nullptr), 0, -300),
                                           ASN1_TIME_free);
    Ossl<ASN1_TIME, ASN1_TIME_free> after(ASN1_TIME_adj(nullptr, std::time(nullptr), 1, 0),
                                          ASN1_TIME_free);
    require(crl && before && after && X509_CRL_set_version(crl.get(), 1) == 1 &&
                X509_CRL_set_issuer_name(crl.get(),
                                         X509_get_subject_name(issuer.certificate.get())) == 1 &&
                X509_CRL_set1_lastUpdate(crl.get(), before.get()) == 1 &&
                X509_CRL_set1_nextUpdate(crl.get(), after.get()) == 1,
            "synthetic CRL fields");
    require(X509_CRL_sign(crl.get(), issuer.key.get(), EVP_sha256()) > 0,
            "synthetic CRL signature");
    const auto size = i2d_X509_CRL(crl.get(), nullptr);
    require(size > 0, "synthetic CRL length");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    auto *cursor = reinterpret_cast<unsigned char *>(bytes.data());
    require(i2d_X509_CRL(crl.get(), &cursor) == size, "synthetic CRL DER");
    return bytes;
}
class TemporaryCa {
  public:
    explicit TemporaryCa(const Identity &identity) {
        unsigned char random[16]{};
        require(RAND_bytes(random, sizeof(random)) == 1, "fixture temp name entropy");
        std::string suffix;
        constexpr char hex[] = "0123456789abcdef";
        for (const auto byte : random) {
            suffix += hex[byte >> 4];
            suffix += hex[byte & 15];
        }
        const auto candidate = std::filesystem::temp_directory_path() / ("tansr-cpp-tls-" + suffix);
        require(std::filesystem::create_directory(candidate), "fixture private temp directory");
        root_ = std::filesystem::canonical(candidate);
        path_ = root_ / "synthetic-ca.pem";
        Ossl<BIO, BIO_free_all> pem(BIO_new(BIO_s_mem()), BIO_free_all);
        require(pem && PEM_write_bio_X509(pem.get(), identity.certificate.get()) == 1,
                "synthetic CA PEM");
        char *bytes = nullptr;
        const auto length = BIO_get_mem_data(pem.get(), &bytes);
        require(length > 0, "synthetic CA PEM bytes");
        std::ofstream output(path_, std::ios::binary);
        output.write(bytes, length);
        output.close();
        require(static_cast<bool>(output), "write synthetic CA fixture");
    }
    ~TemporaryCa() {
        // 仅删除本夹具创建的两个确定条目，不递归清理共享 OS temp。
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(root_, ignored);
    }
    std::string path() const { return path_.u8string(); }

  private:
    std::filesystem::path root_, path_;
};
using TlsContext = std::shared_ptr<SSL_CTX>;
TlsContext tls_context(const Identity &identity) {
    TlsContext context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    require(context && SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) == 1 &&
                SSL_CTX_use_certificate(context.get(), identity.certificate.get()) == 1 &&
                SSL_CTX_use_PrivateKey(context.get(), identity.key.get()) == 1 &&
                SSL_CTX_check_private_key(context.get()) == 1,
            "fixture TLS context");
    return context;
}
bool ssl_retry(SSL *ssl, int result, Socket socket) {
    const auto error = SSL_get_error(ssl, result);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
        return false;
    (void)readable(socket, error == SSL_ERROR_WANT_WRITE);
    return true;
}
LoopbackServer::Handler tls_handler(TlsContext context, std::string response) {
    return [context = std::move(context), response = std::move(response)](
               Socket socket, const std::atomic<bool> &stop, Observed &observed) {
        Ossl<SSL, SSL_free> ssl(SSL_new(context.get()), SSL_free);
        if (!ssl || static_cast<std::uint64_t>(socket) > INT_MAX ||
            SSL_set_fd(ssl.get(), static_cast<int>(socket)) != 1)
            throw std::runtime_error("fixture TLS socket BIO");
        const auto end = std::chrono::steady_clock::now() + 5s;
        bool accepted = false;
        while (!stop.load() && std::chrono::steady_clock::now() < end) {
            const auto result = SSL_accept(ssl.get());
            if (result == 1) {
                accepted = true;
                break;
            }
            if (!ssl_retry(ssl.get(), result, socket))
                return;
        }
        if (!accepted)
            return;
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536 &&
               !stop.load() && std::chrono::steady_clock::now() < end) {
            const auto count = SSL_read(ssl.get(), buffer, sizeof(buffer));
            if (count > 0)
                request.append(buffer, static_cast<std::size_t>(count));
            else if (!ssl_retry(ssl.get(), count, socket))
                return;
        }
        if (request.find("\r\n\r\n") == std::string::npos)
            return;
        ++observed.requests;
        if (request.find(authorization) != std::string::npos)
            ++observed.authorized;
        std::size_t offset = 0;
        while (offset < response.size() && !stop.load() && std::chrono::steady_clock::now() < end) {
            const auto count = SSL_write(ssl.get(), response.data() + offset,
                                         static_cast<int>(response.size() - offset));
            if (count > 0)
                offset += static_cast<std::size_t>(count);
            else if (!ssl_retry(ssl.get(), count, socket))
                return;
        }
        (void)SSL_shutdown(ssl.get());
    };
}
std::string ok_response() {
    return "HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\ntls-ok";
}
HttpRequest request(const LoopbackServer &server) {
    HttpRequest result;
    result.method = "GET";
    result.url = server.url();
    result.deadline_ms = unix_time_ms() + 5000;
    result.headers.emplace_back("Authorization", "Bearer synthetic-tls-fixture-token");
    return result;
}
void finish(std::shared_ptr<Runtime> &runtime, LoopbackServer &server) {
    require(take(runtime->shutdown(2s)) == ShutdownStatus::stopped,
            "TLS runtime shutdown must reclaim all workers");
    runtime.reset();
    server.quiescent();
}
void trusted_ca(const TlsContext &context, const std::string &ca_file) {
    LoopbackServer server(tls_handler(context, ok_response()));
    RuntimeOptions options;
    options.ca_file = ca_file;
    auto runtime = take(Runtime::create(options));
    auto result = runtime->request(request(server));
    if (!result) {
        // 只在合成正向夹具失败时输出底层握手诊断，保留与产品相同的验证要求。
        auto *diagnostic = curl_easy_init();
        if (diagnostic) {
            const auto url = server.url();
            curl_easy_setopt(diagnostic, CURLOPT_URL, url.c_str());
            curl_easy_setopt(diagnostic, CURLOPT_CAINFO, ca_file.c_str());
            curl_easy_setopt(diagnostic, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(diagnostic, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(diagnostic, CURLOPT_PROXY, "");
            curl_easy_setopt(diagnostic, CURLOPT_TIMEOUT_MS, 3000L);
            curl_easy_setopt(diagnostic, CURLOPT_VERBOSE, 1L);
            const auto code = curl_easy_perform(diagnostic);
            std::cerr << "synthetic TLS diagnostic curl code: " << static_cast<int>(code) << '\n';
            curl_easy_cleanup(diagnostic);
        }
    }
    const auto response = take(std::move(result));
    require(response.status == 200 && response.body == "tls-ok",
            "explicit enterprise CA must verify real HTTPS");
    require(server.observed.requests == 1 && server.observed.authorized == 1,
            "HTTPS origin received exact synthetic authorization");
    finish(runtime, server);
    passed("explicit enterprise CA / valid chain and IP SAN");
}
void rejected_certificate(const TlsContext &context, const std::string &ca_file,
                          const char *label) {
    LoopbackServer server(tls_handler(context, ok_response()));
    RuntimeOptions options;
    options.ca_file = ca_file;
    auto runtime = take(Runtime::create(options));
    const auto result = runtime->request(request(server));
    require(!result && result.error().code == ErrorCode::tls,
            std::string(label) + " must fail with TLS classification");
    finish(runtime, server);
    require(server.observed.accepted >= 1 && server.observed.requests == 0 &&
                server.observed.authorized == 0,
            std::string(label) + " emitted application request despite rejected certificate");
    passed(label);
}
void redirects(const TlsContext &context, const std::string &ca_file) {
    LoopbackServer destination(tls_handler(context, ok_response()));
    for (const auto status : {301, 302, 303, 307, 308, 401}) {
        const auto response = "HTTP/1.1 " + std::to_string(status) +
                              " Fixture\r\nLocation: " + destination.url("/capture") +
                              "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        LoopbackServer origin(tls_handler(context, response));
        RuntimeOptions options;
        options.ca_file = ca_file;
        auto runtime = take(Runtime::create(options));
        const auto result = runtime->request(request(origin));
        if (status == 401)
            require(result && result.value().status == 401, "401 remains original response");
        else
            require(!result && result.error().code == ErrorCode::http &&
                        result.error().http_status == status,
                    "HTTPS redirect must fail closed");
        finish(runtime, origin);
        destination.settle_listener();
        require(origin.observed.requests == 1 && origin.observed.authorized == 1,
                "origin did not receive one authorized request");
        require(destination.observed.accepted == 0 && destination.observed.requests == 0 &&
                    destination.observed.authorized == 0,
                "redirect/401 navigated to a new authority or leaked token");
        const auto label =
            "HTTPS " + std::to_string(status) + " does not navigate or forward authorization";
        passed(label.c_str());
    }
    destination.quiescent();
}
void stalled_handshake(const std::string &ca_file, int mode) {
    LoopbackServer server([](Socket socket, const std::atomic<bool> &stop, Observed &observed) {
        bool hello = false;
        char buffer[4096];
        const auto end = std::chrono::steady_clock::now() + 5s;
        while (!stop.load() && std::chrono::steady_clock::now() < end) {
            if (!readable(socket))
                continue;
            const auto count = receive(socket, buffer, sizeof(buffer));
            if (count < 0 && retry_socket())
                continue;
            if (count == 0 || (count < 0 && disconnected_socket())) {
                ++observed.peer_closed;
                return;
            }
            if (count < 0)
                throw std::runtime_error("fixture peer-close observation failed");
            if (!hello) {
                if (static_cast<unsigned char>(buffer[0]) != 0x16)
                    throw std::runtime_error("expected TLS ClientHello");
                hello = true;
                ++observed.client_hello;
            }
            // 已接收 ClientHello 后保持静默，取消不能依赖收到服务端字节。
        }
    });
    RuntimeOptions options;
    options.ca_file = ca_file;
    options.connect_timeout = mode == 2 ? 500ms : 5s;
    auto runtime = take(Runtime::create(options));
    auto input = request(server);
    if (mode == 1)
        input.deadline_ms = unix_time_ms() + 500;
    auto handle = take(runtime->request_async(input));
    require(eventually([&] { return server.observed.client_hello == 1; }),
            "blocked handshake never reached real ClientHello");
    const auto started = std::chrono::steady_clock::now();
    if (mode == 0)
        handle.cancel();
    require(eventually([&] { return handle.ready(); }),
            "blocked TLS operation did not complete promptly");
    const auto result = handle.wait();
    require(!result &&
                result.error().code == (mode == 0 ? ErrorCode::cancelled : ErrorCode::timeout),
            "blocked TLS cancellation/deadline classification");
    require(std::chrono::steady_clock::now() - started < 2s, "blocked TLS completion latency");
    require(eventually([&] { return server.observed.peer_closed == 1; }),
            "client reported completion but retained handshake socket");
    finish(runtime, server);
    require(server.observed.accepted == 1 && server.observed.closed == 1,
            "blocked handshake leaked connection");
    passed(mode == 0   ? "blocked TLS cancel closes peer socket"
           : mode == 1 ? "blocked TLS absolute deadline closes peer socket"
                       : "blocked TLS connect timeout closes peer socket");
}
} // namespace

int main() {
    try {
        SocketEnvironment sockets;
        const auto *version = curl_version_info(CURLVERSION_NOW);
        require(version && version->ssl_version, "actual TLS backend unavailable");
        const std::string backend(version->ssl_version);
#ifdef _WIN32
        require(backend.find("Schannel") != std::string::npos,
                "Windows matrix requires actual Schannel backend");
#elif defined(__linux__)
        require(backend.find("OpenSSL") != std::string::npos,
                "Linux matrix requires actual OpenSSL backend");
#endif
        std::cout << "TLS client backend: " << backend
                  << "; fixture server: " << OpenSSL_version(OPENSSL_VERSION) << '\n';
        struct CrlDocument {
            std::mutex mutex;
            std::string bytes;
        } document;
        LoopbackServer crl_server(
            [&](Socket socket, const std::atomic<bool> &stop, Observed &observed) {
                std::string headers;
                char buffer[4096];
                const auto end = std::chrono::steady_clock::now() + 3s;
                while (headers.find("\r\n\r\n") == std::string::npos && headers.size() < 65536 &&
                       !stop.load() && std::chrono::steady_clock::now() < end) {
                    if (!readable(socket))
                        continue;
                    const auto count = receive(socket, buffer, sizeof(buffer));
                    if (count < 0 && retry_socket())
                        continue;
                    if (count <= 0)
                        return;
                    headers.append(buffer, static_cast<std::size_t>(count));
                }
                if (headers.find("\r\n\r\n") == std::string::npos)
                    return;
                ++observed.requests;
                std::string body;
                {
                    std::lock_guard<std::mutex> lock(document.mutex);
                    body = document.bytes;
                }
                (void)send_plain(
                    socket,
                    "HTTP/1.1 200 OK\r\nContent-Type: application/pkix-crl\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body,
                    stop);
            });
        const auto crl_url = crl_server.url("/synthetic-ca.crl", "http");
        const auto root = identity("Tansr synthetic enterprise root", crl_url);
        {
            std::lock_guard<std::mutex> lock(document.mutex);
            document.bytes = crl_bytes(root);
        }
        const TemporaryCa ca(root);
        const auto leaf = identity("Tansr synthetic TLS server", crl_url, &root);
        const auto valid = tls_context(leaf);
        trusted_ca(valid, ca.path());
#ifdef _WIN32
        require(crl_server.observed.requests >= 1,
                "Schannel fixture must exercise local revocation lookup");
#endif
        rejected_certificate(valid, "", "system trust rejects synthetic uninstalled CA");
        const auto rogue_root = identity("Tansr synthetic unrelated root", crl_url);
        const auto rogue = identity("Tansr synthetic untrusted server", crl_url, &rogue_root);
        rejected_certificate(tls_context(rogue), ca.path(), "wrong certificate chain");
        const auto wrong_host = identity("Tansr synthetic wrong host", crl_url, &root, true);
        rejected_certificate(tls_context(wrong_host), ca.path(), "hostname mismatch");
        const auto expired =
            identity("Tansr synthetic expired server", crl_url, &root, false, true);
        rejected_certificate(tls_context(expired), ca.path(), "expired certificate");
        redirects(valid, ca.path());
        for (int mode = 0; mode < 3; ++mode)
            stalled_handshake(ca.path(), mode);
        crl_server.quiescent();
        std::cout << "tls: " << cases << " cases, " << checks
                  << " checks passed; 0 failed; 0 skipped\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "TLS FAIL after " << cases << " cases: " << error.what() << '\n';
        return 1;
    }
}
