# Tansr C++ SDK

[中文](README.md) · [Guide](doc/guide.md) · [Source](https://github.com/tansrai/tansr-cpp) · [Releases](https://github.com/tansrai/tansr-cpp/releases)

The SDK, three demos and public documentation use the [MIT license](LICENSE). The first version is **0.1.0**. Source archives, binaries and recipes with fixed checksums are being prepared; this does not mean a version has been published or listed in vcpkg/ConanCenter.

A native C++17 library consuming Serve's unified `/api`. Serve owns the agent loop, session state, context, memory selection, permissions, adjudication, tool scheduling and usage. This SDK provides protocol bindings, session streaming, explicitly registered business tools and encrypted local archives. It does not launch a Node/Rust/Go client proxy or alter Electron's integrated SDK mode.

The locked UAPI revision 7 contains 81 operations in 11 families. `sdk1` is the default; `sdk2-offload-v1` is explicit and capability checked. Failures never silently change family/user or replay side effects. Transport is HTTP + SSE, not WebSocket.

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

Build with a C++17 toolchain, CMake 3.25+ and Ninja (used by the presets). Dependencies are fixed to curl 8.22.0, c-ares 1.34.8 and OpenSSL 3.5.9. Source CMake never downloads them implicitly. Match compiler, build configuration and CRT (`/MD` or `/MDd` on Windows). See [dependencies and versioned recipes](packaging/README.md). Candidates target Windows x64 / MSVC 19.44, Linux x64 / Ubuntu 24.04 / GCC 13, and macOS arm64 with minimum deployment target 26.0. See the guide for exact requirements; cross-compilation alone does not establish native support.

The three demos consume only public SDK APIs:

- `tansr-chat`: privately persisted offload creation intent and original-request recovery, multiple turns, observed approval/question tickets, mid-turn input, explicit interruption and reconnection.
- `tansr-tools`: a synthetic order lookup, durable execution journal, stdout/stderr chunks and a separate output seal.
- `tansr-archive`: saved creation intent, Source binding, encrypted durable storage before ACK, materials and explicit recovery.

Start with `--help`. Authentication rereads private short-lived token and current scope files for each request. These files are a demo host input; Serve authentication/application policy remains authoritative. See the [guide](doc/guide.md).

Serve must expose UAPI revision 7 and the selected family's capabilities. Offload, terminal tool output and archive recovery additionally require the corresponding operations to be enabled by Serve's capability declarations and current authorization. The SDK operation catalogue does not enable server features or replace negotiation with a minimum Serve version claim.

The public contract is an explicit **20-file** subset of the frozen assets, checked against `contract/DISTRIBUTION.json` and its export policy. Run `python tools/contract_check.py --mode public` when developing the public source. Private references from the 39-file internal set, Serve/kernel source and private history are outside this license and distribution scope. Third-party licenses are listed in [NOTICE](packaging/NOTICE.md). Arbitrary shell/PTY, GUI/mobile adapters, complete client-side memory orchestration and other SDKs' private storage formats are outside this first high-level release scope.
