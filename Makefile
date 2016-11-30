
LDLIBS = -lpthread
CFLAGS = -O3 -Wall -Wextra -Werror -g

PROGS = ring_cas ring_mutex ring_spin

all: $(PROGS)

ring_cas.o: ring_cas.c ring_common.c
ring_mutex.o: ring_mutex.c ring_common.c
ring_spin.o: ring_spin.c ring_common.c

ring_cas: ring_cas.o 
ring_mutex: ring_mutex.o
ring_spin: ring_spin.o

clean:
	rm -f *.o $(PROGS)

rebuild: clean all

