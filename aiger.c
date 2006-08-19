#include "aiger.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct aiger_internal aiger_internal;

struct aiger_internal
{
  aiger public;
  unsigned size_literals;
  unsigned size_inputs;
  unsigned size_next_state_functions;
  unsigned size_outputs;
  void * memory_mgr;
  aiger_malloc malloc_callback;
  aiger_free free_callback;
};

aiger *
aiger_init_mem (
  void * memory_mgr,
  aiger_malloc external_malloc,
  aiger_free external_free)
{
  aiger_internal * res;
  assert (external_malloc);
  assert (external_free);
  res = external_malloc (memory_mgr, sizeof (*res));
  memset (res, 0, sizeof (*res));
  res->memory_mgr = memory_mgr;
  res->malloc_callback = external_malloc;
  res->free_callback = external_free;
  return &res->public;
}

static void *
aiger_default_malloc (void * state, size_t bytes)
{
  return malloc (bytes);
}

static void
aiger_default_free (void * state, void * ptr, size_t bytes)
{
  free (ptr);
}

aiger *
aiger_init (void)
{
  return aiger_init_mem (0, aiger_default_malloc, aiger_default_free);
}

#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = mgr->malloc_callback (mgr->memory_mgr, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define REALLOCN(p,m,n) \
  do { \
    size_t mbytes = (m) * sizeof (*(p)); \
    size_t nbytes = (n) * sizeof (*(p)); \
    size_t minbytes = (mbytes < nbytes) ? mbytes : nbytes; \
    void * res = mgr->malloc_callback (mgr->memory_mgr, nbytes); \
    memcpy (res, (p), minbytes); \
    if (nbytes > mbytes) \
      memset (res + (m), 0, nbytes - mbytes); \
    mgr->free_callback (mgr->memory_mgr, (p), mbytes); \
    (p) = res; \
  } while (0)

#define DELETEN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    mgr->free_callback (mgr->memory_mgr, (p), bytes); \
  } while (0)

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

#define IMPORT(p) \
  aiger_internal * mgr = (aiger_internal*) (p)

void
aiger_reset (aiger * public)
{
  IMPORT (public);

  aiger_attribute * a;
  aiger_literal * l;
  aiger_symbol * s;
  void * next;
  unsigned i;

  if (mgr->public.max_idx)
    {
      for (i = 0; i <= 2 * mgr->public.max_idx; i++)
	{
	  l = mgr->public.literals[i];

	  for (a = l->attributes; a; a = next)
	    {
	      next = a->next;
	      DELETEN (a->str, strlen (a->str) + 1);
	      DELETE (a);
	    }

	  for (s = l->symbols; s; s = next)
	    {
	      next = s->next;
	      DELETEN (s->str, strlen (s->str) + 1);
	      DELETE (s);
	    }

	  DELETE (l->node);
	  DELETE (l);
	}

      DELETEN (mgr->public.literals, 2 * (mgr->public.max_idx + 1));
    }

  DELETE (mgr);
}

static aiger_literal *
aiger_find_literal (aiger_internal * mgr, unsigned lit)
{
  unsigned idx, old_size, new_size, i;
  aiger_literal * res;

  old_size = mgr->size_literals;
  if (lit >= old_size)
    {
      new_size = old_size ? 2 * old_size : 1;
      while (lit >= new_size)
	new_size *= 2;

      REALLOCN (mgr->public.literals, old_size, new_size);
      mgr->size_literals = new_size;
    }

  idx = lit/2;
  if (idx > mgr->public.max_idx)
    mgr->public.max_idx = idx;

  res = mgr->public.literals[lit];
  if (!res)
    {
      NEW (res);
      mgr->public.literals[lit] = res;
    }

  return res;
}
