# C++ 原生产物准备

源码许可、源码发布、二进制准备、正式发行分别记账。此入口只将**已构建、已安装、已验收**的产物整理为本地候选；不联网、不构建、不签名、不推送、不创建 tag 或 GitHub Release。源码白名单导出使用 `tools/export_source.py`，不从二进制安装树逆向拼接源码包。

## 产物与输入

每个平台生成四个独立包：一个 SDK，以及 `tansr-chat`、`tansr-tools`、`tansr-archive` 各一个 Demo。首批二进制范围为 Windows x64、Linux x64、macOS arm64 的 **Release / static SDK**；Windows 使用 `/MD`。其他架构、Debug、shared 必须另有构建及消费证据，不能改元数据借用现有产物。

SDK 包含公共头、静态库、CMake targets/config、同锁 curl/c-ares/OpenSSL 的头/静态库/CMake 配置、中英指南及许可证。第三方库位于 `dependencies/`；仅把 SDK 包的解压根加入 `CMAKE_PREFIX_PATH`，包配置会优先找到随包依赖。Demo 包各含一个原生可执行文件和指南/许可，不要求外置 Node、Python、Go 或 Rust runtime。Windows `/MD` 仍需匹配的系统 UCRT/MSVC runtime；Linux 的 glibc/动态加载器与 macOS 的系统库也必须按实际构建要求声明，不能把静态第三方依赖等同于全静态系统运行时。

输入是同一次冻结构建的安装前缀和第三方依赖前缀。安装布局由根 CMake 的 `SDK`、`Demos` 组件定义：

```sh
cmake --install out/release --prefix out/release-install --component SDK
cmake --install out/release --prefix out/release-install --component Demos
python tools/package_release_test.py
python tools/package_release.py --install-prefix out/release-install --dependency-prefix out/dependencies --metadata out/release-metadata.json --output out/release-packages
```

`--output` 必须不存在且在输入前缀之外。`--forbid-path` 可重复，用于补充该机器的私有构建前缀；该值只进入校验过程，不进入发行包。具体构建目录可以不同，不需要写入包内元数据。

## 元数据

`--metadata` 是维护者已核对的构建事实，工具不会靠网络证明 GitHub 的提交，也不会把填写字段当作签名验证。以下值都是示意；实际准备必须换成真实公开提交、tree、源码快照 SHA256 和实际 ABI：

```json
{
  "version": "0.1.0",
  "source": {
    "repository": "https://github.com/tansrai/tansr-cpp",
    "commit": "1111111111111111111111111111111111111111",
    "tree": "2222222222222222222222222222222222222222",
    "dirty": false,
    "snapshotSha256": "3333333333333333333333333333333333333333333333333333333333333333"
  },
  "abi": {
    "os": "windows",
    "arch": "x64",
    "compiler": "msvc-19.44",
    "standardLibrary": "MSVC-STL-14.44",
    "runtime": "MD",
    "configuration": "Release",
    "linkage": "static",
    "minimumOs": "Windows-10"
  },
  "signing": { "status": "unsigned", "notarized": false },
  "runtimeDependencies": ["Windows-UCRT", "MSVC-v14-x64-runtime"]
}
```

`source.snapshotSha256` 指独立源码导出流程封存的公开源码包，不是私有工作树 tar；必须在同批来源回执中关联二进制所用源码。工具拒绝 dirty 源标记。`signing.status` 仅接受 `unsigned`，或 macOS 的 `ad-hoc`；本准备入口不支持声称 Apple 公证或正式发行签名。C++ ABI 受编译器/STL/CRT/配置影响，PImpl 不构成跨工具链 ABI 保证。Linux 最低 glibc 与 macOS deployment target 应来自真实工具链和依赖检查，不能按测试机版本猜测。

## 清单、路径及依赖

每个包有 `manifest.json`、`SHA256SUMS`、`USAGE.txt`。清单覆盖全部载荷文件的相对路径、字节数、SHA256、可执行位，以及来源、ABI、依赖源码版本/hash、依赖锁原始字节 hash。清单与校验表自身不递归计入载荷表；它们所在归档的 hash 另见输出根 `SHA256SUMS`。工具写完后重读归档并比较全部载荷，才记 `prepared`。

工具仅收集明确的安装目录和文件，拒绝符号链接/reparse point、路径穿越、大小异常、大小写重名、未知目标和不匹配的锁；不会打入测试、私有 fixture、凭据、内部合同、PDB 或开发仓历史。文本和二进制都检查内部绝对路径与私钥标记。明确的 `C:/absolute/` 示例占位只对两份指南及 `packaging/README.md` 放行，不对二进制放行；这些文档中的其他私有路径仍会拒绝。OpenSSL 标准系统运行目录 `C:/Program Files/OpenSSL` 和 `C:/Program Files/Common Files/SSL` 可保留，它们不是构建机私有路径。

curl 的 CMake 兼容链接列表可能含原依赖前缀。工具只把**完全匹配该输入前缀**的文本替换成该 CMake 目录的相对路径，记录变换前后 hash；任何其他路径残留仍失败。二进制内的 `__FILE__`、CodeView、调试或运行期路径不做盲目字节替换，应从发行构建的路径映射、调试配置及依赖安装配置修正后重新准备。

明确指定的私有前缀按原字节及 UTF-16 在任意偏移拒绝，不依赖字符串边界。通用路径检查只识别有字符串边界、可打印内容和合理组件的路径，不把数表中的偶然盘符字节当成私有路径证据。每个平台应通过 `--forbid-path` 补充实际源码、构建及旧依赖前缀，并对该平台完整载荷执行扫描；工装不声称代替通用秘密检测。

输出根的 `preparation-receipt.private.json` 是本地证据，含原始输入绝对路径和文件来源映射，**不能作为发行附件**。失败会保留 `status: failed` 回执及已写中间包；它们不是可发布成品，不在原目录重试覆盖。对外附件仅限本次成功回执列出的四个归档、对应公开 manifest 和根 `SHA256SUMS`。

## 消费检查与发行边界

在新的目录解包 SDK，只设置该包的 `CMAKE_PREFIX_PATH`，独立构建 `tests/consumer` 并运行 CTest；核实编译链接没有借用旧依赖前缀。分别解包三个 Demo，运行各自 `--help`，检查操作系统动态依赖及真实签名状态。原生运行与真实 Serve 场景的已有回执应关联到本候选 hash；`--help` 不能代替协议验收。

工装测试仅使用合成 PE/ELF/Mach-O 头来验证白名单、归档和负例，不是跨平台二进制可用性证据。只有真实平台消费通过且来源、许可、运行依赖、签名限制明确后，才能将候选列为“产物已准备”。本轮准备不代表已发布；正式 tag、公开 release、包管理器 upload 另按授权执行。
