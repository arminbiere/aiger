#include "aiger.h"

#include <stdlib.h>
#include <assert.h>

static void
init_and_reset (void)
{
  aiger *aiger = aiger_init ();
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
  assert (mgr->bytes);
  return malloc (bytes);
}

static void
test_free (test_memory_mgr * mgr, void *ptr, size_t bytes)
{
  assert (mgr->bytes >= bytes);
  mgr->bytes -= bytes;
  free (ptr);
}

static test_memory_mgr mgr;

static aiger *
my_aiger_init (void)
{
  return aiger_init_mem (&mgr,
			 (aiger_malloc) test_malloc, (aiger_free) test_free);
}

static void
init_and_reset_mem (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
only_add_and_reset (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_and (aiger, 4, 0, 1);
  aiger_input (aiger, 2);
  aiger_output (aiger, 2);
  aiger_output (aiger, 6);
  aiger_latch (aiger, 6, 5);
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
latch_and_input (void)
{
  aiger *aiger = my_aiger_init ();
  unsigned i;
  for (i = 2; i <= 1000; i += 2)
    aiger_input (aiger, i);
  aiger_latch (aiger, 2000, 0);
  aiger_latch (aiger, 3000, 0);
  aiger_latch (aiger, 500, 1);
  aiger_latch (aiger, 7000, 1);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

int
main (void)
{
  init_and_reset ();
  init_and_reset_mem ();
  only_add_and_reset ();
  latch_and_input ();
  return 0;
}
