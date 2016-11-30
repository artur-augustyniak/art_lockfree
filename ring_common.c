#define LOOP_CNT 500000000
#define RING_SIZE 4096
#define BURST_SIZE 32
#define THREAD_CNT 4

static const void *ring_buff[RING_SIZE];
static ring_t ring;
pthread_t thread[4];
bool end;

void* worker(void* param)
{
	size_t thri = (size_t)param;
	size_t i;
	size_t ret1;
	size_t ret2;
	void *obj[BURST_SIZE] = { 0 };

	for (i = 0; i < BURST_SIZE; i++) {
		obj[i] = (void*)i; /* dummy data */
	}

	i = 0;
	while (true) {
		ret1 = ring_enq(&ring, obj, BURST_SIZE);
		ret2 = ring_deq(&ring, obj, ret1);
		assert(ret1 == ret2);
		i += ret1;
		if (i >= LOOP_CNT)
			break;
	}

	printf("Thread %zu done: %zu\n", thri, i);

	return (void*)i;
}

int main(int argc, char *argv[] __unused)
{
	size_t i;
	int ret;
	pthread_attr_t attr;
	cpu_set_t c;
	struct timespec start;
	struct timespec stop;
	size_t cnt;
	size_t sum = 0;
	double persec;

	ring_init(&ring, ring_buff, RING_SIZE);

	if (argc > 1 ) {
		/* this would need root privileges */
		struct sched_param param = { .sched_priority = 30 };
		ret = sched_setscheduler(0, SCHED_FIFO, &param);
		assert(!ret);
	}

	pthread_attr_init(&attr);

	for (i = 0; i < THREAD_CNT; i++) {
		CPU_ZERO(&c);
		CPU_SET(i, &c);
		pthread_attr_setaffinity_np(&attr, sizeof(c), &c);
		ret = pthread_create(&thread[i], &attr, worker, (void*)i);
		assert(!ret);
	}

	if (clock_gettime(CLOCK_REALTIME, &start))
		return -1;

	for (i = 0; i < THREAD_CNT; i++) {
		ret = pthread_join(thread[i], (void**)&cnt);
		assert(!ret);
		sum += cnt;
	}

	if (clock_gettime(CLOCK_REALTIME, &stop))
		return -1;

	stop.tv_nsec -= start.tv_nsec;
	if (stop.tv_nsec < 0) {
		stop.tv_nsec += 1000000000;
		stop.tv_sec -= 1;
	}
	stop.tv_sec -= start.tv_sec;

	persec = (double)sum / ((double)stop.tv_sec + \
				(double)1000000000 / (double)stop.tv_nsec);

	printf("%s took %lld.%.9ld processed %g per sec\n", argv[0],
	       (long long)stop.tv_sec, stop.tv_nsec, persec);

	return 0;
}

