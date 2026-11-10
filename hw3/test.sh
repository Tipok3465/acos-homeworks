#!/bin/sh
set -eu
scratch=$(mktemp -d)
trap 'rm -f "$scratch/station.log" "$scratch/host.out"; rmdir "$scratch"' EXIT

run() {
    expected=$1
    shift
    status=0
    timeout -k 2 5 "$program" "$@" --quiet --log "$scratch/station.log" || status=$?
    test "$status" -eq "$expected"
}

for program in ./station ./station-named; do
    run 0 examples/station.scenario 1
    grep -q 'event=SUMMARY result=0 departed=2 .*reaped=7' "$scratch/station.log"
    run 0 examples/parallel.scenario 5
    grep -q 'event=SUMMARY result=0 departed=2 peak_active=2' "$scratch/station.log"
    run 0 examples/conflicting.scenario 1
    grep -q 'event=SUMMARY result=0 departed=2 peak_active=1' "$scratch/station.log"
    run 4 examples/remainder.scenario 1
    run 4 test-data/competing.scenario 1 --strategy first
    grep -q 'event=REMAINDER reason=selected_plan_exhausted strategy=first remaining=1' "$scratch/station.log"
    grep -q 'event=DEPART train=101 wagons=1' "$scratch/station.log"
    run 0 test-data/competing.scenario 1 --strategy most
    grep -q 'event=DEPART train=102 wagons=2' "$scratch/station.log"
    run 2 examples/parallel.scenario 100 --until 3
    status=0
    timeout --preserve-status -s INT -k 3 0.2 "$program" examples/parallel.scenario 1000 --quiet --log "$scratch/station.log" || status=$?
    test "$status" -eq 5
    grep -q 'event=SUMMARY result=5 .*reaped=8' "$scratch/station.log"
done

start_host() {
    ./station-host "$@" --quiet --log "$scratch/station.log" > "$scratch/host.out" &
    host=$!
    attempts=0
    until grep -q "^READY $host 8$" "$scratch/host.out"; do
        kill -0 "$host"
        attempts=$((attempts + 1))
        test "$attempts" -lt 200
        sleep 0.01
    done
}

check_ipc_cleanup() {
    test ! -e "/dev/shm/acos49-shm-$host"
    test -z "$(find /dev/shm -maxdepth 1 -name "sem.acos49-sem-$host-*")"
}

external() {
    scenario=$1
    tick=$2
    expected=$3
    peak=$4
    start_host "examples/$scenario.scenario" "$tick"
    workers=
    for slot in 7 6 5 4 3 2 1 0; do
        ./station-worker "$host" "$slot" &
        workers="$workers $!"
    done
    if [ "$expected" -eq 5 ]; then
        attempts=0
        until grep -q 'event=START' "$scratch/station.log"; do
            kill -0 "$host"
            attempts=$((attempts + 1))
            test "$attempts" -lt 200
            sleep 0.01
        done
        kill -INT "$host"
    fi
    status=0
    wait "$host" || status=$?
    test "$status" -eq "$expected"
    for worker in $workers; do wait "$worker"; done
    grep -q "event=SUMMARY result=$expected .*peak_active=$peak finished=8" "$scratch/station.log"
    check_ipc_cleanup
}

external parallel 5 0 2
external conflicting 5 0 1
external parallel 100 5 '[12]'

start_host examples/parallel.scenario 100
./station-worker "$host" 0 &
first=$!
./station-worker "$host" 0 &
second=$!
attempts=0
while kill -0 "$first" 2>/dev/null && kill -0 "$second" 2>/dev/null; do
    attempts=$((attempts + 1))
    test "$attempts" -lt 200
    sleep 0.01
done
kill -TERM "$host"
status=0
wait "$host" || status=$?
test "$status" -eq 5
first_status=0
second_status=0
wait "$first" || first_status=$?
wait "$second" || second_status=$?
case "$first_status:$second_status" in
    0:1|1:0) ;;
    *) exit 1 ;;
esac
grep -q 'event=SUMMARY result=5 .*finished=1' "$scratch/station.log"
check_ipc_cleanup
echo 'Independent processes: parallel, conflicting, signals, duplicate slot and IPC cleanup passed.'
