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
| Windows x64 | VS 2022 / MSVC 19.44; the released static SDK is Release `/MD`. `/MDd` applies only to Debug builds made from source. Build applications in an x64 Native Tools shell; running demos requires the matching MSVC runtime |
| Linux x64 | GCC 13; verified on Ubuntu 24.04 / glibc 2.39. The released binaries' observed symbol floors are glibc 2.38 and GLIBCXX 3.4.32; this does not establish validation on every distribution satisfying those versions |
| macOS arm64 | Apple Clang 21; the v0.1.0 artifacts' minimum deployment target is macOS 26.0, with no claim of compatibility with earlier versions |

Building from source still requires prepared, matching curl 8.22.0, c-ares 1.34.8 and OpenSSL 3.5.9 dependencies from [dependencies.json](../packaging/dependencies.json). Default CMake configuration does not download them; generated operations are already in the source tree. See the [recipes](../packaging/README.md) for source installation. For a normal source installation without bundled dependencies, provide both prefixes:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/absolute/tansr-cpp-0.1.0;/absolute/dependencies"
cmake --build build
```

Hosts with their own matching dependencies can add `-DTansrSDK_NO_BUNDLED_DEPENDENCIES=ON` and supply their dependency prefix. This disables only automatic selection of the package's `dependencies/` directory. It does not disable dependency checks or establish compatibility with arbitrary versions. Package-local lookup does not permanently change the application's prefix settings.

For Windows shared builds made from source, deploy the SDK and dependency DLLs from their `bin` directories with the required runtime. A developer machine's global PATH does not prove a complete deployment. The static SDK release bundles development dependencies but still requires its declared OS/CRT runtime conditions.

Each demo has its own artifact; these are not SDK link directories. Extract each package separately and invoke its executable under `bin/`, adding `.exe` on Windows. For example, extract the chat package to `/absolute/tansr-chat-0.1.0` and run `/absolute/tansr-chat-0.1.0/bin/tansr-chat --help`. Their names match the Rust demos, so preserve existing installations. Configure credentials and the service origin as described next.

## 2. Authentication and current identity

Before the first connection, obtain the service origin, available session family, short-lived token and current scope from the Serve operator or application backend, and confirm the model and application policy are configured. The SDK and demos connect to an existing Serve; they do not start or configure it. Neither `--help` nor the startup program in section 1 verifies these server conditions. Running `tansr-chat --message ...` without attach/resume creates a real session and sends a business turn.

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

All three demos share these options. Use `--option value`, not `--option=value`, and quote paths containing spaces. Duplicate options are rejected:

| Option | Default and purpose |
|---|---|
| `--base ORIGIN` | Overrides `TANSR_BASE_URL`; defaults to `http://127.0.0.1:8787` when neither is set. Supply an origin without `/api` |
| `--family FAMILY` | Defaults to `sdk1`; the other accepted value is `sdk2-offload-v1`. Explicitly retain the original family in subsequent processes |
| `--token-file FILE`, `--scope-file FILE` | Override their respective environment variables. Values are absolute file paths, not token or JSON contents |
| `--timeout SECONDS` | Defaults to `600`, accepts `1..86400`; bounds the current demo's total cancellation interval, including interaction, polling and waiting for materials |

The default tools loop therefore does not run indefinitely; use, for example, `--timeout 3600` for a longer observation. This is not a fresh timeout for every request and does not extend a saved creation/material intent's deadline. Ordinary chat/tools writes have a separate 30-second deadline, which increasing `--timeout` does not change. Timeout or Ctrl+C cancels local work; it does not prove that a Serve write or business turn never happened.

ApiClient accepts an HTTP(S) origin without credentials, a path, query or fragment. The current implementation does not restrict HTTP to loopback. Use plaintext only for loopback development as a deployment recommendation, and valid HTTPS for deployed services. Applications configure enterprise CA material through `RuntimeOptions.ca_file`; chain and hostname verification remain enabled. Demos never place tokens in command-line arguments, logs, error details or archives. Raw `Error.detail` may contain server or business content; applications must protect explicitly collected diagnostics.

