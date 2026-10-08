"""原 A25：真实档案 Demo 的创建 201 丢回、进程结束与原意图冷恢复。"""
import argparse
from contextlib import ExitStack
from datetime import datetime
import hashlib
import json
from pathlib import Path
import time
from urllib.parse import parse_qs, urlsplit

from demos import Scenario, executable, one_shot
from run import Child, SessionCreateLossProxy, owned_temp, sha256


def offload(scenario, info_path, info, proxy):
    common = ['--base', proxy.base_url, '--family', 'sdk2-offload-v1', '--timeout', str(scenario.args.timeout)]
    intent = scenario.root / 'intents/session.json'
    prepared = one_shot(scenario, common, 'chat', '--prepare-create', intent,
                        '--request-id', 'cpp-original-cold-session')
    if not any('no session was created' in line for line in prepared) or proxy.records:
        raise RuntimeError('preparation must commit original intent without any network request')
    raw = intent.read_text(encoding='utf-8')
    original = json.loads(raw)
    body_start = raw.index('"body":') + len('"body":')
    _, body_end = json.JSONDecoder().raw_decode(raw, body_start)
    body_hash = hashlib.sha256(raw[body_start:body_end].encode()).hexdigest()
    original_hash = sha256(intent)
    # 不允许覆盖原意图、CLI 偷换原键或转发到另一 origin。
    rejected = [
        [*common, '--prepare-create', intent, '--request-id', 'replacement-session'],
        [*common, '--create-intent', intent, '--request-id', 'replacement-session'],
        ['--base', info['baseURL'], '--family', 'sdk2-offload-v1', '--create-intent', intent],
    ]
    for index, flags in enumerate(rejected):
        scenario.run([scenario.args.demos / executable('tansr-chat'), *flags], f'intent-rejected-{index}', 'nonzero')
    # 仅修改本测试私有合成 scope 文件，保留原文件 ACL；生产 token 不参与。
    scope_file = scenario.root / 'credentials/scope.json'
    original_scope = scope_file.read_text(encoding='utf-8')
    changed_scope = json.loads(original_scope)
    changed_scope['endUserId'] = 'go-other-user'
    try:
        scope_file.write_text(json.dumps(changed_scope), encoding='utf-8')
        scenario.run([scenario.args.demos / executable('tansr-chat'), *common,
                      '--create-intent', intent], 'different-current-owner', 'nonzero')
    finally:
        scope_file.write_text(original_scope, encoding='utf-8')
    if proxy.records or sha256(intent) != original_hash:
        raise RuntimeError('rejected overrides changed the intent or reached the server')
    expired = scenario.root / 'intents/expired.json'
    one_shot(scenario, ['--base', proxy.base_url, '--family', 'sdk2-offload-v1', '--timeout', '1'],
             'chat', '--prepare-create', expired, '--request-id', 'cpp-expired-session')
    time.sleep(1.1)
    scenario.run([scenario.args.demos / executable('tansr-chat'), *common,
                  '--create-intent', expired], 'expired-intent', 'nonzero')
    if proxy.records:
        raise RuntimeError('expired original intent reached the server')
    # 合成损坏复用已安全创建文件，仅测 fail-closed，不改原有效意图。
    expired.write_text('{"format":"unsupported-session-intent"}', encoding='utf-8')
    scenario.run([scenario.args.demos / executable('tansr-chat'), *common,
                  '--create-intent', expired], 'invalid-intent-format', 'nonzero')
    if proxy.records:
        raise RuntimeError('malformed original intent reached the server')
    lost = scenario.run([scenario.args.demos / executable('tansr-chat'), *common,
                         '--create-intent', intent], 'offload-create-lost', 'nonzero')
    if any(line.startswith('session: ') for line in lost) or proxy.lost != 1:
        raise RuntimeError('offload lost creation must fail after real 201 acceptance')
    dropped = [row for row in proxy.records if row.get('responseDropped')]
    if len(dropped) != 1 or not dropped[0].get('upstreamCreatedIdentity'):
        raise RuntimeError('fault injector did not observe original accepted session identity')
    original_session = dropped[0]['upstreamCreatedIdentity']
    before = scenario.seed(info_path, 'sdk2-offload-v1', 'archive-facts', original_session)
    recovered = one_shot(scenario, common, 'chat', '--create-intent', intent)
    if recovered.count('session: ' + original_session) != 1 or any('[turn completed]' in line for line in recovered):
        raise RuntimeError('new process did not recover the same session without starting a turn')
    after = scenario.seed(info_path, 'sdk2-offload-v1', 'archive-facts', original_session)
    if any(before[key] != after[key] for key in ('target', 'binding', 'sourceStatus')):
        raise RuntimeError('original creation replay changed automatic binding or Source facts')
    if sha256(intent) != original_hash:
        raise RuntimeError('replay changed the original body, key, deadline or owner')
    writes = [row for row in proxy.records if row['method'] == 'POST']
    if (len(writes) != 2 or writes[0]['upstreamStatus'] != 201
            or writes[1]['upstreamStatus'] not in (200, 201)
            or any(row['path'] != '/api/sessions' or row['requestSha256'] != body_hash
                   or row.get('requestKey') != original['write']['requestKey'] for row in writes)
            or writes[0].get('deadline') != writes[1].get('deadline') or not writes[0].get('deadline')):
        raise RuntimeError('offload replay did not retain exact original request bytes and headers')
    transmitted = datetime.fromisoformat(writes[0]['deadline'].replace('Z', '+00:00'))
    if round(transmitted.timestamp() * 1000) != original['write']['deadlineMs']:
        raise RuntimeError('offload original absolute deadline was replaced')
    if any('proxyError' in row for row in proxy.records):
        raise RuntimeError('fault injector failed before proving acceptance')
    scenario.result.update({'originalIntentSha256': original_hash, 'originalBodySha256': body_hash,
        'originalRequestId': original['body']['requestId'], 'originalWrite': original['write'],
        'sessionId': original_session, 'originalArchiveFacts': before, 'replayedArchiveFacts': after,
        'checks': ['private-atomic-intent-before-any-network', 'overwrite-key-origin-owner-expiry-format-rejected-before-network',
                   'actual-session-201-response-discarded', 'failed-create-process-exited',
                   'new-demo-process-exact-session-replay', 'same-session-auto-binding-source-status',
                   'unchanged-original-body-key-deadline-owner', 'no-new-turn-on-creation-replay']})


