"""三个真实 Demo 进程消费；沿原封存 Serve fixture，不用 Python HTTP 重写产品调用。"""
import argparse
from contextlib import contextmanager, ExitStack
import json
import os
from pathlib import Path
import queue
import re
import time

from run import Child, owned_temp, sha256


@contextmanager
def demo_environment(root):
    values = {"TANSR_TOKEN_FILE": str(root / "credentials/token.txt"),
              "TANSR_SCOPE_FILE": str(root / "credentials/scope.json"),
              "TANSR_ARCHIVE_KEY_FILE": str(root / "keys/archive.key")}
    original = {key: os.environ.get(key) for key in values}
    os.environ.update(values)
    try:
        yield
    finally:
        for key, value in original.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


class Scenario:
    def __init__(self, args, result, stack, root, label):
        self.args, self.result, self.stack, self.root, self.label = args, result, stack, root, label
        self.children = []
        self.fixture = None
        self.index = 0
        self.deadline = time.monotonic() + args.timeout

    def start(self, command, name):
        self.index += 1
        log = self.stack.enter_context((self.args.logs / f"{self.label}-{self.index:02d}-{name}.log").open("w", encoding="utf-8"))
        with demo_environment(self.root):
            child = Child([str(item) for item in command], self.root, log)
        self.children.append(child)
        self.result.setdefault("processes", []).append({"step": self.index, "name": name,
            "pid": child.process.pid, "arguments": [str(item) for item in command]})
        return child

    def line(self, child):
        while time.monotonic() < self.deadline:
            if child.overflow:
                raise RuntimeError("Demo output queue overflow")
            try:
                return child.lines.get(timeout=0.05)
            except queue.Empty:
                if child.process.poll() is not None and not child.reader.is_alive():
                    raise RuntimeError("Demo exited before expected output; inspect log")
        raise TimeoutError("Demo scenario deadline")

    def wait(self, child, predicate):
        while True:
            line = self.line(child)
            if predicate(line):
                return line

    def finish(self, child, expected=0):
        while child.process.poll() is None and time.monotonic() < self.deadline:
            if child.overflow:
                raise RuntimeError("Demo output queue overflow")
            # 保持读队列有界；已需要的输出在 finish 前核对。
            try:
                child.lines.get(timeout=0.05)
            except queue.Empty:
                pass
        if child.process.poll() is None:
            raise TimeoutError("Demo did not terminate")
        code = child.finish()
        self.children.remove(child)
        if (expected == "nonzero" and code == 0) or (expected != "nonzero" and code != expected):
            raise RuntimeError(f"unexpected Demo exit code {code}, expected {expected}")
        return code

    def run(self, command, name, expected=0):
        child = self.start(command, name)
        lines = []
        while child.process.poll() is None or child.reader.is_alive() or not child.lines.empty():
            if time.monotonic() >= self.deadline or child.overflow:
                raise TimeoutError("Demo output/deadline bound exceeded")
            try:
                lines.append(child.lines.get(timeout=0.05))
            except queue.Empty:
                pass
        self.finish(child, expected)
        return lines

    def seed(self, info, family, command, *values):
        lines = self.run([self.args.build / executable("serve_demo_seed"), info, family, self.root,
                          command, *values], "seed-" + command)
        found = [json.loads(line.removeprefix("TANSR_CPP_DEMO_SEED ")) for line in lines
                 if line.startswith("TANSR_CPP_DEMO_SEED ")]
        if len(found) != 1 or found[0].get("status") != "passed":
            raise RuntimeError("native seed receipt missing")
        return found[0]

    def control(self, command):
        self.fixture.send(json.dumps(command))
        response = self.fixture.prefixed("TANSR_RUST_CONTROL ", 10)
        if response.get("requestId") != command["requestId"] or response.get("ok") is not True:
            raise RuntimeError("actual fixture rejected material control")
        return response

    def cleanup(self):
        for child in reversed(self.children):
            try:
                if child.process.poll() is None:
                    child.process.kill()
                child.finish()
            except Exception as error:
                self.result["cleanupError"] = str(error)
                self.result["status"] = "failed"
        self.children.clear()


def executable(name):
    return name + (".exe" if os.name == "nt" else "")


def one_shot(scenario, common, name, *flags):
    lines = scenario.run([scenario.args.demos / executable("tansr-" + name), *common, *flags], name)
    return lines


