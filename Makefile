CXX = g++
ACK = ack-grep
PERL = perl
WARNINGS = -Wall -Weffc++ -Wshadow -Wno-non-virtual-dtor -Wextra
DEBUG = -ggdb3 -DDEBUG
DFLAGS = -fPIC
CXXFLAGS = --std=gnu++11 $(DEBUG) $(WARNINGS) $(DFLAGS) -Isource/
LDFLAGS = 
LIBRARY_VAR=LD_LIBRARY_PATH

LIBNAME = libactor.so
LIB = blib/$(LIBNAME)
LIBTAPNAME = libtap++.so
LIBTAP = blib/$(LIBTAPNAME)

HDRS := $(wildcard source/*.h)
SRCS := $(wildcard source/*.C)
OBJS := $(patsubst source/%.C,blib/%.o,$(SRCS))

TEST_SRCS := $(wildcard t/*.C)
TEST_OBJS := $(patsubst %.C,%.t,$(TEST_SRCS))
TEST_GOALS = $(TEST_OBJS)

all: $(LIB)

blib:
	mkdir blib

headers:   

$(LIB): blib $(OBJS)
	$(CXX) -shared -o $@ -Wl,-soname,$(LIBNAME) $(OBJS) $(LIBLDFLAGS)

blib/%.o: source/%.C source/%.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

t/%.t: t/%.C $(HDRS) $(LIBTAP)
	$(CXX) $(CXXFLAGS) -Lblib -Itap++ -ltap++ -o $@ $< 

$(LIBTAP): tap++/tap++.C tap++/tap++.h
	$(CXX) -fPIC -shared -o $@ -Wl,-soname,$(LIBTAPNAME) -Itap++/ tap++/tap++.C

main: main.o $(LIB)
	$(CXX) $(CXXFLAGS) -o $@ $< -Lblib -lactor

testbuild: $(LIBTAP) $(TEST_GOALS)

test: $(LIB) testbuild 
	@echo run_tests.pl $(TEST_GOALS)
	@$(LIBRARY_VAR)=blib ./run_tests.pl $(TEST_GOALS)

clean:
	-rm -r blib $(wildcard t/*.t) main.o 2>/dev/null

testclean:
	-rm $(wildcard t/*.t) 2>/dev/null

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
