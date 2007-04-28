/***************************************************************************
Copyright (c) 2006, Armin Biere, Johannes Kepler University.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
***************************************************************************/

#include "simpaig.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>

static size_t allocated;

static void *
mymalloc (void *dummy, size_t bytes)
{
  allocated += bytes;
  return malloc (bytes);
}

static void
myfree (void *dummy, void *ptr, size_t bytes)
{
  assert (allocated >= bytes);
  allocated -= bytes;
  free (ptr);
}

static void
initreset (void)
{
  simpaigmgr *mgr = simpaig_init_mem (0, mymalloc, myfree);
  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);
  assert (!allocated);
}

static void
xorcmp (void)
{
  simpaig *u, *v, *a, *b, *c, *x;
  simpaigmgr *mgr;

  mgr = simpaig_init_mem (0, mymalloc, myfree);

  u = simpaig_var (mgr, mgr, 0);
  v = simpaig_var (mgr, mgr, 1);
  assert (u != v);

  x = simpaig_xor (mgr, u, v);
#if 0
  /* Only one of the two branches is true unless we do not have heavy
   * simplification enabled.
   */
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
  simpaigmgr *mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig *u, *v, *x, *a;

  u = simpaig_var (mgr, mgr, 0);
  v = simpaig_var (mgr, mgr, 1);
  assert (u != v);
  x = simpaig_xor (mgr, u, v);

  simpaig_assign (mgr, v, u);
  a = simpaig_substitute (mgr, x);
  assert (simpaig_isfalse (a));
  simpaig_dec (mgr, a);

  simpaig_assign (mgr, v, simpaig_not (u));
  a = simpaig_substitute (mgr, x);
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
  simpaigmgr *mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig *u, *v, *w, *a, *b, *c;

  u = simpaig_var (mgr, "u", 0);
  v = simpaig_var (mgr, "v", 1);
  w = simpaig_var (mgr, "w", 2);
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

static void
tseitin (void)
{
  simpaigmgr *mgr = simpaig_init_mem (0, mymalloc, myfree);
  simpaig *u, *v, *a, *f;

  u = simpaig_var (mgr, "u", 0);
  v = simpaig_var (mgr, "v", 0);
  a = simpaig_and (mgr, u, v);
  f = simpaig_false (mgr);

  assert (simpaig_max_index (mgr) == 0);
  assert (simpaig_index (f) == 0);
  assert (simpaig_int_index (f) == 1);
  assert (simpaig_int_index (simpaig_not (f)) == -1);
  assert (simpaig_unsigned_index (f) == 0);
  assert (simpaig_unsigned_index (simpaig_not (f)) == 1);

  simpaig_assign_indices (mgr, f);
  assert (simpaig_max_index (mgr) == 0);

  simpaig_assign_indices (mgr, v);
  assert (simpaig_max_index (mgr) == 1);
  assert (simpaig_index (v) == 1);
  simpaig_assign_indices (mgr, a);
  assert (simpaig_max_index (mgr) == 3);
  assert (simpaig_index (u) == 2);
  assert (simpaig_index (a) == 3);

  simpaig_dec (mgr, a);
  simpaig_dec (mgr, u);
  simpaig_dec (mgr, v);
  simpaig_dec (mgr, f);

  simpaig_reset_indices (mgr);	/* otherwise node leaks */
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
  tseitin ();

  return 0;
}
