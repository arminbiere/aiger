#include "simpaig.h"

#include <stdlib.h>
#include <string.h>

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

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

typedef struct AIG AIG;

struct AIG
{
  simpaig public;
  unsigned ref;		/* reference counter */
  AIG * next;		/* collision chain */
  AIG * cache;		/* cache for substitution and shifting */
  AIG * rhs;		/* right hand side (RHS) for substitution */
};

struct simpaigmgr
{
  void * mem;
  simpaig_malloc malloc;
  simpaig_free free;

  AIG false_aig;
  AIG ** table;
  unsigned count_table;
  unsigned size_table;

  AIG ** cached;
  unsigned count_cache;
  unsigned size_cache;

  AIG ** assigned;
  unsigned count_assigned;
  unsigned size_assigned;

  unsigned idx;
};

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
  simpaigmgr * mgr;
  mgr = m (mem_mgr, sizeof (*mgr));
  mgr->mem = mem_mgr;
  mgr->malloc = m;
  mgr->free = f;
  return mgr;
}

void
simpaig_reset (simpaigmgr * mgr)
{
  AIG * p, * next;
  unsigned i;

  for (i = 0; i < mgr->count_table; i++)
  {
    for (p = mgr->table[i]; p; p = next)
      {
	next = p->next;
      }
  }

  mgr->free (mgr->mem, mgr, sizeof (*mgr));
}