def exercise(args, result):
    fixture = proxy = scenario = None
    with owned_temp(result) as directory, ExitStack() as stack:
        root = Path(directory)
        try:
            log = stack.enter_context((args.logs / 'serve.log').open('w', encoding='utf-8'))
            fixture = Child([str(args.node), str(args.fixture), '.', str(root), args.mode], root, log)
            info = fixture.prefixed('TANSR_GO_FIXTURE ', 30)
            if info.get('manifestRevision') != 7 or (args.family == 'sdk1' and not info.get('manualBinding')):
                raise RuntimeError('fixture must explicitly expose an unbound manual Source target')
            info_path = root / 'fixture-info.json'
            info_path.write_text(json.dumps(info), encoding='utf-8')
            client = root / 'client'
            client.mkdir(mode=0o700)
            scenario = Scenario(args, result, stack, client, 'create-loss-' + args.family)
            scenario.seed(info_path, args.family, 'credentials')
            if args.family == 'sdk2-offload-v1':
                proxy = SessionCreateLossProxy(info['baseURL'])
                offload(scenario, info_path, info, proxy)
                result['proxy'] = proxy.finish()
                proxy = None
                result['serveExitCode'] = fixture.finish(graceful=True)
                fixture = None
                if result['serveExitCode'] != 0:
                    raise RuntimeError('Serve cleanup failed')
                result['status'] = 'passed'
                return
            proxy = SessionCreateLossProxy(info['baseURL'], create_path='/api/archive/bindings', identity_field='bindingId')
            common = ['--base', proxy.base_url, '--family', args.family, '--timeout', str(args.timeout)]
            intent = client / 'intents/create.json'
            prepared = one_shot(scenario, common, 'archive', '--mode', 'prepare-create',
                '--session', info['sessionId'], '--source', info['sourceId'],
                '--request-id', 'cpp-original-cold-create', '--intent', intent)
            if not any('no binding was created' in line for line in prepared):
                raise RuntimeError('preparation did not expose its nonmutating boundary')
            original_bytes = intent.read_text(encoding='utf-8')
            original = json.loads(original_bytes)
            body_start = original_bytes.index('"body":') + len('"body":')
            saved_body, body_end = json.JSONDecoder().raw_decode(original_bytes, body_start)
            if saved_body != original['body']:
                raise RuntimeError('original intent body extraction mismatch')
            body_hash = hashlib.sha256(original_bytes[body_start:body_end].encode()).hexdigest()
            hashes = {str(path.name): sha256(path) for path in (intent, Path(str(intent) + '.owner'))}
            failed = scenario.run([args.demos / executable('tansr-archive'), *common,
                '--mode', 'create', '--intent', intent], 'create-lost', 'nonzero')
            if any(line.startswith('binding: ') for line in failed) or proxy.lost != 1:
                raise RuntimeError('lost create must fail after exactly one real accepted response loss')
            # 前一进程已退出；新进程只能从私有磁盘原意图获得 request/epoch/body/deadline。
            recovered = one_shot(scenario, common, 'archive', '--mode', 'creation-status', '--intent', intent)
            states = [line for line in recovered if line.startswith('creation state: completed; binding: ')]
            if len(states) != 1:
                raise RuntimeError('cold original operation query did not confirm creation')
            binding = states[0].split('; binding: ', 1)[1]
            replayed = one_shot(scenario, common, 'archive', '--mode', 'create', '--intent', intent)
            if replayed.count('binding: ' + binding) != 1:
                raise RuntimeError('original replay did not recover the same binding')
            if any(sha256(path) != hashes[path.name] for path in (intent, Path(str(intent) + '.owner'))):
                raise RuntimeError('recovery changed the original intent, deadline or owner')
            evidence = proxy.finish()
            proxy = None
            writes = [row for row in evidence['requests'] if row['method'] == 'POST']
            reads = [row for row in evidence['requests'] if row['method'] == 'GET']
            if (evidence['responsesDropped'] != 1 or len(writes) != 2
                    or any(row['path'] != '/api/archive/bindings' or row['upstreamStatus'] != 201
                           or row['requestSha256'] != body_hash for row in writes)
                    or any('proxyError' in row for row in evidence['requests'])):
                raise RuntimeError('real writes replaced original bytes or unexpected mutations occurred')
            request = original['body']['request']
            queries = [parse_qs(urlsplit(row['path']).query) for row in reads]
            if not any(query.get('requestId') == [request['requestId']]
                       and query.get('operationEpoch') == [request['operationEpoch']] for query in queries):
                raise RuntimeError('cold query did not use the original operation identity')
            result.update({'originalIntentSha256': hashes, 'originalBodySha256': body_hash,
                'originalRequest': request, 'originalDeadlineMs': original['deadlineMs'],
                'bindingId': binding, 'proxy': evidence,
                'checks': ['durable-original-intent-before-send', 'actual-201-response-discarded',
                           'failed-create-process-exited', 'new-demo-process-original-query',
                           'new-demo-process-exact-replay-same-binding', 'unchanged-body-epoch-deadline-owner']})
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
            if proxy:
                result['proxy'] = proxy.finish()
            if fixture:
                try:
                    fixture.finish(graceful=True)
                except Exception as error:
                    result.update({'status': 'failed', 'cleanupError': str(error)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('fixture', 'provenance', 'node', 'build', 'logs'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--family', choices=('all', 'sdk1', 'sdk2-offload-v1'), default='all')
    parser.add_argument('--mode', help='Explicit fixture mode override; requires one --family')
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    if args.mode and args.family == 'all':
        parser.error('--mode requires one explicit --family')
    for name in ('fixture', 'provenance', 'node', 'build', 'logs'):
        setattr(args, name, getattr(args, name).resolve())
    args.demos = args.build / 'demo'
    provenance = json.loads(args.provenance.read_text(encoding='utf-8'))
    if sha256(args.fixture) != provenance['output']['sha256']:
        raise ValueError('fixture provenance does not match')
    args.logs.mkdir(parents=True, exist_ok=False)
    families = ['sdk1', 'sdk2-offload-v1'] if args.family == 'all' else [args.family]
    results = []
    for family in families:
        child_args = argparse.Namespace(**vars(args))
        child_args.family = family
        child_args.mode = args.mode or ('archive-manual' if family == 'sdk1' else 'archive-offload')
        child_args.logs = args.logs / family
        child_args.logs.mkdir()
        result = {'status': 'running', 'family': family, 'mode': child_args.mode}
        exercise(child_args, result)
        results.append(result)
    record = {'fixtureSha256': sha256(args.fixture), 'provenance': provenance,
              'archiveSha256': sha256(args.demos / executable('tansr-archive')),
              'chatSha256': sha256(args.demos / executable('tansr-chat')),
              'seedSha256': sha256(args.build / executable('serve_demo_seed')),
              'results': results, 'total': len(results), 'passed': sum(item['status'] == 'passed' for item in results), 'skipped': 0}
    (args.logs / 'receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps([{key: value for key, value in item.items() if key not in ('processes', 'proxy')} for item in results]))
    return 0 if record['passed'] == record['total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
