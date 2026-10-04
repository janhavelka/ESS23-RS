"""Short, explicitly selected free-shaft experiment phases; never replay writes.

Prepare/print the complete phase before opening COM13. Uses the public console
operations, checked host harness, and the regular firmware image.
No persistence, power control, continuous velocity or endurance loop is included.
"""
import argparse
import json
from pathlib import Path
import time

from bench_probe import BenchError, Console, Evidence, debug_session, open_port


def phase_plan(phase):
    common = ['version', 'host', 'config', 'stats', 'load', 'read-config 1', 'read-state 1']
    steps = {
        'inspect': ['read-identity 1', 'motion-profile read', 'motion-profile inspect'],
        'actions': ['stop normal 1', 'read-state 1', 'stop direct 1', 'read-state 1',
                    'motor-release 1', 'read-state 1', 'stop direct 1',
                    'enable 1', 'read-state 1', 'stop direct 1', 'read-state 1'],
        'forward': ['motion-profile read', 'read-state 1', 'move relative 100 steps native 60 configured 1', 'read-state 1',
                    'stop direct 1', 'read-state 1'],
        'absolute': ['motion-profile read', 'read-state 1', 'move absolute 100 steps native 60 configured 1', 'read-state 1', 'stop direct 1', 'read-state 1'],
        'return': ['motion-profile read', 'read-state 1', 'move absolute 0 steps native 60 configured 1', 'read-state 1',
                   'stop direct 1', 'read-state 1'],
        'stop-normal': ['motion-profile read', 'read-state 1', 'move relative 250 steps native 60 configured 1',
                        'read-state 1 until new running report (at most 32 reads)',
                        'stop normal 1', 'inspect interrupted move result', 'read-state 1'],
        'stop-direct': ['motion-profile read', 'read-state 1', 'move relative 250 steps native 60 configured 1',
                        'read-state 1 until new running report (at most 32 reads)',
                        'stop direct 1', 'inspect interrupted move result', 'read-state 1'],
        'restore': ['stop direct 1', 'read-state 1', 'motion-profile restore',
                    'motion-profile inspect until settled (at most 100 reads)', 'probe 1'],
        'status': ['motion-profile inspect', 'probe 1'],
    }
    cleanup = ['on failed accepted finite move: one direct stop only if no stop was attempted and framing remains synchronized; then read-state 1'] if phase in ('forward', 'absolute', 'return', 'stop-normal', 'stop-direct') else []
    settling = ['after motion/stop: read-state 1 up to ten times, 50ms apart, until not running and raw speed zero'] if phase in ('forward', 'absolute', 'return', 'stop-normal', 'stop-direct') else []
    return common + steps[phase] + settling + cleanup + ['stats', 'load', 'drv', 'memory', 'host']


def run_phase(console, phase, record):
    record['version'] = console.identify()
    with debug_session(console, record.get('debug_mode'), 5) as diagnostics:
        record['debug'] = diagnostics
        return _run_phase(console, phase, record)


