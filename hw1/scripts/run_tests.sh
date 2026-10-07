#!/bin/sh
set -eu
mkdir -p results
./tests > results/tests.log
tail -n 1 results/tests.log
