"""Repeatable named finite scenarios using the ordinary console and shared checks.

Quick is motor read-only. Motion, settings and persistence require explicit
selection. No all-feature sweep, implicit recovery, retry or motor rebinding.
"""
import argparse
import json
from pathlib import Path

import bench_motion as motion
import bench_probe as probe
from bench_session import checked, run_recorded

SCENARIOS = {
    'quick': 'ten probes by default; typed baseline and passive health; no motor writes',
    'observations': 'finite explicit health refreshes with per-field freshness checks',
    'load': 'finite read-only probes with explicitly selected host workload and measured execution',
    'configuration': 'typed configuration and optional-I/O reads only',
    'native-read': 'one selected typed native group/record; no writes or segment trigger',
    'settings': 'one explicit input-filter or lock-delay increment/decrement; readback and restore once',
    'position': 'one qualified finite100-step position phase; same-session stop and profile restoration',
    'actions': 'explicit stopped-state enable/release cycle from enabled state; ends enabled',
    'angle': 'one explicit wrapped-angle request; existing preparation/gates; stop and profile restoration',
    'velocity': 'one explicitly bounded velocity request and stop; native parameter restoration not implemented',
    'stop': 'normal/direct stop during one finite250-step move; same-session restoration',
    'homing-prerequisites': 'capabilities, wiring, configuration and I/O evidence; no home execution',
    'discovery': 'bounded explicit scan; COMPLETE and host restoration required, no recovery',
    'persistence-plan': 'local plan only; no durability claim or nonvolatile write',
    'persistence': 'one explicit save/factory-restore through existing backup/recommissioning gates',
}


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM13')
    parser.add_argument('--out', type=Path, help='exclusive new summary/JSONL prefix')
    parser.add_argument('--scenario', choices=SCENARIOS, default='quick')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--address', type=int, default=1)
    parser.add_argument('--count', type=int, help='quick/load/observations only; default ten')
    parser.add_argument('--interval', type=float, help='quick/load/observations only; default 0.05s')
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--debug', choices=('off', 'raw', 'decoded'))
    parser.add_argument('--firmware', type=Path, help='image provenance; no upload or flash attestation')
    parser.add_argument('--load', nargs=3, type=int, metavar=('WORK_US', 'OWNER_DELAY_US', 'CONSOLE_BYTES'))
    parser.add_argument('--phase', choices=('forward', 'absolute', 'return'))
    parser.add_argument('--stop-policy', choices=('normal', 'direct'))
    parser.add_argument('--arguments', nargs='+', help='exact public angle/velocity grammar tokens')
    parser.add_argument('--native', choices=('driver', 'io', 'control', 'segment', 'tuning'))
    parser.add_argument('--native-args', nargs='+')
    parser.add_argument('--setting', choices=('input-filter', 'lock-delay'))
    parser.add_argument('--value', type=int)
    parser.add_argument('--discovery', nargs='*', default=None)
    parser.add_argument('--persistence-kind', choices=('save', 'factory-restore'))
    parser.add_argument('--evidence-records', type=int, default=20000)
    parser.add_argument('--evidence-mib', type=int, default=16)
    args = parser.parse_args(argv)
    if args.list:
        return args
    repeatable = args.scenario in ('quick', 'load', 'observations')
    if not repeatable and (args.count is not None or args.interval is not None):
        parser.error('--count/--interval apply only to quick, load or observations')
    if args.count is None:
        args.count = 10 if repeatable else 1
    if args.interval is None:
        args.interval = .05 if repeatable else 0
    if not 1 <= args.address <= 247 or not 1 <= args.count <= 100:
        parser.error('address1..247 and finite count1..100 required')
    if not 0 <= args.interval <= 1 or not 0 < args.timeout <= 60:
        parser.error('finite interval0..1s and timeout0..60s required')
    if not 2 <= args.evidence_records <= 200000 or not 1 <= args.evidence_mib <= 64:
        parser.error('finite evidence record/byte limits required')
    if args.load is not None:
        if args.scenario != 'load': parser.error('--load requires the load scenario')
        probe.check_load(tuple(args.load))
    elif args.scenario == 'load': parser.error('load requires all three explicit workload settings')
    for option, scopes in ((args.phase, ('position',)), (args.arguments, ('angle', 'velocity')),
                          (args.native, ('native-read',)), (args.native_args, ('native-read',)),
                          (args.setting, ('settings',)), (args.value, ('settings',)),
                          (args.discovery, ('discovery',)),
                          (args.persistence_kind, ('persistence', 'persistence-plan'))):
        if option is not None and args.scenario not in scopes:
            parser.error('scenario-specific arguments cannot enter another scenario')
    if args.stop_policy is not None and args.scenario not in ('angle', 'velocity', 'stop'):
        parser.error('--stop-policy requires angle, velocity or stop')
    if args.scenario in ('position', 'actions', 'stop', 'angle') and args.address != 1:
        parser.error('qualified fixed bench phases select node1 only')
    if args.scenario in ('angle', 'velocity'):
        if not args.arguments: parser.error('explicit public motion arguments required')
        tokens = tuple(args.arguments)
        parsed = probe.move_arguments('angle', tokens) if args.scenario == 'angle' else probe.velocity_arguments(tokens)
        if args.scenario == 'velocity' and args.timeout * 1000000 <= parsed['duration_us']:
            parser.error('velocity timeout must include finite duration and explicit stop time')
    if args.scenario == 'native-read':
        if args.native is None: parser.error('one explicit native group required')
        tokens = tuple(args.native_args or ('read',))
        candidate = probe.tuning_arguments(tokens)[1] if args.native == 'tuning' else \
            probe.segment_arguments(tokens)[2] if args.native == 'segment' else \
            probe.driver_arguments(tokens, 'control_settings' if args.native == 'control' else 'io' if args.native == 'io' else 'drive')
        if candidate: parser.error('native-read never writes; select a bounded settings experiment explicitly')
    if args.scenario == 'settings' and (args.setting is None or args.value is None or not 0 <= args.value <= 65535):
        parser.error('settings requires a documented selected field and native uint16 candidate')
    if args.scenario == 'discovery':
        request = probe.discovery_arguments(tuple(args.discovery or ()))
        if request['action'] != 'begin': parser.error('discovery scenario requires a finite scan, not a local control')
    if args.scenario in ('persistence', 'persistence-plan') and args.persistence_kind is None:
        parser.error('explicit save/factory-restore kind required')
    if not args.plan_only and args.out is None:
        parser.error('--out is required for exclusive evidence before port access')
    return args


