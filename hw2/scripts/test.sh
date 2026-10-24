#!/bin/sh
set -eu
if [ "${1:-}" != '--scenarios' ]; then
    for backend in ./station ./station-cond; do
        STATION="$backend" sh "$0" --scenarios
    done

    for program in ./station-fail ./station-cond-fail; do
        for number in 1 3 8; do
            status=0
            FAIL_THREAD_AT=$number timeout 5 "$program" examples/parallel.scenario 1 --quiet || status=$?
            test "$status" -eq 1
            grep -q "event=SUMMARY result=1 .*joined=$((number - 1))" station.log
        done
    done
    for number in 1 2 4; do
        status=0
        FAIL_SEMAPHORE_AT=$number timeout 5 ./station-fail examples/parallel.scenario 1 --quiet || status=$?
        test "$status" -eq 1
        status=0
        FAIL_COND_AT=$number timeout 5 ./station-cond-fail examples/parallel.scenario 1 --quiet || status=$?
        test "$status" -eq 1
    done
    status=0
    timeout 5 ./station examples/parallel.scenario 1 --quiet --log /dev/full || status=$?
    test "$status" -eq 1
    echo 'Partial startup and log write failure checks passed.'

    scratch=$(mktemp -d /tmp/hw2-test.XXXXXX)
    trap 'rm -f "$scratch/sem.log" "$scratch/cond.log" "$scratch/sem.out" "$scratch/cond.out"; rmdir "$scratch"' EXIT HUP INT TERM
    for scenario in examples/*.scenario test-data/*.scenario; do
        sem_status=0
        cond_status=0
        timeout 10 ./station "$scenario" 1 --quiet --log "$scratch/sem.log" > /dev/null || sem_status=$?
        timeout 10 ./station-cond "$scenario" 1 --quiet --log "$scratch/cond.log" > /dev/null || cond_status=$?
        test "$sem_status" -eq "$cond_status"
        test "$sem_status" -eq 0 || test "$sem_status" -eq 4
        for backend in sem cond; do
            awk '
                /event=DEPART/ { print $4, $5, $6 }
                /event=SUMMARY/ { print $4, $5 }
            ' "$scratch/$backend.log" | sort > "$scratch/$backend.out"
        done
        cmp "$scratch/sem.out" "$scratch/cond.out"
    done
    echo 'Both backends agree on outcomes and train compositions for 12 scenarios.'
    exit 0
fi
program=${STATION:-./station}
timeout 10 "$program" examples/station.scenario 1
grep -q 'event=SUMMARY result=0 departed=2 .*joined=7' station.log
awk -f scripts/check_log.awk station.log

timeout 10 "$program" examples/parallel.scenario 5
grep -q 'event=SUMMARY result=0 departed=2 peak_active=2' station.log
timeout 10 "$program" examples/conflicting.scenario 1
grep -q 'event=SUMMARY result=0 departed=2 peak_active=1' station.log

status=0
timeout 10 "$program" test-data/capacity.scenario 1 || status=$?
test "$status" -eq 4
test "$(grep -c 'event=PLACE' station.log)" -eq 1
status=0
timeout 10 "$program" examples/remainder.scenario 1 || status=$?
test "$status" -eq 4

if "$program" examples/station.scenario 0; then
    echo 'Invalid time scale accepted' >&2
    exit 1
fi
echo 'Concurrency, resource reservation and completion checks passed.'

status=0
timeout --preserve-status -s INT -k 3 0.1 "$program" examples/parallel.scenario 1000 --quiet || status=$?
test "$status" -eq 5
grep -q 'event=SIGNAL number=2' station.log
grep -q 'event=SUMMARY result=5 departed=0 .*joined=8' station.log
status=0
timeout --preserve-status -s TERM -k 3 0.5 "$program" examples/parallel.scenario 100 --quiet || status=$?
test "$status" -eq 5
grep -q 'event=START' station.log
grep -q 'event=SIGNAL number=15' station.log
grep -q 'event=SUMMARY result=5 departed=0 .*joined=8' station.log

status=0
timeout 5 "$program" --scenario examples/parallel.scenario --tick-ms 100 --until 3 --quiet || status=$?
test "$status" -eq 2
grep -q 'event=SUMMARY result=2 departed=0 .*joined=8' station.log
"$program" --help > /dev/null
status=0
"$program" --until -1 || status=$?
test "$status" -eq 1
echo 'Signals, interrupted maneuvers and time limit checks passed.'

timeout 5 "$program" --scenario test-data/strategy.scenario --tick-ms 1 --strategy first --quiet
grep -q 'event=ASSIGN wagon=1 locomotive=1 track=2' station.log
timeout 5 "$program" --scenario test-data/strategy.scenario --tick-ms 1 --strategy most --quiet
grep -q 'event=ASSIGN wagon=1 locomotive=1 track=3' station.log
timeout 5 "$program" test-data/competing.scenario 1 --quiet
grep -q 'event=SUMMARY result=0 departed=3' station.log
test "$(grep -c 'event=DEPART train=101 wagons=1' station.log)" -eq 1
test "$(grep -c 'event=DEPART train=102 wagons=2' station.log)" -eq 1
echo 'Dispatch policies and competing departure checks passed.'

for strategy in first most; do
    timeout 10 "$program" --scenario test-data/allocation.scenario --tick-ms 1 --strategy "$strategy" --quiet
    grep -q 'event=SUMMARY result=0 departed=2' station.log
    test "$(grep -c 'event=PLACE .*track=3' station.log)" -eq 2
done
for scenario in flexible groups; do
    timeout 10 "$program" "test-data/$scenario.scenario" 1 --quiet
    grep -q 'event=SUMMARY result=0' station.log
    test "$(grep -c 'event=DEPART' station.log)" -eq 2
done
timeout 10 "$program" test-data/large.scenario 1 --quiet
grep -q 'event=SUMMARY result=0 departed=48 .*joined=61' station.log
test "$(grep -c 'event=PLACE' station.log)" -eq 48
test "$(grep -c 'event=DEPART' station.log)" -eq 4
awk -f scripts/check_log.awk station.log
echo 'Demand allocation, flexible routing and 48-wagon checks passed.'
timeout 5 "$program" test-data/empty-arrival.scenario 1 --quiet
grep -q 'event=ARRIVE train=2' station.log
grep -q 'event=SUMMARY result=0 departed=1' station.log