def chat_turn(scenario, common, session=None, resume=False, request="demo-create-001"):
    family = common[common.index("--family") + 1]
    flags = (["--resume" if resume else "--attach", session] if session else
             (["--request-id", request] if family == "sdk2-offload-v1" else []))
    lines = one_shot(scenario, common, "chat", *flags, "--message", "CPP-DEMO synthetic turn")
    sessions = [line[9:] for line in lines if line.startswith("session: ")]
    if len(sessions) != 1 or "[turn completed]" not in lines:
        raise RuntimeError("chat did not confirm the actual current turn")
    if session and sessions[0] != session:
        raise RuntimeError("chat changed original session identity")
    if not any("go-real-serve-answer" in line or "go-archive-" in line or "rust-archive-" in line
               or "rust manual archive" in line for line in lines):
        raise RuntimeError("chat omitted actual fixture model answer")
    return sessions[0]


def session_case(scenario, info_path, info, family, common):
    session = chat_turn(scenario, common)
    chat_turn(scenario, common, session, resume=True)
    history = scenario.seed(info_path, family, "history", session)["history"]
    if history["total"] < 4:
        raise RuntimeError("two Demo invocations did not retain original history")
    scenario.result["checks"] = ["real-demo-two-turns", "live-resume-original-id", "original-history"]


def tools_case(scenario, info_path, info, family, common):
    tools = scenario.start([scenario.args.demos / executable("tansr-tools"), *common,
                            "--executor", info["executorId"], "--journal", scenario.root / "journal",
                            "--run-once", "--require-output"], "tools")
    session = scenario.wait(tools, lambda line: line.startswith("session: "))[9:]
    scenario.wait(tools, lambda line: line.startswith("ready: "))
    chat = scenario.start([scenario.args.demos / executable("tansr-chat"), *common, "--attach", session], "chat")
    scenario.wait(chat, lambda line: line == "session: " + session)
    chat.send("GO-TOOL query DEMO-001")
    approved = set()
    saw_answer = False
    while True:
        line = scenario.line(chat)
        match = re.match(r"\[permission ([^\] ]+)\] (.*)", line)
        if match:
            # 只批准本场景真实收到的指定合成只读业务票据；不是通用自动批准器。
            if "DemoOrderStatus" not in match[2] or match[1] in approved:
                raise RuntimeError("unexpected or repeated permission ticket")
            approved.add(match[1])
            chat.send("/allow " + match[1])
        if "go-tool-complete" in line:
            saw_answer = True
        if line == "[turn completed]":
            if not saw_answer:
                raise RuntimeError("model did not consume actual client tool result")
            break
    chat.send("/quit")
    scenario.finish(chat)
    output = scenario.wait(tools, lambda line: line.startswith("business receipt state: "))
    if "output confirmed=true" not in output:
        raise RuntimeError("actual stdout/stderr seal not confirmed")
    scenario.finish(tools)
    scenario.result["approvedObservedTickets"] = len(approved)
    scenario.result["checks"] = ["two-real-demo-processes-shared-private-credentials",
        "explicit-current-tool-binding", "actual-business-result-consumed", "stdout-stderr-seal-confirmed"]


