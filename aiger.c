#include "aiger.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct aiger_internal aiger_internal;

struct aiger_internal
{
  aiger public;
  unsigned size_literals;
  unsigned size_nodes;
  unsigned size_inputs;
  unsigned size_latches;
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

#define ENLARGE(p,s) \
  do { \
    size_t old_size = (s); \
    size_t new_size = old_size ? 2 * old_size : 1; \
    REALLOCN (p,old_size,new_size); \
    (s) = new_size; \
  } while (0)

#define DELETEN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    mgr->free_callback (mgr->memory_mgr, (p), bytes); \
  } while (0)

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

#define IMPORT_MGR(p) \
  aiger_internal * mgr = (aiger_internal*) (p)

static void
aiger_delete_string_list (aiger_internal * mgr, aiger_string * head)
{
  aiger_string * s, * next;

  for (s = head; s; s = next)
    {
      next = s->next;
      DELETEN (s->str, strlen (s->str) + 1);
      DELETE (s);
    }
}

void
aiger_reset (aiger * public)
{
  IMPORT_MGR (public);
  aiger_literal * l;
  aiger_node * n;
  unsigned i;

  if (mgr->public.literals)
    {
      for (i = 0; i <= 2 * mgr->public.max_idx; i++)
	{
	  l = mgr->public.literals[i];
	  if (!l)
	    continue;

	  aiger_delete_string_list (mgr, l->symbols);
	  aiger_delete_string_list (mgr, l->attributes);
	  DELETE (l);
	}

      DELETEN (mgr->public.literals, mgr->size_literals);
    }

  if (mgr->public.nodes)
    {
      for (i = 0; i <= mgr->public.max_idx; i++)
	{
	  n = mgr->public.nodes[i];
	  if (n)
	    DELETE (n);
	}

      DELETEN (mgr->public.nodes, mgr->size_nodes);
    }

  DELETEN (mgr->public.inputs, mgr->size_inputs);
  DELETEN (mgr->public.latches, mgr->size_latches);
  DELETEN (mgr->public.next_state_functions, mgr->size_latches);
  DELETEN (mgr->public.outputs, mgr->size_outputs);

  DELETE (mgr);
}

static void
aiger_import_literal (aiger_internal * mgr, unsigned lit)
{
  unsigned idx;

  while (lit >= mgr->size_literals)
    ENLARGE (mgr->public.literals, mgr->size_literals);

  idx = aiger_lit2idx (lit);
  if (idx > mgr->public.max_idx)
    {
      mgr->public.max_idx = idx;

      while (idx >= mgr->size_nodes)
	ENLARGE (mgr->public.nodes, mgr->size_nodes);
    }
}

void
aiger_input (aiger * public, unsigned lit)
{
  IMPORT_MGR (public);
  assert (lit);

  aiger_import_literal (mgr, lit);
}
