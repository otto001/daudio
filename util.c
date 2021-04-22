/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <errno.h>

#include "util.h"


char buf[1024];


void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

void
die(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

char *readFromPipe(int fd) {
    ssize_t totalRead = 0;

    for (;;) {
        ssize_t count;
        while ((count = read(fd, buf + totalRead, sizeof(buf) - totalRead - 1)) == -1) {
            if (errno != EINTR) {
                return NULL;
            }
        }
        totalRead += count;
        if (totalRead > sizeof(buf)-1) {
            die("buffer overflow");
        }
        else if (count == 0 || totalRead == sizeof(buf)-1) {
            break;
        }
    }
    buf[totalRead] = '\0';
    return buf[0] ? buf : NULL;
}


char *
runCommand(const char *cmd)
{
    FILE *fp;

    if (!(fp = popen(cmd, "r"))) {
        return NULL;
    }

    int fd = fileno(fp);
    readFromPipe(fd);
    return buf[0] ? buf : NULL;
}

const char* execRead(const char *file, const char *params[]) {
    int fileDescriptors[2];
    if (pipe(fileDescriptors) == -1) {
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        return NULL;
    } else if (pid == 0) {
        while ((dup2(fileDescriptors[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        close(fileDescriptors[0]);
        execvp(file, (char *const *) params);
        _exit(1);
    }
    close(fileDescriptors[1]);

    readFromPipe(fileDescriptors[0]);
    close(fileDescriptors[0]);
    waitpid(pid, NULL, 0);

    return buf[0] ? buf : NULL;

}

void timespecSetMs(struct timespec *ts, int32_t ms) {
    ts->tv_sec =  ms/1000;
    ts->tv_nsec = (ms - 1000*ts->tv_sec) * 1000000;
}

void timespecAddMs(struct timespec *ts, int32_t ms) {
    __time_t seconds = ms/1000;
    __time_t nanoseconds = (ms - 1000*seconds) * 1000000 + ts->tv_nsec;
    ts->tv_sec = ts->tv_sec + seconds + nanoseconds/1000000000;
    ts->tv_nsec = nanoseconds % 1000000000;
}


void timespecDiff(struct timespec *res, struct timespec *a, struct timespec *b) {
    res->tv_sec = a->tv_sec - b->tv_sec - (a->tv_nsec < b->tv_nsec);
    res->tv_nsec = a->tv_nsec - b->tv_nsec + (a->tv_nsec < b->tv_nsec) * 1000000000;
}

int32_t timespecToMs(struct timespec *ts) {
    return ts->tv_sec*1000 + ts->tv_nsec/1000000;
}

int timespecGte(struct timespec *a, struct timespec *b) {
    return (a->tv_sec > b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec >= b->tv_nsec));
}

