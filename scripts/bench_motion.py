"""Short, explicitly selected free-shaft experiment phases; never replay writes.

Prepare/print the complete phase before opening COM13. Uses the public console
operations, checked host harness, and the regular firmware image.
No persistence, power control, continuous velocity or endurance loop is included.
"""
import argparse
import json
from pathlib import Path
import time

from bench_probe import (BenchError, debug_session,
                         drive_reported_stopped, observe_stopped)
from bench_session import run_recorded

PHASES = ('inspect', 'actions', 'forward', 'absolute', 'return', 'stop-normal', 'stop-direct', 'restore', 'status')
MOTION_PHASES = ('forward', 'absolute', 'return', 'stop-normal', 'stop-direct')
EVENT_TAIL = 32


def profile_snapshot(console, action='read', timeout_s=5, on_command=None):
    """One explicit read/restore attempt and bounded passive settlement.

    The original snapshot belongs to this open console/firmware session. A
    failed restore never triggers another write or implicit recovery.
    """
    if action not in ('read', 'restore') or not 0 < timeout_s <= 60:
        raise ValueError('profile snapshot requires read/restore and a 0..60s deadline')
    clock = getattr(console, 'clock', time.monotonic)
    sleep = getattr(console, 'sleep', time.sleep)
    deadline = clock() + timeout_s
    tokens = (action,)
    for _ in range(101):
        remaining = deadline - clock()
        if remaining <= 0:
            break
        kwargs = dict(host_args=tokens)
        result = console.command('motion-profile', timeout_s=remaining, **kwargs)
        if on_command is not None:
            on_command('motion-profile', kwargs, result)
        if not result['ok']:
            raise BenchError('motion-profile refused/failed: ' + str(result.get('result')))
        if not result['pending']:
            if not result['session_ok']:
                raise BenchError('motion snapshot/restoration failed: ' + result['error'])
            return result
        sleep(min(.01, max(0, deadline - clock())))
        tokens = ('inspect',)
    raise BenchError('motion snapshot/restoration observation bound exhausted')


def restore_profile(console, timeout_s=5, on_command=None):
    """Restore once after the caller's explicit stop/standstill cleanup."""
    result = profile_snapshot(console, 'restore', timeout_s, on_command)
    if not result['restored'] or result['original'] != result['current']:
        raise BenchError('exact motion profile restoration not established')
    return result


def phase_plan(phase):
    if phase not in PHASES:
        raise ValueError('unknown finite experiment phase')
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
    restoration = ['before closing this same connection: restore the saved original motion profile once, only after successful stop and fresh non-running/zero-speed evidence; verify exact readback',
                   'on failed/unknown stop or broken framing: no restore write; retain backup and unknown cleanup'] if phase in MOTION_PHASES else []
    return common + steps[phase] + settling + cleanup + restoration + ['stats', 'load', 'drv', 'memory', 'host']


def run_phase(console, phase, record):
    phase_plan(phase)  # Validate even direct callers before version/debug or device I/O.
    record['version'] = console.identify()
    with debug_session(console, record.get('debug_mode'), 5) as diagnostics:
        record['debug'] = diagnostics
        return _run_phase(console, phase, record)


