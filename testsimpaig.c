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
initreset (void)
{
  simpaigmgr * mgr = simpaig_init_mem (0, mymalloc, myfree);
  assert (!simpaig_current_nodes (mgr));
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

static void
subst (void)
{
  simpaigmgr * mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig * u, * v, * x, * a;

  u = simpaig_var (mgr, mgr, 0);
  v = simpaig_var (mgr, mgr, 1);
  assert (u != v);
  x = simpaig_xor (mgr, u, v);

  simpaig_assign (mgr, v, u);
  a  = simpaig_substitute (mgr, x);
  assert (simpaig_isfalse (a));
  simpaig_dec (mgr, a);

  simpaig_assign (mgr, v, simpaig_not (u));
  a  = simpaig_substitute (mgr, x);
  assert (simpaig_istrue (a));
  simpaig_dec (mgr, a);

  simpaig_dec (mgr, u);
  simpaig_dec (mgr, v);
  simpaig_dec (mgr, x);

  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);
  assert (!allocated);
}

static void
shift (void)
{
  simpaigmgr * mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig * u, * v, * w, * a, * b, * c;

  u = simpaig_var (mgr, "u", 0);
  v = simpaig_var (mgr, "v", 0);
  w = simpaig_var (mgr, "w", 0);
  assert (u != v);
  assert (u != w);
  assert (v != w);

  a = simpaig_ite (mgr, u, v, w);
  b = simpaig_shift (mgr, a, 1);
  c = simpaig_shift (mgr, b, -1);
  assert (a == c);

  simpaig_dec (mgr, u);
  simpaig_dec (mgr, v);
  simpaig_dec (mgr, w);

  simpaig_dec (mgr, a);
  simpaig_dec (mgr, b);
  simpaig_dec (mgr, c);

  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);
  assert (!allocated);
}

int
main (void)
{
  initreset ();
  xorcmp ();
  subst ();
  shift ();

  return 0;
}
