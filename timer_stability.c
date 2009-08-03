/*
 * Test stability of timer frequency.
 *
 * To compile:
 *	gcc -O2 -o timer_stability timer_stability.c  -lrt -lm
 *
 * To run:
 *	timer_stability ==> Will print usage.
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
#include <getopt.h>

#include <sys/types.h>
#include <sys/time.h>


#define MYSIG	(SIGRTMAX - 2)

#define DFLT_ITERS	1000

#define DFLT_TIMERFREQ	10000	/* us */

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

static int iters, timerfreq, yieldtime, yieldpct;

struct cpu_stat {
	uint64_t	user;
	uint64_t	lowp;
	uint64_t	sys;
	uint64_t	idle;
	uint64_t	iowait;
	uint64_t	irq;
	uint64_t	softirq;
	uint64_t	steal;
};

static inline void
read_proc_stat(struct cpu_stat *cpu)
{
	FILE *fp;
	int num;

	fp = fopen("/proc/stat", "r");
	if (fp == NULL) {
		fprintf(stderr, "Can not open /proc/stat\n");
		exit(1);
	}

	num = fscanf(fp, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
	    &cpu->user, &cpu->lowp, &cpu->sys,
	    &cpu->idle, &cpu->iowait, &cpu->irq, &cpu->softirq,
	    &cpu->steal);
	if (num != 8) {
		fprintf(stderr, "Can not read 8 items from /proc/stat\n");
		exit(1);
	}

	fclose(fp);
}

static inline uint64_t
total_proc_stat_time(struct cpu_stat *cpu)
{

	return cpu->user + cpu->lowp + cpu->sys + cpu->idle +
	    cpu->iowait + cpu->irq + cpu->softirq + cpu->steal;
}


