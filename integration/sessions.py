"""CPP-02 剩余会话格专属宿主；复用共有进程清理，不修改共有验收矩阵。"""
import argparse
import hashlib
import http.client
import http.server
import importlib.util
import json
import os
from pathlib import Path
import queue
import socket
import sys
import threading
import time
from urllib.parse import urlsplit

sys.dont_write_bytecode = True


class MutationProxy:
    """每个响应均来自真实 Serve；只丢弃指定原写的一次已成功响应。"""

    def __init__(self, origin, variant):
        target = urlsplit(origin)
        if target.scheme != "http" or target.hostname != "127.0.0.1" or not target.port:
            raise RuntimeError("proxy requires owned loopback Serve")
        self.records = []
        self.lock = threading.Lock()
        self.lost = 0
        self.variant = variant
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_):
                pass

            def forward(self):
                self.connection.settimeout(10)
                if not self.path.startswith("/api/") or self.path.startswith("http"):
                    self.send_error(400)
                    return
                length = int(self.headers.get("content-length", "0"))
                if not 0 <= length <= 8 * 1024 * 1024:
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
                          "requestBytes": len(body), "requestSha256": hashlib.sha256(body).hexdigest()}
                try:
                    upstream.request(self.command, self.path, body=body, headers=headers)
                    response = upstream.getresponse()
                    response_body = response.read(8 * 1024 * 1024 + 1)
                    if len(response_body) > 8 * 1024 * 1024:
                        raise RuntimeError("proxy response capacity")
                    record.update(upstreamStatus=response.status, responseBytes=len(response_body),
                                  responseSha256=hashlib.sha256(response_body).hexdigest())
                    create = self.command == "POST" and self.path == "/api/sessions"
                    interrupt = self.command == "POST" and self.path.endswith("/interrupt")
                    if create and response.status in (200, 201):
                        parsed = json.loads(response_body)
                        identity = parsed.get("sessionId")
                        if not isinstance(identity, str) or not identity:
                            raise RuntimeError("accepted create lacked real identity")
                        record["sessionIdSha256"] = hashlib.sha256(identity.encode()).hexdigest()
                    if interrupt and response.status == 202:
                        if json.loads(response_body).get("accepted") is not True:
                            raise RuntimeError("real interrupt was not accepted")
                        record["accepted"] = True
                    with owner.lock:
                        discard = owner.lost == 0 and (
                            variant == "offload-create-loss" and create and response.status == 201 or
                            variant == "interrupt-loss" and interrupt and response.status == 202)
                        if discard:
                            owner.lost += 1
                        record["responseDropped"] = discard
                        owner.records.append(record)
                    if discard:
                        self.close_connection = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        return
                    self.send_response(response.status)
                    for key, value in response.getheaders():
                        if key.lower() not in {"connection", "content-length", "transfer-encoding"}:
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
            raise RuntimeError("proxy listener did not stop")
        return {"variant": self.variant, "requests": self.records,
                "responsesDropped": self.lost, "listenerStopped": True}

    @staticmethod
    def validate(receipt):
        records = receipt["requests"]
        if any("proxyError" in r for r in records):
            raise RuntimeError("proxy error before proving real acceptance")
        posts = [r for r in records if r["method"] == "POST"]
        if receipt["variant"] == "disabled":
            if len(records) != 1 or posts or records[0]["method"] != "GET" or not records[0]["path"].startswith("/api/capabilities/sessions?"):
                raise RuntimeError("unavailable offload family attempted a write or fallback")
            return
        if receipt["responsesDropped"] != 1:
            raise RuntimeError("mutation loss did not discard exactly one real acceptance")
        if receipt["variant"] == "offload-create-loss":
            if len(posts) != 3 or any(r["path"] != "/api/sessions" for r in posts):
                raise RuntimeError("offload creation used an unexpected write/retry")
            if [r["upstreamStatus"] for r in posts] != [201, 200, 409]:
                raise RuntimeError("offload original create/recovery/conflict statuses changed")
            if posts[0]["requestSha256"] != posts[1]["requestSha256"] or posts[0]["sessionIdSha256"] != posts[1]["sessionIdSha256"]:
                raise RuntimeError("offload recovery changed original body or session identity")
            reads = [r for r in records if r["method"] == "GET"]
            if len(records) != 6 or len(reads) != 3 or any(not r["path"].startswith("/api/capabilities/sessions?") for r in reads):
                raise RuntimeError("offload recovery guessed list/session or invented status route")
        elif len(posts) != 1 or not posts[0]["path"].endswith("/interrupt") or posts[0].get("accepted") is not True:
            raise RuntimeError("interrupt acceptance loss retried or created another session")


