#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#define ISPOW2(n)  (!(n & (n-1)))
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define min(x,y) ({ \
	typeof(x) _x = (x);     \
	typeof(y) _y = (y);     \
	(void) (&_x == &_y);    \
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);     \
	typeof(y) _y = (y);     \
	(void) (&_x == &_y);    \
	_x > _y ? _x : _y; })

#define __unused __attribute__((unused))

typedef struct {
	pthread_spinlock_t mtx;
	uint32_t prod;
	uint32_t cons;
	uint32_t size;
	const void **ring;
} ring_t;

void ring_init(ring_t *r, const void **ring, uint32_t size)
{
	assert(ISPOW2(size));
	pthread_spin_init(&r->mtx, 0);
	r->prod = 0;
	r->cons = 0;
	r->size = size;
	r->ring = ring;
}

uint32_t ring_enq(ring_t *r, void * const *obj_table, uint32_t n)
{
	uint32_t free_entries;
	uint32_t idx;
	const uint32_t size = r->size;
	const uint32_t mask = r->size - 1;

	pthread_spin_lock(&r->mtx);

	free_entries = mask + r->cons - r->prod;
	/* check that we have enough room in ring */
	if (unlikely(n > free_entries)) {
		if (unlikely(free_entries == 0)) {
			n = 0;
			goto exit; /* No free entry available */
		}

		n = free_entries;
	}

	idx = r->prod & mask;
	if (likely(idx + n < size)) {
		memcpy(&r->ring[idx], obj_table, sizeof(void*) * n);
	} else {
		size_t cpy = size - idx;
		memcpy(&r->ring[idx], obj_table, sizeof(void*) * cpy);
		memcpy(r->ring, &obj_table[cpy], sizeof(void*) * (n - cpy));
	}

	r->prod += n;

exit:
	pthread_spin_unlock(&r->mtx);
	return n;
}

uint32_t ring_deq(ring_t *r, void **obj_table, size_t n)
{
	uint32_t entries;
	uint32_t idx;
	const uint32_t size = r->size;
	const uint32_t mask = r->size - 1;

	pthread_spin_lock(&r->mtx);

	entries = r->prod - r->cons;
	if (n > entries) {
		if (unlikely(entries == 0)) {
			n = 0;
			goto exit; /* No free entry available */
		}

		n = entries;
	}

	/* copy out table */
	idx = r->cons & mask;
	if (likely(idx + n < size)) {
		memcpy(obj_table, &r->ring[idx], sizeof(void*) * n);
	} else {
		size_t cpy = size - idx;
		memcpy(obj_table, &r->ring[idx], sizeof(void*) * cpy);
		memcpy(&obj_table[cpy], r->ring, sizeof(void*) * (n - cpy));
	}

	r->cons += n;
exit:
	pthread_spin_unlock(&r->mtx);
	return n;
}

#include "ring_common.c"
