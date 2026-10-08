# Tansr C++ SDK guide

[中文](使用指南.md) · [README](../README.en.md)

This native C++17 SDK consumes the unified `/api` contract, UAPI revision 7. It uses HTTP requests and SSE streams, not WebSocket. Serve owns execution, sessions, context, memory selection, permissions, adjudication, tool scheduling and accounting. The client supplies explicitly authorized device/business capabilities and durable archive storage; it does not implement a second agent kernel or change Electron's integrated SDK.

## 1. Build and consume

The SDK and three CLI demos are licensed under [MIT](../LICENSE), with source at [tansrai/tansr-cpp](https://github.com/tansrai/tansr-cpp). Download the source archive, matching SDK or separate demo packages from the **[v0.1.0 release](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0)** and verify them against that release's checksums. Binaries cover Windows x64, Linux x64 and macOS arm64. Use the release's independently distributed Conan/vcpkg recipe bundle; these recipes are not listed in the public community registries.

Use CMake 3.25+ and a C++17 compiler; the commands below use Ninja. An extracted binary SDK contains `include/`, `lib/`, `dependencies/` and `share/TansrSDK/`. Preserve this layout and pass only the SDK root to `CMAKE_PREFIX_PATH`; its package configuration finds the bundled dependencies locally. Consuming the binary SDK requires no Node, Go, Rust or SDK source checkout.

Save these two complete files in a new application directory. This minimal program checks public APIs, linking and Runtime startup/shutdown without making a network request. Authentication and real sessions are covered below.

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)
find_package(TansrSDK 0.1.0 EXACT CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_17)
target_link_libraries(my_app PRIVATE tansr::sdk)
enable_testing()
add_test(NAME sdk_startup COMMAND my_app)
```

`main.cpp`:

```cpp
#include <chrono>
#include <iostream>
#include <tansr/crypto.hpp>
#include <tansr/json.hpp>
#include <tansr/runtime.hpp>

