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
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#define MYSIG	(SIGRTMAX - 2)

#define DFLT_ITERS	1000

#define DFLT_TIMERFREQ	10000	/* us */

#define DFLT_IO_BS	16384
#define DFLT_IO_COUNT	20000
#define DFLT_IO_WAIT	4	/* seconds */

static ssize_t
readn(int fd, void *buf, size_t count)
{
	size_t r, toread;

	toread = count;
	while (toread > 0) {
		r = read(fd, buf + (count - toread), toread);
		if (r <= 0) {
			fprintf(stderr, "Read returned %zd\n",
			    r);
			return count - toread;
		}

		toread -= r;
	}

	return count;
}

static ssize_t
writen(int fd, void *buf, size_t count)
{
	size_t r, towrite;

	towrite = count;
	while (towrite > 0) {
		r = write(fd, buf + (count - towrite), towrite);
		if (r <= 0) {
			fprintf(stderr, "Write returned %zd\n",
			    r);
			return count - towrite;
		}

		towrite -= r;
	}

	return count;
}

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

static inline int
read_proc_stat(struct cpu_stat *cpu)
{
	FILE *fp;
	int num;
	static int printed_err = 0;

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
		if (!printed_err) {
			fprintf(stderr,
				"Can not read 8 items from /proc/stat\n");
			printed_err = 1;
		}
		fclose(fp);
		return 1;
	}

	fclose(fp);

	return 0;
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
	static int use_proc_stat = 0;
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
		if (read_proc_stat(&cpu_start) != 0)
			use_proc_stat = 0;
		else
			use_proc_stat = 1;
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

		if (use_proc_stat) {
			read_proc_stat(&cpu_end);

			elapsed_hz = total_proc_stat_time(&cpu_end) -
			    total_proc_stat_time(&cpu_start);
			elapsed_st_hz = cpu_end.steal - cpu_start.steal;
		} else {
			elapsed_hz = 0;
			elapsed_st_hz = 0;
		}

		std_dev = sqrt((double)count * (double)gaps_sq -
		    (double)gaps * (double)gaps);
		std_dev /= (double)count;

		printf("T> P: %d, I: %ld, Min: %ld, Max: %ld, Avg: %7.1f, Dev: %5.1f%% (%4.2f), Steal pct: %5.1f%%\n",
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
	    "          [--io-procs <num>] [--io-bs <bs>] \\\n"
	    "          [--io-count <count>] [--io-wait <secs>] \\\n"
	    "          [--no-busy-loop] \\\n"
	    "          --nprocs <nprocs>\n"
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
	    "       Yield percentage: 100%%. Will yield this frequently.\n"
	    "       I/O Processes: zero. Starts a 'dd' like process to\n"
	    "                            generate I/O load.\n"
	    "       I/O Blocksize: 16k\n"
	    "       I/O Count: 20000\n"
	    "       I/O Wait: 4 seconds\n"
	    ,
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
	int io_procs, io_count, io_wait, io_bs;
	char *procname, *buf;
	size_t procname_len;
	int proc_index;

	enum {
		OPT_ITERS	= (1 << 8),
		OPT_FREQ,
		OPT_NPROCS,
		OPT_YIELD,
		OPT_YIELDPCT,
		OPT_USESLEEP,
		OPT_NOBUSYLOOP,
		OPT_IO_PROCS,
		OPT_IO_BS,
		OPT_IO_COUNT,
		OPT_IO_WAIT,
	};

	struct option longopts[] = {
		{ "iterations", required_argument, NULL, OPT_ITERS },
		{ "freq", required_argument, NULL, OPT_FREQ },
		{ "nprocs", required_argument, NULL, OPT_NPROCS },
		{ "yield", required_argument, NULL, OPT_YIELD },
		{ "yieldpct", required_argument, NULL, OPT_YIELDPCT },
		{ "no-busy-loop", no_argument, NULL, OPT_NOBUSYLOOP },
		{ "use-sleep", no_argument, NULL, OPT_USESLEEP },
		{ "io-procs", required_argument, NULL, OPT_IO_PROCS },
		{ "io-bs", required_argument, NULL, OPT_IO_BS },
		{ "io-count", required_argument, NULL, OPT_IO_COUNT },
		{ "io-wait", required_argument, NULL, OPT_IO_WAIT },
		{ NULL, 0, NULL, 0}
	};

	procname_len = (av[ac - 1] + strlen(av[ac - 1])) - av[0];

	timerfreq = DFLT_TIMERFREQ;
	iters = DFLT_ITERS;
	yieldtime = -1;
	yieldpct = 100;
	nprocs = -1;
	idx = 0;
	use_sleep = 0;
	use_busyloop = 1;
	io_procs = 0;
	io_bs = DFLT_IO_BS;
	io_count = DFLT_IO_COUNT;
	io_wait = DFLT_IO_WAIT;
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
		case OPT_IO_PROCS:
			io_procs = atoi(optarg);
			break;
		case OPT_IO_BS:
			io_bs = atoi(optarg);
			break;
		case OPT_IO_COUNT:
			io_count = atoi(optarg);
			break;
		case OPT_IO_WAIT:
			io_wait = atoi(optarg);
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

	if (io_procs < 0) {
		fprintf(stderr, "Invalid number of I/O procs: %d\n", io_procs);
		usage(av[0]);
	}

	if (io_bs <= 0) {
		fprintf(stderr, "Invalid I/O blocksize: %d\n", io_bs);
		usage(av[0]);
	}

	if (io_count < 0) {
		fprintf(stderr, "Invalid I/O count: %d\n", io_count);
		usage(av[0]);
	}

	if (io_wait < 0) {
		fprintf(stderr, "Invalid I/O wait time: %d\n", io_wait);
		usage(av[0]);
	}

	procname = calloc(procname_len, sizeof(char));
	if (procname == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	printf("Spawning %d timer processes...\n", nprocs);
	fflush(stdout);

	/* Fork procs. */
	for (i = 1; i < nprocs; i++) {
		proc_index = i;
		int pid = fork();
		if (pid == -1) {
			perror("fork");
			exit(1);
		} else if (pid == 0)
			goto timer_proc;
	}

	/* Fork I/O processes. */
	if (io_procs > 0) {
		printf("Spawning %d I/O processes...\n", io_procs);
		fflush(stdout);

		for (i = 0; i < io_procs; i++) {
			proc_index = i;
			int pid = fork();
			if (pid == -1) {
				perror("fork");
				exit(1);
			} else if (pid == 0)
				goto io_proc;
		}
	}

	/* Proc index for main process is zero and always a timer. */
	proc_index = 0;

timer_proc:
	snprintf(procname, procname_len, "Timer #%d", proc_index);
	memcpy(av[0], procname, procname_len);

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


io_proc:
	snprintf(procname, procname_len, "I/O Load #%d", proc_index);
	memcpy(av[0], procname, procname_len);

	buf = malloc(io_bs);
	if (buf == NULL) {
		fprintf(stderr, "Failed to allocate I/O buffer\n");
		exit(1);
	}

	while (1) {
		int ifd, ofd, j;
		uint64_t io_start, io_end;
		ssize_t rd, wr;
		char filetmp[] = "/tmp/tmpXXXXXXXXXX";

		ifd = open("/dev/zero", O_RDONLY);
		if (ifd == -1) {
			fprintf(stderr, "Failed to open /dev/zero\n");
			exit(1);
		}

		ofd = mkstemp(filetmp);
		if (ofd == -1) {
			fprintf(stderr, "Failed to open tempfile\n");
			exit(1);
		} else
			unlink(filetmp);

		io_start = get_time();
		for (j = 0; j < io_count; j++) {
			rd = readn(ifd, buf, io_bs);
			if (rd != io_bs) {
				fprintf(stderr,
				    "Failed to read from input file");
				exit(1);
			}

			wr = writen(ofd, buf, rd);
			if (wr != rd) {
				fprintf(stderr,
				    "Failed to write to output file");
				exit(1);
			}
		}

		/* Include the close in the time calc to include time to
		 * flush the buffer cache.
		 */
		close(ofd);
		io_end = get_time();

		close(ifd);

		printf("I> P: %d, MBytes: %5.1f, Time (s): %4.1f, MB/s: %5.1f\n",
		    getpid(),
		    ((double)io_bs * (double)io_count) / (double)1000000,
		    ((double)io_end - (double)io_start) / (double)1000000,
		    ((double)io_bs * (double)io_count) /
		    ((double)io_end - (double)io_start));
		fflush(stdout);

		if (io_wait > 0)
			usleep(io_wait * 1000000);
	}

	return 0;
}