`RuntimeOptions.ca_file` is an SDK host setting. Released demos have no `--ca-file` option and use the default Runtime trust configuration. An application needing an explicit enterprise CA should configure this field through the SDK, rather than pass an unsupported demo option or disable certificate verification.

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

Wait for tools to print both `session:` and `ready:` before starting the second terminal. Configure the same credential paths and base in each terminal and use the family printed by tools; a new terminal does not automatically inherit another terminal's temporary environment settings. Tools creates a session when `--session` is absent. Use `--session SESSION_ID` to connect to a known session, rather than chat's `--attach` option. When restarting an executor, retain its journal and executor identity (`--executor` defaults to `cpp-demo`), for example:

```sh
tansr-tools --family sdk1 --session SESSION_ID --executor cpp-demo --journal /absolute/private-journal --require-output --timeout 3600
```

The existing session must still permit the current `DemoOrderStatus` capability binding; reconnecting does not bypass Serve policy. Creating a new offload session through tools also requires a caller-preserved `--request-id STABLE_ID`. Once the session ID is known, reconnect with `--session` and the same family. The journal records execution facts, not a durable session-creation intent. Keep uncertain journal facts; restarting does not authorize executing an uncertain business operation again.

Serve application policy must permit `DemoOrderStatus`. The demo registers a read-only synthetic order function, without shell, file tools or a general executor. It uses the public registration, initialize, first bind, optional output negotiation and Runner APIs. Ask for order `DEMO-001`: the handler captures stdout, waits cooperatively for 750 ms, captures stderr, then the Runner seals output. Capture, durable output ACK/seal and the business receipt are separate facts. Output uncertainty never justifies re-running the business handler.

`--run-once` handles one queued operation. The default loop continues polling and renewing its lease; Ctrl+C cooperatively cancels local execution. FileJournal is a private plaintext fact journal, not the encrypted archive. Return `ToolFailure::rejected` only when zero side effects can be proved; ambiguous cancellation, exceptions or lost output confirmation remain unknown. A business JSON result with `status: error` may still be a determinate execution result.

The authorizer rereads scope and checks app/user/revision/session/binding/workspace/tool. Production hosts must connect real revocation and resource policy. Platform registration is not authorization, and a missing client executor never permits a fallback to Serve's host machine.

## 6. Archives and materials

The Serve host registers a Source/provider first; this demo invents no registration route. Use separate private directories for credentials, keys, archive and intents. Set `TANSR_ARCHIVE_KEY_FILE` to a private file with 64 hexadecimal digits representing an AES-256 key. Use one key per archive and a host-managed `--key-id`. Do not store the key beside the archive or automatically replace it after decryption failure. FileStore has explicit key-rotation operations.

First identify the binding to use. The prepare-create/create steps below apply to an SDK1 session without a binding when the host supports manual binding. Serve creates an offload session's Source/binding during session creation; reuse that original binding and proceed to sync/status instead of creating another one. Obtain the binding ID from the trusted host, or read it through `ArchiveClient::binding_target(SESSION_ID)` in an SDK application. The demo has no `--mode target`. Add `--family sdk2-offload-v1` to every offload archive invocation; do not accidentally use the default SDK1 family shown below.

```sh
tansr-archive --mode prepare-create --session SESSION --source SOURCE --request-id bind-001 --intent /absolute/intents/create.json
tansr-archive --mode create --intent /absolute/intents/create.json
tansr-archive --mode creation-status --intent /absolute/intents/create.json
tansr-archive --mode sync --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1
tansr-archive --mode status --binding BINDING
tansr-archive --mode recover --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1 --request-id recover-001
tansr-archive --mode materials --binding BINDING --file /absolute/archive/history.bin --key-id local-key-1 --request-id response-001 --intent /absolute/materials/response.json
tansr-archive --mode material-submit --intent /absolute/materials/response.json
tansr-archive --mode material-status --intent /absolute/materials/response.json
```

