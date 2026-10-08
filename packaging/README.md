# 版本化包配方 / Versioned package recipes

SDK、三个 Demo 与公开文档使用根目录 [MIT LICENSE](../LICENSE)，仓库为 [tansrai/tansr-cpp](https://github.com/tansrai/tansr-cpp)。0.1.0 的正式源码包、二进制和固定摘要配方正在准备；当前不宣称正式版已发布或已进入 vcpkg/ConanCenter。

二进制 SDK 包在 `dependencies/` 携带匹配的开发依赖，安装配置局部查找该目录；普通应用只需 SDK 解包根目录作为 `CMAKE_PREFIX_PATH`。完整 `main.cpp`、CMake 和运行命令见[中文指南](../doc/使用指南.md)与 [English guide](../doc/guide.md)。宿主显式设置 `TansrSDK_NO_BUNDLED_DEPENDENCIES=ON` 时须另行提供匹配依赖前缀。三个 Demo 各自单独成包，从各自的 `bin/` 调用，不用作 SDK 链接目录。

从源码构建的默认 CMake 不联网。先按 [dependencies.json](dependencies.json) 准备相同 OS/arch/编译器/CRT 的 curl 8.22.0、c-ares 1.34.8、OpenSSL 3.5.9 前缀。包管理器明确获取 SDK 源码，依赖仍使用已准备前缀；不是社区 registry 自动解析依赖的承诺。

## 源码 CMake 安装消费

从经校验的源码根运行，预设要求 CMake 3.25+、Ninja 和 C++17 工具链：

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/absolute/dependencies
cmake --build --preset release
cmake --install out/release --prefix /absolute/tansr-cpp-0.1.0
cmake -S tests/consumer -B out/consumer -DCMAKE_PREFIX_PATH="/absolute/tansr-cpp-0.1.0;/absolute/dependencies"
cmake --build out/consumer
ctest --test-dir out/consumer --output-on-failure
```

应用使用 `find_package(TansrSDK 0.1.0 EXACT CONFIG REQUIRED)` 和 `tansr::sdk`。SDK 与 Demo 安装到独立前缀，不能覆盖已有 Rust/Go 同名程序。

## 固定源码与配方入口

正式入口为同版 [GitHub Release](https://github.com/tansrai/tansr-cpp/releases) 的源码归档、校验清单及独立配方包；源码归档有单个顶层目录。配方只允许该仓 `releases/download/v0.1.0/` 下的固定制品，并由密码摘要校验内容，不读取开发工作树、不下载 main 或移动分支。

- Conan：`conan/conandata.yml` 的 `sources["0.1.0"]` 记录真实 URL、SHA256。
- vcpkg：`vcpkg/tansr-sdk/source.json` 记录版本、真实 URL、SHA512。

源码包冻结后，再将真实 URL/摘要写入独立配方发行包。这样不要求源码归档内的文件包含其自身摘要。当前空值是明确的准备状态，配方会在下载前失败；不能填零摘要、猜测摘要或去掉校验。使用完整同版配方包，不能将未完成的入口当成已经可用的正式包。

## vcpkg overlay

从同版配方包指定 overlay 和 triplet。先准备 vcpkg、`vcpkg-cmake`、`vcpkg-cmake-config` 辅助包；它们的获取遵循 vcpkg 配置。设置 `TANSR_CPP_DEPENDENCY_PREFIX` 为 Release 前缀，`TANSR_CPP_DEPENDENCY_PREFIX_DEBUG` 为 Debug 前缀。双配置缺 Debug 依赖会失败；只有显式 release-only triplet 可省略 Debug 前缀。

```sh
export TANSR_CPP_DEPENDENCY_PREFIX=/absolute/dependencies-release
export TANSR_CPP_DEPENDENCY_PREFIX_DEBUG=/absolute/dependencies-debug
vcpkg install tansr-sdk --overlay-ports=/absolute/recipes/vcpkg --overlay-triplets=/absolute/recipes/vcpkg/triplets --triplet=x64-linux-tansr --x-install-root=/absolute/cpp-vcpkg --enforce-port-checks
```

Windows PowerShell 用 `$env:TANSR_CPP_DEPENDENCY_PREFIX = 'C:/absolute/dependencies-release'` 和对应 Debug 变量；triplet 为 `x64-windows-tansr-static-md`。Mac 使用 `arm64-osx-tansr`。自有 triplet 须在 `VCPKG_ENV_PASSTHROUGH` 中保留这两个非秘密变量；不再使用 `TANSR_CPP_SOURCE_DIR`。Debug/Release、static/shared 与 CRT 必须和依赖一致。

仅在所需归档和辅助包已缓存时使用 `--no-downloads`；首次源码包获取需要网络。开发验收可用 `--binarysource=clear` 强制重新构建。本 overlay 是自有分发配方，不代表社区索引收录。

## Conan 2

```sh
conan create /absolute/recipes/conan --version=0.1.0 -s build_type=Release -c user.tansr:dependency_prefix=/absolute/dependencies
```

Conan 从版本记录获取源码并验证 SHA256，再配置、构建和安装。`test_package` 只消费安装包的 `find_package(TansrSDK)` 并实际运行链接消费者；交叉编译无法运行时明确失败，不能跳过后称消费通过。此 recipe 不上传 ConanCenter，也不自动改宿主远端配置。正式版本的下载/构建/原生消费须单独保留实证，不能用旧本地源码消费结果替代。

## 制品标签与许可

每个二进制记录 OS/arch/compiler/STL/CRT、Debug/Release、static/shared 和依赖 backend。C++17/20 源码消费不代表跨编译器或版本 ABI 稳定。安装头不暴露 curl/OpenSSL/JSON 私有类型；静态 SDK 包携带其链接所需的开发依赖，仍须满足声明的 OS/CRT 运行条件。自行构建的动态库须同时部署必要运行时库。

配方携带 SDK 的 MIT LICENSE、[RIGHTS](RIGHTS.txt)、[NOTICE](NOTICE.md) 和实际运行依赖的原许可。不得收入内部参考、私有 Git 历史、工具链、私钥、凭据、archive 或 out。社区 vcpkg/ConanCenter 审核与自有正式制品分列，投稿不等于已收录。

## English summary

The SDK and demos use MIT. Version 0.1.0 release artifacts are being prepared; no public release or community registry listing is claimed. The binary SDK bundles matching development dependencies under `dependencies/`; preserve that layout and pass only the SDK root to `CMAKE_PREFIX_PATH`. See the [guide](../doc/guide.md) for a standalone application. Opting out with `TansrSDK_NO_BUNDLED_DEPENDENCIES=ON` requires a matching host dependency prefix. Each demo is a separate artifact with its executable under `bin/`.

Build CMake sources with matched external dependencies from `dependencies.json`. Use the commands above with the same compiler, architecture, configuration and CRT; Windows defaults to `/MD` or `/MDd`. Binary artifacts still require their declared OS/CRT runtimes.

The completed recipe bundle for a GitHub Release pins its source archive URL and SHA256 (Conan) or SHA512 (vcpkg). Source archives have one top-level directory. Empty records intentionally fail before downloading. Release maintainers freeze the source archive first, then fill its actual checksums into a separate recipe bundle; consumers do not provide a development source directory or bypass validation. `TANSR_CPP_DEPENDENCY_PREFIX` and its `_DEBUG` counterpart identify prepared dependency prefixes only. Conan uses `user.tansr:dependency_prefix` for the matching build configuration.

These are independently distributed recipes, not vcpkg/ConanCenter entries. Native package consumption, checksums and ABI labels must be verified for each released platform. Retain the SDK MIT license and third-party notices, and install the demos to a separate prefix from other language SDKs.
