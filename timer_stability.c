/*
 * Test stability of timer frequency.
 *
 * To compile:
 *	gcc -o timer_stability timer_stability.c  -lrt -lm
 *
 * To run:
 *	timer_stability <number procs>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/time.h>


#define MYSIG	(SIGRTMAX - 2)

#define ITERS	1000

#define TIMERFREQ	10000	/* us */

static uint64_t
get_time(void)
{
	uint64_t current_time=0;
	static uint64_t last_time = 0;
	struct timeval time_val;

	gettimeofday(&time_val, NULL);
	current_time = time_val.tv_sec * 1000000LL + time_val.tv_usec;
  
	if (current_time < last_time)
		return last_time;

	last_time = current_time;

	return current_time;
}


static void
handle_sig(int sig, siginfo_t *info, void *ctxt)
{
	static uint64_t last_time = 0, min = 0, max = 0;
	static uint64_t gaps = 0;
	static uint64_t gaps_sq = 0;
	static uint64_t count = 0;
	static int pid = 0;
	uint64_t curr_time;
	uint64_t gap;

	curr_time = get_time();
	if (last_time == 0) {
		last_time = curr_time;
		gaps = gaps_sq = 0;
		count = 0;
		min = 1000000000;
		max = 0;
		pid = getpid();
		return;
	}


	gap = curr_time - last_time;
	gaps += gap;
	gaps_sq += gap * gap;
	count++;

	if (gap > max)
		max = gap;
	if (gap < min)
		min = gap;

	last_time = curr_time;

	if (count == ITERS) {
		double std_dev;

		std_dev = sqrt((double)count * (double)gaps_sq -
		    (double)gaps * (double)gaps);
		std_dev /= (double)count;

		printf("P: %d, I: %ld, Min: %ld, Max: %ld, Avg: %4.2f, Dev: %4.2f\n",
		    pid, count, min, max, (double)gaps / (double)count,
		    std_dev);
		fflush(stdout);

		last_time = 0;
	}
}


int main(int ac, char **av)
{
	struct sigaction sact;
	int ret;
	timer_t timer_id;
	struct sigevent sevt;
	struct itimerspec ts;
	int nprocs;
	int i;

	if (ac != 2) {
		printf("Usage: %s <num procs>\n", av[0]);
		return 1;
	}

	nprocs = atoi(av[1]);
	if (nprocs < 1) {
		printf("Wrong proc count\n");
		return 1;
	}

	printf("Spawning %d processes...\n", nprocs);
	fflush(stdout);

	/* Fork procs. */
	for (i = 1; i < nprocs; i++) {
		int pid = fork();
		if (pid == -1) {
			perror("fork");
			exit(1);
		} else if (pid == 0)
			break;
	}


	memset(&sact, 0, sizeof(sact));

	sact.sa_sigaction = handle_sig;
	sigemptyset(&sact.sa_mask);
	sigaddset(&sact.sa_mask, MYSIG);
	sact.sa_flags = SA_RESTART|SA_SIGINFO;

	ret = sigaction(MYSIG, &sact, NULL);
	if (ret != 0) {
		perror("sigaction");
		return 1;
	}

	memset(&sevt, 0, sizeof(sevt));
	sevt.sigev_notify = SIGEV_SIGNAL;
	sevt.sigev_signo = MYSIG;
	sevt.sigev_value.sival_int = 0;

	ret = timer_create(CLOCK_REALTIME, &sevt, &timer_id);
	if (ret != 0) {
		perror("timer_create");
		return 1;
	}

	ts.it_value.tv_sec = TIMERFREQ / 1000000;
	ts.it_value.tv_nsec = (TIMERFREQ % 1000000) * 1000;

	ts.it_interval.tv_sec = ts.it_value.tv_sec;
	ts.it_interval.tv_nsec = ts.it_value.tv_nsec;

	timer_settime(timer_id, 0, &ts, NULL);

	/* Work loop. */
	while (1);

	return 0;
}
