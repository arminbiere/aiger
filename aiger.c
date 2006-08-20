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
  void *memory_mgr;
  aiger_malloc malloc_callback;
  aiger_free free_callback;
};

aiger *
aiger_init_mem (void *memory_mgr,
		aiger_malloc external_malloc, aiger_free external_free)
{
  aiger_internal *res;
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
aiger_default_malloc (void *state, size_t bytes)
{
  return malloc (bytes);
}

static void
aiger_default_free (void *state, void *ptr, size_t bytes)
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
    (p) = private->malloc_callback (private->memory_mgr, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define REALLOCN(p,m,n) \
  do { \
    size_t mbytes = (m) * sizeof (*(p)); \
    size_t nbytes = (n) * sizeof (*(p)); \
    size_t minbytes = (mbytes < nbytes) ? mbytes : nbytes; \
    void * res = private->malloc_callback (private->memory_mgr, nbytes); \
    memcpy (res, (p), minbytes); \
    if (nbytes > mbytes) \
      memset (res + (m), 0, nbytes - mbytes); \
    private->free_callback (private->memory_mgr, (p), mbytes); \
    (p) = res; \
  } while (0)

#define ENLARGE(p,s) \
  do { \
    size_t old_size = (s); \
    size_t new_size = old_size ? 2 * old_size : 1; \
    REALLOCN (p,old_size,new_size); \
    (s) = new_size; \
  } while (0)

#define PUSH(p,n,s,l) \
  do { \
    if ((n) == (s)) \
      ENLARGE (p, s); \
    (p)[(n)++] = (l); \
  } while (0)

#define DELETEN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    private->free_callback (private->memory_mgr, (p), bytes); \
  } while (0)

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

#define IMPORT_private_FROM(p) \
  aiger_internal * private = (aiger_internal*) (p)

#define EXPORT_public_FROM(p) \
  aiger * public = &(p)->public

static void
aiger_delete_string_list (aiger_internal * private, aiger_string * head)
{
  aiger_string *s, *next;

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
  IMPORT_private_FROM (public);
  aiger_literal *literal;
  aiger_node *node;
  unsigned i;

  if (public->literals)
    {
      for (i = 0; i <= 2 * public->max_idx; i++)
	{
	  literal = public->literals[i];
	  if (!literal)
	    continue;

	  aiger_delete_string_list (private, literal->symbols);
	  aiger_delete_string_list (private, literal->attributes);
	  DELETE (literal);
	}

      DELETEN (public->literals, private->size_literals);
    }

  if (public->nodes)
    {
      for (i = 0; i <= public->max_idx; i++)
	{
	  node = public->nodes[i];
	  if (node)
	    DELETE (node);
	}

      DELETEN (public->nodes, private->size_nodes);
    }

  DELETEN (public->inputs, private->size_inputs);
  DELETEN (public->latches, private->size_latches);
  DELETEN (public->next_state_functions, private->size_latches);
  DELETEN (public->outputs, private->size_outputs);

  DELETE (private);
}

static void
aiger_import_literal (aiger_internal * private, unsigned lit)
{
  EXPORT_public_FROM (private);
  unsigned idx;

  while (lit >= private->size_literals)
    ENLARGE (public->literals, private->size_literals);

  idx = aiger_lit2idx (lit);
  if (idx > public->max_idx)
    {
      public->max_idx = idx;

      while (idx >= private->size_nodes)
	ENLARGE (public->nodes, private->size_nodes);
    }
}

void
aiger_input (aiger * public, unsigned lit)
{
  IMPORT_private_FROM (public);
  assert (lit);
  assert (!aiger_sign (lit));
  aiger_import_literal (private, lit);
  PUSH (public->inputs, public->num_inputs, private->size_inputs, lit);
}

void
aiger_output (aiger * public, unsigned lit)
{
  IMPORT_private_FROM (public);
  aiger_import_literal (private, lit);
  PUSH (public->outputs, public->num_outputs, private->size_outputs, lit);
}

void
aiger_latch (aiger * public, unsigned lit, unsigned next)
{
  IMPORT_private_FROM (public);
  assert (lit);
  assert (!aiger_sign (lit));
  aiger_import_literal (private, lit);
  PUSH (public->latches, public->num_latches, private->size_latches, lit);
}

void
aiger_and (aiger * public, unsigned lhs, unsigned rhs0, unsigned rhs1)
{
  IMPORT_private_FROM (public);
  aiger_node *node;
  unsigned idx;

  assert (lhs > 1);
  assert (!aiger_sign (lhs));

  aiger_import_literal (private, lhs);
  aiger_import_literal (private, rhs0);
  aiger_import_literal (private, rhs1);

  idx = aiger_lit2idx (lhs);
  assert (!public->nodes[idx]);

  NEW (node);
  node->lhs = lhs;
  node->rhs0 = rhs0;
  node->rhs1 = rhs1;
  assert (!node->client_data);

  public->nodes[idx] = node;
}
