#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static uint
next_random(uint *state)
{
  *state = *state * 1664525U + 1013904223U;
  return *state;
}

static void
fail(uint seed, int test_case, int operation)
{
  printf("VOS_FUZZ_REPRO seed=%d case=%d op=%d\n", seed, test_case, operation);
  exit(1);
}

int
main(int argc, char **argv)
{
  uint seed = argc > 1 ? (uint)atoi(argv[1]) : 20260809U;
  int cases = argc > 2 ? atoi(argv[2]) : 128;
  uint state = seed;
  char byte;

  if (cases < 1 || cases > 4096)
    fail(seed, -1, -1);
  for (int i = 0; i < cases; i++) {
    int operation = next_random(&state) % 3;
    if (operation == 0) {
      int size = (next_random(&state) % 128) + 1;
      char *memory = malloc(size);
      if (memory == 0)
        fail(seed, i, operation);
      byte = (char)next_random(&state);
      memset(memory, byte, size);
      for (int j = 0; j < size; j++)
        if (memory[j] != byte)
          fail(seed, i, operation);
      free(memory);
    } else if (operation == 1) {
      int descriptors[2];
      char received = 0;
      byte = (char)next_random(&state);
      if (pipe(descriptors) < 0 || write(descriptors[1], &byte, 1) != 1 ||
          read(descriptors[0], &received, 1) != 1 || received != byte)
        fail(seed, i, operation);
      close(descriptors[0]);
      close(descriptors[1]);
    } else {
      int descriptor = open("vos-fuzz.tmp", O_CREATE | O_RDWR);
      byte = (char)next_random(&state);
      if (descriptor < 0 || write(descriptor, &byte, 1) != 1)
        fail(seed, i, operation);
      close(descriptor);
      if (unlink("vos-fuzz.tmp") < 0)
        fail(seed, i, operation);
    }
  }
  printf("VOS_FUZZ_OK seed=%d cases=%d\n", seed, cases);
  exit(0);
}
