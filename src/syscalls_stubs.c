#include <errno.h>
#include <sys/types.h>

int _getpid(void) {
  return 1;
}

int _kill(pid_t pid, int sig) {
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}
