# Tansr C++ SDK

[中文](README.md) · [Guide](doc/guide.md) · [Source](https://github.com/tansrai/tansr-cpp) · [Download v0.1.0](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0)

The SDK, three CLI demos and public documentation use the [MIT license](LICENSE). The **[v0.1.0 release](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0)** provides source archives, SDK and separate demo packages for Windows x64 / Linux x64 / macOS arm64, checksums and pinned recipes. Conan/vcpkg recipes are independently distributed and are not listed in ConanCenter or the public vcpkg registry.

A native C++17 library consuming Serve's unified `/api`. Serve owns the agent loop, session state, context, memory selection, permissions, adjudication, tool scheduling and usage. This SDK provides protocol bindings, session streaming, explicitly registered business tools and encrypted local archives. It does not launch a Node/Rust/Go client proxy or alter Electron's integrated SDK mode.

The locked UAPI revision 7 contains 81 operations in 11 families. `sdk1` is the default; `sdk2-offload-v1` is explicit and capability checked. Failures never silently change family/user or replay side effects. Transport is HTTP + SSE, not WebSocket.

## Choose and verify a release package

Download the matching **SDK** package to integrate a C++ application, or the **chat / tools / archive** packages to run the examples. Each demo is a separate executable in its own extracted `bin/` directory. Running a session requires a reachable Serve and valid short-lived credentials; start with `--help` to inspect the options.

| Platform | SDK | Separate demos |
|---|---|---|
| Windows x64 / MSVC 19.44 | [SDK (zip)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-sdk.zip) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-chat.zip) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-tools.zip) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-archive.zip) |
| Linux x64 / GCC 13.3 | [SDK (tar.gz)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-sdk.tar.gz) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-chat.tar.gz) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-tools.tar.gz) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-archive.tar.gz) |
| macOS arm64 / AppleClang 21 | [SDK (tar.gz)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-sdk.tar.gz) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-chat.tar.gz) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-tools.tar.gz) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-archive.tar.gz) |

SDK packages contain Release static libraries; each demo package contains a standalone executable. Windows requires x64, Release `/MD` and matching MSVC runtimes. The Linux binaries were validated on Ubuntu 24.04 and require at least the glibc 2.38 / GLIBCXX 3.4.32 symbols. The Mac package requires arm64 and macOS 26.0+. For other compilers, architectures, Debug or shared-library configurations, follow the [source-build guide](doc/guide.md) with matching dependencies and configuration.

Download the versioned [SHA256SUMS](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/SHA256SUMS) and [asset manifest](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/public-asset-manifest.json). In your download directory, calculate the selected file's SHA256 and compare it with the row for its **complete filename**. Extract the package only after the values match. Use the command for your operating system:

```powershell
# Windows PowerShell
Get-FileHash .\tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-sdk.zip -Algorithm SHA256
```

```sh
# Linux
sha256sum tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-sdk.tar.gz
# macOS
shasum -a 256 tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-sdk.tar.gz
```

The extracted SDK root directly contains `include/`, `lib/`, `dependencies/` and `share/`; pass that root to CMake below. Configure each demo's credential files, origin, session ID and persistent directories using the [guide](doc/guide.md). Your trusted backend and Serve provide authentication. The release does not include a test account or model key for a live service.

The v0.1.0 tag and assets remain frozen; repository `main` maintains supplemental documentation for that release. [Versioned recipes](packaging/README.md) use the pinned source checksums in the separate recipes archive. Empty recipe metadata inside the source snapshot retains its original validation behavior.

## Build your application

```cmake
find_package(TansrSDK 0.1.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE tansr::sdk)
```

Keep the binary SDK's `include/`, `lib/`, `dependencies/` and `share/TansrSDK/` layout intact. An application supplies only the extracted SDK root in `CMAKE_PREFIX_PATH`; the [guide](doc/guide.md) includes a complete standalone application and commands. Each demo has its own artifact with its executable under `bin/`, adding `.exe` on Windows.

The following commands build from source and require prepared dependencies:

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/absolute/prepared-dependencies
cmake --build --preset release
ctest --preset release
cmake --install out/release --prefix /absolute/tansr-cpp-0.1.0
```

Build with a C++17 toolchain, CMake 3.25+ and Ninja (used by the presets). Dependencies are fixed to curl 8.22.0, c-ares 1.34.8 and OpenSSL 3.5.9. Source CMake never downloads them implicitly. Match compiler, build configuration and CRT (`/MD` or `/MDd` on Windows). See [dependencies and versioned recipes](packaging/README.md). The v0.1.0 binaries target Windows x64 / MSVC 19.44, Linux x64 / Ubuntu 24.04 / GCC 13, and macOS arm64 with minimum deployment target 26.0. See the guide and each artifact manifest for exact requirements.

The three demos consume only public SDK APIs:

- `tansr-chat`: privately persisted offload creation intent and original-request recovery, multiple turns, observed approval/question tickets, mid-turn input, explicit interruption and reconnection.
- `tansr-tools`: a synthetic order lookup, durable execution journal, stdout/stderr chunks and a separate output seal.
- `tansr-archive`: saved creation intent, Source binding, encrypted durable storage before ACK, materials and explicit recovery.

Start with `--help`. Authentication rereads private short-lived token and current scope files for each request. These files are a demo host input; Serve authentication/application policy remains authoritative. See the [guide](doc/guide.md).

Serve must expose UAPI revision 7 and the selected family's capabilities. Offload, terminal tool output and archive recovery additionally require the corresponding operations to be enabled by Serve's capability declarations and current authorization. The SDK operation catalogue does not enable server features or replace negotiation with a minimum Serve version claim.

The public contract is an explicit **20-file** subset of the frozen assets, checked against `contract/DISTRIBUTION.json` and its export policy. Run `python tools/contract_check.py --mode public` when developing the public source. Private references from the 39-file internal set, Serve/kernel source and private history are outside this license and distribution scope. Third-party licenses are listed in [NOTICE](packaging/NOTICE.md). Arbitrary shell/PTY, GUI/mobile adapters, complete client-side memory orchestration and other SDKs' private storage formats are outside this first high-level release scope.
