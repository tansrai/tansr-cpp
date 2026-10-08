"""显式真实 Serve 测试入口。无宿主/缺驱动直接失败，不记 skip 为成功。

仅消费已核验的私有 Serve fixture；该依赖不进入 SDK 安装物或公开发行包。
Python 负责有界子进程/合成控制通道，所有产品请求由原生 C++ SDK 执行。
"""
import argparse
from contextlib import contextmanager
import hashlib
import http.client
import http.server
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time
from urllib.parse import urlsplit


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@contextmanager
def owned_temp(result):
    directory = Path(tempfile.mkdtemp(prefix="tansr-cpp-serve-")).resolve()
    parent = Path(tempfile.gettempdir()).resolve()
    if directory.parent != parent or not directory.name.startswith("tansr-cpp-serve-"):
        raise RuntimeError("temporary cleanup boundary invalid")
    try:
        yield str(directory)
    finally:
        # Windows 上已退出进程的 SQLite/扫描句柄可能短暂延迟释放；只重试本次专属根。
        for attempt in range(5):
            try:
                if directory.exists():
                    target = str(directory)
                    if os.name == "nt":
                        target = "\\\\?\\" + target
                    shutil.rmtree(target)
                result["temporaryCleanup"] = "removed"
                break
            except OSError as error:
                if attempt == 4:
                    result["temporaryCleanup"] = "failed"
                    result["retainedTemporaryDirectory"] = str(directory)
                    result["cleanupError"] = str(error)
                    result["status"] = "failed"
                else:
                    time.sleep(0.2)


