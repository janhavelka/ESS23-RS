"""Finite comparison of position setup policies through the ordinary move path.

Small positive free-shaft moves only; no replay, persistence or restart. Timing
is firmware admission-to-start acknowledgement, not physical shaft latency.
"""
import argparse
import json
from pathlib import Path
import statistics

from bench_probe import BenchError, observe_stopped
from bench_motion import profile_snapshot, restore_profile
from bench_session import checked, run_recorded


def campaign(console, record, count):
    if not 1 <= count <= 20:
        raise ValueError('count must be 1..20')
    load = checked(console, 'load')
    if any(load[key] for key in ('workload_us', 'owner_delay_us', 'console_bytes')):
        raise BenchError('comparison requires unloaded bench')
    record['saved'] = profile_snapshot(console)
    record['trials'] = []
    attempted = False
    try:
        schedule = [('write', 100, 60), ('verify', 100, 60), ('stored', 100, 60)] * count
        # Single speed, pair-only target and mixed changes exercise all selections.
        schedule += [('verify', 100, 50), ('verify', 101, 50), ('verify', 100, 60)]
        for setup, target, speed in schedule:
            # Arrival and feedback are separate registers, not an atomic sample.
            # Reuse the existing finite standstill observer before the next move.
            observe_stopped(console, address=1, timeout_s=5)
            attempted = True
            result = checked(console, 'move-relative', address=1,
                             move_args=(str(target), 'steps', 'native', str(speed), 'configured', 'setup', setup))
            if result['completion'] != 'observed' or result['uncertain']:
                raise BenchError('move did not report new activity and completion')
            trial = dict(setup=setup, target=target, speed=speed,
                         start_ack_us=result['trigger_evidence']['delivered_us'] - result['started_us'],
                         start_closure_us=result['trigger_evidence']['latest_us'] - result['started_us'],
                         setup_offset=result['setup_offset'], setup_count=result['setup_count'],
                         operation_id=result['operation_id'])
            console.emit('repeat_trial', **trial)
            record['trials'].append(trial)
        expected = [(2, 1), (3, 2), (0, 5)]
        if [(t['setup_offset'], t['setup_count']) for t in record['trials'][-3:]] != expected:
            raise BenchError('selective write shapes differ from planned comparison')
        for trial in record['trials'][:-3]:
            if trial['setup_count'] != (5 if trial['setup'] == 'write' else 0):
                raise BenchError('identical repeat did not select the expected setup')
        record['latency_us'] = {}
        for setup in ('write', 'verify', 'stored'):
            values = [t['start_ack_us'] for t in record['trials'][:-3] if t['setup'] == setup]
            record['latency_us'][setup] = dict(count=len(values), minimum=min(values),
                                               median=statistics.median(values), maximum=max(values))
        record['workload_verified'] = True
    except BaseException as error:
        record['failure'] = str(error) or type(error).__name__
        raise
    finally:
        if attempted:
            record['cleanup'] = 'unknown'
            if not console.synchronized:
                raise BenchError('framing unavailable; no cleanup write or replay')
            record['stop'] = checked(console, 'stop', stop_policy='direct', address=1)
            record['stopped'] = observe_stopped(console, address=1, timeout_s=5)
            record['restored'] = restore_profile(console)
            record['cleanup'] = 'stopped_and_restored'
    record['probes'] = [checked(console, 'probe', address=1) for _ in range(10)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM13')
    parser.add_argument('--count', type=int, choices=range(1, 21), default=10)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--plan-only', action='store_true')
    args = parser.parse_args()
    plan = dict(repeats_per_policy=args.count, total_moves=3 * args.count + 3,
                maximum_increments=101, maximum_rpm=60, operation_deadline_s=3,
                thresholds='Every terminal accounted, observed completion, expected write shape, no replay, exact restoration',
                cleanup='One explicit direct stop, checked zero speed, restore saved profile once, ten probes',
                timing='Firmware admission to start acknowledgement; initial/state refresh outside timed interval')
    print(json.dumps(plan, indent=2), flush=True)
    if args.plan_only:
        return
    run_recorded(port=args.port, out=args.out, scenario='repeat-position-setup', inputs=plan,
                 run=lambda console, record: campaign(console, record, args.count),
                 firmware=args.firmware, debug='decoded')


if __name__ == '__main__':
    main()
