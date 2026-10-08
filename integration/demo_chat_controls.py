"""只补真实 chat 进程的同轮插入、本地退出与显式中断；不重复完整 Demo 池。"""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import queue
import time

from demos import Scenario, executable
from run import Child, owned_temp, sha256


def until(scenario, child, lines, predicate):
    end = min(scenario.deadline, time.monotonic() + 10)
    while time.monotonic() < end:
        try:
            line = child.lines.get(timeout=0.05)
        except queue.Empty:
            if child.process.poll() is not None and not child.reader.is_alive():
                raise RuntimeError('chat exited before expected control output')
            continue
        lines.append(line)
        if predicate(line):
            return line
    raise TimeoutError('chat control output was not delivered within 10 seconds')


def target(scenario, chat, lines):
    chat.send('/target')
    line = until(scenario, chat, lines, lambda value: '"target":' in value)
    capabilities = json.loads(line[line.index('{'):])
    value = capabilities['target']
    if not value['turnId'] or not value['historyEpoch']:
        raise RuntimeError('current input target missing')
    return value


def open_chat(scenario, common, attached=None):
    child = scenario.start([scenario.args.demos / executable('tansr-chat'), *common,
                            *(['--attach', attached] if attached else [])], 'chat')
    lines = []
    session = until(scenario, child, lines, lambda line: line.startswith('session: '))[9:]
    if attached and session != attached:
        raise RuntimeError('chat attached to a different session')
    return child, session, lines


def stopped_output(scenario, chat, lines, expected):
    while chat.process.poll() is None or chat.reader.is_alive() or not chat.lines.empty():
        if time.monotonic() >= scenario.deadline:
            raise TimeoutError('chat did not exit')
        try:
            lines.append(chat.lines.get(timeout=0.05))
        except queue.Empty:
            pass
    return scenario.finish(chat, expected)


def controls(scenario, info_path, common, kind):
    chat, session, lines = open_chat(scenario, common)
    chat.send('GO-BLOCK')
    until(scenario, chat, lines, lambda line: '[event: turn.started]' in line)
    original_target = target(scenario, chat, lines)
    if kind == 'insert':
        insertion = {'inputId': 'cpp-demo-original-input', 'target': original_target,
                     'content': {'text': 'CPP-DEMO-INSERTED original same-turn requirement'}, 'ack': 'memory'}
        chat.send('/insert ' + json.dumps(insertion))
        until(scenario, chat, lines, lambda line: 'input acknowledged; this is not core consumption' in line)
        if target(scenario, chat, lines) != original_target:
            raise RuntimeError('insertion restarted the original turn')
        scenario.control({'command': 'release-model', 'requestId': 'cpp-demo-release-original-turn'})
        until(scenario, chat, lines, lambda line: line == '[turn completed]')
        chat.send('/quit')
        stopped_output(scenario, chat, lines, 0)
        history = scenario.seed(info_path, 'sdk1', 'history', session)['history']
        if 'CPP-DEMO-INSERTED original same-turn requirement' not in json.dumps(history):
            raise RuntimeError('accepted insertion was not actually consumed into history')
        if sum('[event: turn.started]' in line for line in lines) != 1:
            raise RuntimeError('insertion created another turn')
        scenario.result.update({'checks': ['real-chat-target-delivered', 'same-turn-original-input',
            'accepted-is-not-consumed', 'actual-history-consumption', 'single-turn-completed'],
            'originalInput': insertion, 'historyTotal': history['total']})
    else:
        chat.send('/quit')
        stopped_output(scenario, chat, lines, 0)
        if '[turn completed]' in lines or not any('no remote interruption was requested' in line for line in lines):
            raise RuntimeError('local quit misreported remote completion')
        resumed, attached, observed = open_chat(scenario, common, session)
        if target(scenario, resumed, observed) != original_target:
            raise RuntimeError('local quit changed or interrupted the active turn')
        resumed.send('/interrupt')
        code = stopped_output(scenario, resumed, observed, 'nonzero')
        if (not any('interruption accepted; waiting for actual terminal event' in line for line in observed)
                or not any('[event: turn.aborted]' in line for line in observed)
                or '[turn completed]' in observed):
            raise RuntimeError('explicit interruption lacked its actual aborted terminal')
        scenario.seed(info_path, 'sdk1', 'history', attached)  # seed 等待真实 idle 后查询。
        scenario.result.update({'checks': ['local-quit-preserves-running-turn', 'reattach-original-target',
            'explicit-interrupt-actual-aborted-terminal', 'aborted-is-not-success', 'actual-idle-after-interruption'],
            'originalTarget': original_target, 'interruptedExitCode': code})


def run_case(args, kind):
    result = {'kind': kind, 'family': 'sdk1', 'status': 'running'}
    fixture = scenario = None
    with owned_temp(result) as directory, ExitStack() as stack:
        root = Path(directory)
        try:
            log = stack.enter_context((args.logs / (kind + '-serve.log')).open('w', encoding='utf-8'))
            fixture = Child([str(args.node), str(args.fixture), '.', str(root), 'session'], root, log)
            info = fixture.prefixed('TANSR_GO_FIXTURE ', 30)
            if info.get('manifestRevision') != 7:
                raise RuntimeError('fixture revision changed')
            info_path = root / 'fixture-info.json'
            info_path.write_text(json.dumps(info), encoding='utf-8')
            client_root = root / 'client'
            client_root.mkdir(mode=0o700)
            scenario = Scenario(args, result, stack, client_root, kind)
            scenario.fixture = fixture
            scenario.seed(info_path, 'sdk1', 'credentials')
            controls(scenario, info_path, ['--base', info['baseURL'], '--family', 'sdk1',
                                           '--timeout', str(args.timeout)], kind)
            result['serveExitCode'] = fixture.finish(graceful=True)
            fixture = None
            if result['serveExitCode'] != 0:
                raise RuntimeError('Serve cleanup failed')
            result['status'] = 'passed'
        except Exception as error:
            result.update({'status': 'failed', 'error': str(error)})
        finally:
            if scenario:
                scenario.cleanup()
            if fixture:
                try:
                    fixture.finish(graceful=True)
                except Exception as error:
                    result.update({'status': 'failed', 'cleanupError': str(error)})
    print(json.dumps({key: value for key, value in result.items() if key != 'processes'}), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('fixture', 'provenance', 'node', 'build', 'logs'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    for name in ('fixture', 'provenance', 'node', 'build', 'logs'):
        setattr(args, name, getattr(args, name).resolve())
    args.demos = args.build / 'demo'
    provenance = json.loads(args.provenance.read_text(encoding='utf-8'))
    if sha256(args.fixture) != provenance['output']['sha256']:
        raise ValueError('fixture provenance does not match')
    args.logs.mkdir(parents=True, exist_ok=False)
    results = [run_case(args, kind) for kind in ('insert', 'interrupt')]
    record = {'fixtureSha256': sha256(args.fixture), 'provenance': provenance,
              'chatSha256': sha256(args.demos / executable('tansr-chat')),
              'seedSha256': sha256(args.build / executable('serve_demo_seed')),
              'results': results, 'total': len(results), 'passed': sum(r['status'] == 'passed' for r in results), 'skipped': 0}
    (args.logs / 'receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0 if record['passed'] == record['total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
