/***************************************************************************
Copyright (c) 2006-2018, Armin Biere, Johannes Kepler University.

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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = mgr->malloc (mgr->mem, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define REALLOCN(p,m,n) \
  do { \
    size_t mbytes = (m) * sizeof (*(p)); \
    size_t nbytes = (n) * sizeof (*(p)); \
    size_t minbytes = (mbytes < nbytes) ? mbytes : nbytes; \
    void * res = mgr->malloc (mgr->mem, nbytes); \
    memcpy (res, (p), minbytes); \
    if (nbytes > mbytes) \
      memset (((char*)res) + mbytes, 0, nbytes - mbytes); \
    mgr->free (mgr->mem, (p), mbytes); \
    (p) = res; \
  } while (0)

#define ENLARGE(p,s) \
  do { \
    size_t old_size = (s); \
    size_t new_size = old_size ? 2 * old_size : 1; \
    REALLOCN (p,old_size,new_size); \
    (s) = new_size; \
  } while (0)

#define PUSH(p,t,s,l) \
  do { \
    if ((t) == (s)) \
      ENLARGE (p, s); \
    (p)[(t)++] = (l); \
  } while (0)

#define DELETEN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    mgr->free (mgr->mem, (p), bytes); \
    (p) = 0; \
  } while (0)

typedef unsigned long WORD;

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

#define NOT(p) ((simpaig*)(1^(WORD)(p)))
#define STRIP(p) ((simpaig*)((~1)&(WORD)(p)))
#define CONSTSTRIP(p) ((const simpaig*)((~1)&(WORD)(p)))
#define SIGN(p) (1&(WORD)(p))
#define ISVAR(p) (!SIGN(p) && (p)->var != 0)
#define ISFALSE(p) (!SIGN (p) && !(p)->var && !(p)->c0)
#define ISTRUE(p) (SIGN (p) && ISFALSE (NOT(p)))
#define ISCONST(p) (!STRIP(p)->var && !STRIP(p)->c0)
#define ISAND(p) (!SIGN(p) && ((p)->c0 != 0))

#define IMPORT(p) \
  (assert (simpaig_valid (p)), ((simpaig *)(p)))

struct simpaig
{
  void *var;			/* generic variable pointer */
  int slice;			/* time slice */
  simpaig *c0;			/* child 0 */
  simpaig *c1;			/* child 1 */

  unsigned ref;			/* reference counter */
  simpaig *next;		/* collision chain */
  simpaig *cache;		/* cache for substitution and shifting */
  simpaig *rhs;			/* right hand side (RHS) for substitution */
  unsigned idx;			/* tseitin index */
};

struct simpaigmgr
{
  void *mem;
  simpaig_malloc malloc;
  simpaig_free free;

  simpaig false_aig;
  simpaig **table;
  unsigned count_table;
  unsigned size_table;

  simpaig **cached;
  unsigned count_cached;
  unsigned size_cached;

  simpaig **assigned;
  unsigned count_assigned;
  unsigned size_assigned;

  simpaig **indices;
  unsigned count_indices;
  unsigned size_indices;
};


#ifndef NDEBUG

static int
simpaig_valid (const simpaig * aig)
{
  const simpaig *tmp = CONSTSTRIP (aig);
  return tmp && tmp->ref > 0;
}

#endif

int
simpaig_isfalse (const simpaig * aig)
{
  return ISFALSE (IMPORT (aig));
}

int
simpaig_istrue (const simpaig * aig)
{
  return ISFALSE (NOT (IMPORT (aig)));
}

int
simpaig_signed (const simpaig * aig)
{
  return SIGN (IMPORT (aig));
}

void *
simpaig_isvar (const simpaig * aig)
{
  const simpaig *tmp = IMPORT (aig);
  return ISVAR (tmp) ? tmp->var : 0;
}

int
simpaig_slice (const simpaig * aig)
{
  const simpaig *tmp = IMPORT (aig);
  return ISVAR (tmp) ? aig->slice : 0;
}

int
simpaig_isand (const simpaig * aig)
{
  const simpaig *tmp = STRIP (IMPORT (aig));
  return !tmp->var && tmp->c0;
}


simpaig *
simpaig_strip (simpaig * aig)
{
  return STRIP (IMPORT (aig));
}

simpaig *
simpaig_not (simpaig * aig)
{
  return NOT (IMPORT (aig));
}

simpaig *
simpaig_child (simpaig * aig, int child)
{
  aig = STRIP (aig);
  assert (ISAND (aig));
  assert (child == 0 || child == 1);
  return child ? aig->c1 : aig->c0;
}

static void *
simpaig_default_malloc (void *state, size_t bytes)
{
  return malloc (bytes);
}