def _same_configuration(before, after):
    if before['config']['raw'] != after['config']['raw']:
        raise probe.BenchError('observed drive configuration changed; no implicit repair')


def _setting(console, args, record):
    group = 'tuning' if args.setting == 'input-filter' else 'control'
    prefix = ('filters',) if group == 'tuning' else ()
    fields = probe.TUNING_FIELDS['filters'] if group == 'tuning' else probe.CONTROL_FIELDS
    before = checked(console, group, driver_args=prefix + ('read',), address=args.address, timeout_s=args.timeout)
    if group == 'tuning':
        # Observe changed settings before rebuilding dependent I/O evidence.
        record['setting_io'] = checked(console, 'io', driver_args=('read',),
                                       address=args.address, timeout_s=args.timeout)
    original = before['observation']['raw'][fields.index(args.setting)]
    if abs(args.value - original) != 1:
        raise probe.BenchError('this finite settings experiment permits one native-unit change only; no-op does not qualify work')
    requested = prefix + ('set', args.setting, str(args.value))
    restore = prefix + ('set', args.setting, str(original))
    record['setting'] = dict(before=before, requested=requested, restoration=restore, restored=False)
    console.emit('settings_plan', **record['setting'])
    probe.observe_stopped(console, address=args.address, timeout_s=args.timeout)
    applied = False
    try:
        result = checked(console, group, driver_args=requested, address=args.address, timeout_s=args.timeout)
        applied = True  # Checked terminal includes exact stored readback, not just echo.
        record['setting']['applied'] = result
    finally:
        # Only a known applied update can enter its preplanned distinct restore.
        # Failed/uncertain update retains backup and interlock; no write replay.
        if applied and console.synchronized:
            # Effects invalidate preparation caches. Re-establish the same
            # checked prerequisites before the distinct restoration request.
            record['setting']['restore_baseline'] = checked(console, group, driver_args=prefix + ('read',), address=args.address, timeout_s=args.timeout)
            record['setting']['restore_identity'] = checked(console, 'read-identity', address=args.address, timeout_s=args.timeout)
            record['setting']['restore_configuration'] = checked(console, 'read-config', address=args.address, timeout_s=args.timeout)
            if group == 'tuning':
                record['setting']['restore_io'] = checked(console, 'io', driver_args=('read',), address=args.address, timeout_s=args.timeout)
            probe.observe_stopped(console, address=args.address, timeout_s=args.timeout)
            record['setting']['restore_result'] = checked(console, group, driver_args=restore, address=args.address, timeout_s=args.timeout)
            after = checked(console, group, driver_args=prefix + ('read',), address=args.address, timeout_s=args.timeout)
            record['setting']['after'] = after
            if before['observation']['raw'] != after['observation']['raw']:
                raise probe.BenchError('exact native settings restoration disagrees')
            record['setting']['restored'] = True


