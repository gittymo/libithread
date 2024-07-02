GCC=clang
CFLAGS=-g -Wall
SUFFIX=d

release: SUFFIX=
release: CFLAGS=-O2

libithread$(SUFFIX).o : ithread.c ithread.h
	$(GCC) -pthread $(CFLAGS) -c -o libithread$(SUFFIX).o ithread.c -I./

debug: libithread$(SUFFIX).o
	ar rcs libithread$(SUFFIX).a libithread$(SUFFIX).o

release: libithread$(SUFFIX).o
	ar rcs libithread$(SUFFIX).a libithread$(SUFFIX).o

test: debug ithreadtest.c
	$(GCC) -pthread $(CFLAGS) -o ithreadtest ithreadtest.c -I./ -L./ -lithread$(SUFFIX) -lm

.PHONY: clean

clean:
	rm -rf *.a
	rm -rf *.o
