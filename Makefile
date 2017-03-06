CXX = g++
WARNINGS = -Wall -Weffc++ -Wshadow -Wextra -Wno-unused-parameter
#DEBUG = -ggdb3 -DDEBUG
DEBUG=-O3 -flto -Wl,--no-as-needed
DFLAGS = -fPIC
LDFLAGS = -pthread
CXXFLAGS = --std=c++17 $(LDFLAGS) $(DEBUG) $(WARNINGS) $(DFLAGS) -Isource/

all: examples

examples: chameneosredux

chameneosredux: chameneosredux.C source/actor.h
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

