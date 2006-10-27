#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include <stdlib.h>		/* for 'size_t' */

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;
typedef long simpaig_word;

typedef void *(*simpaig_malloc) (void *mem_mgr, size_t);
typedef void (*simpaig_free) (void *mem_mgr, void *ptr, size_t);

int simpaig_signed (simpaig *);
void * simpaig_isvar (simpaig *);
int simpaig_isfalse (const simpaig *);
int simpaig_istrue (const simpaig *);

/* The following functions do not give  back a new reference.  The reference
 * is shared with the argument.
 */
simpaig * simpaig_strip (simpaig *);
simpaig * simpaig_not (simpaig *);
simpaig * simpaig_child (simpaig *, int child);

simpaigmgr * simpaig_init (void);
simpaigmgr * simpaig_init_mem (void *mem_mgr, simpaig_malloc, simpaig_free);
void simpaig_reset (simpaigmgr *);

/* The functions below this point will all return a new reference, if they
 * return a 'simpaig *'.  The user should delete the returned aig, if memory
 * is scarce.
 */
simpaig * simpaig_false (simpaigmgr *);
simpaig * simpaig_true (simpaigmgr *);
simpaig * simpaig_var (simpaigmgr *, void * var);	/* var != 0 */
simpaig * simpaig_and (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_or (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_implies (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_xor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_xnor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig * simpaig_ite (simpaigmgr *, simpaig * c, simpaig * t, simpaig * e);

simpaig * simpaig_inc (simpaigmgr *, simpaig *);
void simpaig_dec (simpaigmgr *, simpaig *);

void simpaig_assign (simpaigmgr *, simpaig * lhs, simpaig * rhs);
simpaig * simpaig_substitute (simpaigmgr *, simpaig *);
simpaig * simpaig_shift (simpaigmgr *, simpaig *, int delta);

int simpaig_tseitin (simpaigmgr * mgr, simpaig *);
int simpaig_reset_tseitin_indices (simpaigmgr * mgr);

#endif