def archive_case(scenario, info_path, info, family, common, manual):
    checks = []
    if manual:
        session = info["sessionId"]
        intent = scenario.root / "intents/create.json"
        prepared = one_shot(scenario, common, "archive", "--mode", "prepare-create", "--session", session,
                            "--source", info["sourceId"], "--request-id", "demo-original-create", "--intent", intent)
        if not any("no binding was created" in line for line in prepared):
            raise RuntimeError("prepare did not identify its nonmutating boundary")
        created = one_shot(scenario, common, "archive", "--mode", "create", "--intent", intent)
        ids = [line[9:] for line in created if line.startswith("binding: ")]
        if len(ids) != 1:
            raise RuntimeError("real binding creation missing")
        binding = ids[0]
        one_shot(scenario, common, "archive", "--mode", "creation-status", "--intent", intent)
        checks.extend(["original-private-intent-before-create", "actual-create-and-original-status"])
    else:
        session = chat_turn(scenario, common)
        binding = scenario.seed(info_path, family, "target", session)["target"]["bindingId"]
    store = scenario.root / "archive/history.bin"
    args = ["--binding", binding, "--file", store, "--key-id", "cpp-demo-test-key"]
    synced = one_shot(scenario, common, "archive", "--mode", "sync", *args)
    if not any("archive synchronized" in line for line in synced) or not store.is_file():
        raise RuntimeError("real encrypted archive sync not confirmed")
    one_shot(scenario, common, "archive", "--mode", "status", "--binding", binding)
    checks.extend(["real-demo-encrypted-durable-sync", "live-binding-status"])
    if not manual:
        checkpoint = scenario.seed(info_path, family, "checkpoint", session)["checkpointId"] if family == "sdk1" else None
        chat_turn(scenario, common, session)
        # 两族均覆盖真实 2xx 受理后失回，再由全新 Demo 进程读取原 pending ACK。
        lose_accepted = True
        staged = scenario.seed(info_path, family,
            "stage-accepted-pending" if lose_accepted else "stage-pending", session)["staged"]
        if staged["records"] < 1 or staged["ackSent"] != lose_accepted or staged["acceptedResponseDiscarded"] != lose_accepted:
            raise RuntimeError("pending seed did not retain the expected actual ACK boundary")
        recovered = one_shot(scenario, common, "archive", "--mode", "recover", *args, "--request-id", "demo-recovery-original")
        if not any("pending ACK confirmed" in line for line in recovered):
            raise RuntimeError("cold Demo recovery not confirmed")
        one_shot(scenario, common, "archive", "--mode", "sync", *args)
        checks.append("cold-demo-recover-original-pending-ack")
        scenario.result["coldRecoveryEvidence"] = {"status": "passed", "original": staged,
            "recoveryConfirmed": True, "storeSha256": sha256(store),
            "scope": "intermediate result only; does not close the combined scenario"}
        if lose_accepted:
            checks.append("actual-accepted-ack-response-lost-then-new-demo-process-recovery")
        intent = scenario.root / "materials/response.json"
        observer = scenario.start([scenario.args.demos / executable("tansr-archive"), *common,
            "--mode", "materials", *args, "--request-id", "demo-material-response-original",
            "--intent", intent], "materials")
        scenario.wait(observer, lambda line: line.startswith("waiting for one current material request"))
        subject = {"endUserId": "go-user", "sessionId": session}
        scenario.control({"requestId": "demo-request-material", "command": "request-materials",
            "subject": subject, "request": {"materialRequestId": "demo-material-original",
            "recordIds": staged["recordIds"], "purpose": "context-recall"}})
        scenario.wait(observer, lambda line: line.startswith("material state: received"))
        scenario.finish(observer)
        if not intent.is_file() or not Path(str(intent) + ".request").is_file():
            raise RuntimeError("material request and response were not durably retained")
        original_intent = sha256(intent)
        before_consumption = scenario.run([scenario.args.demos / executable("tansr-archive"), *common,
            "--mode", "material-status", "--intent", intent], "material-received-status", "nonzero")
        if not any("material state: received" in line for line in before_consumption):
            raise RuntimeError("material ingress was incorrectly reported as core consumption")
        one_shot(scenario, common, "archive", "--mode", "material-submit", "--intent", intent)
        if sha256(intent) != original_intent:
            raise RuntimeError("material replay replaced original intent or deadline")
        scenario.control({"requestId": "demo-enqueue-material", "command": "enqueue-materials",
            "subject": subject, "request": {"materialRequestId": "demo-material-original",
            "leaseId": "demo-material-consumer"}})
        chat_turn(scenario, common, session)
        consumed = one_shot(scenario, common, "archive", "--mode", "material-status", "--intent", intent)
        if not any("material state: core-consumed" in line for line in consumed):
            raise RuntimeError("actual core material consumption not confirmed")
        one_shot(scenario, common, "archive", "--mode", "sync", *args)
        checks.append("real-demo-material-received-original-replay-core-consumed")
        if checkpoint:
            receipt = scenario.seed(info_path, family, "restore", session, checkpoint)["receipt"]
            if receipt["status"] != "restored" or receipt["fromMessages"] <= receipt["toMessages"]:
                raise RuntimeError("real SDK restore did not complete")
            history = scenario.seed(info_path, family, "history", session)["history"]
            if history["total"] != receipt["toMessages"]:
                raise RuntimeError("restored session history does not match original checkpoint")
            # 会话快照恢复与删除档案不是一回事；真实档案仍保留原已确认事实。
            before = sha256(store)
            one_shot(scenario, common, "archive", "--mode", "sync", *args)
            if sha256(store) != before:
                raise RuntimeError("snapshot restore silently replaced retained archive facts")
            checks.append("actual-checkpoint-restore-preserves-independent-archive-facts")
    scenario.result["checks"] = checks