Every invocation must retain the original family, base URL and scope. The `.owner` sidecar rejects cross-family/identity replay. `prepare-create` durably preserves the original body, request ID, epoch and absolute deadline before `create`; preparation does not create a binding. Expiry does not authorize changing the timestamp or key. Query the original operation's status instead. Existing intents are never overwritten; partial local failures retain evidence and are not reported as success.

FileStore verifies page chains, record/body/attachment digests and current identity. It durably commits the complete page, attachments and pending ACK before sending the ACK. Sync completion, confirmed pending-ACK recovery, coverage and material consumption are different facts. `recover` reconciles the original pending operation or explicit stale recovery; it does not synchronize the whole archive. A generic 412, revoked authorization or expired epoch does not automatically rebase.

Sync defaults to at most 64 pages per invocation; `--max-pages` accepts `1..1024`. Reaching that bound exits nonzero: continue with the original binding/file/key without deleting the archive. After recover confirms the original pending ACK, use sync for remaining pages. Recover/materials require the original archive file to exist. Materials waits for one actual current material request, or until local cancellation; material-status printing received and exiting nonzero means core-consumed is still unconfirmed, not permission to replace the intent and retransmit.

`materials` observes a current request, fixes its first remaining TTL as an absolute deadline and persists the original `.request` intent. It supplies only explicitly requested verified records. Preparation, chunking and waiting do not extend that deadline. The final response is saved before submission. Reconcile a lost response with `material-submit` or `material-status` using the saved response; received does not mean core-consumed. Archive and materials are not a second authority for context, approval or billing, nor a claim of fully local memory, multi-copy backup or compatibility with another SDK's private file format.

## 7. Errors and delivery evidence

Check every `Result<T>`. Retain original identifiers, request bodies, deadlines and local media when outcomes are uncertain. `ApiClient::retry_same_request` requires a permitted original error and matching request witness; it is not a generic automatic retry. Default logs exclude raw error detail, tokens, tool arguments and response bodies.

Serve must provide UAPI revision 7, the selected family and enabled operations. Handle disabled capabilities, insufficient current authorization and mismatched contract fingerprints as failures; do not change family or recreate an operation to avoid them. Tool output, offload and archive materials require their corresponding capability negotiation. A Serve version number does not replace these checks.

Demos exit normally with `0` on success and `1` for failures they report. On stderr, `code` is a numeric local `ErrorCode`, while `status` is the HTTP status (possibly `0` for a local failure); server errors may also include `wire` and `retry`. These fields help diagnosis and do not grant generic retry permission. For a first connection, check:

| Symptom | Check and action |
|---|---|
| Package discovery, linking or executable loading fails | Point the SDK prefix at the extracted root with its complete layout. Check architecture, compiler, Release/CRT and the platform baselines above. Configure a new build directory after changing toolchains or prefixes; running a demo directly does not require CMake |
| Credential, journal or archive path is rejected | Check absolute paths, current-user ownership, private permissions and every ancestor for links/reparse points. macOS `/tmp` and `/var` are often system path aliases; confirm the physical path of a directory you created and trust before passing it, rather than automatically following unknown input links |
| Network/TLS error without a valid HTTP status | Check the origin, DNS/network, Serve listening address and certificate chain/hostname. Demos do not follow redirects; supply the final Serve origin. See section 2 for enterprise CA configuration |
| Authentication, capability or contract rejection | Check the token, all three scope fields, original family, Serve configuration and application policy. `--require-output` additionally needs output capability. Do not hide rejection by switching identity/family or automatically dropping a requirement |
| Timeout, nonzero exit or an unconfirmed outcome | Retain the printed session ID, original intent, journal, archive and key. Follow sections 4–6 for attach, original-intent reconciliation or recover as appropriate. An exit code is not proof that a business operation never ran |

