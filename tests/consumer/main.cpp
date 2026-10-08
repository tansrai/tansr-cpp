#include <iostream>
#include <tansr/api.hpp>
#include <tansr/archive.hpp>
#include <tansr/canonical.hpp>
#include <tansr/crypto.hpp>
#include <tansr/executor.hpp>
#include <tansr/session.hpp>

// 独立安装树消费：仅包含公共头和公开 target，不引用 src 或内部合同。
int main() {
    auto value = tansr::Json::parse("{\"kind\":\"consumer\",\"decimal\":1.25}");
    if (!value || value.value().at("decimal").number_token() != "1.25")
        return 1;
    auto digest = tansr::crypto::sha256_hex("abc");
    if (!digest ||
        digest.value() != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return 2;
    auto runtime = tansr::Runtime::create();
    if (!runtime)
        return 3;
    auto stopped = runtime.value()->shutdown(std::chrono::seconds(5));
    if (!stopped || stopped.value() != tansr::ShutdownStatus::stopped)
        return 4;
    std::cout << "installed C++ consumer passed; no network request was made\n";
    return 0;
}