def _angle(console, args, record):
    record['profile_before'] = motion.profile_snapshot(console, timeout_s=args.timeout)
    failure = None
    try:
        probe.move_campaign(console, command='move-angle', move_args=tuple(args.arguments),
                            cleanup_stop=args.stop_policy or 'normal', timeout_s=args.timeout, address=args.address)
    except BaseException as error:
        failure = error
        raise
    finally:
        summaries = record.get('campaigns', [])
        settled = next((s for s in reversed(summaries) if s.get('mode') == 'move-angle'), {})
        if console.synchronized and settled.get('cleanup') == 'drive_reported_standstill':
            try:
                record['profile_restoration'] = motion.restore_profile(console, timeout_s=args.timeout)
            except BaseException as error:
                record['profile_restoration'] = {'known': False, 'error': str(error) or 'interrupted'}
                if failure is None:
                    raise
        else:
            record['profile_restoration'] = {'known': False, 'reason': 'no qualified standstill or no admitted move; no restore write'}


def run_scenario(console, args, record):
    name = args.scenario
    if name not in SCENARIOS:
        raise ValueError('unknown finite scenario: ' + str(name))
    if name == 'quick':
        if any(record['before']['load'][key] for key in probe.LOAD_FIELDS):
            raise probe.BenchError('quick regression expects unloaded host; select load explicitly')
        probe.campaign(console, 'stress', count=args.count,
                       interval_s=args.interval, timeout_s=args.timeout, address=args.address,
                       load=None)
    elif name == 'load':
        original = tuple(record['before']['load'][key] for key in probe.LOAD_FIELDS)
        failure = None
        try:
            probe.campaign(console, 'load', count=args.count, interval_s=args.interval,
                           timeout_s=args.timeout, address=args.address, load=tuple(args.load))
        except BaseException as error:
            failure = error
            raise
        finally:
            record['load_cleanup'] = {'original': original, 'restored': False}
            if console.synchronized:
                try:
                    # Read the measured window before resetting it on restoration.
                    record['load_window'] = checked(console, 'load', timeout_s=args.timeout)
                    record['load_cleanup']['result'] = checked(console, 'load', load=original, timeout_s=args.timeout)
                    record['load_cleanup']['restored'] = True
                except BaseException as error:
                    record['load_cleanup']['error'] = str(error) or 'interrupted'
                    if failure is None: raise
    elif name == 'observations':
        probe.state_health_campaign(console, count=args.count, interval_s=args.interval,
                                    timeout_s=args.timeout, address=args.address)
    elif name == 'configuration':
        probe.typed_read_campaign(console, kind='config', timeout_s=args.timeout, address=args.address)
        probe.driver_read_campaign(console, command='io', driver_args=('read',), timeout_s=args.timeout, address=args.address)
    elif name == 'native-read':
        probe.driver_read_campaign(console, command=args.native, driver_args=tuple(args.native_args or ('read',)),
                                   timeout_s=args.timeout, address=args.address)
    elif name == 'settings': _setting(console, args, record)
    elif name in ('position', 'actions', 'stop'):
        record['motion'] = {}
        phase = (args.phase or 'forward') if name == 'position' else 'actions' if name == 'actions' else 'stop-' + (args.stop_policy or 'normal')
        motion.run_phase(console, phase, record['motion'])
    elif name == 'angle': _angle(console, args, record)
    elif name == 'velocity':
        record['native_parameter_restoration'] = {'known': False, 'owning_gap': '11', 'reason': 'standalone native velocity parameter reads unavailable'}
        probe.velocity_campaign(console, velocity_args=tuple(args.arguments), cleanup_stop=args.stop_policy or 'normal',
                                timeout_s=args.timeout, address=args.address)
    elif name == 'homing-prerequisites':
        record['homing'] = dict(capabilities=checked(console, 'caps', timeout_s=args.timeout),
                               wiring=checked(console, 'wiring', timeout_s=args.timeout),
                               inputs=checked(console, 'io', driver_args=('read',), address=args.address, timeout_s=args.timeout),
                               execution_attempted=False, readiness='method-specific admission still required',
                               native_parameter_restoration='standalone reads remain owning gap14')
    elif name == 'discovery':
        record['scan'] = probe.discovery_campaign(console, tokens=tuple(args.discovery or ()), timeout_s=args.timeout)
    elif name in ('persistence', 'persistence-plan'):
        record['persistence'] = probe.persistence_campaign(console, kind=args.persistence_kind, timeout_s=args.timeout,
                                execute=name == 'persistence', verify=name == 'persistence', finish=name == 'persistence')
        record['durability_verified'] = False  # No physical restart supplied by this tool.
    # Settings baseline is refreshed only after successful scenario cleanup.
    if name != 'discovery':
        after = checked(console, 'read-config', address=args.address, timeout_s=args.timeout)
        if name == 'persistence' and args.persistence_kind == 'factory-restore':
            record['configuration_after_action'] = after
        else:
            _same_configuration(record['before']['config'], after)
    record['workload_verified'] = True


def main(argv=None):
    try:
        args = arguments(argv)
        if args.list:
            print(json.dumps(SCENARIOS, indent=2)); return 0
        inputs = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
        print(json.dumps({'scenario': args.scenario, 'effect': SCENARIOS[args.scenario], 'inputs': inputs}, indent=2), flush=True)
        if args.plan_only: return 0
        run_recorded(port=args.port, out=args.out, scenario=args.scenario, inputs=inputs,
                     run=lambda console, record: run_scenario(console, args, record),
                     address=args.address, timeout_s=args.timeout, debug=args.debug,
                     firmware=args.firmware, refresh=args.scenario != 'discovery',
                     max_records=args.evidence_records, max_bytes=args.evidence_mib * 1024 * 1024)
    except (Exception, KeyboardInterrupt) as error:
        print('Scenario FAILED: ' + (str(error) or 'interrupted')); return 1
    print('Scenario completed; exact evidence: ' + str(args.out)); return 0


if __name__ == '__main__':
    raise SystemExit(main())
