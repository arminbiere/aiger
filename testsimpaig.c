#include "simpaig.h"

static void
init_reset (void)
{
  simpaigmgr * mgr = simpaig_init ();
  simpaig_reset (mgr);
}

int
main (void)
{
  init_reset ();

  return 0;
}
