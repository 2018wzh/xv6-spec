#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void
check(int condition, const char *stage)
{
  if (!condition) {
    printf("VOS_TRACE_FAIL %s\n", stage);
    exit(1);
  }
}

int
main(void)
{
  int descriptors[2];
  char value = 'T';
  char received = 0;

  printf("VOS_TRACE start\n");
  check(pipe(descriptors) == 0, "pipe-create");
  check(write(descriptors[1], &value, 1) == 1, "pipe-write");
  check(read(descriptors[0], &received, 1) == 1 && received == value, "pipe-read");
  close(descriptors[0]);
  close(descriptors[1]);
  printf("VOS_TRACE pipe\n");

  int descriptor = open("vos-trace.tmp", O_CREATE | O_RDWR);
  check(descriptor >= 0, "file-open");
  check(write(descriptor, &value, 1) == 1, "file-write");
  close(descriptor);
  check(unlink("vos-trace.tmp") == 0, "file-unlink");
  printf("VOS_TRACE file\n");

  int child = fork();
  check(child >= 0, "fork");
  if (child == 0)
    exit(0);
  check(wait(0) == child, "wait");
  printf("VOS_TRACE process\n");
  printf("VOS_TRACE_OK\n");
  exit(0);
}