int main() {
    auto value = tansr::Json::parse("{\"decimal\":1.25}");
    if (!value || value.value().at("decimal").number_token() != "1.25")
        return 1;
    auto digest = tansr::crypto::sha256_hex("abc");
    if (!digest ||
        digest.value() != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return 2;
    auto runtime = tansr::Runtime::create();
    if (!runtime)
        return 3;
    runtime.value()->stop();
    auto stopped = runtime.value()->shutdown(std::chrono::seconds(30));
    if (!stopped || stopped.value() != tansr::ShutdownStatus::stopped)
        return 4;
    std::cout << "Tansr SDK ready; no network request was made\n";
    return 0;
}
```

The v0.1.0 binary package provides a Release static SDK. With a matching compiler, architecture and CRT, run from the application directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/absolute/tansr-cpp-sdk-0.1.0
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, use an x64 Native Tools shell and replace the prefix with `C:/absolute/tansr-cpp-sdk-0.1.0`. Retain Release `/MD`; do not configure a Debug `/MDd` application against this Release artifact.

Public headers under `include/tansr` do not expose curl, OpenSSL or third-party JSON handles. Match compiler, STL, architecture, Debug/Release, static/shared and CRT configuration; Windows uses `/MD` or `/MDd`. PImpl does not promise binary compatibility across compilers, library versions or STLs. C++20 applications can consume the C++17 API. A C ABI, GUI framework and arbitrary shell are outside this release scope.

The demos have the same executable names as the Rust demos. Install to an independent prefix and invoke the intended executable by full path; do not replace another SDK's installation.

The v0.1.0 artifacts use these platform baselines. Check each artifact manifest's compiler, dependency and minimum-system labels. Rebuilding the source does not establish that an existing binary works on older systems.

| Platform | Build and installation requirements |
|---|---|
| Windows x64 | VS 2022 / MSVC 19.44, Release `/MD` and Debug `/MDd`; use an x64 Native Tools shell and deploy the matching MSVC runtime |
| Linux x64 | Ubuntu 24.04 / GCC 13 / glibc 2.39; verify the final artifact's actual glibc/GLIBCXX symbol requirements |
| macOS arm64 | Apple toolchain; the v0.1.0 artifacts' minimum deployment target is macOS 26.0, with no claim of compatibility with earlier versions |

Building from source still requires prepared, matching curl 8.22.0, c-ares 1.34.8 and OpenSSL 3.5.9 dependencies from [dependencies.json](../packaging/dependencies.json). Default CMake configuration does not download them; generated operations are already in the source tree. See the [recipes](../packaging/README.md) for source installation. For a normal source installation without bundled dependencies, provide both prefixes:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/absolute/tansr-cpp-0.1.0;/absolute/dependencies"
cmake --build build
```

Hosts with their own matching dependencies can add `-DTansrSDK_NO_BUNDLED_DEPENDENCIES=ON` and supply their dependency prefix. This disables only automatic selection of the package's `dependencies/` directory. It does not disable dependency checks or establish compatibility with arbitrary versions. Package-local lookup does not permanently change the application's prefix settings.

For Windows shared builds made from source, deploy the SDK and dependency DLLs from their `bin` directories with the required runtime. A developer machine's global PATH does not prove a complete deployment. The static SDK release bundles development dependencies but still requires its declared OS/CRT runtime conditions.

Each demo has its own artifact; these are not SDK link directories. Extract each package separately and invoke its executable under `bin/`, adding `.exe` on Windows. For example, extract the chat package to `/absolute/tansr-chat-0.1.0` and run `/absolute/tansr-chat-0.1.0/bin/tansr-chat --help`. Their names match the Rust demos, so preserve existing installations. Configure credentials and the service origin as described next.

## 2. Authentication and current identity

Serve validates short-lived tokens and current application policy. The SDK does not sign users in or retain a long-lived appkey. The demos read `TANSR_TOKEN_FILE` (one token line) and `TANSR_SCOPE_FILE` (the following UTF-8 JSON). Use two distinct files in the same private credential directory:

```json
{"applicationScopeId":"app-demo","endUserId":"user-demo","authorizationRevision":"1"}
```

These fields must come from the application's trusted backend and match the token. A local scope file does not grant permissions. Each request rereads the token and checks scope before and after reading; tool authorization and archive access also check current scope. A refreshed token for the same principal may continue. A changed application, user or authorization revision rejects use of the old client. Reconcile existing resources before creating objects for the new identity; do not replay writes under another user.

Files and their immediate directory must be private to the current user. Ancestors must not contain symbolic links, junctions or reparse points; files must not have multiple hard links. Use directory mode 0700/file mode 0600 on Unix, or current-user ownership and a private DACL on Windows. The SDK rejects insecure existing paths without weakening or rewriting their permissions. Shared read-only credential access allows chat and tools processes to use the same directory.

```sh
export TANSR_TOKEN_FILE=/absolute/private-credentials/token.txt
export TANSR_SCOPE_FILE=/absolute/private-credentials/scope.json
export TANSR_BASE_URL=https://serve.example.invalid
```

Equivalent Windows PowerShell settings contain file paths and the service origin only:

```powershell
$env:TANSR_TOKEN_FILE = 'C:/absolute/private-credentials/token.txt'
$env:TANSR_SCOPE_FILE = 'C:/absolute/private-credentials/scope.json'
$env:TANSR_BASE_URL = 'https://serve.example.invalid'
& 'C:/absolute/tansr-chat-0.1.0/bin/tansr-chat.exe' --help
& 'C:/absolute/tansr-tools-0.1.0/bin/tansr-tools.exe' --help
& 'C:/absolute/tansr-archive-0.1.0/bin/tansr-archive.exe' --help
```

On Unix, use `bin/tansr-chat`, `bin/tansr-tools` and `bin/tansr-archive` from the selected install prefix. The commands below refer to that installation. A trusted host must create the credential files first; these environment variables contain paths, not token contents.

ApiClient accepts an HTTP(S) origin without credentials, a path, query or fragment. The current implementation does not restrict HTTP to loopback. Use plaintext only for loopback development as a deployment recommendation, and valid HTTPS for deployed services. Applications configure enterprise CA material through `RuntimeOptions.ca_file`; chain and hostname verification remain enabled. Demos never place tokens in command-line arguments, logs, error details or archives. Raw `Error.detail` may contain server or business content; applications must protect explicitly collected diagnostics.

## 3. Runtime ownership

The host retains the final Runtime owner. ApiClient, sessions and executors share that runtime. Cancel application work, stop and join application workers, shut down clients, then stop/shut down Runtime. `ShutdownStatus::pending` is not quiescence; retain ownership until shutdown is confirmed. Callbacks may request stop/cancellation, but must not wait for themselves, release the final Runtime or unload its DLL.

```cpp
#include <tansr/api.hpp>
#include <tansr/session.hpp>

void connect(std::string base, tansr::TokenProvider provider) {
    auto runtime = tansr::Runtime::create();
    if (!runtime) return;
    tansr::ClientOptions options;
    options.base_url = std::move(base);
    options.token_provider = std::move(provider);
    auto api = tansr::ApiClient::create(std::move(options), runtime.value());
    if (api) {
        auto sessions = tansr::session::SessionClient::create(api.value());
        // Check sessions before starting application work here.
        api.value()->shutdown();
    }
    runtime.value()->stop();
    auto stopped = runtime.value()->shutdown(std::chrono::seconds(30));
    // Handle errors/pending and retain ownership while work is still running.
    (void)stopped;
}
```

`CancellationToken` cancels local requests/observation. It does not implicitly interrupt or close a Serve session, or acknowledge archive data. JSON, response bytes, events and Results own their storage; references captured by application callbacks must still outlive their use. Tool handlers cooperate with cancellation; the SDK cannot safely force-kill C++ threads.

## 4. Session demo

```sh
tansr-chat --base http://127.0.0.1:8787 --message "Hello"
tansr-chat --family sdk2-offload-v1 --prepare-create /absolute/intents/session.json --request-id app-create-001
tansr-chat --family sdk2-offload-v1 --create-intent /absolute/intents/session.json
tansr-chat --attach SESSION_ID
tansr-chat --resume SESSION_ID --last-event-id 42
```

The default family is `sdk1`. Select `sdk2-offload-v1` explicitly and preserve the caller's creation `--request-id`. A lost response must not trigger a new identifier or replacement session. `--attach` opens an existing live session; `--resume` invokes its recovery operation. They are distinct and mutually exclusive. Supplying `--message` to an active turn fails instead of starting another turn silently.

Use the two-step offload creation above. `--prepare-create` atomically stores the original requestId, optional `--model`, complete creation body, transport request key, absolute deadline and origin/family/current-authorization owner in one private file, without making a request. The preparation command's `--timeout` sets the intent lifetime. `--create-intent` restores those exact values through public `SessionClient::create`; success only prints `session:` and never starts or resends a business turn. Continue using `--attach SESSION_ID` with the same family. If creation loses its response, repeat the same `--create-intent` before the original deadline; do not prepare a replacement, change keys or extend the deadline. Serve creates the offload Source/binding as part of session creation, so original session replay recovers that ownership without inventing a manual binding epoch.

Neither intent mode accepts `--message`, `--last-event-id`, `--resume` or `--attach`; replay also rejects overriding `--model` or `--request-id`. An existing file, invalid format, expiry or changed origin/family/application/user/authorization revision fails while retaining the original file. Direct creation with `--request-id` remains available for hosts that durably preserve the complete intent themselves; retaining an ID alone does not preserve its original body and deadline.

The demo opens the stream before sending. A new `TurnTracker` for each turn uses the previous sequence and the actual turn identity. HTTP 202, EOF, another turn's completion and session termination are not proof that this turn completed. `--last-event-id` is an event cursor, not an archive ACK, coverage watermark or history generation. A replay gap requires reconciliation; the demo does not silently jump forward.

| Command | Behavior |
|---|---|
| `/interrupt` | Explicitly request remote interruption and wait for the actual terminal event. |
| `/allow TICKET`, `/deny TICKET` | Respond only to a received, still-open ticket using its original digest. No automatic approval. |
| `/answers TICKET JSON_ARRAY` | Answer a received question; items contain questionId, selectedOptionIds and optional freeText. |
| `/target` | Read current mid-turn input capabilities and target. |
| `/insert JSON_OBJECT` | Supply inputId, target.historyEpoch/turnId, content.text and optional ack. The current turn is retained. |
| `/history` | Read history(0,0), the count view; a zero limit is valid. |
| `/quit` or Ctrl+C | Stop local observation without claiming the remote turn stopped. |

```text
/insert {"inputId":"input-001","target":{"historyEpoch":"original-value","turnId":"original-value"},"content":{"text":"An additional requirement"},"ack":"memory"}
```

Run `/target` first and use the current original identifiers in `/insert`; the values above are placeholders. Control receipts are flushed immediately to stdout so a piped host can read them while the model is waiting. After `/interrupt` is accepted, the Demo still waits for the actual `turn.aborted` event and exits with a nonzero status, without printing `[turn completed]`. `/quit` stops local observation; an active original turn can still be attached again.

The demo rejects unsupported input fields instead of silently dropping them. A durable ACK request fails if unsupported; it is not downgraded to memory. Acceptance/receipt is not core consumption. Noninteractive `--message` exits nonzero when approval or an answer is needed, retaining the session for an interactive attachment.

The SDK also exposes text/image blocks, STT/TTS, checkpoints, restore/export/import/delete, compaction, cwd and application prompt metadata. Transcription/speech requests do not imply a microphone/player or native audio/video model blocks. Core policy composes platform, Serve and request prompt configuration; this client does not concatenate an independent system prompt.

## 5. Explicit client tools

```sh
tansr-tools --journal /absolute/private-journal --require-output
# In another terminal, use the printed session ID and the same base/family.
tansr-chat --attach SESSION_ID
```

Serve application policy must permit `DemoOrderStatus`. The demo registers a read-only synthetic order function, without shell, file tools or a general executor. It uses the public registration, initialize, first bind, optional output negotiation and Runner APIs. Ask for order `DEMO-001`: the handler captures stdout, waits cooperatively for 750 ms, captures stderr, then the Runner seals output. Capture, durable output ACK/seal and the business receipt are separate facts. Output uncertainty never justifies re-running the business handler.

`--run-once` handles one queued operation. The default loop continues polling and renewing its lease; Ctrl+C cooperatively cancels local execution. FileJournal is a private plaintext fact journal, not the encrypted archive. Return `ToolFailure::rejected` only when zero side effects can be proved; ambiguous cancellation, exceptions or lost output confirmation remain unknown. A business JSON result with `status: error` may still be a determinate execution result.

The authorizer rereads scope and checks app/user/revision/session/binding/workspace/tool. Production hosts must connect real revocation and resource policy. Platform registration is not authorization, and a missing client executor never permits a fallback to Serve's host machine.

## 6. Archives and materials

The Serve host registers a Source/provider first; this demo invents no registration route. Use separate private directories for credentials, keys, archive and intents. Set `TANSR_ARCHIVE_KEY_FILE` to a private file with 64 hexadecimal digits representing an AES-256 key. Use one key per archive and a host-managed `--key-id`. Do not store the key beside the archive or automatically replace it after decryption failure. FileStore has explicit key-rotation operations.

```sh
tansr-archive --mode prepare-create --session SESSION --source SOURCE --request-id bind-001 --intent /absolute/intents/create.json
tansr-archive --mode create --intent /absolute/intents/create.json
tansr-archive --mode creation-status --intent /absolute/intents/create.json
tansr-archive --mode sync --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1
tansr-archive --mode status --binding BINDING
tansr-archive --mode recover --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1 --request-id recover-001
tansr-archive --mode materials --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1 --request-id response-001 --intent /absolute/materials/response.json
tansr-archive --mode material-status --intent /absolute/materials/response.json
```

Every invocation must retain the original family, base URL and scope. The `.owner` sidecar rejects cross-family/identity replay. `prepare-create` durably preserves the original body, request ID, epoch and absolute deadline before `create`; preparation does not create a binding. Expiry does not authorize changing the timestamp or key. Query the original operation's status instead. Existing intents are never overwritten; partial local failures retain evidence and are not reported as success.

FileStore verifies page chains, record/body/attachment digests and current identity. It durably commits the complete page, attachments and pending ACK before sending the ACK. Sync completion, confirmed pending-ACK recovery, coverage and material consumption are different facts. `recover` reconciles the original pending operation or explicit stale recovery; it does not synchronize the whole archive. A generic 412, revoked authorization or expired epoch does not automatically rebase.

`materials` observes a current request, fixes its first remaining TTL as an absolute deadline and persists the original `.request` intent. It supplies only explicitly requested verified records. Preparation, chunking and waiting do not extend that deadline. The final response is saved before submission. Reconcile a lost response with `material-submit` or `material-status` using the saved response; received does not mean core-consumed. Archive and materials are not a second authority for context, approval or billing, nor a claim of fully local memory, multi-copy backup or compatibility with another SDK's private file format.

## 7. Errors and delivery evidence

Check every `Result<T>`. Retain original identifiers, request bodies, deadlines and local media when outcomes are uncertain. `ApiClient::retry_same_request` requires a permitted original error and matching request witness; it is not a generic automatic retry. Default logs exclude raw error detail, tokens, tool arguments and response bodies.

Serve must provide UAPI revision 7, the selected family and enabled operations. Handle disabled capabilities, insufficient current authorization and mismatched contract fingerprints as failures; do not change family or recreate an operation to avoid them. Tool output, offload and archive materials require their corresponding capability negotiation. A Serve version number does not replace these checks.

Public-source development uses `python tools/contract_check.py --mode public` to verify the 20 original assets in the distribution declaration. The 39-file internal reference set and private history are not distributed. [Mainline CI](https://github.com/tansrai/tansr-cpp/actions) and the [v0.1.0 release record](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0) are the entry points for validation and artifacts. Check the release record for native execution on three operating systems, real Serve and installed-package evidence with their limits. This guide or successful `--help` execution does not replace that evidence. See [NOTICE](../packaging/NOTICE.md) for third-party licenses.
