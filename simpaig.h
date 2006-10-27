#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include <stdlib.h>		/* for 'size_t' */

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;
typedef long simpaig_word;

typedef void *(*simpaig_malloc) (void *mem_mgr, size_t);
typedef void (*simpaig_free) (void *mem_mgr, void *ptr, size_t);

struct simpaig
{
  void * var;			/* generic variable pointer */
  unsigned slice;		/* time slice */
  simpaig * c0;			/* child 0 */
  simpaig * c1;			/* child 1 */
  int idx;			/* Tseitin index */
};

#define simpaig_not(p) ((simpaig*)(1^(simpaig_word)(p)))
#define simpaig_sign(p) (1&(simpaig_word)(p))
#define simpaig_is_var(p) (assert (!simpaig_sign(p)), (p)->var != 0)

#define simpaig_istrue (p) (!simpaig_sign (p) && !(p)->c0)
#define simpaig_isfalse (p) (simpaig_sign (p) && !simpaig_not (p)->c0)

simpaig * simpaig_true (simpaigmgr *);
simpaig * simpaig_false (simpaigmgr *);

simpaigmgr * simpaig_init (void);
simpaigmgr * simpaig_init_mem (void *mem_mgr, simpaig_malloc, simpaig_free);
void simpaig_reset (simpaigmgr *);

simpaig * simpaig_var (simpaigmgr *, void * var);	/* var != 0 */
simpaig * simpaig_and (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_or (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_implies (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_xor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_xnor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_ite (simpaigmgr *, simpaig * c, simpaig * t, simpaig * e);

simpaig * simpaig_inc (simpaigmgr *, simpaig *);
simpaig * simpaig_dec (simpaigmgr *, simpaig *);

simpaig * simpaig_assign (simpaigmgr *, simpaig * lhs, simpaig * rhs);
simpaig * simpaig_substitute (simpaigmgr *, simpaig *);
simpaig * simpaig_shift (simpaigmgr *, simpaig *, int delta);

int simpaig_tseitin (simpaigmgr * mgr, simpaig *);
int simpaig_reset_tseitin_indices (simpaigmgr * mgr);

#endif
