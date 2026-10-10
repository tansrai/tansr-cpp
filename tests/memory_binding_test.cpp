#include "../demo/memory_binding.hpp"
#include "tansr/operations.hpp"
#include <iostream>
#include <stdexcept>
using namespace tansr;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
class Transport final : public HttpTransport {
  public:
    std::function<void(Json &)> alter;
    int calls = 0;
    Result<HttpResponse> request(const HttpRequest &request, CancellationToken) override {
        ++calls;
        auto body = take(Json::parse(request.body));
        check(body.at("required").as_array().front().as_string() == "memory-lifecycle-v1",
              "required feature");
        auto response = Json::object({{"contract", "terminal-services-v1"},
                                      {"requestId", body.at("requestId")},
                                      {"session", body.at("session")},
                                      {"executionBinding", body.at("executionBinding")},
                                      {"scope", Json::object({{"applicationScopeId", "app"},
                                                              {"endUserId", "user"},
                                                              {"authorizationRevision", "1"}})},
                                      {"accepted", Json::array({"memory-lifecycle-v1"})},
                                      {"unavailable", Json::array()},
                                      {"outputAuthority", "session-events"},
                                      {"limits", Json::object({{"maxControlBytes", 262144},
                                                               {"maxBlockBytes", 16384},
                                                               {"maxBatchBytes", 65536},
                                                               {"maxPendingBytes", 2097152},
                                                               {"maxRetainedBytes", 8388608}})}});
        if (alter)
            alter(response);
        return HttpResponse{200,
                            {{"content-type", "application/json"},
                             {"tansr-contract", "unified-v1"},
                             {"tansr-manifest-revision", "7"},
                             {"tansr-schema-hash", "sha256:" + std::string(schema_hash)},
                             {"tansr-domain", "terminal"}},
                            response.dump()};
    }
    Result<std::shared_ptr<ByteStream>> stream(const HttpRequest &, CancellationToken) override {
        return Error{ErrorCode::internal, "unused"};
    }
};
} // namespace
int main() {
    try {
        auto transport = std::make_shared<Transport>();
        ClientOptions options;
        options.base_url = "http://127.0.0.1:1";
        options.token_provider = [](CancellationToken) -> Result<AuthToken> {
            return AuthToken{"synthetic", "user"};
        };
        auto api = take(ApiClient::create(options, transport));
        executor::Binding binding{
            "binding", "1", {"executor", "connection", "1", "workspace", "1", {}}};
        const auto negotiate = [&] {
            return demo::memory::negotiate(api, {"app", "user", "1"}, {"sdk1", "session"}, binding,
                                           "original-request");
        };
        check(bool(negotiate()), "valid memory negotiation");
        for (const char *field :
             {"requestId", "session", "scope", "executionBinding", "accepted"}) {
            transport->alter = [field](Json &body) {
                const std::string f = field;
                if (f == "requestId")
                    body.set(f, "different-request");
                if (f == "session")
                    body.at(f).set("sessionId", "different-session");
                if (f == "scope")
                    body.at(f).set("endUserId", "different-user");
                if (f == "executionBinding")
                    body.at(f).at("target").set("connectionRevision", "2");
                if (f == "accepted")
                    body.set(f, Json::array({"execution-stream-v1"}));
            };
            check(!negotiate(), "mismatched negotiation accepted");
        }
        transport->alter = [](Json &body) { body.set("extra", true); };
        check(!negotiate(), "invalid response shape accepted");
        check(!demo::memory::negotiate(api, {"app", "user", "1"}, {"sdk1", "session"}, binding, ""),
              "empty request id accepted");
        check(transport->calls == 7, "invalid request contacted server");
        auto different = binding;
        different.target.interpreter = executor::Interpreter{"pwsh", "1", "powershell"};
        check(!demo::memory::same_binding(binding, different), "interpreter mismatch accepted");
        std::cout << checks << " memory negotiation checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