def _run_phase(console, phase, record):
    record['events'] = []
    move = None
    stop_attempted = False

    def command(name, **kwargs):
        result = console.command(name, timeout_s=5, **kwargs)
        record['events'].append(dict(command=name, arguments=kwargs, result=result))
        if not result['ok']:
            raise BenchError(f'{name} refused/failed: {result.get("result", result.get("outcome"))}')
        return result

    def state(require_stopped=True):
        result = command('read-state', address=1)
        blocks = {b['block']: b for b in result['state_blocks']}
        if blocks[0]['raw_alarm'] or blocks[0]['alarm_flag']:
            raise BenchError('drive reports an alarm; no subsequent move')
        if require_stopped and (blocks[0]['running'] or blocks[2]['raw_speed'] != 0):
            raise BenchError('drive does not report stopped and zero raw speed')
        return blocks

    def stop(policy):
        nonlocal stop_attempted
        stop_attempted = True
        return command('stop', stop_policy=policy, address=1)

    def settled():
        # ARRIVED/RUNNING and speed are distinct drive reports. Preserve every
        # sample; observe at most ten times without repeating a motion write.
        for _ in range(10):
            observed = state(False)
            if not observed[0]['running'] and observed[2]['raw_speed'] == 0:
                return observed
            time.sleep(.05)
        raise BenchError('zero-speed observation bound exhausted')

    def fixture(action):
        result = command('motion-profile', host_args=(action,))
        for _ in range(100):
            if not result['pending']:
                if not result['session_ok']:
                    raise BenchError('motion snapshot/restoration failed: ' + result['error'])
                return result
            time.sleep(.01)
            result = command('motion-profile', host_args=('inspect',))
        raise BenchError('motion snapshot/restoration observation bound exhausted')

    record['host'] = command('host')
    if record['host']['active'] != dict(baud=115200, format='8N1') or record['host']['blocked']:
        raise BenchError('unexpected host tuple; no automatic reconfiguration')
    record['config'] = command('config')
    record['stats_before'] = command('stats')
    load = command('load')
    if any(load[k] for k in ('workload_us', 'owner_delay_us', 'console_bytes')):
        raise BenchError('functional tests require explicitly unloaded bench')
    record['configuration_before'] = command('read-config', address=1)
    record['state_before'] = state()
    try:
        if phase == 'inspect':
            record['identity'] = command('read-identity', address=1)
            record['profile'] = fixture('read')
        elif phase == 'actions':
            stop('normal'); state()
            stop('direct'); state()
            command('motor-release', address=1)
            if not state()[0]['released']:
                raise BenchError('release transition was not reported')
            stop('direct')  # Reconcile uncertainty through a separate stopped report.
            command('enable', address=1)
            if state()[0]['released']:
                raise BenchError('enable transition was not reported')
            stop('direct'); state()
        elif phase in ('forward', 'absolute', 'return', 'stop-normal', 'stop-direct'):
            # Reading the fixture refreshes current profile/provenance but keeps
            # the original saved words. It sends no setting write.
            record['profile'] = fixture('read')
            before_move = state()
            if not 0 <= before_move[2]['raw_position'] <= 250:
                raise BenchError('experiment requires bounded native position report 0..250')
            value = '0' if phase == 'return' else '100' if phase in ('forward', 'absolute') else '250'
            kind = 'move-absolute' if phase in ('return', 'absolute') else 'move-relative'
            stop_attempted = False
            move = console.begin(kind, move_args=(value, 'steps', 'native', '60', 'configured'), address=1, timeout_s=5)
            record['move_admission'] = dict(accepted=move.accepted, operation_id=move.operation_id)
            if not move.accepted:
                record['move'] = console.wait(move)
                raise BenchError('move refused before transmission')
            if phase.startswith('stop-'):
                activity = None
                for _ in range(32):
                    observed = state(False)
                    if observed[0]['running']:
                        activity = observed
                        break
                    if move.terminal is not None:
                        break
                record['activity_before_stop'] = activity
                if activity is None:
                    raise BenchError('no running report before finite move ended; dynamic stop NOT RUN')
                record['stop_host_started_s'] = time.monotonic()
                stop(phase[5:])
                record['move'] = console.wait(move, release=True)
                if record['move']['ok'] or not record['move']['interrupted_by_stop']:
                    raise BenchError('stop did not retain the interrupted operation')
            else:
                record['move'] = console.wait(move, release=True)
                if not record['move']['ok']:
                    raise BenchError('finite move did not obtain new activity/completion reports')
                record['state_after_move'] = settled()
                stop('direct')
            record['state_after'] = settled()
        elif phase == 'restore':
            stop('direct'); state()
            record['restore'] = fixture('restore')
            if not record['restore']['restored'] or record['restore']['original'] != record['restore']['current']:
                raise BenchError('exact motion profile restoration not established')
            command('probe', address=1)
        else:
            record['profile'] = command('motion-profile', host_args=('inspect',))
            command('probe', address=1)
    except BaseException as phase_error:
        record['phase_error'] = str(phase_error)
        # A different, preplanned stop may be sent once after a failed finite
        # attempt if console framing remains usable. Never repeat a failed stop.
        if move is not None and move.accepted and not stop_attempted and console.synchronized:
            try:
                record['failure_cleanup_stop'] = stop('direct')
                record['failure_cleanup_state'] = state()
            except BaseException as error:
                record['failure_cleanup_error'] = str(error)
        raise
    finally:
        if console.synchronized:
            for name in ('stats', 'load', 'drv', 'memory', 'host'):
                try:
                    record['ending_' + name] = command(name)
                except BaseException as error:
                    record['ending_error'] = str(error)
                    if 'phase_error' not in record:
                        raise
                    break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM13')
    parser.add_argument('--debug', choices=('off', 'raw', 'decoded'), default=None)
    parser.add_argument('--phase', required=True, choices=('inspect', 'actions', 'forward', 'absolute', 'return', 'stop-normal', 'stop-direct', 'restore', 'status'))
    parser.add_argument('--out', required=True, type=Path, help='new evidence filename prefix; existing files are never overwritten')
    parser.add_argument('--plan-only', action='store_true')
    args = parser.parse_args()
    plan = phase_plan(args.phase)
    if args.debug is not None:
        plan[1:1] = ['debug', 'debug ' + args.debug]
        plan.extend(['debug', 'restore previous debug mode if changed and console synchronized'])
    record = dict(phase=args.phase, debug_mode=args.debug, plan=plan, physical_shaft_observation='unmeasured',
                  electrical_timing='unmeasured', automatic_write_replay=False, hours_soak='NOT RUN')
    print(json.dumps(record, indent=2), flush=True)
    if args.plan_only:
        return
    args.out.parent.mkdir(parents=True, exist_ok=True)
    path = args.out.with_suffix('.json')
    # Reserve both artifacts before any device command.
    with path.open('x', encoding='utf-8') as summary, args.out.with_suffix('.jsonl').open('x', encoding='utf-8') as log:
        try:
            with open_port(args.port, 115200, 5) as port:
                console = Console(port, on_event=Evidence(log))
                console.drain_startup(.3)
                run_phase(console, args.phase, record)
            record['ok'] = True
        except BaseException as error:
            record.update(ok=False, error=str(error))
            raise
        finally:
            summary.write(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
