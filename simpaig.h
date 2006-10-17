#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include "aiger.h"

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;

struct simpaig
{
  void * var;
  unsigned slice;
  simpaig * c0;
  simpaig * c1;
  simpaig * next;		/* collistion chain */
  unsigned idx;			/* Tseitin index */
  simpaig * cache;		/* cache for substitution operation */
};

simpaigmgr * simpaig_init (void);
simpaigmgr * simpaig_init_mem (void *mem_mgr, aiger_malloc, aiger_free);
void simpaig_reset (simpaigmgr *);

#endif