class Child:
    def __init__(self, command, cwd, log):
        self.log = log
        self.lines = queue.Queue(maxsize=128)
        self.overflow = False
        self.process = subprocess.Popen(
            command, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=log, text=True, encoding="utf-8", errors="strict",
            bufsize=1, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.reader = threading.Thread(target=self.drain)
        self.reader.start()

    def drain(self):
        try:
            for line in self.process.stdout:
                self.log.write(line)
                self.log.flush()
                try:
                    self.lines.put_nowait(line.rstrip("\n"))
                except queue.Full:
                    self.overflow = True
        finally:
            self.process.stdout.close()

    def send(self, value):
        self.process.stdin.write(value + "\n")
        self.process.stdin.flush()

    def prefixed(self, prefix, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.overflow:
                raise RuntimeError("fixture output queue overflow")
            try:
                line = self.lines.get(timeout=min(0.1, max(0.001, end - time.monotonic())))
            except queue.Empty:
                if self.process.poll() is not None:
                    raise RuntimeError("process exited before readiness/control response")
                continue
            if line.startswith(prefix):
                return json.loads(line[len(prefix):])
        raise TimeoutError("fixture readiness/control deadline")

    def finish(self, graceful=False):
        if graceful and self.process.poll() is None:
            self.send("stop")
        try:
            code = self.process.wait(timeout=20 if graceful else 2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            raise TimeoutError("owned test process did not stop")
        finally:
            self.process.stdin.close()
            self.reader.join(timeout=5)
        if self.reader.is_alive():
            raise RuntimeError("owned test stdout reader did not stop")
        return code


class SessionCreateLossProxy:
    """指定新建入口故障注入：转发真实请求，收到真实 201 后丢弃一次响应。"""
    def __init__(self, origin, *, create_path="/api/sessions", identity_field="sessionId"):
        target = urlsplit(origin)
        if target.scheme != "http" or target.hostname != "127.0.0.1" or not target.port:
            raise RuntimeError("session proxy requires owned loopback Serve")
        if (create_path, identity_field) not in {
            ("/api/sessions", "sessionId"), ("/api/archive/bindings", "bindingId")
        }:
            raise RuntimeError("create-loss fixture route/identity pair is unsupported")
        self.records = []
        self.lock = threading.Lock()
        self.lost = 0
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_):
                pass

            def forward(self):
                self.connection.settimeout(10)
                if self.path.startswith("http") or not self.path.startswith("/api/"):
                    self.send_error(400)
                    return
                length = int(self.headers.get("content-length", "0"))
                if length < 0 or length > 8 * 1024 * 1024:
                    self.send_error(413)
                    return
                body = self.rfile.read(length)
                excluded = {"host", "connection", "content-length", "transfer-encoding",
                            "proxy-authorization", "proxy-connection", "keep-alive", "upgrade"}
                headers = {key: value for key, value in self.headers.items()
                           if key.lower() not in excluded}
                headers["connection"] = "close"
                upstream = http.client.HTTPConnection(target.hostname, target.port, timeout=15)
                record = {"method": self.command, "path": self.path,
                          "requestBytes": len(body), "requestSha256": hashlib.sha256(body).hexdigest(),
                          "requestKey": self.headers.get("idempotency-key"),
                          "deadline": self.headers.get("deadline")}
                try:
                    upstream.request(self.command, self.path, body=body, headers=headers)
                    response = upstream.getresponse()
                    response_body = response.read(8 * 1024 * 1024 + 1)
                    if len(response_body) > 8 * 1024 * 1024:
                        raise RuntimeError("session proxy response capacity")
                    record["upstreamStatus"] = response.status
                    record["responseBytes"] = len(response_body)
                    record["responseSha256"] = hashlib.sha256(response_body).hexdigest()
                    with owner.lock:
                        drop = self.command == "POST" and self.path == create_path and response.status == 201 and owner.lost == 0
                        if drop:
                            owner.lost += 1
                        record["responseDropped"] = drop
                        owner.records.append(record)
                    if drop:
                        # 只有宿主知道该 HTTP 结果；不将新标识交给丢失回执的客户端。
                        parsed = json.loads(response_body)
                        if not isinstance(parsed.get(identity_field), str) or not parsed[identity_field]:
                            raise RuntimeError("real create response lacked expected identity")
                        record["upstreamCreatedIdentityPresent"] = True
                        record["upstreamCreatedIdentity"] = parsed[identity_field]
                        self.close_connection = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        return
                    self.send_response(response.status)
                    for key, value in response.getheaders():
                        if key.lower() not in {"content-length", "transfer-encoding", "connection"}:
                            self.send_header(key, value)
                    self.send_header("content-length", str(len(response_body)))
                    self.send_header("connection", "close")
                    self.end_headers()
                    self.wfile.write(response_body)
                    self.wfile.flush()
                    self.close_connection = True
                except Exception as error:
                    record["proxyError"] = type(error).__name__
                    with owner.lock:
                        if record not in owner.records:
                            owner.records.append(record)
                    self.close_connection = True
                finally:
                    upstream.close()

            do_GET = forward
            do_POST = forward
            do_DELETE = forward

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = False
        self.base_url = f"http://127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05})
        self.thread.start()

    def finish(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            raise RuntimeError("session proxy listener did not stop")
        with self.lock:
            records = list(self.records)
        return {"requests": records, "responsesDropped": self.lost,
                "listenerStopped": True}

    @staticmethod
    def validate(receipt):
        records = receipt["requests"]
        creates = [r for r in records if r["method"] == "POST" and r["path"] == "/api/sessions"]
        reads = [r for r in records if r["method"] == "GET"]
        if receipt["responsesDropped"] != 1 or len(creates) != 1 or creates[0].get("upstreamStatus") != 201:
            raise RuntimeError("SDK1 create loss did not retain exactly one accepted original write")
        if len(reads) != 1 or not reads[0]["path"].startswith("/api/capabilities/sessions?") or len(records) != 2:
            raise RuntimeError("SDK1 lost create retried, guessed a session/list or invented create-status")
        if not creates[0].get("upstreamCreatedIdentityPresent") or any("proxyError" in r for r in records):
            raise RuntimeError("session proxy failed before proving real create acceptance")


def run_one(args, suite, mode, family, variant=""):
    suffix = ".exe" if os.name == "nt" else ""
    executable = args.build / ("serve_" + suite + suffix)
    if not executable.is_file():
        raise FileNotFoundError(f"required native driver missing: {executable}")
    label = f"{suite}-{mode}-{family}" + ("-" + variant if variant else "")
    started = time.monotonic()
    result = {"suite": suite, "mode": mode, "family": family, "variant": variant,
              "executable": str(executable), "sha256": sha256(executable)}
    fixture = test = proxy = None
    with owned_temp(result) as temp, \
            (args.logs / (label + "-serve.log")).open("w", encoding="utf-8") as serve_log, \
            (args.logs / (label + "-client.log")).open("w", encoding="utf-8") as client_log:
        physical = Path(temp).resolve()
        try:
            fixture = Child([str(args.node), str(args.fixture), ".", str(physical), mode], physical, serve_log)
            info = fixture.prefixed("TANSR_GO_FIXTURE ", 30)
            if info.get("manifestRevision") != 7:
                raise RuntimeError("fixture revision is not frozen r7")
            if suite == "session" and variant == "create-loss":
                proxy = SessionCreateLossProxy(info["baseURL"])
                info = dict(info, baseURL=proxy.base_url)
            info_path = physical / "fixture-info.json"
            info_path.write_text(json.dumps(info), encoding="utf-8")
            workspace = physical / "client"
            workspace.mkdir(mode=0o700)
            command = [str(executable), str(info_path), family, str(workspace)]
            if variant:
                command.append(variant)
            test = Child(command, physical, client_log)
            while time.monotonic() - started < args.timeout:
                if fixture.overflow or test.overflow:
                    raise RuntimeError("test output queue overflow")
                try:
                    line = test.lines.get(timeout=0.1)
                except queue.Empty:
                    if test.process.poll() is not None:
                        break
                    continue
                if line.startswith("TANSR_CPP_CONTROL "):
                    control = json.loads(line[len("TANSR_CPP_CONTROL "):])
                    fixture.send(json.dumps(control))
                    reply = fixture.prefixed("TANSR_RUST_CONTROL ", 10)
                    if reply.get("requestId") != control.get("requestId"):
                        raise RuntimeError("fixture control identity mismatch")
                    test.send(json.dumps(reply))
            else:
                raise TimeoutError("native real-Serve scenario exceeded deadline")
            result["exitCode"] = test.finish()
            test = None
            if proxy is not None:
                result["sessionCreateLoss"] = proxy.finish()
                proxy = None
                SessionCreateLossProxy.validate(result["sessionCreateLoss"])
            if result["exitCode"] != 0:
                raise RuntimeError("native scenario failed; inspect client log")
            result["serveExitCode"] = fixture.finish(graceful=True)
            fixture = None
            if result["serveExitCode"] != 0:
                raise RuntimeError("real Serve cleanup failed")
            result["status"] = "passed"
        except Exception as error:
            result["status"] = "failed"
            result["error"] = str(error)
        finally:
            for child, graceful in ((test, False), (fixture, True)):
                if child is not None:
                    try:
                        if not graceful and child.process.poll() is None:
                            child.process.kill()
                        child.finish(graceful)
                    except Exception as error:
                        result["cleanupError"] = str(error)
                        result["status"] = "failed"
            if proxy is not None:
                try:
                    result["sessionCreateLoss"] = proxy.finish()
                except Exception as error:
                    result["cleanupError"] = str(error)
                    result["status"] = "failed"
            if suite == "session":
                native = physical / "client" / "session-integration.json"
                if native.is_file():
                    try:
                        result["nativeReceipt"] = json.loads(native.read_text(encoding="utf-8"))
                        (args.logs / (label + "-native.json")).write_text(
                            json.dumps(result["nativeReceipt"], indent=2, ensure_ascii=False), encoding="utf-8")
                        if result["nativeReceipt"].get("status") != "passed-covered-paths":
                            result["status"] = "failed"
                    except Exception as error:
                        result["status"] = "failed"
                        result["error"] = "session native receipt invalid: " + str(error)
                elif result.get("status") == "passed":
                    result["status"] = "failed"
                    result["error"] = "session native receipt missing"
            result["elapsedSeconds"] = round(time.monotonic() - started, 3)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    parser.add_argument("--node", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--suite", choices=["all", "session", "executor", "archive"], default="all")
    parser.add_argument("--session-variant", choices=["all", "normal", "questions", "permission", "create-loss"], default="all")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    for key in ("fixture", "provenance", "node", "build", "logs"):
        setattr(args, key, getattr(args, key).resolve())
    provenance = json.loads(args.provenance.read_text(encoding="utf-8"))
    if sha256(args.fixture) != provenance["output"]["sha256"]:
        raise RuntimeError("private fixture differs from recorded source provenance")
    args.logs.mkdir(parents=True, exist_ok=False)
    matrix = [("session", "session", "sdk1"),
              ("session", "archive-offload", "sdk2-offload-v1"),
              ("session", "session", "sdk1", "questions"),
              ("session", "execution", "sdk1", "permission"),
              ("session", "session", "sdk1", "create-loss"),
              ("executor", "execution", "sdk1"),
              ("executor", "execution", "sdk1", "policy"),
              ("executor", "execution", "sdk1", "auth"),
              ("executor", "execution", "sdk1", "foreign"),
              ("archive", "archive", "sdk1"),
              ("archive", "archive-offload", "sdk2-offload-v1"),
              ("archive", "archive-manual", "sdk1")]
    results = [run_one(args, *case) for case in matrix if args.suite in ("all", case[0]) and
               (case[0] != "session" or args.session_variant == "all" or
                (case[3] if len(case) > 3 else "normal") == args.session_variant)]
    receipt = {"fixtureSha256": sha256(args.fixture), "provenance": provenance,
               "results": results, "passed": sum(r["status"] == "passed" for r in results),
               "total": len(results), "skipped": 0}
    (args.logs / "receipt.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if results and receipt["passed"] == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
