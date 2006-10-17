#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include "aiger.h"

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;

struct simpaig
{
};

simpaigmgr * simpaig_init (void);
simpaigmgr * simpaig_init_mem (void *mem_mgr, aiger_malloc, aiger_free);
void simpaig_reset (simpaigmgr *);

#endif
