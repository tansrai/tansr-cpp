#include <tansr/crypto.hpp>
#include <tansr/json.hpp>
#include <tansr/runtime.hpp>
int main() {
    auto json = tansr::Json::parse("{\"decimal\":1.25}");
    if (!json || json.value().at("decimal").number_token() != "1.25")
        return 1;
    auto hash = tansr::crypto::sha256_hex("abc");
    if (!hash || hash.value() != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return 2;
    auto runtime = tansr::Runtime::create();
    if (!runtime)
        return 3;
    auto stopped = runtime.value()->shutdown(std::chrono::seconds(5));
    return stopped && stopped.value() == tansr::ShutdownStatus::stopped ? 0 : 4;
}
