#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include "aiger.h"

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;
typedef long simpaig_word;

struct simpaig
{
  unsigned ref;			/* reference counter */
  void * var;			/* generic variable pointer */
  unsigned slice;		/* time slice */
  simpaig * c0;			/* child 0 */
  simpaig * c1;			/* child 1 */
  simpaig * next;		/* collision chain */
  unsigned idx;			/* Tseitin index */
  simpaig * cache;		/* cache for substitution operation */
};

#define simpaig_not(p) ((simpaig*)(1^(simpaig_word)(p)))
#define simpaig_sign(p) (1&(simpaig_word)(p))
#define simpaig_is_var(p) (assert (!simpaig_sign(p)), (p)->var != 0)

extern simpaig * simpaig_true;
extern simpaig * simpaig_false;

simpaigmgr * simpaig_init (void);
simpaigmgr * simpaig_init_mem (void *mem_mgr, aiger_malloc, aiger_free);
void simpaig_reset (simpaigmgr *);

#endif