static inline void
iter_update(void)
{
	static uint64_t last_time = 0, min = 0, max = 0;
	static uint64_t gaps = 0;
	static uint64_t gaps_sq = 0;
	static uint64_t count = 0;
	static int pid = 0;
	static struct timespec stime;
	uint64_t curr_time;
	uint64_t gap;
	struct cpu_stat cpu_start, cpu_end;
	long r;

	if (last_time == 0) {
		/* Reset all tracking variables. */
		gaps = gaps_sq = 0;
		count = 0;
		min = 1000000000;
		max = 0;
		pid = getpid();
		read_proc_stat(&cpu_start);
		curr_time = get_time();
		last_time = curr_time;

		if (yieldtime != -1) {
			stime.tv_sec = yieldtime / 1000000;
			stime.tv_nsec = (yieldtime % 1000000) * 1000;

			r = random() % 10000;

			if (r < yieldpct * 100)
				/* Ensure we yield in the first time sample. */
				nanosleep(&stime, NULL);
		}

		/* No timer interval yet, so exit. */
		return;
	} else
		curr_time = get_time();

	if (yieldtime != -1) {
		r = random() % 10000;

		if (r < yieldpct * 100)
			nanosleep(&stime, NULL);
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

	if (count == iters) {
		double std_dev;
		uint64_t elapsed_hz, elapsed_st_hz;

		read_proc_stat(&cpu_end);

		elapsed_hz = total_proc_stat_time(&cpu_end) -
		    total_proc_stat_time(&cpu_start);
		elapsed_st_hz = cpu_end.steal - cpu_start.steal;

		std_dev = sqrt((double)count * (double)gaps_sq -
		    (double)gaps * (double)gaps);
		std_dev /= (double)count;

		printf("P: %d, I: %ld, Min: %ld, Max: %ld, Avg: %4.2f, Dev: %5.1f%% (%4.2f), Steal pct: %5.1f%%\n",
		    pid, count, min, max, (double)gaps / (double)count,
		    (std_dev / (double)timerfreq) * 100.0, std_dev,
		    elapsed_hz == 0 ? -0.1 :
		    ((double)elapsed_st_hz / (double)elapsed_hz) * 100.0);
		fflush(stdout);

		last_time = 0;
	}
}

static void
handle_sig(int sig, siginfo_t *info, void *ctxt)
{

	iter_update();
}

static void
usage(const char *name)
{

	fprintf(stderr,
	    "Usage: %s [--iterations <iters (#)>] [--freq <freq (us)>] \\\n"
	    "          [--yield <time (us)>] [--yieldpct <percentag> ] \\\n"
	    "          [--no-busy-loop] --nprocs <nprocs>\n"
	    "\n"
	    "       %s [--iterations <iters (#)>] [--freq <freq (us)>] \\\n"
	    "          --use-sleep --nprocs <nprocs>\n"
	    "\n"
	    "  Defaults:\n"
	    "       Print iterations: %d\n"
	    "       Timer frequency:  %d us.\n"
	    "         => Will print approx. every: Iterations * Frequency us.\n"
	    "       Yield time: no yield. If set, will usleep for this long\n"
	    "                             each timer fire.\n"
	    "       Yield percentage: 100%%. Will yield this frequently.\n",
	    name, name, DFLT_ITERS, DFLT_TIMERFREQ);
	exit(1);
}

int main(int ac, char **av)
{
	struct sigaction sact;
	int ret;
	timer_t timer_id;
	struct sigevent sevt;
	struct itimerspec ts;
	int nprocs, use_sleep, use_busyloop;
	int i, opt, idx;

	enum {
		OPT_ITERS	= (1 << 8),
		OPT_FREQ,
		OPT_NPROCS,
		OPT_YIELD,
		OPT_YIELDPCT,
		OPT_USESLEEP,
		OPT_NOBUSYLOOP,
	};

	struct option longopts[] = {
		{ "iterations", required_argument, NULL, OPT_ITERS },
		{ "freq", required_argument, NULL, OPT_FREQ },
		{ "nprocs", required_argument, NULL, OPT_NPROCS },
		{ "yield", required_argument, NULL, OPT_YIELD },
		{ "yieldpct", required_argument, NULL, OPT_YIELDPCT },
		{ "no-busy-loop", no_argument, NULL, OPT_NOBUSYLOOP },
		{ "use-sleep", no_argument, NULL, OPT_USESLEEP },
		{ NULL, 0, NULL, 0}
	};

	timerfreq = DFLT_TIMERFREQ;
	iters = DFLT_ITERS;
	yieldtime = -1;
	yieldpct = 100;
	nprocs = -1;
	idx = 0;
	use_sleep = 0;
	use_busyloop = 1;
	while ((opt = getopt_long(ac, av, "", longopts, &idx)) != -1) {
		switch (opt) {
		case OPT_ITERS:
			iters = atoi(optarg);
			break;
		case OPT_FREQ:
			timerfreq = atoi(optarg);
			break;
		case OPT_NPROCS:
			nprocs = atoi(optarg);
			break;
		case OPT_YIELD:
			if (atoi(optarg) >= 0)
				yieldtime = atoi(optarg);
			break;
		case OPT_YIELDPCT:
			if (atoi(optarg) >= 0)
				yieldpct = atoi(optarg);

			break;
		case OPT_USESLEEP:
			use_sleep = 1;
			break;
		case OPT_NOBUSYLOOP:
			use_busyloop = 0;
			break;
		default:
			printf ("Invalid option: %d\n", opt);
			usage(av[0]);
		}
	}

	if (nprocs < 1) {
		fprintf(stderr, "Invalid proc count: %d\n", nprocs);
		usage(av[0]);
	}

	if (use_sleep && yieldtime != -1) {
		fprintf(stderr, "Yield time can not be used with sleep mode.\n");
		usage(av[0]);
	}

	if (yieldpct < 0 || yieldpct > 100) {
		fprintf(stderr, "Yield percentage value invalid: %d\n",
		    yieldpct);
		usage(av[0]);
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

	if (!use_sleep) {
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

		ts.it_value.tv_sec = timerfreq / 1000000;
		ts.it_value.tv_nsec = (timerfreq % 1000000) * 1000;

		ts.it_interval.tv_sec = ts.it_value.tv_sec;
		ts.it_interval.tv_nsec = ts.it_value.tv_nsec;

		timer_settime(timer_id, 0, &ts, NULL);

		/* Work loop. */
		while (1) {
			if (!use_busyloop)
				/* Sleep 60 seconds...this will be interrupted
				 * by the timer anyways.
				 */
				usleep(60000000);
		}
	} else {
		struct timespec freq_ts;

		freq_ts.tv_sec = timerfreq / 1000000;
		freq_ts.tv_nsec = (timerfreq % 1000000) * 1000;

		while (1) {
			iter_update();

			nanosleep(&freq_ts, NULL);
		}
	}

	return 0;
}
