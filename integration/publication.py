"""PST-05 native publication process restart against a pinned public-package Serve.

Python owns processes and synthetic host controls only. Product requests, frozen
wire validation, storage and recovery run through the public C++ SDK. Failed
runtime directories remain intact for diagnosis; no source or receipt is reset.
"""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import sys
import threading
import tempfile
import time
from run import Child, sha256

PREFIX = "TANSR_CPP_PUBLICATION "

class DemoChild(Child):
    """仅管理本次 Demo 的进程与控制台，不向调用方终端广播信号。"""
    def __init__(self, command, cwd, log):
        self.stop_sent = False
        if os.name != "nt":
            super().__init__(command, cwd, log)
            return
        self.log = log
        self.lines = queue.Queue(maxsize=128)
        self.overflow = False
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self.process = subprocess.Popen(
            [sys.executable, str(Path(__file__).with_name("demo_console.py"))] + command,
            cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log,
            text=True, encoding="utf-8", errors="strict", bufsize=1,
            creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
        self.reader = threading.Thread(target=self.drain)
        self.reader.start()

    def interrupt(self):
        if self.process.poll() is None and not self.stop_sent:
            if os.name == "nt":
                self.send("PST_DEMO_CTRL_C")
            else:
                self.process.send_signal(signal.SIGINT)
            self.stop_sent = True

    def finish(self, graceful=False):
        self.interrupt()
        return super().finish(graceful)

class Scenario:
    def __init__(self, args, label, result, stack):
        self.args, self.label, self.result, self.stack = args, label, result, stack
        self.root = Path(tempfile.mkdtemp(prefix="tansr-cpp-publication-")).resolve()
        if self.root.parent != Path(tempfile.gettempdir()).resolve():
            raise RuntimeError("owned OS temporary boundary")
        self.children = []
        self.sequence = 0
        self.info = None
        self.fixture = None
        result["temporaryDirectory"] = str(self.root)

    def start(self, command, label, env=None):
        self.sequence += 1
        log = self.stack.enter_context((self.args.logs / f"{self.label}-{self.sequence:02d}-{label}.log").open("w", encoding="utf-8"))
        original = {key: os.environ.get(key) for key in (env or {})}
        try:
            os.environ.update(env or {})
            child_type = DemoChild if label == "actual-memory-demo" else Child
            child = child_type([str(value) for value in command], self.root, log)
        finally:
            for key, value in original.items():
                if value is None: os.environ.pop(key, None)
                else: os.environ[key] = value
        self.children.append(child)
        self.result.setdefault("processes", []).append({"name": label, "pid": child.process.pid})
        return child

    def wait(self, child, expected, timeout=105):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            value = child.prefixed(PREFIX, max(.1, until-time.monotonic()))
            self.result.setdefault("events", []).append(value)
            if value["event"] == expected:
                return value
        raise TimeoutError(expected)

    def finish(self, child, expected=0, timeout=105):
        if isinstance(child, DemoChild): child.interrupt()
        until = min(time.monotonic() + timeout, getattr(self, "persistence_deadline", float("inf")))
        while child.process.poll() is None or not child.lines.empty() or child.reader.is_alive():
            if time.monotonic() >= until: raise TimeoutError("native child completion")
            if child.overflow: raise RuntimeError("bounded stdout queue overflow")
            for other in self.children:
                if other is child or other is self.fixture: continue
                if other.overflow: raise RuntimeError("concurrent stdout queue overflow")
                while not other.lines.empty():
                    line = other.lines.get_nowait()
                    if line.startswith(PREFIX): self.result.setdefault("events", []).append(json.loads(line[len(PREFIX):]))
            try:
                line = child.lines.get(timeout=.05)
                if line.startswith(PREFIX): self.result.setdefault("events", []).append(json.loads(line[len(PREFIX):]))
            except queue.Empty: pass
        code = child.finish()
        self.children.remove(child)
        if code != expected: raise RuntimeError(f"native child exited {code}, expected {expected}")
        return code

    def native(self, mode, label="seed"):
        return self.start([self.args.build / ("serve_publication.exe" if os.name == "nt" else "serve_publication"), self.root / "fixture.json", self.args.family, self.root, mode, label], mode)

    def start_host(self):
        command = [self.args.node]
        if self.args.profile == "persistence-v1":
            command += ["--import", (self.args.source_root / "scripts/inject-globals.mjs").as_uri(),
                        "--import", (self.args.source_root / "node_modules/tsx/dist/loader.mjs").as_uri()]
            command += [self.args.fixture, self.args.source_root, self.root, "persistence"]
        else:
            command += [self.args.fixture, ".", self.root, "publication"]
        self.fixture = self.start(command, "host")
        self.info = self.fixture.prefixed("TANSR_GO_FIXTURE ", 30)
        if self.info["manifestRevision"] != 7: raise RuntimeError("unfrozen Serve")
        self.result["hostReady"] = self.info.copy()
        if self.args.profile == "persistence-v1":
            if self.info.get("profile") != "terminal-persistence-v1": raise RuntimeError("wrong persistence Host")
            if self.args.original_root:
                self.info["originalStoragePath"] = str(self.args.original_root / "media/publication.bin")
            self.info["executorId"] = self.args.executor
            self.info["scope"] = {k:self.info[k] for k in ("applicationScopeId","endUserId","authorizationRevision")}
            identity = self.info["publicationIdentity"]
            self.info["publicationIdentity"] = {"scope":{k:identity[k] for k in ("applicationScopeId","endUserId")},
                **{k:identity[k] for k in ("sourceId","sourceGeneration","domainKey")}}
        (self.root / "fixture.json").write_text(json.dumps(self.info), encoding="utf-8")

    def control(self, command, expect_error=False, **fields):
        request = {"requestId": self.label + "-" + command, "command": command, **fields}
        self.fixture.send(json.dumps(request))
        prefix = "TANSR_GO_CONTROL " if self.args.profile == "persistence-v1" else "TANSR_RUST_CONTROL "
        started = time.monotonic()
        timeout = 10
        if self.args.profile == "persistence-v1":
            remaining = self.persistence_deadline - started
            timeout = remaining if command in ("archive-receipts", "settle-publication", "set-publication-mode") else min(timeout, remaining)
            if timeout <= 0: raise TimeoutError("persistence current 105 second cycle deadline")
        value = self.fixture.prefixed(prefix, timeout)
        self.result.setdefault("controlTimings", []).append({"command":command,"seconds":time.monotonic()-started,"ok":value.get("ok")})
        if expect_error:
            if value.get("ok") or value["requestId"] != request["requestId"]: raise RuntimeError("host negative control mismatch")
            self.result.setdefault("rejectedControls", []).append(value)
            return value
        if not value.get("ok") or value["requestId"] != request["requestId"]: raise RuntimeError("host control mismatch")
        return value["result"]

    def stop_worker(self, worker):
        (self.root / "stop").write_text("stop", encoding="utf-8")
        self.finish(worker)
        (self.root / "stop").unlink()

    def close(self):
        for child in list(reversed(self.children)):
            try:
                code = child.finish(graceful=child is self.fixture)
                if child is self.fixture:
                    self.result["hostExitCode"] = code
                    if code != 0: raise RuntimeError("Serve did not close normally")
            except Exception as error:
                self.result["cleanupError"] = str(error)
                self.result["status"] = "failed"
        self.children.clear()
        if self.result.get("status") == "passed" and self.args.retain_media:
            self.result["temporaryCleanup"] = "retained by explicit request for Demo reuse"
        if self.result.get("status") == "passed" and not self.args.retain_media:
            for attempt in range(5):
                try:
                    shutil.rmtree(str(self.root))
                    self.result["temporaryCleanup"] = "removed"
                    break
                except OSError as error:
                    if attempt == 4:
                        self.result["status"] = "failed"
                        self.result["cleanupError"] = str(error)
                    time.sleep(.2)
        if self.root.exists(): self.result["retainedTemporaryDirectory"] = str(self.root)

def completed_transfers(events):
    transfers = {}
    for event in events:
        if event["event"] not in ("receipt", "chunk-receipt-durable") or "transferId" not in event:
            continue
        group = transfers.setdefault(event["transferId"], {"chunks": {}})
        if event["action"] == "begin": group["begin"] = event
        elif event["action"] == "commit": group["commit"] = event
        elif event["action"] == "chunk":
            prior = group["chunks"].setdefault(event["offset"], event)
            if (prior["byteLength"], prior["payloadDigest"]) != (event["byteLength"], event["payloadDigest"]):
                raise RuntimeError("same transfer offset changed payload")
    complete = []
    for transfer_id, group in transfers.items():
        if "commit" not in group: continue
        if "begin" not in group: raise RuntimeError("commit without recorded original begin")
        begin, commit = group["begin"], group["commit"]
        offset = 0
        for chunk_offset, part in sorted(group["chunks"].items()):
            if offset != chunk_offset: raise RuntimeError("publication chunks have a gap or overlap")
            offset += part["byteLength"]
        if offset != begin["byteLength"] or commit["transferStatus"] != "committed" or commit["receivedBytes"] != offset or commit["etag"] != begin["sha256"]:
            raise RuntimeError("publication commit does not match original complete body")
        complete.append({"transferId": transfer_id, "byteLength": offset, "sha256": begin["sha256"],
                         "chunks": len(group["chunks"]), "chunkOffsets": sorted(group["chunks"])})
    return complete

def native_chain(s):
    s.finish(s.native("setup"))
    worker = s.native("warm")
    s.wait(worker,"worker-ready")
    for index in range(12):
        s.finish(s.native("seed-pin", f"large-{index}"))
        complete = completed_transfers(s.result["events"])
        if complete and complete[-1]["byteLength"] > 12288: break
        # Each process remains bounded; rotate only after a complete original pin transaction.
        if index % 3 == 2:
            s.stop_worker(worker)
            worker = s.native("serve")
            s.wait(worker, "worker-ready")
    else: raise RuntimeError("12 bounded original pins did not produce >12 KiB publication")
    s.result["largeSeedPins"] = index + 1
    s.stop_worker(worker)
    worker = s.native("chunk")
    s.wait(worker,"worker-ready")
    trigger = s.native("pin","resume-chunk")
    s.wait(worker,"chunk-receipt-durable")
    s.finish(worker)
    worker = s.native("resume-chunk")
    s.wait(worker,"original-key-reconciled")
    s.finish(trigger)
    s.finish(s.native("close-session"))
    # Host settlement may still require the original publisher; keep polling until it proves quiet.
    witness = s.control("set-publication-mode", mode="reopen")
    if witness["nextPublicationMode"] != "reopen": raise RuntimeError("Host did not arm explicit reopen")
    s.result["coldReopenWitness"] = witness
    s.stop_worker(worker)
    s.finish(s.native("resume-session"))
    worker = s.native("serve")
    s.wait(worker, "worker-ready")
    read_start = len(s.result["events"])
    s.finish(s.native("read"))
    s.stop_worker(worker)
    cold_reads = [event for event in s.result["events"][read_start:] if event.get("event") == "receipt" and event.get("action") == "read"]
    if len(cold_reads) < 2 or not any(event.get("complete") and event.get("nextOffset", 0) > 12288 and event.get("verifiedPublicationSha256") == event["etag"] for event in cold_reads):
        raise RuntimeError("cold reopen did not read and verify an actual multi-chunk publication")
    s.result["coldReadChunks"] = cold_reads
    worker = s.native("claim")
    s.wait(worker,"worker-ready")
    trigger = s.native("unknown","only-claim")
    s.wait(worker,"claim-durable")
    s.finish(worker)
    worker = s.native("resume-unknown")
    s.wait(worker,"original-key-reconciled")
    s.finish(trigger)
    s.stop_worker(worker)
    s.finish(s.native("inspect"))
    events = s.result["events"]
    receipts = [x for x in events if x["event"] == "receipt"]
    actions = sorted(set(x["action"] for x in receipts))
    s.result["realReceiptActions"] = actions
    if not {"head","read","begin","chunk","commit","query"} <= set(actions): raise RuntimeError("real publication action coverage incomplete")
    completed = completed_transfers(events)
    checkpoint = next(event for event in events if event["event"] == "chunk-receipt-durable")
    continued = [item for item in completed if item["transferId"] == checkpoint["transferId"] and item["byteLength"] > 12288 and item["chunks"] >= 2]
    if len(continued) != 1: raise RuntimeError("original checkpoint transfer did not resume all actual chunks")
    s.result["restartedMultiChunkTransfer"] = continued[0]
    s.result["multipleChunkPublication"] = True
    s.result["unverified"] = ["Linux/macOS", "whole Serve process cold restart"]
    s.result["stats"] = s.control("stats")
    if s.result["stats"]["modelCalls"] != 0: raise RuntimeError("publication unexpectedly invoked synthetic model")

def demo_chain(s):
    s.finish(s.native("seed"))
    state = json.loads((s.root / "handoff/state.json").read_text(encoding="utf-8"))
    identity = s.info["publicationIdentity"]
    binary = s.args.demo or s.args.build / "demo" / ("tansr-memory.exe" if os.name == "nt" else "tansr-memory")
    command = [binary,"--base",s.info["baseURL"],"--family",s.args.family,"--session",state["sessionId"],"--file",s.root/"media/publication.bin","--key-id","cpp-real-publication-1","--source",identity["sourceId"],"--generation",identity["sourceGeneration"],"--domain",identity["domainKey"],"--executor",s.info["executorId"],"--create","--timeout","10"]
    if s.args.before:
        command.insert(command.index("--create")+1, "true")
    else: command += ["--binding-request","cpp-demo-binding"]
    child = s.start(command,"memory-demo", {"TANSR_TOKEN_FILE":str(s.root/"credentials/token"),"TANSR_SCOPE_FILE":str(s.root/"credentials/scope"),"TANSR_ARCHIVE_KEY_FILE":str(s.root/"credentials/key")})
    until = time.monotonic()+15
    ready = False
    while time.monotonic()<until:
        try:
            line=child.lines.get(timeout=.05)
            if line.startswith("ready:"): ready=True; break
        except queue.Empty:
            if child.process.poll() is not None: break
    if not ready: raise RuntimeError("Demo never became ready")
    s.finish(s.native("read"),timeout=20)
    s.finish(child,timeout=20)
    s.finish(s.native("inspect-demo"))
    s.result["stats"]=s.control("stats")
    if s.result["stats"]["modelCalls"] != 0: raise RuntimeError("Demo invoked model")

def _start_cycle_budget(s):
    if s.args.suite != 'demo' or s.args.original_root is None:
        s.persistence_deadline = time.monotonic() + 105
        return
    if s.args.profile != 'persistence-v1' or s.args.suite != 'demo' or s.args.original_root is None:
        raise RuntimeError('final aggregate entry requires the original two-cycle Demo scenario')
    started = time.monotonic()
    s.persistence_started = started
    s.persistence_cycle_started = started
    s.persistence_total_deadline = started + 210
    s.persistence_deadline = started + 105
    s.result['budget'] = {'kind': 'two complete cold-consumer cycles, preparation belongs to first', 'cycleSeconds': 105, 'totalSeconds': 210, 'productLifetimeSeconds': 600, 'singleStepTimeoutsChanged': False, 'cycles': []}

def _enter_cycle_budget(s, index):
    if s.args.suite != 'demo' or s.args.original_root is None:
        return
    if index == 0:
        return
    if index != 1:
        raise RuntimeError('only original two Demo cycles allowed')
    now = time.monotonic()
    if now >= s.persistence_deadline:
        raise TimeoutError('first original 105-second cycle exceeded')
    s.result['budget']['cycles'].append({'cycle': 1, 'seconds': now - s.persistence_cycle_started, 'status': 'passed original body'})
    s.persistence_cycle_started = now
    s.persistence_deadline = min(now + 105, s.persistence_total_deadline)

def _finish_cycle_budget(s):
    if s.args.suite != 'demo' or s.args.original_root is None:
        return
    now = time.monotonic()
    if now >= s.persistence_deadline:
        raise TimeoutError('second original 105-second cycle exceeded')
    s.result['budget']['cycles'].append({'cycle': 2, 'seconds': now - s.persistence_cycle_started, 'status': 'passed original body'})
    s.result['budget']['totalSecondsActual'] = now - s.persistence_started

def persistence_chain(s):
    _start_cycle_budget(s)
    demo = s.args.suite == "demo"
    roots = []
    reused = demo and s.args.original_root is not None
    if reused:
        # 只复制前轮真实响应作为比较预期；Store/.writes始终在原绝对路径打开。
        s.finish(s.native("setup"))
        for name in ("pin.json", "persistence-checkpoint.json"):
            source = s.args.original_root / "handoff" / name
            shutil.copyfile(source, s.root / "handoff" / name)
        s.result["originalMedia"] = {"path":s.info["originalStoragePath"],
            "beforeSha256":sha256(Path(s.info["originalStoragePath"]))}
        s.finish(s.native("close-session"))
        mode = s.control("set-publication-mode", mode="reopen")
        if mode["operations"]: raise RuntimeError("reopen preparation touched terminal storage")
    for round_index in range(2 if reused else 3 if demo else 2):
        _enter_cycle_budget(s, round_index)
        use_demo = demo and (reused or round_index > 0)
        s.finish(s.native("seed" if use_demo else "setup"))
        state = json.loads((s.root / "handoff/state.json").read_text(encoding="utf-8"))
        session = state["sessionId"]
        if use_demo:
            if not s.args.demo: raise RuntimeError("actual installed Memory Demo is required")
            identity = s.info["publicationIdentity"]
            command = [s.args.demo, "--base", s.info["baseURL"], "--family", s.args.family,
                "--session", session, "--binding-request", f"cpp-persistence-demo-{round_index}",
                "--file", s.info.get("originalStoragePath", s.root/"media/publication.bin"), "--key-id", "cpp-real-publication-1",
                "--source", identity["sourceId"], "--generation", identity["sourceGeneration"],
                "--domain", identity["domainKey"], "--executor", s.info["executorId"],
                "--profile", "persistence-v1"]
            if round_index == 0 and not reused: command.append("--create")
            worker = s.start(command, "actual-memory-demo", {"TANSR_TOKEN_FILE":str(s.root/"credentials/token"),
                "TANSR_SCOPE_FILE":str(s.root/"credentials/scope"), "TANSR_ARCHIVE_KEY_FILE":str(s.root/"credentials/key")})
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                try: line = worker.lines.get(timeout=.1)
                except queue.Empty: continue
                if line.startswith("ready:") and "profile=persistence-v1" in line: break
            else: raise TimeoutError("actual V1 Demo readiness")
        else:
            worker = s.native("persistence-warm" if round_index == 0 else "persistence-cold")
            s.wait(worker, "worker-ready")
            if round_index > 0:
                restored = [v["root"] for v in s.result["events"] if v["event"] == "cold-before-serve"][-1]
                if restored != roots[-1]: raise RuntimeError("cold store did not restore previous exact Root")
        s.finish(s.native("read"))
        s.control("settle-publication", sessionId=session)
        if round_index == 0 and not reused:
            s.finish(s.native("pin", "cpp-persistence"))
            s.control("settle-publication", sessionId=session)
            pin = json.loads((s.root/"handoff/pin.json").read_text(encoding="utf-8"))
            archive = {"sessionId":session, "operationIds":[pin["command"]["operationId"]], "limit":1}
            s.control("archive-receipts", expect_error=True, **archive)
            archived = s.control("archive-receipts", consume=True, **archive)
            if not archived["explicitlyConsumed"] or archived["archived"]["archived"] != 1 or archived["archived"]["hot"] != 0:
                raise RuntimeError("trusted host did not archive original committed receipt")
            s.result["trustedHostArchive"] = archived
        before = s.control("facts")["operations"]
        start_event = len(s.result.get("events", []))
        s.finish(s.native("persistence-keys"))
        after = s.control("facts")["operations"]
        if use_demo and not any(op["sessionId"] == session and op["action"] == "read" and op["receiptStatus"] == "completed" for op in after):
            raise RuntimeError("actual Demo did not cold-read the original publication")
        old_ids = {op["operationId"] for op in before}
        new = [op for op in after if op["operationId"] not in old_ids]
        if not use_demo and any(op["action"] in ("begin","put","commit") for op in new):
            raise RuntimeError("permanent key replay wrote another root")
        if not use_demo:
            hits = {v.get("keyKind") for v in s.result["events"][start_event:]
                    if v["event"] == "persistence-storage" and v.get("hit")}
            if not {"primary","secondary"}.issubset(hits): raise RuntimeError("dual-key storage hits absent")
        s.control("settle-publication", sessionId=session)
        s.finish(s.native("close-session"))
        s.control("set-publication-mode", mode="reopen")
        if use_demo: s.finish(worker)
        else: s.stop_worker(worker)
        s.finish(s.native("persistence-inspect"))
        roots.append([v["root"] for v in s.result["events"] if v["event"] == "persistence-inspection"][-1])
    if any(value["index"] != roots[0]["index"] for value in roots[1:]): raise RuntimeError("cold read/key replay changed permanent index")
    facts = s.control("facts")
    if facts["modelExchanges"] != 0: raise RuntimeError("storage-only test dispatched model")
    reconciled = [v for v in s.result["events"] if v["event"] == "original-key-reconciled"]
    cold = [v for v in s.result["events"] if v["event"] == "cold-original-journal-and-transfer"]
    if len(cold) != len(roots): raise RuntimeError("cold journal coverage missing")
    if not reused:
        if len(reconciled) != 1: raise RuntimeError("loss/query coverage missing")
        op = reconciled[0]
        original = [v for v in facts["operations"] if v["operationId"] == op["operationId"]]
        if len(original) != 1 or original[0]["digest"] != op["digest"] or original[0]["receiptStatus"] != "completed":
            raise RuntimeError("Serve changed original accepted operation")
    else:
        if reconciled: raise RuntimeError("Demo reuse must not repeat original loss scenario")
        s.result["originalMedia"]["afterSha256"] = sha256(Path(s.info["originalStoragePath"]))
        s.result["originalMedia"]["retained"] = True
    s.result["facts"] = facts
    s.result["durableRoot"] = roots[-1]
    s.result["demoBoundary"] = "two installed Demo processes reopen the native-bootstrap durable original; neither fabricates receipts" if demo else None
    s.result["consumptionBoundary"] = "trusted host explicitly consumes original committed key; no automatic model consumption"
    _finish_cycle_budget(s)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest",required=True,type=Path)
    parser.add_argument("--node",required=True,type=Path)
    parser.add_argument("--build",required=True,type=Path)
    parser.add_argument("--logs",required=True,type=Path)
    parser.add_argument("--family",default="sdk1",choices=["sdk1","sdk2-offload-v1"])
    parser.add_argument("--suite",choices=["native","demo"],default="native")
    parser.add_argument("--demo",type=Path)
    parser.add_argument("--before",action="store_true")
    parser.add_argument("--profile",choices=["publication","persistence-v1"],default="publication")
    parser.add_argument("--source-root",type=Path)
    parser.add_argument("--executor")
    parser.add_argument("--original-root",type=Path)
    parser.add_argument("--retain-media",action="store_true")
    args=parser.parse_args()
    if args.profile == "persistence-v1" and args.suite == "demo" and not args.original_root:
        raise RuntimeError("Demo suite needs the original media from a native --retain-media run")
    if args.original_root:
        args.original_root = args.original_root.resolve(strict=True)
        if args.profile != "persistence-v1" or args.suite != "demo": raise RuntimeError("original media reuse is explicit V1 Demo only")
        for name in ("media/publication.bin", "media/publication.bin.writes", "handoff/pin.json", "handoff/persistence-checkpoint.json"):
            if not (args.original_root/name).is_file(): raise RuntimeError("complete original media/expected evidence required")
    manifest=json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    args.fixture=Path(manifest["fixture"])
    if args.profile == "persistence-v1" and (not args.source_root or not args.executor):
        raise RuntimeError("source V1 fixture needs explicit source root and configured executor")
    for item in manifest.get("sourceInputs", []):
        if sha256(Path(item["path"])) != item["sha256"]: raise RuntimeError("source candidate SHA256 mismatch")
    if sha256(args.fixture)!=manifest["fixtureSha256"]: raise RuntimeError("shared Host SHA256 mismatch")
    for package in manifest["packages"]:
        if sha256(Path(package["path"]))!=package["sha256"]: raise RuntimeError("sealed package SHA256 mismatch")
    args.logs.mkdir(parents=True,exist_ok=False)
    result={"suite":args.suite,"family":args.family,"profile":args.profile,"manifestSha256":sha256(args.manifest),"fixtureSha256":manifest["fixtureSha256"],"status":"failed"}
    with ExitStack() as stack:
        scenario=Scenario(args,args.suite,result,stack)
        try:
            scenario.start_host()
            (persistence_chain if args.profile == "persistence-v1" else native_chain if args.suite=="native" else demo_chain)(scenario)
            result["status"]="passed"
        except Exception as error: result["error"]=str(error)
        finally: scenario.close()
    (args.logs/"summary.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({key:result.get(key) for key in ["status","error","hostExitCode","cleanupError"]}))
    return 0 if result["status"]=="passed" else 1

if __name__=="__main__": raise SystemExit(main())
