CC ?= cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Werror
LDLIBS = -lm

APP = station
APP_OBJECTS = main.o config.o simulation.o
TEST_OBJECTS = tests.o config.o simulation.o

.PHONY: all test examples clean

all: $(APP)

$(APP): $(APP_OBJECTS)
	$(CC) $(APP_OBJECTS) $(LDLIBS) -o $@

tests: $(TEST_OBJECTS)
	$(CC) $(TEST_OBJECTS) $(LDLIBS) -o $@

%.o: %.c station.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: tests
	./scripts/run_tests.sh

examples: $(APP)
	./scripts/run_examples.sh

clean:
	rm -f $(APP) tests tests-sanitize *.o *.log
