CC = gcc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean test sanitize
all: traffic_analyzer
traffic_analyzer: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(LDLIBS) -o $@
src/%.o: src/%.c include/analyzer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c $< -o $@
test: traffic_analyzer
	python3 tests/test_analyzer.py
sanitize:
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -pthread -o traffic_analyzer_san
	ASAN_OPTIONS=detect_leaks=1 TRAFFIC_BINARY=./traffic_analyzer_san python3 tests/test_analyzer.py
clean:
	rm -f $(OBJECTS) traffic_analyzer traffic_analyzer_san
