#ifndef OSX_H
#define OSX_H

#include <dirent.h>
#include <sys/types.h>
#include <time.h>

struct pollfd {
  int fd;
  short events;
  short revents;
};

#define POLLIN 1
#define POLLOUT 2
#define POLLPRI 3

extern int poll(struct pollfd *, unsigned long, int);
extern char *strtok_r(char *s, const char *delim, char **last);
extern int readdir_r(DIR * dir, struct dirent *ent, struct dirent **entPtr);
extern char *ctime_r(const time_t * clock, char *buf);
extern char *asctime_r(const struct tm *tmPtr, char *buf);
extern struct tm *localtime_r(const time_t * clock, struct tm *ptmPtr);
extern struct tm *gmtime_r(const time_t * clock, struct tm *ptmPtr);
extern int pthread_sigmask(int how, sigset_t *set, sigset_t *oset);
extern int sigwait(sigset_t * set, int *sig);

#endif