def run_case(args, mode, family):
    label = "demos-" + mode + "-" + family
    result = {"mode": mode, "family": family, "status": "running", "checks": []}
    started = time.monotonic()
    fixture = scenario = None
    with owned_temp(result) as directory, ExitStack() as stack:
        root = Path(directory)
        try:
            log = stack.enter_context((args.logs / (label + "-serve.log")).open("w", encoding="utf-8"))
            fixture = Child([str(args.node), str(args.fixture), ".", str(root), mode], root, log)
            info = fixture.prefixed("TANSR_GO_FIXTURE ", 30)
            if info.get("manifestRevision") != 7:
                raise RuntimeError("fixture revision changed")
            info_path = root / "fixture-info.json"
            info_path.write_text(json.dumps(info), encoding="utf-8")
            client_root = root / "client"
            client_root.mkdir(mode=0o700)
            scenario = Scenario(args, result, stack, client_root, label)
            scenario.fixture = fixture
            scenario.seed(info_path, family, "credentials")
            common = ["--base", info["baseURL"], "--family", family, "--timeout", str(args.timeout)]
            if mode == "execution-demo":
                tools_case(scenario, info_path, info, family, common)
            elif mode == "session" or mode == "archive-offload" and args.suite == "chat":
                session_case(scenario, info_path, info, family, common)
            else:
                archive_case(scenario, info_path, info, family, common, mode == "archive-manual")
            result["serveExitCode"] = fixture.finish(graceful=True)
            fixture = None
            if result["serveExitCode"] != 0:
                raise RuntimeError("real Serve cleanup failed")
            result["status"] = "passed"
        except Exception as error:
            result["status"], result["error"] = "failed", str(error)
        finally:
            if scenario:
                scenario.cleanup()
            if fixture:
                try:
                    fixture.finish(graceful=True)
                except Exception as error:
                    result["cleanupError"], result["status"] = str(error), "failed"
            result["elapsedSeconds"] = round(time.monotonic() - started, 3)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("fixture", "provenance", "node", "build", "logs"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--demos", type=Path, help="default BUILD/demo; also supports an installed bin directory")
    parser.add_argument("--suite", choices=("all", "chat", "tools", "archive"), default="all")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    for name in ("fixture", "provenance", "node", "build", "logs"):
        setattr(args, name, getattr(args, name).resolve())
    args.demos = (args.demos or args.build / "demo").resolve()
    provenance = json.loads(args.provenance.read_text(encoding="utf-8"))
    if sha256(args.fixture) != provenance["output"]["sha256"]:
        raise RuntimeError("fixture differs from sealed source provenance")
    binaries = [args.build / executable("serve_demo_seed")] + [args.demos / executable("tansr-" + name) for name in ("chat", "tools", "archive")]
    for binary in binaries:
        if not binary.is_file():
            raise FileNotFoundError(binary)
    args.logs.mkdir(parents=True, exist_ok=False)
    matrix = []
    if args.suite in ("all", "chat"):
        matrix.append(("session", "sdk1"))
        if args.suite == "chat":
            matrix.append(("archive-offload", "sdk2-offload-v1"))
    if args.suite in ("all", "tools"):
        matrix.append(("execution-demo", "sdk1"))
    if args.suite in ("all", "archive"):
        matrix.extend([("archive-manual", "sdk1"), ("archive", "sdk1"), ("archive-offload", "sdk2-offload-v1")])
    results = [run_case(args, *case) for case in matrix]
    receipt = {"fixtureSha256": sha256(args.fixture), "provenance": provenance,
               "binaries": [{"path": str(path), "sha256": sha256(path)} for path in binaries],
               "results": results, "passed": sum(item["status"] == "passed" for item in results),
               "total": len(results), "skipped": 0}
    (args.logs / "receipt.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if results and receipt["passed"] == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
