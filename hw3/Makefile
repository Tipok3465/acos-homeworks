CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Werror -pthread
LDLIBS = -pthread -lrt -lm
SOURCES = main.c config.c scenario.c plan.c state.c engine.c notify.c
HEADERS = station.h notify.h

.PHONY: all test clean
all: station station-named station-host station-worker

station: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDLIBS) -o $@

station-named: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DUSE_NAMED $(SOURCES) $(LDLIBS) -o $@

EXTERNAL_FLAGS = -DUSE_NAMED -DUSE_EXTERNAL

station-host: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTERNAL_FLAGS) $(SOURCES) $(LDLIBS) -o $@

station-worker: worker.c engine.c notify.c plan.c state.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTERNAL_FLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

test: all
	timeout -k 3 40 sh test.sh

clean:
	rm -f station station-named station-host station-worker station.log
