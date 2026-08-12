#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum state { UNUSED, USED, RUNNABLE, RUNNING, ZOMBIE };
struct slot { enum state state; int parent; int status; int published; };

static int fork_model(struct slot *slots, int cap, int parent, int fail_step)
{
  int i;
  for (i = 1; i < cap; i++) if (slots[i].state == UNUSED) break;
  if (i == cap) return -1;
  slots[i].state = USED;
  if (fail_step) { slots[i] = (struct slot){0}; return -1; }
  slots[i].parent = parent;
  slots[i].published = 1;
  slots[i].state = RUNNABLE;
  return i;
}

static int wait_model(struct slot *slots, int cap, int parent, int *status)
{
  for (int i = 1; i < cap; i++)
    if (slots[i].parent == parent && slots[i].state == ZOMBIE) {
      *status = slots[i].status;
      slots[i] = (struct slot){0};
      return i;
    }
  return -1;
}

int main(int argc, char **argv)
{
  unsigned seed = argc > 1 ? (unsigned)strtoul(argv[1], 0, 10) : 42;
  int cases = argc > 2 ? atoi(argv[2]) : 500;
  struct slot slots[16] = {{0}};
  slots[0].state = RUNNING;
  for (int n = 0; n < cases; n++) {
    seed = seed * 1103515245u + 12345u;
    int fail = (seed >> 16) % 7 == 0;
    int child = fork_model(slots, 16, 0, fail);
    if (fail) { assert(child == -1); continue; }
    assert(child > 0 && slots[child].published);
    slots[child].status = n;
    slots[child].state = ZOMBIE;
    int status = -1;
    assert(wait_model(slots, 16, 0, &status) == child);
    assert(status == n);
    assert(wait_model(slots, 16, 0, &status) == -1);
  }
  puts("PROCESS_TREE_MODEL_OK");
}
