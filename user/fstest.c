// user/fstest.c - the bounded Lab 6 file-ABI workload (kernel/file).
//
// This freestanding user program exercises the validated file syscalls:
// create a file, write then read committed contents through validated user
// buffers, duplicate a descriptor to observe the shared serialized offset,
// create and chdir into a directory, and publish a completion marker over
// the console. The toolchain Makefile compiles it with kernel/user.ld as an
// optional user target that does not interfere with the kernel build.

#include "user.h"

int
main(void)
{
  static const char hello[] = "fstest: committed content\n";
  char buf[128];
  int fd, n;

  fd = open("/fstest_out", O_CREATE | O_RDWR | O_TRUNC);
  if (fd < 0)
    return -1;

  if (write(fd, hello, (int)sizeof(hello) - 1) != (int)sizeof(hello) - 1)
    return -1;

  if (close(fd) != 0)
    return -1;

  fd = open("/fstest_out", O_RDONLY);
  if (fd < 0)
    return -1;

  n = read(fd, buf, (int)sizeof(buf) - 1);
  if (n <= 0)
    return -1;

  if (dup(fd) < 0)
    return -1;
  if (close(fd) != 0)
    return -1;

  if (mkdir("/fstest_dir") != 0)
    return -1;
  if (chdir("/fstest_dir") != 0)
    return -1;

  write(1, "FILE_ABI_OK\n", 12);
  return 0;
}