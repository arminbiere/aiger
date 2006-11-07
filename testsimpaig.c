#include "simpaig.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>

static size_t allocated;

static void *
mymalloc (void * dummy, size_t bytes)
{
  allocated += bytes;
  return malloc (bytes);
}

static void
myfree (void * dummy, void * ptr, size_t bytes)
{
  assert (allocated >= bytes);
  allocated -= bytes;
  free (ptr);
}

static void
init_reset (void)
{
  simpaigmgr * mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig_reset (mgr);
  assert (!allocated);
}

static void
xorcmp (void)
{
  simpaigmgr * mgr;
  simpaig * u, * v;
  int a;

  mgr = simpaig_init_mem (0, mymalloc, myfree);

  u = simpaig_var (mgr, &a, 0);
  v = simpaig_var (mgr, &a, 1);
  assert (u != v);

  simpaig_dec (mgr, u);
  simpaig_dec (mgr, v);

  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);
  assert (!allocated);
}

int
main (void)
{
  init_reset ();
  xorcmp ();

  return 0;
}
