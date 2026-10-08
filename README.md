# Tansr C++ SDK

[English](README.en.md) · [使用指南](doc/使用指南.md) · [源码](https://github.com/tansrai/tansr-cpp) · [v0.1.0 下载](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0)

SDK、三个 CLI Demo 和公开文档使用 [MIT 许可](LICENSE)。**[v0.1.0 正式发行](https://github.com/tansrai/tansr-cpp/releases/tag/v0.1.0)** 提供源码包、Windows x64 / Linux x64 / macOS arm64 的 SDK 与独立 Demo 包、校验清单及固定摘要配方。Conan/vcpkg 配方为自有分发，尚未进入 ConanCenter/vcpkg 公共索引。

原生 C++17 库，通过统一 `/api` 连接本地或远端 Serve。Serve 保有智能体循环、会话、上下文、记忆准入、权限裁决、工具调度与用量；C++ 提供协议客户端、会话流、显式业务工具执行及加密本地档案。无需 Node/Rust/Go 客户端代理，Electron 集成模式保持不变。

冻结 UAPI revision 7：11 族、81 操作；源码与指纹在 `contract/`。默认 `sdk1`，`sdk2-offload-v1` 必须显式选择并通过能力协商；错误不会触发改族、换用户或自动重放副作用。HTTP + SSE，不使用 WebSocket。

## 选择发行包并校验

集成自己的 C++ 应用下载对应的 **SDK** 包；运行示例下载该平台的 **chat / tools / archive** 包。三个 Demo 都是独立可执行程序，分别使用各自解包目录中的 `bin/`。它们需要可访问的 Serve 和有效的短期凭据；`--help` 可先查看参数。

| 平台 | SDK | 独立 Demo |
|---|---|---|
| Windows x64 / MSVC 19.44 | [SDK (zip)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-sdk.zip) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-chat.zip) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-tools.zip) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-windows-x64-msvc-19.44.35221-release-static-tansr-archive.zip) |
| Linux x64 / GCC 13.3 | [SDK (tar.gz)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-sdk.tar.gz) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-chat.tar.gz) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-tools.tar.gz) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-linux-x64-gcc-13.3.0-release-static-tansr-archive.tar.gz) |
| macOS arm64 / AppleClang 21 | [SDK (tar.gz)](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-sdk.tar.gz) | [chat](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-chat.tar.gz) · [tools](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-tools.tar.gz) · [archive](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/tansr-cpp-0.1.0-macos-arm64-appleclang21.0.0-release-static-tansr-archive.tar.gz) |

SDK 包提供 Release 静态库，Demo 包各提供独立可执行程序。Windows 使用 x64、Release `/MD` 与匹配的 MSVC 运行库；Linux 制品在 Ubuntu 24.04 验证，所需符号下界为 glibc 2.38 / GLIBCXX 3.4.32；Mac 包要求 arm64、macOS 26.0+。其他编译器、架构、Debug 或共享库配置按[指南](doc/使用指南.md)从源码构建，保持依赖与配置匹配。

同版下载 [SHA256SUMS](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/SHA256SUMS) 和 [制品清单](https://github.com/tansrai/tansr-cpp/releases/download/v0.1.0/public-asset-manifest.json)，在下载目录计算所选文件的 SHA256，与校验表中**该完整文件名**的一行核对；一致后再解包。以下各行分别用于对应系统：

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

解包后，SDK 的根目录应直接包含 `include/`、`lib/`、`dependencies/` 和 `share/`；将这个根目录传给后面的 CMake 命令。Demo 的凭据文件、服务地址、会话 ID 和持久目录按[使用指南](doc/使用指南.md)设置。认证由开发者可信后端及 Serve 提供，发行包不包含可用于真实服务的测试账号或模型密钥。

v0.1.0 的 tag 和下载附件保持冻结；本仓 `main` 持续补充同版本的使用文档。[版本化配方](packaging/README.md)使用独立 recipes 附件中的固定源码摘要；SDK 源码归档内的空配方元数据仍保留其原检查行为。

## 构建自己的应用

```cmake
find_package(TansrSDK 0.1.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE tansr::sdk)
```

二进制 SDK 包保留 `include/`、`lib/`、`dependencies/` 和 `share/TansrSDK/` 的完整结构；应用只需把解包后的 SDK 根目录传给 `CMAKE_PREFIX_PATH`。完整独立应用与命令见[使用指南](doc/使用指南.md)。三个 Demo 各有独立制品包，从各自的 `bin/` 调用；Windows 程序加 `.exe`。

以下命令用于从源码构建，依赖须事先准备：

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/absolute/prepared-dependencies
cmake --build --preset release
ctest --preset release
cmake --install out/release --prefix /absolute/tansr-cpp-0.1.0
```

构建需要 C++17 工具链、CMake 3.25+ 和 Ninja（预设使用 Ninja）。依赖固定为 curl 8.22.0、c-ares 1.34.8、OpenSSL 3.5.9；源码 CMake 不自动下载。Windows 默认 `/MD`/`/MDd`，所有依赖须同配置。见[依赖与版本配方](packaging/README.md)。v0.1.0 二进制平台为 Windows x64 / MSVC 19.44、Linux x64 / Ubuntu 24.04 / GCC 13 和 macOS arm64 / 最低部署目标 26.0；精确条件见指南及对应制品清单。

三个 Demo 只链接公开 SDK：

- `tansr-chat`：私有持久化的 offload 创建意图与原请求恢复、多轮、真实审批/问答、同轮插入、显式中断与恢复观察。
- `tansr-tools`：合成订单业务工具、耐久执行日志、stdout/stderr 分块与独立 seal。
- `tansr-archive`：创建意图、Source binding、加密落盘后 ACK、状态、材料及显式恢复。

认证使用每请求重读的私有短期 token 和当前 scope 文件；从 `--help` 开始。Serve 认证和应用策略仍是授权事实源；不能把 Demo scope 文件当作生产身份系统。完整流程见[使用指南](doc/使用指南.md)。

Serve 须提供 UAPI revision 7 及所选 family 的实际能力；offload、终端工具输出和档案恢复还要求对应操作被 Serve 的能力声明与当前授权启用。SDK 的操作目录不替服务端开启能力，也不指定一个可绕过协商的最低 Serve 版本。

公开合同为原冻结资产的明确 **20 文件**子集，按 `contract/DISTRIBUTION.json` 与导出策略核对；源码开发检查使用 `python tools/contract_check.py --mode public`。内部 39 文件的私有参考、Serve/kernel 源码及历史不在本次许可与分发范围内。第三方许可证见 [NOTICE](packaging/NOTICE.md)。首版不承诺任意 Shell/PTY、GUI、移动端、完整本地记忆编排或跨 SDK 私有存储互读。
