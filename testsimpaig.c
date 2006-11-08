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
  simpaig * u, * v, * a, * b, * c, * x;
  simpaigmgr * mgr;

  mgr = simpaig_init_mem (0, mymalloc, myfree);

  u = simpaig_var (mgr, mgr, 0);
  v = simpaig_var (mgr, mgr, 1);
  assert (u != v);

  x = simpaig_xor (mgr, u, v);
#if 0
  a = simpaig_and (mgr, u, simpaig_not (v));
  b = simpaig_and (mgr, v, simpaig_not (u));
  c = simpaig_or (mgr, a, b);
  assert (c == x);
  simpaig_dec (mgr, a);
  simpaig_dec (mgr, b);
  simpaig_dec (mgr, c);
#else
  a = simpaig_or (mgr, u, v);
  b = simpaig_or (mgr, simpaig_not (u), simpaig_not (v));
  c = simpaig_and (mgr, a, b);
  assert (c == x);
  simpaig_dec (mgr, a);
  simpaig_dec (mgr, b);
  simpaig_dec (mgr, c);
#endif
  simpaig_dec (mgr, u);
  simpaig_dec (mgr, v);
  simpaig_dec (mgr, x);

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
