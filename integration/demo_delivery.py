"""补原 A33：真实档案 Demo 显式 rebase，真实工具 Demo 的首块/业务完成时序。"""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import re

from demo_chat_controls import until
from demos import Scenario, chat_turn, executable, one_shot
from run import Child, owned_temp, sha256


def rebase(scenario, info_path, info, family, common):
    session = chat_turn(scenario, common)
    original = scenario.seed(info_path, family, 'target', session)['target']
    staged = scenario.seed(info_path, family, 'stage-pending', session)['staged']
    if staged['ackSent'] or staged['records'] < 1:
        raise RuntimeError('rebase must start from an original unaccepted durable ACK')
    store = scenario.root / 'archive/history.bin'
    retained = sha256(store)
    chat_turn(scenario, common, session)
    current = scenario.seed(info_path, family, 'target', session)['target']
    if current['revision'] == original['revision']:
        raise RuntimeError('second real turn did not advance binding revision')
    flags = ['--binding', original['bindingId'], '--file', store, '--key-id', 'cpp-demo-test-key']
    scenario.run([scenario.args.demos / executable('tansr-archive'), *common,
                  '--mode', 'sync', *flags], 'expected-stale-sync', 'nonzero')
    # Child 将 stderr 直接写日志；失败诊断不会出现在 stdout 队列。
    failed = (scenario.args.logs / f'{scenario.label}-{scenario.index:02d}-expected-stale-sync.log').read_text(encoding='utf-8')
    if 'status=412' not in failed or 'wire=precondition_failed' not in failed:
        raise RuntimeError('ordinary sync did not expose the exact stale revision error')
    if sha256(store) != retained:
        raise RuntimeError('failed ordinary sync changed the original pending archive')
    request_id = 'cpp-demo-rebase-original'
    recovered = one_shot(scenario, common, 'archive', '--mode', 'recover', *flags,
                         '--request-id', request_id)
    if not any('pending ACK confirmed' in line for line in recovered) or any('archive synchronized' in line for line in recovered):
        raise RuntimeError('explicit recover did not preserve its partial recovery boundary')
    receipt = scenario.seed(info_path, family, 'ack-status', session, request_id)['receipt']
    if receipt['request']['requestId'] != request_id or receipt['state'] != 'completed':
        raise RuntimeError('actual rebase operation did not confirm the original recovery request')
    synchronized = one_shot(scenario, common, 'archive', '--mode', 'sync', *flags)
    if not any('verified records=1' in line for line in synchronized) or not any('archive synchronized' in line for line in synchronized):
        raise RuntimeError('rebase was incorrectly treated as complete archive synchronization')
    scenario.result.update({'originalPending': staged, 'originalRevision': original['revision'],
        'advancedRevision': current['revision'], 'rebaseReceipt': receipt,
        'checks': ['actual-stale-412', 'ordinary-sync-retains-original-pending', 'real-demo-explicit-rebase',
                   'original-recovery-operation-confirmed', 'remaining-page-sync-after-rebase']})


