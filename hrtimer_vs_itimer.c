/* hrtimer_vs_itimer: Use an HR timer to DOS an itimer.
 *
 * TO COMPILE
 * ----------
 *  $ gcc -Wall -o hrtimer_vs_itimer hrtimer_vs_itimer.c -lrt
 *
 * TO RUN
 * ------
 *  $ ./hrtimer_vs_itimer
 *
 * DESCRIPTION
 * -----------
 *
 * This test program demonstrates that a periodically firing High
 * Resolution timer can prevent an itimer signal from correctly
 * interrupting a blocking system call. A typical programming practice
 * when using blocking system calls is to install a timeout itimer to
 * interrupt the system call with EINTR if it is still blocking after
 * the timeout duration.
 *
 * This test uses the flock(2) system call to demonstrate how a
 * periodically firing HR timer can block an itimer from correctly
 * interrupting the blocking system call. The HR timer does not block
 * the itimer from firing, but does prevent the itimer from
 * interrupting the system call with EINTR. This leads to a permanent
 * hang of the program because the flock is never interrupted. The
 * condition occurs occaisionally so it is likely a race condition
 * between the HR timer and the itimer.
 *
 * Obviously, a work-around would be to block the HR timer during the
 * blocking system call. However, this is not a permanent fix as the
 * HR timer may be critical for profiling, performance tuning, or
 * heart-beating of the process so blocking it may not be feasible.
 *
 * The basic process flow for the test program is:
 *
 *   [Start]
 *      |
 *      |
 *   [Choose file name]
 *      |
 *      |
 *   [Fork]
 *      +------> [Child]
 *   [Parent]       |
 *      |        [Create/Open File and flock()]
 *   [Sleep 1]      |
 *      |        [Sleep 20]
 *      |
 *   [Install HR Timer]
 *      |
 *   [Open file]
 *      |
 *   [Install itimer]
 *      |
 *   [flock() file] ==> Blocks for min. 3 seconds. Should get EINTR after 3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

/* Define this to block the HR timer -- allows the SIGALRM to interrupt
 * the flock.
 */
/* #define BLOCK_HR_TIMER */

#define MYSIG	(SIGRTMAX - 2)

static void
handle_sig(int sig, siginfo_t *info, void *ctxt)
{
	char msg[] = "HR timer fired!\n";

	/* Avoid printf() due to potential malloc. */
	write(STDOUT_FILENO, msg, strlen(msg));
}

static void
handle_alrm(int sig)
{
	char msg[] = "SIGALRM fired!\n";

	write(STDOUT_FILENO, msg, strlen(msg));
}

static int
create_timer()
{
	struct sigaction sact;
	timer_t timer_id;
	struct sigevent sevt;
	struct itimerspec ts;
	int ret;

	memset(&sact, 0, sizeof(sact));

	sact.sa_sigaction = handle_sig;
	/* Ignore everything in our signal handler. */
	sigfillset(&sact.sa_mask);
	sact.sa_flags = SA_RESTART|SA_SIGINFO;

	ret = sigaction(MYSIG, &sact, NULL);
	if (ret != 0) {
		perror("sigaction");
		return -1;
	}

	memset(&sevt, 0, sizeof(sevt));
	sevt.sigev_notify = SIGEV_SIGNAL;
	sevt.sigev_signo = MYSIG;
	sevt.sigev_value.sival_int = 0;

	ret = timer_create(CLOCK_REALTIME, &sevt, &timer_id);
	if (ret != 0) {
		perror("timer_create");
		return -1;
	}

	/* Signal fires every one second. */
	ts.it_value.tv_sec = 1;
	ts.it_value.tv_nsec = 0;

	ts.it_interval.tv_sec = ts.it_value.tv_sec;
	ts.it_interval.tv_nsec = ts.it_value.tv_nsec;

	timer_settime(timer_id, 0, &ts, NULL);

	return 0;
}

/*
 * Retreive the current time in microseconds. (time won't go backwards)
 */
static uint64_t
get_time(void)
{
	uint64_t current_time=0;
	static uint64_t last_time = 0;
	struct timeval time_val;
	int save_errno;

	/* Preserve errno. */
	save_errno = errno;

	gettimeofday(&time_val, NULL);
	current_time = time_val.tv_sec * 1000000LL + time_val.tv_usec;
  
	if (current_time < last_time) {
		errno = save_errno;
		return last_time;
	}

	last_time = current_time;

	errno = save_errno;
	return current_time;
}

int main(int ac, char **av)
{
	int fd, pid, ret;
	char template[] = "/tmp/tmpXXXXXX";
	struct itimerval it;
	struct sigaction sact;
	uint64_t start, end;
#ifdef BLOCK_HR_TIMER
	sigset_t set;
#endif

	if (tmpnam(template) == NULL)
		return 1;

	pid = fork();
	if (pid == 0) {
		/*
		 * The child process will open and hold a lock on the file.
		 */
		fd = open(template, O_RDONLY|O_CREAT|O_EXCL, 0600);
		if (fd == -1)
			exit(1);
		printf("Child attempting to grab file lock..\n");
		if (flock(fd, LOCK_EX) != 0) {
			perror("flock");
			exit(1);
		}
		printf("Child grabbed file lock...sleeping\n");
		sleep(20);
		exit(0);
	} else
		sleep(1);

	/* Create a periodic timer. */
	if (create_timer() != 0) {
		fprintf(stderr, "Failed to create timer.\n");
		return 1;
	}

	/* Open file. */
	fd = open(template, O_RDONLY, 0600);
	if (fd == -1)
		exit(1);

	/* Let's do our housekeeping. */

	/* XXX: Uncommenting this appears to make the SIGALRM
	 * interrupt the flock() MUCH more frequently. I've still seen
	 * the EINTR not happen with unlinking, but removing this
	 * makes it alot harder to hit the indefinite block condition.
	 */
	/*unlink(template);*/


	/* Construct an empty handler for SIGALRM that will fire
	 * and interrupt the blocking flock below.
	 */
	memset(&sact, 0, sizeof(sact));
	sact.sa_handler = handle_alrm;
	sigfillset(&sact.sa_mask);
	sact.sa_flags = 0;

	ret = sigaction(SIGALRM, &sact, NULL);
	if (ret != 0) {
		perror("sigaction");
		return -1;
	}

	/* Set a 3 second timer that should fire and interrupt our
	 * blocking flock.
	 */
	it.it_value.tv_sec = 3;
	it.it_value.tv_usec = 0;
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
		perror("setitimer");
		return 1;
	}

	printf("Parent attempting to grab file lock (will block)\n");
	fflush(stdout);

#ifdef BLOCK_HR_TIMER
	sigemptyset(&set);
	sigaddset(&set, MYSIG);
	sigprocmask(SIG_BLOCK, &set, NULL);
#endif

	start = get_time();
	ret = flock(fd, LOCK_EX);
	end = get_time();

	printf("flock returned with %d (errno %d:%s) after %4.2f "
	    "secs (wanted ~3)\n", ret, errno, strerror(errno),
	    ((double)end - (double)start) / 1000000);

	return 0;
}
