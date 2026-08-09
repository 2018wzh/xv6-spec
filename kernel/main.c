// main.c - supervisor-mode entry reached via start()'s mret.

#include "types.h"
#include "riscv.h"
#include "defs.h"

// Called from start() after it mret's into supervisor mode.
void
main(void)
{
  // Publish the Lab 2 deterministic serial banner exactly once.
  publish_boot_banner();

  // Lab 2 has no further mechanisms; park forever. Later labs extend main().
  for (;;)
    ;
}