CXX = g++
WARNINGS = -Wall -Weffc++ -Wshadow -Wextra
#DEBUG = -ggdb3 -DDEBUG
DEBUG=-O3 -flto -Wl,--no-as-needed
DFLAGS = -fPIC
LDFLAGS = -pthread
CPP_VERSION = c++14
CXXFLAGS = --std=$(CPP_VERSION) $(LDFLAGS) $(DEBUG) $(WARNINGS) $(DFLAGS) -Isource/

all: examples

examples: chameneosredux

chameneosredux: examples/chameneosredux.C source/actor.h
	$(CXX) -o $@ $< $(CXXFLAGS)

clean:
	-rm chameneosredux 2>/dev/null

again: clean all

love:
	@echo Not war?

lines:
	@wc -l source/*.[hC] | sort -gr

install: $(LIB)
	@echo not implemented yet
	false;

.PHONY: examples lines install clean all

