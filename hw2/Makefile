CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c17 -g -Wall -Wextra -Wpedantic -Werror -pthread
LDLIBS = -pthread -lm

.PHONY: all test clean
all: station station-cond

station: main.o config.o scenario.o state.o plan.o engine.o notify.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c station.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

engine.o notify.o tests.o: notify.h

%-cond.o: %.c station.h notify.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DUSE_CONDVAR -c $< -o $@

station-cond: main.o config.o scenario.o state.o plan.o engine-cond.o notify-cond.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

tests: tests.o config.o scenario.o state.o plan.o notify.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

tests-cond: tests-cond.o config.o scenario.o state.o plan.o notify-cond.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

station-fail: main.o config.o scenario.o state.o plan.o engine.o notify.o tests_fail.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -Wl,--wrap=pthread_create -Wl,--wrap=sem_init -o $@

station-cond-fail: main.o config.o scenario.o state.o plan.o engine-cond.o notify-cond.o tests_fail-cond.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -Wl,--wrap=pthread_create -Wl,--wrap=pthread_cond_init -o $@

test: all tests tests-cond station-fail station-cond-fail
	timeout 10 ./tests
	timeout 10 ./tests-cond
	sh scripts/test.sh

clean:
	rm -f station station-cond tests tests-cond station-fail station-cond-fail *.o *.log