Public-source development uses `python tools/contract_check.py --mode public` to verify the 20 original assets in the distribution declaration. The 39-file internal reference set and private history are not distributed. [Mainline CI](https://github.com/tansrai/tansr-cpp/actions) and the [v0.1.0 release record](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0) are the entry points for validation and artifacts. Check the release record for native execution on three operating systems, real Serve and installed-package evidence with their limits. This guide or successful `--help` execution does not replace that evidence. See [NOTICE](../packaging/NOTICE.md) for third-party licenses.

## Dedicated local MemoryPublication host (PST-05)

`<tansr/memory_publication.hpp>` exposes the narrow `Store::execute(request, owner)` storage port for the frozen terminal-services-v1 head/read/begin/chunk/commit/query actions. The owner is the original execution scope/sessionId/binding. Serve and the trusted host retain authorization and memory decisions. Storage does not run models, extract memories, or change Archive, canonical encoding, digests, or ACK semantics. The original 4 MiB body and 12 KiB chunk bounds apply, with exact hashes, contiguous offsets, UTF-8 validation, original-transfer idempotency and persisted CAS conflict outcomes.

Open `FileStore` with explicit create/reopen, an absolute private file path, identity, key ID, a provider returning the current 32-byte key, and a current scope provider. Create the parent through `storage::create_private_directory`. Create never overwrites; reopen never substitutes an empty domain for a missing original. The existing PrivateDirectory supplies the exclusive lock and atomic durable replacement. AES-256-GCM protects the body, staging, owner, transfer outcomes, and execution journal requests/results together; AAD binds format, identity and key ID. Wrong/missing keys, corruption and authorization changes fail closed and preserve the original. Use a dedicated key for each publication file, separate from Archive. The 2^20 encryption limit fails explicitly. Use the explicit new-path migration/key-rotation API below. Deleting receipts, resetting the file or restoring old backups is not a capacity or rotation workaround.

`create_host({store, journal, authorize, true})` requires durable publication and encrypted Store/Journal declarations. FileStore implements both. Pass the returned tools **and** journal to the original ExecutorRunner. Custom implementations remain injectable; the plaintext executor FileJournal does not satisfy the publication host's encrypted journal interface. ToolContext carries an owned original operation; absence grants no dedicated authority. The authorizer checks current identity and binding before and after execution. Only the frozen MemoryPublication permission, TansrTerminalMemoryPublication tool name and definition digest select this profile. It is neither a model tool nor Shell.

The built `tansr-memory` demo reuses the existing private credential/key-file facilities. In this process, `TANSR_ARCHIVE_KEY_FILE` must identify a **dedicated publication key**, never an existing Archive key. Configure a legitimate Source/domain and MemoryPublication permission in Serve, then attach an existing session:

```sh
tansr-memory --session EXISTING_SESSION --binding-request ORIGINAL_BINDING_REQUEST_ID --file /absolute/private-memory/publication.bin --key-id memory-key-1 --source SOURCE_ID --generation 1 --domain DOMAIN_ID --create
# Reopen with the same identity, file and key; omit --create.
```

The demo registers the dedicated executor host, initializes without a model-tool declaration, binds it, then calls the original `terminal.binding.create` with required `memory-lifecycle-v1`. It validates the returned request ID, session, complete binding (including any interpreter), scope and accepted feature before printing ready. The generic execution effective-tools list is not a declaration of this auxiliary capability. The original Runner checks Serve execution status for each operation; the trusted host also rechecks current scope/session/binding. It drains Runner before closing storage and Runtime. It does not create replacement sessions, add a cloud index, or promise cloud access to device-only data while offline. Cross-owner queries are denied by default. An optional authorize_recovery callback must verify the original Serve recovery proof, revocation of the old authorization and current connection permission. The adapter additionally requires the same app/user/session/executor/workspace and only permits query, never ownership transfer for writes. The demo has no recovery-proof provider; do not substitute an unconditional true callback.

Keep the original binding request ID **and complete original request body** for an uncertain binding result. A process restart that registers a new connection creates a different binding; do not replay that new body under the old request ID or invent a new ID to bypass an unknown result. This small demo does not persist a connection/binding recovery intent. A production controller must reconcile the original binding first; the existing encrypted operation/transfer records remain authoritative. Merely reopening the file does not grant a new owner permission to finish an old transfer.

`capacity()` reports retained transfers, staging bytes, journal entries and remaining limits. Limits are bound to the file; outcomes and original keys are not silently expired. A failed replacement response or post-commit revocation yields unknown and requires close/reopen before reconciliation using the original transferId or operation journal. An only-claim record retains the existing Runner's unknown semantics. Back up a closed complete ciphertext file and preserve external key recovery. Old backups can lack replay witnesses and must not simply replace current authoritative storage. Application logs, RAM and swap are outside the encryption claim.

Windows local evidence is recorded in the development ledger. `integration/publication.py` consumes a SHA256-pinned public-package Serve host, runs native C++ processes and the actual demo, and retains failed temporary directories. It does not count generic Archive tests as publication evidence. Linux/macOS and platform installable artifacts still require their own evidence.
The dedicated real-Serve entry point requires the trusted host manifest and exact public package hashes; the host itself is not shipped with this SDK:

```sh
python integration/publication.py --manifest /absolute/shared-host.json --node /absolute/node --build /absolute/out/release --logs /absolute/new-evidence/native --suite native
python integration/publication.py --manifest /absolute/shared-host.json --node /absolute/node --build /absolute/out/release --logs /absolute/new-evidence/demo --suite demo
```

The scenario defaults to `sdk1`. `--family sdk2-offload-v1` requires a Host that installs the original public offload family and publication configuration; using an SDK2 client alone does not install that session family. Do not bypass `capability_unavailable` / `not_installed` by changing headers, falling back to another family or inventing a Source. Before creating a new-family session, the consumer retains its original requestId, complete body and absolute deadline and refuses to overwrite an uncertain creation intent.

The native scenario sends at most twelve synthetic Chinese pins and stops when an actually committed publication exceeds 12 KiB; otherwise it fails. After every three complete original transactions it gracefully closes and reopens the worker with the same medium and connection; each process retains its 360-second total budget. The original pin implementation clamps anchors to 240 characters, regardless of the 4096-character wire bound. Busy commands are retried with the same keys and body only after the original receipt query confirms no acceptance. The comparator checks one transfer ID, begin length/SHA, continuous chunk offsets/digests, and commit length/etag. Chunks from different transfers cannot establish continuation.

After the first real chunk receipt is durable, the process exits and reopens the original encrypted medium, preserving connection, operation ID, digest, transfer and receipt. A separate injection drops an HTTP response only after Serve actually accepts the receipt; reconciliation does not reenter storage. After the original session closes and the trusted Host confirms settlement through its public handle, the Host explicitly selects reopen for the next source installation. The client resumes the same session, explicitly binds the same connection for this new session epoch, and verifies every actual read chunk and the full SHA. The new binding never replaces prior operation identities. Surviving leases are rejected; reopening does not clear unresolved commands. Claim-only permanent unknown is tested after cold reading. See the development ledger for actual candidate/platform results; this does not establish whole-Serve process restart, cross-owner takeover or Linux/macOS behavior.

Hosts calling `Runner::execute` directly manage heartbeat themselves. A renewed connection returned by `Client::heartbeat` does not update an existing Runner. Expiry after a durable effect can still require a permanent unknown receipt. At a serial idle point, save the renewed expiry of the same connection and construct a Runner with it; `Runner::run` manages its own heartbeat. Never reset the journal or invent a replacement operation ID to bypass expiry.

### Explicit migration and key rotation

`FileStore::migrate(source, destination)` supports the existing encrypted `Tansr-Cpp-MemoryPublication/1` format. Stop the original Runner and close the Store first. The source must use `create=false`; the destination must use `create=true`, a nonexistent file in **another private directory**, and exactly the same app/user/sourceId/sourceGeneration/domainKey. Both directories are exclusively locked throughout the operation. Parent directories are not automatically created. Explicit capacity changes are permitted only if the complete original state fits before publication.

Provide a **fresh key ID and an exclusive key never used for another store**. The actual 32 key bytes must also differ from the source. Renaming a key is rejected. Same-key copies are not supported because forked snapshot counters cannot establish total use of one key; the new key counter starts with the first complete snapshot encryption. Keep host-managed key providers stable; a failed migration never authorizes generating a replacement key and retrying automatically.

```cpp
auto source = current_options; // Real current-scope and key callbacks.
source.create = false;
auto destination = source;
destination.path = new_private_directory / "publication.bin";
destination.create = true;
destination.key_id = fresh_key_id;
destination.read_key = read_fresh_exclusive_key;
auto made = tansr::storage::create_private_directory(new_private_directory);
if (!made) return made.error();
auto migrated = tansr::memory_publication::FileStore::migrate(source, destination);
if (!migrated) return migrated.error();
// Record the version, identity, ciphertext hashes/byte counts and fact counts.
// Explicitly select one active path, then reopen with destination.create=false.
```

The first atomic publication contains the entire imported snapshot; no empty destination store is published first. Body, committed/conflict/staging transfers, partial chunks, original owners, operation IDs/digests/deadlines, determinate and unknown receipts, and pending claims are preserved. The co-located encrypted execution journal is included. Migration does not invoke tools, change Serve bindings or recovery authorization, or complete pending operations. `MigrationReceipt` covers only this store: its format, identity, source/destination ciphertext SHA256 and byte counts, and transfer/journal counts. It excludes external sessions, budgets, other snapshots and keys. The receipt is returned only after complete publication and checks of both sides; both locks are then released.

On failure, retain the original. The destination may be absent or already contain the complete snapshot. Lost confirmation after publication reports `unknown`; it does not prove that publication never happened. Keep both paths and keys. Explicitly reopen using the original configurations and reconcile the complete snapshot and original operations before selecting a path; do not delete or overwrite a target to retry. Missing/corrupt/unknown-format originals and wrong keys or identities fail closed without creating an empty store.

The source remains unchanged, but it becomes an offline historical copy. Never connect both copies to Runners simultaneously. Once the target accepts new facts, switching back to the old snapshot would lose deduplication evidence. An explicit rollback to the old file is only valid before any new facts have been accepted. This tool provides no distributed ownership transfer or parallel-write coordination.

The Demo provides a local-only migration mode using the existing private scope/token file facilities. It needs no `--session` and makes no network requests or registration/binding calls:

```sh
# TANSR_ARCHIVE_KEY_FILE still selects the original publication-only key.
export TANSR_MEMORY_NEXT_KEY_FILE=/absolute/private-keys/fresh-memory-key.hex
tansr-memory --file /absolute/private-memory/publication.bin --key-id memory-key-1 --source SOURCE_ID --generation 1 --domain DOMAIN_ID --migrate-to /absolute/private-memory-next/publication.bin --new-key-id memory-key-2
# After checking the receipt, explicitly select the new key and target file/key-id; omit --create.
```

`--migrate-to` rejects `--create`. Retain the old file/key and switch to a single active path under host authority. Successful output confirms only local snapshot migration, not a new Serve binding or completion of business recovery.

## Explicit TerminalPersistence v1 profile (PST-05)

`<tansr/terminal_persistence.hpp>` exposes `terminal_persistence::FileStore` and `create_host` for the approved, separate `TansrTerminalPersistenceV1` profile. The original six actions, default factory, contract assets and `Tansr-Cpp-MemoryPublication/1` layout retain their meaning. The new `Tansr-Cpp-TerminalPersistence/1` layout is rejected by the old factory; an unavailable explicit v1 profile does not downgrade.

The store handles opaque fixed 12,288-byte body blocks, canonical descriptor pages, dual-key entries/values, roots and permanent transfer results. A body is at most 4 MiB and one batch has at most 256 entries. `commitRoot` is distinct from the body digest. Reads and lookups require the current root. Body root, permanent entry ordinals/both keys, transfer result and reservations commit in one encrypted snapshot replacement. Historical query returns that transfer's original complete Root. Validated pages authorize objects; reuse is limited to the protected base and exactly matching permanent entries. No memory or receipt business decision is copied into the SDK.

Configure `tp::Options` with an absolute private path, explicit `create`, trusted source identity, dedicated fresh `key_id`, current key provider and scope provider; open `tp::FileStore`, then pass it as both store and journal to `tp::create_host({store, store, current_authorizer, true})`. Supply both returned tools and journal to the existing Runner. The host passes an original-operation authorization/cancellation guard through every storage IO boundary. Direct `Store::execute(request, owner, guard)` callers have the same trusted configuration responsibility. Query-only recovery cannot authorize a new owner to put or commit. Execution claims and full receipts remain encrypted; pending/unknown never grants re-execution.

Actual defaults are 32 active transfers, 8 MiB staging raw bytes, 4,096 each permanent entries/transfers/objects, 32 MiB logical retained-plus-reserved bytes, 8,192 journal entries and a 64 MiB ciphertext file. `head` reports actual lower quotas. Begin reserves declared objects/bytes and 262,144 metadata bytes under the shared accounting rules. Physical snapshot, future journal space and the per-key budget below 2^20 writes are checked separately; unrelated operations cannot consume accepted settlement reservations. Quota and IO failure preserve original facts. The private directory holds an exclusive cross-process lock.

This implementation rewrites and audits the whole snapshot and scans its in-memory index. It claims neither constant IO, a million entries nor 1 GiB capacity. Old and temporary encrypted files may coexist during atomic replace; plan at least twice the configured maximum file size. This is not OS disk preallocation or a power-loss SLA. Full disk or lost sync confirmation can remain unknown. No physical GC is exposed, and permanent entries/transfers are never TTL-evicted.

Explicit Demo usage:

```sh
tansr-memory --profile persistence-v1 --session EXISTING_SESSION --binding-request ORIGINAL_BINDING_REQUEST_ID --file /absolute/private-v1/persistence.bin --key-id dedicated-v1-key --source SOURCE_ID --generation 1 --domain DOMAIN_ID --create
```

Serve must explicitly install/select this same profile. Existing capability fields and routes are unchanged; omitting `--profile` retains the original publication host. Never run both profiles as current writers for the same domain. The Demo does not invent cross-owner takeover or resolve an unknown binding by changing its request body.

Same-format v1 maintenance uses `FileStore::migrate(source,destination)` or `--profile persistence-v1 --migrate-to NEW_PATH --new-key-id NEW_ID`. Stop the Runner, reopen the source and create a nonexistent target in another private directory with a new, independently dedicated key ID and actual key. The first target publication already contains all objects, index, roots, pending/unknown/permanent transfer facts and journal. Existing targets and insufficient capacity are refused without deleting the source. Lost confirmation preserves both paths for explicit reconciliation. The destination is persistently read-only: its authenticated snapshot permits head/read/lookup/query after ordinary cold reopen, but rejects begin/put/commit. Because the C++ journal shares this file, it also rejects new execution claims (including read operations) and completion of existing pending claims. Existing original pending/receipts remain queryable; repeating an already stored identical receipt is a no-op. There is no activation option. This copy supports verification and evidence, not execution takeover or a writable cutover. The source and its write counter remain unchanged; without a common source fence the utility cannot authorize a second writer.

Conversion from the legacy six-action business snapshot is not inferred by this storage-only utility. A trusted controller must retain legacy keys/files/history query, prove that the old writer stopped and unknown operations settled, and explicitly establish v1. Missing common cutover proof remains pending, rather than selecting a layout from file existence.

Each AES-GCM encryption attempt first durably burns a counter in the adjacent `<file>.writes` sidecar, including attempts whose snapshot replacement later fails. This small sidecar contains only a random store ID, identity hash, key ID and counter authenticated with HMAC-SHA256; it contains no plaintext body, index, owner or receipt and does not itself use GCM. The store refuses a missing, invalid or lower-than-snapshot counter. Keep both files together for closed backups; an orphan counter left by a failed first publication is retained and blocks `create`. Same-format migration validates the original pair and creates a new store ID/counter only under the explicitly new dedicated key. The migration receipt hashes name the main encrypted snapshot; preserve its paired counter as well. A running instance detects counter replacement, but replay of a complete old snapshot/counter pair—or an old counter after a failed attempt with no newer snapshot—cannot be detected without an external monotonic witness. Do not restore an old pair or reset this counter to recover capacity. This does not add a whole-directory rollback guarantee.