static void
simpaig_default_free (void *state, void *ptr, size_t bytes)
{
  free (ptr);
}

simpaigmgr *
simpaig_init (void)
{
  return simpaig_init_mem (0, simpaig_default_malloc, simpaig_default_free);
}

simpaigmgr *
simpaig_init_mem (void *mem_mgr, simpaig_malloc m, simpaig_free f)
{
  simpaigmgr *mgr;
  mgr = m (mem_mgr, sizeof (*mgr));
  memset (mgr, 0, sizeof (*mgr));
  mgr->mem = mem_mgr;
  mgr->malloc = m;
  mgr->free = f;
  return mgr;
}

void
simpaig_reset (simpaigmgr * mgr)
{
  simpaig *p, *next;
  unsigned i;

  for (i = 0; i < mgr->count_table; i++)
    {
      for (p = mgr->table[i]; p; p = next)
	{
	  next = p->next;
	  DELETE (p);
	}
    }

  DELETEN (mgr->table, mgr->size_table);
  DELETEN (mgr->cached, mgr->size_cached);
  DELETEN (mgr->assigned, mgr->size_assigned);
  DELETEN (mgr->indices, mgr->size_indices);

  mgr->free (mgr->mem, mgr, sizeof (*mgr));
}


unsigned
simpaig_current_nodes (simpaigmgr * mgr)
{
  return mgr->count_table + mgr->false_aig.ref;
}

static simpaig *
inc (simpaig * res)
{
  simpaig *tmp = STRIP (res);
  if (tmp->ref < UINT_MAX) tmp->ref++;
  return res;
}

simpaig *
simpaig_false (simpaigmgr * mgr)
{
  return inc (&mgr->false_aig);
}

simpaig *
simpaig_true (simpaigmgr * mgr)
{
  return NOT (inc (&mgr->false_aig));
}

simpaig *
simpaig_inc (simpaigmgr * mgr, simpaig * res)
{
  return inc (IMPORT (res));
}

static unsigned
simpaig_hash_ptr (void *ptr)
{
  return (unsigned) (unsigned long)ptr;
}

static unsigned
simpaig_hash (simpaigmgr * mgr,
	      void *var, int slice, simpaig * c0, simpaig * c1)
{
  unsigned res = 1223683247 * simpaig_hash_ptr (var);
  res += 2221648459u * slice;
  res += 432586151 * simpaig_hash_ptr (c0);
  res += 3333529009u * simpaig_hash_ptr (c1);
  res &= mgr->size_table - 1;
  return res;
}

static void
simpaig_enlarge (simpaigmgr * mgr)
{
  simpaig **old_table, *p, *next;
  unsigned old_size_table, i, h;

  old_table = mgr->table;
  old_size_table = mgr->size_table;

  mgr->size_table = old_size_table ? 2 * old_size_table : 1;
  NEWN (mgr->table, mgr->size_table);

  for (i = 0; i < old_size_table; i++)
    {
      for (p = old_table[i]; p; p = next)
	{
	  next = p->next;
	  h = simpaig_hash (mgr, p->var, p->slice, p->c0, p->c1);
	  p->next = mgr->table[h];
	  mgr->table[h] = p;
	}
    }

  DELETEN (old_table, old_size_table);
}

static simpaig **
simpaig_find (simpaigmgr * mgr,
	      void *var, int slice, simpaig * c0, simpaig * c1)
{
  simpaig **res, *n;
  unsigned h = simpaig_hash (mgr, var, slice, c0, c1);
  for (res = mgr->table + h;
       (n = *res) &&
       (n->var != var || n->slice != slice || n->c0 != c0 || n->c1 != c1);
       res = &n->next)
    ;
  return res;
}

static void
dec (simpaigmgr * mgr, simpaig * aig)
{
  simpaig **p;

  aig = STRIP (aig);

  assert (aig);
  assert (aig->ref > 0);

  if (aig->ref < UINT_MAX) aig->ref--;

  if (aig->ref)
    return;

  if (ISFALSE (aig))
    return;

  if (aig->c0)
    {
      dec (mgr, aig->c0);	/* TODO: derecursify */
      dec (mgr, aig->c1);	/* TODO: derecursify */
    }

  p = simpaig_find (mgr, aig->var, aig->slice, aig->c0, aig->c1);
  if (*p != aig)
    p = simpaig_find (mgr, aig->var, aig->slice, aig->c1, aig->c0);

  assert (*p == aig);
  *p = aig->next;
  DELETE (aig);

  assert (mgr->count_table > 0);
  mgr->count_table--;
}

void
simpaig_dec (simpaigmgr * mgr, simpaig * res)
{
  dec (mgr, IMPORT (res));
}