def _run_phase(console, phase, record):
    record['events'] = []
    move = None
    move_request_id = None
    stop_attempted = False
    stop_confirmed = False
    standing = None
    settlement_failed = False
    record['event_count'] = 0

    def retain_command(name, kwargs, result):
        event = dict(command=name, arguments=kwargs, result=result)
        console.emit('phase_command', phase=phase, **event)
        record['event_count'] += 1
        record['events'].append(event)
        if len(record['events']) > EVENT_TAIL:
            del record['events'][0]

    def command(name, **kwargs):
        result = console.command(name, timeout_s=5, **kwargs)
        retain_command(name, kwargs, result)
        if not result['ok']:
            raise BenchError(f'{name} refused/failed: {result.get("result", result.get("outcome"))}')
        return result

    def state_blocks(result, require_stopped=True):
        blocks = {b['block']: b for b in result['state_blocks']}
        if blocks[0]['raw_alarm'] or blocks[0]['alarm_flag']:
            raise BenchError('drive reports an alarm; no subsequent move')
        if require_stopped and not drive_reported_stopped(result):
            raise BenchError('drive does not report stopped and zero raw speed')
        return blocks

    def state(require_stopped=True):
        return state_blocks(command('read-state', address=1), require_stopped)

    def stop(policy):
        nonlocal stop_attempted, stop_confirmed, standing, settlement_failed
        stop_attempted = True
        result = command('stop', stop_policy=policy, address=1)
        stop_confirmed = True
        standing = None  # A report preceding this stop does not settle this command.
        settlement_failed = False
        return result

    def settled():
        nonlocal standing, settlement_failed
        def sample(result):
            retain_command('read-state', dict(address=1), result)
            state_blocks(result, False)
        try:
            result = observe_stopped(console, address=1, timeout_s=5, on_sample=sample)
            standing = state_blocks(result)
            return standing
        except BaseException:
            settlement_failed = True
            raise

    def fixture(action):
        return profile_snapshot(console, action, on_command=retain_command)

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
            if record['state_before'][0]['released']:
                raise BenchError('action experiment requires initially enabled stationary drive; no action write')
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
            # The absolute experiments retain the example's recorded raw
            # fixture window. Raw feedback is not a calibrated command position;
            # finite relative displacement needs no host origin or baseline.
            if phase in ('absolute', 'return') and not 0 <= before_move[2]['raw_position'] <= 250:
                raise BenchError('absolute experiment requires recorded raw feedback fixture window 0..250')
            value = '0' if phase == 'return' else '100' if phase in ('forward', 'absolute') else '250'
            kind = 'move-absolute' if phase in ('return', 'absolute') else 'move-relative'
            stop_attempted = False
            move_request_id = getattr(console, 'next_id', None)
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
            record['restore'] = restore_profile(console, on_command=retain_command)
            command('probe', address=1)
        else:
            record['profile'] = command('motion-profile', host_args=('inspect',))
            command('probe', address=1)
    except BaseException as phase_error:
        record['phase_error'] = str(phase_error)
        raise
    finally:
        cleanup_error = None
        if move is None and move_request_id is not None:
            move = console.pending.get(move_request_id)
        if phase in MOTION_PHASES and move is not None and (move.accepted or
                (move.terminal is None and not console.synchronized)):
            record['cleanup'] = 'unknown'
            try:
                if not console.synchronized:
                    raise BenchError('framing unavailable; no stop or restoration command sent')
                if not stop_attempted:
                    record['failure_cleanup_stop'] = stop('direct')
                if not stop_confirmed:
                    raise BenchError('stop outcome unknown; no restoration command sent')
                if settlement_failed:
                    raise BenchError('standstill observation failed; no restoration command sent')
                if standing is None:
                    standing = state()
                    record['failure_cleanup_state'] = standing
                record['cleanup'] = 'drive_reported_stopped'
                record['restoration'] = restore_profile(console, on_command=retain_command)
                record['cleanup'] = 'stopped_and_restored'
            except BaseException as error:
                cleanup_error = error
                record['failure_cleanup_error'] = str(error) or 'cleanup interrupted'
                console.emit('phase_cleanup_failure', phase=phase, cleanup=record['cleanup'],
                             error=record['failure_cleanup_error'])
        if console.synchronized:
            for name in ('stats', 'load', 'drv', 'memory', 'host'):
                try:
                    record['ending_' + name] = command(name)
                except BaseException as error:
                    record['ending_error'] = str(error)
                    if 'phase_error' not in record and cleanup_error is None:
                        raise
                    break
        if cleanup_error is not None and 'phase_error' not in record:
            raise cleanup_error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM13')
    parser.add_argument('--debug', choices=('off', 'raw', 'decoded'), default=None)
    parser.add_argument('--phase', required=True, choices=PHASES)
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
    def run(console, summary):
        # Shared session owns debug selection, connection and artifacts. The
        # same reusable phase receives no second debug-mode selection.
        details = dict(phase=args.phase, plan=plan)
        summary['motion'] = details
        run_phase(console, args.phase, details)
        summary['workload_verified'] = True
    run_recorded(port=args.port, out=args.out, scenario='motion-' + args.phase,
                 inputs=dict(phase=args.phase, debug=args.debug, plan=plan), run=run,
                 debug=args.debug)


if __name__ == '__main__':
    main()
