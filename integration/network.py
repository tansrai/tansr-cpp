#!/usr/bin/env python3
"""原 A06 独立网络实证；blocked-dns 仅用于自身 resolv.conf 指向 127.0.0.1 的隔离 Linux 容器。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import struct
import subprocess
import tempfile
import time
import uuid


def dns_question(packet):
    if len(packet) < 12:
        raise ValueError("short DNS packet")
    transaction, flags, questions = struct.unpack("!HHH", packet[:6])
    if flags & 0x8000 or questions != 1:
        raise ValueError("not a single-question DNS request")
    labels, offset = [], 12
    while offset < len(packet) and packet[offset]:
        size = packet[offset]
        offset += 1
        if size > 63 or offset + size > len(packet):
            raise ValueError("invalid outgoing DNS label")
        labels.append(packet[offset:offset + size].decode("ascii"))
        offset += size
    offset += 1
    query_type, query_class = struct.unpack("!HH", packet[offset:offset + 4])
    return {"id": transaction, "host": ".".join(labels), "type": query_type,
            "class": query_class, "bytes": len(packet), "sha256": hashlib.sha256(packet).hexdigest()}


def native_receipt(log):
    lines = log.read_text(encoding="utf-8").splitlines()
    return json.loads(lines[-1]) if lines else {"status": "failed", "error": "no native receipt"}


def system_trust(args):
    log = args.logs / "native.log"
    environment = os.environ.copy()
    removed = [name for name in ("SSL_CERT_FILE", "SSL_CERT_DIR", "CURL_CA_BUNDLE") if name in environment]
    for name in removed:
        environment.pop(name)
    with log.open("wb") as output:
        process = subprocess.run([str(args.driver), "system-trust"], env=environment,
                                 stdout=output, stderr=subprocess.STDOUT, timeout=25)
    result = {"exitCode": process.returncode, "native": native_receipt(log),
              "trustEnvironmentOverrideNamesRemovedForChild": removed}
    result["passed"] = process.returncode == 0 and result["native"]["status"] == "passed"
    return result


def blocked_dns(args):
    if platform.system() != "Linux":
        raise RuntimeError("blocked-dns requires the dedicated isolated Linux container")
    resolver = Path("/etc/resolv.conf").read_text()
    nameservers = [line.split()[1] for line in resolver.splitlines()
                   if line.strip().startswith("nameserver ")]
    if nameservers != ["127.0.0.1"]:
        raise RuntimeError("container resolver must exclusively use its own loopback DNS")
    result = {"resolverNameservers": nameservers,
              "resolverSha256": hashlib.sha256(resolver.encode()).hexdigest(),
              "queries": [], "dnsRepliesSent": 0, "queryHoldMs": 250}
    Path(args.logs / "resolv.conf").write_text(resolver)
    with tempfile.TemporaryDirectory(prefix="tansr-a06-dns-") as temporary:
        marker = Path(temporary) / "query-observed"
        host = "a06-" + uuid.uuid4().hex + ".test"
        result["temporaryRoot"] = temporary
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 53))
            server.settimeout(0.05)
            log = args.logs / "native.log"
            with log.open("wb") as output:
                process = subprocess.Popen([str(args.driver), "blocked-dns", host, str(marker)],
                                           stdout=output, stderr=subprocess.STDOUT)
                result["pid"] = process.pid
                first_query = None
                deadline = time.monotonic() + 15
                try:
                    while process.poll() is None and time.monotonic() < deadline:
                        try:
                            packet, peer = server.recvfrom(4096)
                            query = dns_question(packet)
                            query["peer"] = list(peer)
                            query["observedMonotonic"] = time.monotonic()
                            result["queries"].append(query)
                            if query["host"] == host and query["class"] == 1 and query["type"] in (1, 28):
                                first_query = first_query or time.monotonic()
                        except socket.timeout:
                            pass
                        if first_query and not marker.exists() and time.monotonic() - first_query >= 0.25:
                            pending = marker.with_suffix(".pending")
                            pending.write_text(host + "\n", encoding="ascii")
                            pending.replace(marker)
                    if process.poll() is None:
                        result["timeout"] = True
                        process.kill()
                    result["exitCode"] = process.wait(timeout=5)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
            result["native"] = native_receipt(log)
            result["processReaped"] = process.poll() is not None
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 53))
            result["dnsSocketReleased"] = True
    result["temporaryCleanup"] = not Path(result["temporaryRoot"]).exists()
    result["passed"] = (result["exitCode"] == 0 and result["native"].get("status") == "passed"
                        and bool(result["queries"]) and result["dnsSocketReleased"]
                        and result["temporaryCleanup"] and result["processReaped"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--mode", choices=("system-trust", "blocked-dns"), required=True)
    args = parser.parse_args()
    args.driver, args.logs = args.driver.resolve(), args.logs.resolve()
    args.logs.mkdir(parents=True, exist_ok=False)
    result = {"kind": "cpp-a06-network", "mode": args.mode, "platform": platform.platform(),
              "driverSha256": hashlib.sha256(args.driver.read_bytes()).hexdigest()}
    try:
        result.update(system_trust(args) if args.mode == "system-trust" else blocked_dns(args))
    except Exception as error:
        result.update({"passed": False, "error": str(error)})
    (args.logs / "receipt.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