simpaig *
simpaig_var (simpaigmgr * mgr, void *var, int slice)
{
  simpaig **p, *res;
  assert (var);
  if (mgr->size_table == mgr->count_table)
    simpaig_enlarge (mgr);

  p = simpaig_find (mgr, var, slice, 0, 0);
  if (!(res = *p))
    {
      NEW (res);
      res->var = var;
      res->slice = slice;
      mgr->count_table++;
      *p = res;
    }

  return inc (res);
}

simpaig *
simpaig_and (simpaigmgr * mgr, simpaig * c0, simpaig * c1)
{
  simpaig **p, *res;

  if (ISFALSE (c0) || ISFALSE (c1) || c0 == NOT (c1))
    return simpaig_false (mgr);

  if (ISTRUE (c0) || c0 == c1)
    return inc (c1);

  if (ISTRUE (c1))
    return inc (c0);

  if (mgr->size_table == mgr->count_table)
    simpaig_enlarge (mgr);

  p = simpaig_find (mgr, 0, 0, c1, c0);
  if (!(res = *p))
    {
      p = simpaig_find (mgr, 0, 0, c0, c1);
      if (!(res = *p))
	{
	  NEW (res);
	  res->c0 = inc (c0);
	  res->c1 = inc (c1);
	  mgr->count_table++;
	  *p = res;
	}
    }

  return inc (res);
}

simpaig *
simpaig_or (simpaigmgr * mgr, simpaig * a, simpaig * b)
{
  return NOT (simpaig_and (mgr, NOT (a), NOT (b)));
}

simpaig *
simpaig_implies (simpaigmgr * mgr, simpaig * a, simpaig * b)
{
  return NOT (simpaig_and (mgr, a, NOT (b)));
}

simpaig *
simpaig_xor (simpaigmgr * mgr, simpaig * a, simpaig * b)
{
  simpaig *l = simpaig_or (mgr, a, b);
  simpaig *r = simpaig_or (mgr, NOT (a), NOT (b));
  simpaig *res = simpaig_and (mgr, l, r);
  dec (mgr, l);
  dec (mgr, r);
  return res;
}

simpaig *
simpaig_xnor (simpaigmgr * mgr, simpaig * a, simpaig * b)
{
  return simpaig_xor (mgr, a, NOT (b));
}

simpaig *
simpaig_ite (simpaigmgr * mgr, simpaig * c, simpaig * t, simpaig * e)
{
  simpaig *l = simpaig_implies (mgr, c, t);
  simpaig *r = simpaig_implies (mgr, NOT (c), e);
  simpaig *res = simpaig_and (mgr, l, r);
  dec (mgr, l);
  dec (mgr, r);
  return res;
}

void
simpaig_assign (simpaigmgr * mgr, simpaig * lhs, simpaig * rhs)
{
  lhs = IMPORT (lhs);
  rhs = IMPORT (rhs);

  assert (ISVAR (lhs));
  assert (!lhs->rhs);
  assert (!lhs->cache);

  inc (lhs);
  lhs->rhs = inc (rhs);
  PUSH (mgr->assigned, mgr->count_assigned, mgr->size_assigned, lhs);
}

static void
simpaig_reset_assignment (simpaigmgr * mgr)
{
  simpaig *aig;
  int i;

  for (i = 0; i < mgr->count_assigned; i++)
    {
      aig = mgr->assigned[i];
      assert (aig->rhs);
      dec (mgr, aig->rhs);
      aig->rhs = 0;
      dec (mgr, aig);
    }

  mgr->count_assigned = 0;
}

static void
simpaig_cache (simpaigmgr * mgr, simpaig * lhs, simpaig * rhs)
{
  assert (!SIGN (lhs));
  assert (!lhs->cache);
  inc (lhs);
  lhs->cache = inc (rhs);
  PUSH (mgr->cached, mgr->count_cached, mgr->size_cached, lhs);
}

static void
simpaig_reset_cache (simpaigmgr * mgr)
{
  simpaig *aig;
  int i;

  for (i = 0; i < mgr->count_cached; i++)
    {
      aig = mgr->cached[i];
      assert (aig);
      assert (!SIGN (aig));
      assert (aig->cache);
      dec (mgr, aig->cache);
      aig->cache = 0;
      dec (mgr, aig);
    }

  mgr->count_cached = 0;
}

