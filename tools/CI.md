# 可复用依赖与主线基础门

普通 CMake 不联网。`prepare_dependencies.py` 是显式准备命令，唯一运行依赖锁来自 `packaging/dependencies.json`，不引用 archive 中的脚本、二进制或私有 fixture。Python 3.10+、CMake 3.25+、Ninja、Perl 和本机 C/C++ 编译器由宿主提供；Windows 需 MSVC x64 开发环境及原生 Windows Perl，不能使用 MSYS Perl。各命令日志与版本、输入 SHA256、实际退出码写入新建的 `out` 子目录。

```sh
# 离线：cache 中已有三个固定源码包（允许已有文件名，以 SHA256 认身份）。
python tools/prepare_dependencies.py --cache /absolute/download-cache --output out/dependencies-r1 --jobs 2
# 显式允许从锁内 HTTPS 官方地址获取缺失源码：加 --download。
# 只验摘要/安全解包：加 --extract-only，回执为 extracted_only，不代表依赖已构建。
python tools/ci.py --prefix out/dependencies-r1/prefix --output out/ci-r1 --contract-mode public --jobs 2
```

源码缓存中不接收 `.partial`。解包前拒绝绝对路径、路径穿越、重复成员、设备和通过链接写入；只允许指向包内普通文件的链接，并展开为同字节普通文件，兼容 Windows 无创建符号链接权限的宿主。输出及已有祖先不能是 symlink/junction/reparse，拒绝覆盖旧输出；失败保留原日志，重试使用新目录。Linux/macOS curl 使用本次 OpenSSL、c-ares 和系统 CA 文件，可显式 `--ca-bundle` 指定企业信任文件；Windows curl 使用 Schannel。依赖为 PIC 静态库，Windows Release `/MD`、Debug `/MDd`；OpenSSL 明确 `no-asm`，不作性能承诺。

两个入口都支持 `--config Debug`；依赖与 SDK 必须使用相同配置/CRT。基础门还支持 `--shared`，安装消费在 C++20 宿主中执行，Windows 动态库从本次安装目录加载。未知配置失败，不偷换默认。CTest 零测试、失败或 skipped 均不通过。公开源码显式选择 `--contract-mode public`，核对已批准的20项分发资产；内部维护树使用默认 `internal`，核对完整39项。分发声明与选择的模式必须匹配，不因缺私有资产自动降级。

真实 Serve 是另一项明确输入的验收：

```sh
python tools/ci.py --prefix /absolute/dependency-prefix --output out/ci-serve-r1 --contract-mode public --fixture /private/serve-fixture.mjs --provenance /private/serve-fixture.provenance.json --node /absolute/node
```

三项参数必须同时提供；顺序运行 `integration/run.py`、`integration/sessions.py`、`integration/demos.py` 及 `demo_chat_controls.py`、`demo_delivery.py`、`demo_create_loss.py`，核对调用方提供的 provenance 后执行。会话补充入口要求 fixture 同时支持原场景，以及 `session-media-disabled`、`session-prompt-fallback`、`session-prompt-prepend`、`archive-offload-durable`。缺少平台提示词和持久 Serve 重启能力的 fixture 应明确失败，不能省略场景。fixture 实现不随 SDK 公开分发。

后面三个入口补真实 Demo 的插入/中断、输出首块/显式 rebase、两族原始创建意图失回冷恢复；SDK1 使用手动 Source 绑定创建，SDK2 使用会话创建与自动 Source/binding，不伪造未绑定窗口。Demo 新进程恢复与 `sessions.py` 的 Serve 持久重启分开记录。缺参数时回执固定 `realServe: not_run`，不构建集成驱动、不伪报真实服务通过；显式参数错误、任一场景失败都返回非零。不要将私有 fixture 放进公开仓或工作流附件。

手动工作流提供相同三个路径输入，只消费 runner 上事先由获授权方式准备的现有文件，不执行路径对应内容的下载。托管 runner 默认没有这些私有输入；不要为了点绿而填写占位路径。三平台矩阵的每台 runner 都必须具有其有效输入，否则该平台明确失败；需要不同平台路径时在对应的可信宿主直接运行上述同源入口。

`.github/workflows/ci.yml` 只在 push main 和手动运行 main 时触发，三平台 Release 静态基础矩阵，不监听 PR/开发分支，不发布、不申请 secrets。使用 runner 既有工具，完整版本留在准备/构建日志；runner 镜像变化不冒充固定工具链重现。工作流上传限定日志/回执，排除源码、合同参考、安装物和凭据。GitHub 实跑前，本文件与 YAML 只代表交付机制已实现，不代表主线 CI 已通过；Debug/shared、Sanitizer、三 Demo 真实行为、发行包等原 DoD 仍按各自证据结算。

动作来源：[checkout v4.2.2 固定提交](https://github.com/actions/checkout/commit/11bd71901bbe5b1630ceea73d27597364c9af683)、[upload-artifact v4.6.2 固定提交](https://github.com/actions/upload-artifact/commit/ea165f8d65b6e75b540449e92b4886f43607fa02)。宿主工具清单以 [GitHub runner-images](https://github.com/actions/runner-images) 与实际版本日志为准。
