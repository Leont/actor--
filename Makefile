CXX = g++
ACK = ack-grep
PERL = perl
WARNINGS = -Wall -Weffc++ -Wshadow -Wextra -Wno-unused-parameter
#DEBUG = -ggdb3 -DDEBUG
DEBUG=-O3 -flto
DFLAGS = -fPIC
CXXFLAGS = --std=gnu++11 -pthread $(DEBUG) $(WARNINGS) $(DFLAGS) -Isource/
LDFLAGS = -pthread

LIBTAPNAME = libtap++.so
LIBTAP = blib/$(LIBTAPNAME)

HDRS := $(wildcard source/*.h)
SRCS := $(wildcard source/*.C)
OBJS := $(patsubst source/%.C,blib/%.o,$(SRCS))

TEST_SRCS := $(wildcard t/*.C)
TEST_OBJS := $(patsubst %.C,%.t,$(TEST_SRCS))
TEST_GOALS = $(TEST_OBJS)

all: chameneosredux

blib:
	mkdir blib

headers:   

#t/%.t: t/%.C $(HDRS) $(LIBTAP)
#	$(CXX) $(CXXFLAGS) -Lblib -Itap++ -ltap++ -o $@ $< 

#$(LIBTAP): tap++/tap++.C tap++/tap++.h
#	$(CXX) -fPIC -shared -o $@ -Wl,-soname,$(LIBTAPNAME) -Itap++/ tap++/tap++.C

chameneosredux.o: source/actor.h

chameneosredux: chameneosredux.o
	$(CXX) $(LDFLAGS) -o $@ $<

testbuild: $(LIBTAP) $(TEST_GOALS)

#test: $(LIB) testbuild
#	@echo run_tests.pl $(TEST_GOALS)
#	@$(LIBRARY_VAR)=blib ./run_tests.pl $(TEST_GOALS)

clean:
	-rm -r blib $(wildcard t/*.t) chameneosredux chameneosredux.o 2>/dev/null

#testclean:
#	-rm $(wildcard t/*.t) 2>/dev/null

again: clean all

love:
	@echo Not war?

lines:
	@wc -l source/*.[hC] | sort -gr
linesh:
	@wc -l ls source/*.h | sort -gr
linesC:
	@wc -l source/*.C | sort -gr

install: $(LIB)
	@echo not implemented yet
	false;

.PHONY: wordsC wordsh words lines linesh linesC todo install test testbuild clean testclean all

words: 
	@(for i in source/*.[hC]; do cpp -fpreprocessed $$i | sed 's/[_a-zA-Z0-9][_a-zA-Z0-9]*/x/g' | tr -d ' \012' | wc -c | tr "\n" " " ; echo $$i; done) | sort -gr | column -t;

wordsC:
	@(for i in source/*.C; do cpp -fpreprocessed $$i | sed 's/[_a-zA-Z0-9][_a-zA-Z0-9]*/x/g' | tr -d ' \012' | wc -c | tr "\n" " " ; echo $$i; done) | sort -gr | column -t;
wordsh:
	@(for i in source/*.h; do cpp -fpreprocessed $$i 2>/dev/null | sed 's/[_a-zA-Z0-9][_a-zA-Z0-9]*/x/g' | tr -d ' \012' | wc -c | tr "\n" " " ; echo $$i; done) | sort -gr | column -t;

todo:
	@for i in FIX''ME XX''X TO''DO; do echo -n "$$i: "; $(ACK) $$i | wc -l; done;

apicount: blib/$(LIBNAME)
	@echo -n "Number of entries: "
	@nm blib/libpack++.so -C --defined-only | egrep -i " [TW] pack::" | wc -l
