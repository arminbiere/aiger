#include "aiger.h"

#include <stdlib.h>
#include <assert.h>

static void
init_and_reset (void)
{
  aiger * aiger = aiger_init ();
  aiger_reset (aiger);
}

typedef struct test_memory_mgr test_memory_mgr;

struct test_memory_mgr
{
  size_t bytes;
};

static void *
test_malloc (test_memory_mgr * mgr, size_t bytes)
{
  mgr->bytes += bytes;
  return malloc (bytes);
}

static void
test_free (test_memory_mgr * mgr, void * ptr, size_t bytes)
{
  assert (mgr->bytes >= bytes);
  mgr->bytes -= bytes;
  free (ptr);
}

static void
init_and_reset_mem (void)
{
  aiger * aiger;
  test_memory_mgr test_memory_mgr;
  test_memory_mgr.bytes = 0;
  aiger = aiger_init_mem (&test_memory_mgr, 
                          (aiger_malloc) test_malloc,
			  (aiger_free) test_free);
  aiger_reset (aiger);
}

int
main (void)
{
  init_and_reset ();
  init_and_reset_mem ();
  return 0;
}
