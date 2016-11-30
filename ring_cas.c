#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>

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

#if 1
#define CACHE_LINE_SIZE 128
#define CACHE_ALIGNED __attribute__ ((aligned(CACHE_LINE_SIZE)))
#else
#define CACHE_ALIGNED
#endif

typedef struct {
	struct {
		uint32_t res;
		uint32_t fin;
	} prod CACHE_ALIGNED;
	struct {
		uint32_t res;
		uint32_t fin;
	} cons CACHE_ALIGNED;
	uint32_t size;
	const void **ring;
} ring_t;

void ring_init(ring_t *r, const void **ring, uint32_t size)
{
	assert(ISPOW2(size));
	r->prod.res = 0;
	r->prod.fin = 0;
	r->cons.res = 0;
	r->cons.fin = 0;
	r->size = size;
	r->ring = ring;
}

uint32_t ring_enq(ring_t *r, void * const *obj_table, uint32_t n)
{
	uint32_t prod_res, prod_next;
	uint32_t cons_fin, free_entries;
	const uint32_t max = n;
	const uint32_t size = r->size;
	const uint32_t mask = r->size - 1;
	uint32_t idx, exp;
	int success;

	if (unlikely(0 == n))
		return 0;

	/* move prod.res atomically */
	do {
		/* Reset n to the initial burst count */
		n = max;

		prod_res = __atomic_load_n(&r->prod.res, __ATOMIC_ACQUIRE);
		cons_fin = __atomic_load_n(&r->cons.fin, __ATOMIC_ACQUIRE);
		free_entries = (mask + cons_fin - prod_res);

		/* check that we have enough room in ring */
		if (unlikely(n > free_entries)) {
			if (unlikely(free_entries == 0)) {
				n = 0;
				goto exit; /* No free entry available */
			}

			n = free_entries;
		}

		prod_next = prod_res + n;
		exp = prod_res;
		success = __atomic_compare_exchange_n(&r->prod.res,
				&exp,
				prod_next,
				false/*strong*/,
				__ATOMIC_ACQ_REL,
				__ATOMIC_RELAXED);
	} while (unlikely(success == 0));

	/* write entries into ring */
	idx = prod_res & mask;
	if (likely(idx + n < size)) {
		memcpy(&r->ring[idx], obj_table, sizeof(void*) * n);
	} else {
		size_t cpy = size - idx;
		memcpy(&r->ring[idx], obj_table, sizeof(void*) * cpy);
		memcpy(r->ring, &obj_table[cpy], sizeof(void*) * (n - cpy));
	}

	/* If there are other enqueues in progress that preceeded us, * we
	 * need to wait for them to complete */
	while (__atomic_load_n(&r->prod.fin, __ATOMIC_ACQUIRE) != prod_res);
	/* update the prod index and signalize that there are new entries inside
	 * ring */
	__atomic_store_n(&r->prod.fin, prod_next, __ATOMIC_RELEASE);

exit:
	return n;
}

uint32_t ring_deq(ring_t *r, void **obj_table, size_t n)
{
	uint32_t cons_res, prod_fin;
	uint32_t cons_next, entries;
	uint32_t idx, exp;
	const uint32_t max = n;
	const uint32_t size = r->size;
	const uint32_t mask = r->size - 1;
	int success;

	if (unlikely(0 == n))
		return 0;

	/* move cons.res atomically */
	do {
		/* Restore n as it may change every loop */
		n = max;

		cons_res = __atomic_load_n(&r->cons.res, __ATOMIC_ACQUIRE);
		prod_fin = __atomic_load_n(&r->prod.fin, __ATOMIC_ACQUIRE);
		entries = (prod_fin - cons_res);

		/* Set the actual entries for dequeue */
		if (n > entries) {
			if (unlikely(entries == 0)) {
				n = 0;
				goto exit; /* No free entry available */
			}

			n = entries;
		}

		cons_next = cons_res + n;
		exp = cons_res;
		success = __atomic_compare_exchange_n(&r->cons.res,
				&exp,
				cons_next,
				false/*strong*/,
				__ATOMIC_ACQ_REL,
				__ATOMIC_RELAXED);
	} while (unlikely(success == 0));

	/* copy out table */
	idx = cons_res & mask;
	if (likely(idx + n < size)) {
		memcpy(obj_table, &r->ring[idx], sizeof(void*) * n);
	} else {
		size_t cpy = size - idx;
		memcpy(obj_table, &r->ring[idx], sizeof(void*) * cpy);
		memcpy(&obj_table[cpy], r->ring, sizeof(void*) * (n - cpy));
	}

	/* If there are other dequeues in progress that preceded us, we need
	 * to wait for them to complete */
	while (__atomic_load_n(&r->cons.fin, __ATOMIC_ACQUIRE) != cons_res);
	/* update the cons_fin and notify that there are new free entries
	 * available for usage */
	__atomic_store_n(&r->cons.fin, cons_next, __ATOMIC_RELEASE);

exit:
	return n;
}

#include "ring_common.c"