def run_one(args, common, mode, family, variant):
    executable = args.build / ("serve_session.exe" if os.name == "nt" else "serve_session")
    if not executable.is_file():
        raise FileNotFoundError(executable)
    result = {"suite": "session", "mode": mode, "family": family, "variant": variant,
              "executableSha256": common.sha256(executable), "processes": []}
    started = time.monotonic()
    fixture = test = proxy = None
    label = mode + "-" + variant
    with common.owned_temp(result) as temp, \
            (args.logs / (label + "-serve.log")).open("w", encoding="utf-8") as serve_log, \
            (args.logs / (label + "-client.log")).open("w", encoding="utf-8") as client_log:
        physical = Path(temp).resolve()

        def start_fixture():
            child = common.Child([str(args.node), str(args.fixture), ".", str(physical), mode], physical, serve_log)
            result["processes"].append({"pid": child.process.pid, "samePhysicalDirectory": True})
            return child

        def cold_evidence():
            directory = physical / "cold" / "objects"
            files = sorted(path for path in directory.rglob("*") if path.is_file())
            if not files or len(files) > 4096:
                raise RuntimeError("offload filesystem cold evidence absent or unbounded")
            rows = []
            for path in files:
                data = path.read_bytes()
                rows.append({"path": path.relative_to(physical).as_posix(), "bytes": len(data),
                             "sha256": hashlib.sha256(data).hexdigest(),
                             "containsOriginalSyntheticPrompt": b"CPP-OFFLOAD-COLD-ORIGINAL" in data})
            return rows

        try:
            fixture = start_fixture()
            info = fixture.prefixed("TANSR_GO_FIXTURE ", 30)
            if info.get("manifestRevision") != 7:
                raise RuntimeError("fixture revision differs from frozen revision 7")
            result["fixtureConfiguration"] = {key: info[key] for key in
                ("mode", "promptPolicy", "persistenceMode", "sourceId", "sourceGeneration", "seedSha256", "coldStore") if key in info}
            if variant in ("disabled", "offload-create-loss", "interrupt-loss"):
                proxy = MutationProxy(info["baseURL"], variant)
                info = dict(info, proxyBaseURL=proxy.base_url)
            info_path = physical / "fixture-info.json"
            info_path.write_text(json.dumps(info), encoding="utf-8")
            workspace = physical / "client"
            workspace.mkdir(mode=0o700)
            test = common.Child([str(executable), str(info_path), family, str(workspace), variant], physical, client_log)
            while time.monotonic() - started < args.timeout:
                if fixture.overflow or test.overflow:
                    raise RuntimeError("child output queue overflow")
                try:
                    line = test.lines.get(timeout=0.1)
                except queue.Empty:
                    if test.process.poll() is not None:
                        break
                    continue
                if line.startswith("TANSR_CPP_CONTROL "):
                    command = json.loads(line[len("TANSR_CPP_CONTROL "):])
                    if command.get("command") == "prompt-observations" and variant == "platform-prompt":
                        fixture.send(json.dumps(command))
                        reply = fixture.prefixed("TANSR_RUST_CONTROL ", 10)
                        if reply.get("requestId") != command.get("requestId"):
                            raise RuntimeError("prompt control identity mismatch")
                        test.send(json.dumps(reply))
                        continue
                    if command.get("command") != "restart-serve" or variant not in ("process-restart", "offload-restart") or len(result["processes"]) != 1:
                        raise RuntimeError("unexpected session host control")
                    old_pid = fixture.process.pid
                    code = fixture.finish(graceful=True)
                    result["processes"][-1]["exitCode"] = code
                    fixture = None
                    if code != 0:
                        raise RuntimeError("original Serve did not exit before restart")
                    if variant == "offload-restart":
                        result["coldBeforeRestart"] = cold_evidence()
                        result["originalPromptFoundInColdObjects"] = any(
                            row["containsOriginalSyntheticPrompt"] for row in result["coldBeforeRestart"])
                        result["coldOnlyHistoryRecoveryClaimed"] = False
                    fixture = start_fixture()
                    if fixture.process.pid == old_pid:
                        raise RuntimeError("restarted process did not change PID")
                    next_info = fixture.prefixed("TANSR_GO_FIXTURE ", 30)
                    if variant == "offload-restart":
                        result["coldAfterReopen"] = cold_evidence()
                        if result["coldBeforeRestart"] != result["coldAfterReopen"]:
                            raise RuntimeError("offload reopen changed original cold objects before client recovery")
                        result["reopenedConfiguration"] = {key: next_info[key] for key in
                            ("mode", "persistenceMode", "sourceId", "sourceGeneration", "seedSha256", "coldStore")}
                    test.send(json.dumps({"requestId": command["requestId"], "ok": True, "result": next_info}))
            else:
                raise TimeoutError("session remainder scenario deadline")
            result["exitCode"] = test.finish()
            test = None
            if proxy is not None:
                result["mutationProxy"] = proxy.finish()
                proxy = None
                MutationProxy.validate(result["mutationProxy"])
            if result["exitCode"] != 0:
                raise RuntimeError("native scenario failed; inspect client log")
            code = fixture.finish(graceful=True)
            result["processes"][-1]["exitCode"] = code
            fixture = None
            if code != 0:
                raise RuntimeError("Serve cleanup failed")
            if variant in ("process-restart", "offload-restart") and len(result["processes"]) != 2:
                raise RuntimeError("independent restart did not occur")
            result["status"] = "passed"
        except Exception as error:
            result.update(status="failed", error=str(error))
        finally:
            for child, graceful in ((test, False), (fixture, True)):
                if child is not None:
                    try:
                        if not graceful and child.process.poll() is None:
                            child.process.kill()
                        child.finish(graceful)
                    except Exception as error:
                        result.update(status="failed", cleanupError=str(error))
            if proxy is not None:
                try:
                    result["mutationProxy"] = proxy.finish()
                except Exception as error:
                    result.update(status="failed", cleanupError=str(error))
            native = physical / "client" / "session-integration.json"
            if native.is_file():
                result["nativeReceipt"] = json.loads(native.read_text(encoding="utf-8"))
                if result["nativeReceipt"].get("status") != "passed-covered-paths":
                    result["status"] = "failed"
            elif result.get("status") == "passed":
                result.update(status="failed", error="native receipt missing")
            result["elapsedSeconds"] = round(time.monotonic() - started, 3)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("fixture", "provenance", "node", "build", "logs"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--common-runner", type=Path, default=Path(__file__).with_name("run.py"))
    parser.add_argument("--variant", choices=["all", "disabled", "offload-create-loss", "interrupt-loss", "process-restart", "callback-release", "platform-prompt", "offload-restart", "configuration"], default="all")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    for key in ("fixture", "provenance", "node", "build", "logs", "common_runner"):
        setattr(args, key, getattr(args, key).resolve())
    spec = importlib.util.spec_from_file_location("cpp_integration_common", args.common_runner)
    common = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(common)
    provenance = json.loads(args.provenance.read_text(encoding="utf-8"))
    if common.sha256(args.fixture) != provenance["output"]["sha256"]:
        raise RuntimeError("fixture differs from source provenance")
    args.logs.mkdir(parents=True, exist_ok=False)
    matrix = [("session-media-disabled", "sdk1", "disabled"),
              ("archive-offload", "sdk2-offload-v1", "offload-create-loss"),
              ("session", "sdk1", "interrupt-loss"),
              ("session", "sdk1", "process-restart"),
              ("session", "sdk1", "callback-release"),
              ("session-prompt-fallback", "sdk1", "platform-prompt"),
              ("session-prompt-prepend", "sdk1", "platform-prompt"),
              ("archive-offload-durable", "sdk2-offload-v1", "offload-restart")]
    results = [run_one(args, common, *case) for case in matrix if args.variant in ("all", case[2]) or
               args.variant == "configuration" and case[2] in ("platform-prompt", "offload-restart")]
    receipt = {"fixtureSha256": common.sha256(args.fixture), "commonRunnerSha256": common.sha256(args.common_runner),
               "harnessSha256": common.sha256(Path(__file__)), "provenance": provenance, "results": results,
               "passed": sum(r["status"] == "passed" for r in results), "total": len(results), "skipped": 0}
    (args.logs / "receipt.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if results and receipt["passed"] == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
