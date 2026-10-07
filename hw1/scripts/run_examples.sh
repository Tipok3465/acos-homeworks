#!/bin/sh
set -eu
mkdir -p results
./station --scenario examples/station.scenario --log results/station.log
grep -q '^time=00002 event=UNCOUPLE' results/station.log
grep -q '^time=00002 event=ASSIGN' results/station.log
grep -q '^time=00004 event=START' results/station.log
grep -q '^time=00024 event=FINISH' results/station.log
./station --scenario examples/multi.scenario --strategy most --log results/multi.log
set +e
./station --scenario examples/remainder.scenario --log results/remainder.log
status=$?
set -e
test "$status" -eq 4
echo "All examples behaved as expected."
