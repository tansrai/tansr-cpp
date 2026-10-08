# 合同资产的显式消费模式

2026-10-08，用户授权 C++ SDK 源码与 Demo 按 MIT 开源到 `tansrai` 并准备产物。`EXPORT-POLICY.json` 记录本库授权及逐项批准的 20 个 JSON，不继承 Rust 许可。导出不表示已推送或正式发行。

冻结 `LOCK.json` 及其 39 份原资产不因发行改变。内部树的 `DISTRIBUTION.json` 为 `internal`；公开树显式声明 `public`，只携带批准的 20 项、原来源锁和五份消费元数据。原锁保留其余私有参考的路径及摘要用于追溯；公开包不含这些参考正文，也不要求取得私有仓才能构建或测试。

- 内部检查：`python tools/contract_check.py --mode internal`，严格校验全部 39 份。省略模式仍固定 internal，缺文件不会降级。可用 `--source <本地CLI检出>` 只读比较冻结 Git 对象。
- 公开检查：`python tools/contract_check.py --mode public`，验证完整允许集合、公开声明和已批准策略。多文件、少文件、同数量换名、私有目录、软链接或 reparse point 均不能通过。
- 再生成检查：`python tools/generate_api.py --mode public --check`（内部树用 internal）。两种模式生成的 81 操作及 10 个嵌入 schema 完全相同；Python 只供研发，普通 CMake 消费者无需运行生成器。
- 合同导出：`python tools/contract_export.py --mode public --source-mode internal --output <源树外全新目录>`。源已经是公开树时必须显式用 `--source-mode public`。只创建新目录，不覆盖、不复制 reference、不提交、不上传、不发布。
- 完整源码导出：`python tools/export_source.py --contract-mode internal --output <源树外全新目录>`，按固定文件白名单导出并写逐文件摘要。排除 .git、内部台账和真实 Serve 打包夹具；审核后由维护者建立新的公开 Git 根历史。

`python tools/contract_check_test.py --mode public` 在公开树运行同一合同工具的正反例；内部树显式用 internal。测试在 OS 临时目录模拟 pending/approved，不改变版本化授权。不能用调用者自造名单绕过范围；未来更改公开范围须走代码评审。