static simpaig *
simpaig_substitute_rec (simpaigmgr * mgr, simpaig * node)
{
  simpaig *res, *l, *r;
  unsigned sign;

  sign = SIGN (node);
  if (sign)
    node = NOT (node);

  if (node->cache)
    {
      res = inc (node->cache);
    }
  else
    {
      if (ISVAR (node))
	{
	  if (node->rhs)
	    {
	      if (ISCONST (node->rhs))
		res = simpaig_inc (mgr, node->rhs);
	      else
		res = simpaig_substitute_rec (mgr, node->rhs);
	    }
	  else
	    res = inc (node);
	}
      else
	{
	  l = simpaig_substitute_rec (mgr, node->c0);	/* TODO derecursify */
	  r = simpaig_substitute_rec (mgr, node->c1);	/* TODO derecursify */
	  res = simpaig_and (mgr, l, r);
	  dec (mgr, l);
	  dec (mgr, r);
	}

      simpaig_cache (mgr, node, res);
    }

  if (sign)
    res = NOT (res);

  return res;
}

simpaig *
simpaig_substitute (simpaigmgr * mgr, simpaig * node)
{
  simpaig *res;

  node = IMPORT (node);
  if (!ISCONST (node))
    {
      assert (!mgr->count_cached);
      res = simpaig_substitute_rec (mgr, node);
      simpaig_reset_cache (mgr);
    }
  else
    res = inc (node);

  simpaig_reset_assignment (mgr);

  return res;
}

void
simpaig_substitute_parallel (simpaigmgr * mgr, simpaig ** a, unsigned n)
{
  simpaig * node, * res;
  unsigned i;

  assert (!mgr->count_cached);

  for (i = 0; i < n; i++)
    {
      node = IMPORT (a[i]);
      if (ISCONST (node)) res = inc (node);
      else res = simpaig_substitute_rec (mgr, node);
      dec (mgr, node);
      a[i] = res;
    }

  simpaig_reset_cache (mgr);
  simpaig_reset_assignment (mgr);
}

static simpaig *
simpaig_shift_rec (simpaigmgr * mgr, simpaig * node, int delta)
{
  simpaig *res, *l, *r;
  unsigned sign;

  sign = SIGN (node);
  if (sign)
    node = NOT (node);

  if (node->cache)
    {
      res = inc (node->cache);
    }
  else
    {
      if (ISVAR (node))
	{
	  res = simpaig_var (mgr, node->var, node->slice + delta);
	}
      else
	{
	  l = simpaig_shift_rec (mgr, node->c0, delta);
	  r = simpaig_shift_rec (mgr, node->c1, delta);
	  res = simpaig_and (mgr, l, r);
	  dec (mgr, l);
	  dec (mgr, r);
	}

      simpaig_cache (mgr, node, res);
    }

  if (sign)
    res = NOT (res);

  return res;
}

simpaig *
simpaig_shift (simpaigmgr * mgr, simpaig * node, int delta)
{
  simpaig *res;

  node = IMPORT (node);
  if (ISCONST (node))
    return inc (node);

  assert (!mgr->count_cached);
  res = simpaig_shift_rec (mgr, node, delta);
  simpaig_reset_cache (mgr);

  return res;
}

static void
simpaig_push_index (simpaigmgr * mgr, simpaig * node)
{
  assert (!SIGN (node));
  assert (!ISFALSE (node));
  assert (!node->idx);
  PUSH (mgr->indices, mgr->count_indices, mgr->size_indices, node);
  node->idx = mgr->count_indices;
  assert (node->idx);
  inc (node);
}

static void
simpaig_assign_indices_rec (simpaigmgr * mgr, simpaig * node)
{
  node = STRIP (node);
  if (node->idx)
    return;

  if (!ISVAR (node))
    {
      simpaig_assign_indices_rec (mgr, node->c0);
      simpaig_assign_indices_rec (mgr, node->c1);
    }

  simpaig_push_index (mgr, node);
}

void
simpaig_assign_indices (simpaigmgr * mgr, simpaig * node)
{
  if (!ISCONST (node))
    simpaig_assign_indices_rec (mgr, node);
}

void
simpaig_reset_indices (simpaigmgr * mgr)
{
  simpaig *aig;
  int i;

  for (i = 0; i < mgr->count_indices; i++)
    {
      aig = mgr->indices[i];
      assert (aig);
      assert (!SIGN (aig));
      assert (aig->idx);
      aig->idx = 0;
      dec (mgr, aig);
    }

  mgr->count_indices = 0;
}

unsigned
simpaig_index (simpaig * node)
{
  assert (!SIGN (node));
  return node->idx;
}

unsigned
simpaig_max_index (simpaigmgr * mgr)
{
  return mgr->count_indices;
}

int
simpaig_int_index (simpaig * node)
{
  int sign = SIGN (node) ? -1 : 1;
  int res = simpaig_index (STRIP (node)) + 1;
  res *= sign;
  return res;
}

unsigned
simpaig_unsigned_index (simpaig * node)
{
  unsigned sign = SIGN (node) ? 1 : 0;
  unsigned res = 2 * simpaig_index (STRIP (node));
  res |= sign;
  return res;
}