def output(scenario, info_path, info, family, common):
    tools = scenario.start([scenario.args.demos / executable('tansr-tools'), *common,
        '--executor', info['executorId'], '--journal', scenario.root / 'journal',
        '--run-once', '--require-output'], 'tools')
    session = scenario.wait(tools, lambda line: line.startswith('session: '))[9:]
    scenario.wait(tools, lambda line: line.startswith('ready: '))
    observer = scenario.start([scenario.args.build / executable('serve_demo_seed'), info_path,
        family, scenario.root, 'observe-output', session], 'readonly-output-observer')
    observed = []
    until(scenario, observer, observed, lambda line: line == 'TANSR_CPP_DEMO_OUTPUT_READY')
    chat = scenario.start([scenario.args.demos / executable('tansr-chat'), *common,
                           '--attach', session], 'chat')
    scenario.wait(chat, lambda line: line == 'session: ' + session)
    chat.send('GO-TOOL query DEMO-001')
    tickets, answer = set(), False
    while True:
        line = scenario.line(chat)
        match = re.match(r'\[permission ([^\] ]+)\] (.*)', line)
        if match:
            if 'DemoOrderStatus' not in match[2] or match[1] in tickets:
                raise RuntimeError('unexpected permission request')
            tickets.add(match[1])
            chat.send('/allow ' + match[1])
        if 'go-tool-complete' in line:
            answer = True
        if line == '[turn completed]':
            if not answer or len(tickets) != 1:
                raise RuntimeError('actual approved tool result was not consumed')
            break
    chat.send('/quit')
    scenario.finish(chat)
    first = until(scenario, observer, observed, lambda line: line.startswith('TANSR_CPP_DEMO_OUTPUT_FIRST '))
    final = until(scenario, observer, observed, lambda line: line.startswith('TANSR_CPP_DEMO_SEED '))
    result = json.loads(final.removeprefix('TANSR_CPP_DEMO_SEED '))
    if result['status'] != 'passed' or result['output']['first'] != json.loads(first.removeprefix('TANSR_CPP_DEMO_OUTPUT_FIRST ')):
        raise RuntimeError('readonly observer lost original first-block evidence')
    scenario.finish(observer)
    business = scenario.wait(tools, lambda line: line.startswith('business receipt state: '))
    if 'output confirmed=true' not in business:
        raise RuntimeError('Demo did not confirm the output seal')
    scenario.finish(tools)
    scenario.result.update({'output': result['output'], 'approvedObservedTickets': len(tickets),
        'checks': ['real-tools-and-chat-processes', 'readonly-observer-does-not-execute',
                   'first-21-bytes-while-business-pending', 'actual-44-byte-seal-and-digest',
                   'actual-business-result-consumed']})


def run_case(args, kind, family):
    result = {'kind': kind, 'family': family, 'status': 'running'}
    fixture = scenario = None
    label = kind + '-' + family
    with owned_temp(result) as directory, ExitStack() as stack:
        root = Path(directory)
        try:
            log = stack.enter_context((args.logs / (label + '-serve.log')).open('w', encoding='utf-8'))
            mode = 'execution-demo' if kind == 'output' else ('archive' if family == 'sdk1' else 'archive-offload')
            fixture = Child([str(args.node), str(args.fixture), '.', str(root), mode], root, log)
            info = fixture.prefixed('TANSR_GO_FIXTURE ', 30)
            if info.get('manifestRevision') != 7:
                raise RuntimeError('fixture revision changed')
            info_path = root / 'fixture-info.json'
            info_path.write_text(json.dumps(info), encoding='utf-8')
            client_root = root / 'client'
            client_root.mkdir(mode=0o700)
            scenario = Scenario(args, result, stack, client_root, label)
            scenario.fixture = fixture
            scenario.seed(info_path, family, 'credentials')
            common = ['--base', info['baseURL'], '--family', family, '--timeout', str(args.timeout)]
            (rebase if kind == 'rebase' else output)(scenario, info_path, info, family, common)
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
    parser.add_argument('--timeout', type=int, default=90)
    parser.add_argument('--case', choices=('all', 'rebase', 'output'), default='all')
    args = parser.parse_args()
    for name in ('fixture', 'provenance', 'node', 'build', 'logs'):
        setattr(args, name, getattr(args, name).resolve())
    args.demos = args.build / 'demo'
    provenance = json.loads(args.provenance.read_text(encoding='utf-8'))
    if sha256(args.fixture) != provenance['output']['sha256']:
        raise ValueError('fixture provenance does not match')
    args.logs.mkdir(parents=True, exist_ok=False)
    matrix = [('rebase', 'sdk1'), ('rebase', 'sdk2-offload-v1'), ('output', 'sdk1')]
    results = [run_case(args, kind, family) for kind, family in matrix
               if args.case == 'all' or args.case == kind]
    binaries = [args.build / executable('serve_demo_seed')] + [args.demos / executable('tansr-' + name) for name in ('chat', 'tools', 'archive')]
    record = {'fixtureSha256': sha256(args.fixture), 'provenance': provenance,
              'binaries': [{'path': str(path), 'sha256': sha256(path)} for path in binaries],
              'results': results, 'total': len(results), 'passed': sum(r['status'] == 'passed' for r in results), 'skipped': 0}
    (args.logs / 'receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0 if record['passed'] == record['total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
