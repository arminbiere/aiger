#include "aiger.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

static void
init_and_reset (void)
{
  aiger *aiger = aiger_init ();
  aiger_reset (aiger);
}

typedef struct test_memory_mgr test_memory_mgr;

struct test_memory_mgr
{
  size_t bytes;		/* only the allocated bytes as state */
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
latch_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_latch (aiger, 2, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
output_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_output (aiger, 2);
  aiger_output (aiger, 6);
  aiger_latch (aiger, 2, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
rhs_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_and (aiger, 4, 2, 1);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
cyclic0 (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_and (aiger, 4, 4, 1);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
cyclic1 (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_input (aiger, 2);
  aiger_and (aiger, 4, 6, 2);
  aiger_and (aiger, 6, 4, 2);
  aiger_output (aiger, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * empty_aig = 
"p aig 0 0 0 0\nc inputs\nc latches\nc outputs\nc ands\n";

static void
write_empty (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  assert (aiger_write_to_string (aiger, aiger_ascii_write_mode, buffer, 100));
  assert (!strcmp (buffer, empty_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * false_aig =
"p aig 0 0 0 1\nc inputs\nc latches\nc outputs\n0\nc ands\n";

static void
write_false (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_output (aiger, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_write_mode, buffer, 100));
  assert (!strcmp (buffer, false_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * true_aig =
"p aig 0 0 0 1\nc inputs\nc latches\nc outputs\n1\nc ands\n";

static void
write_true (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_output (aiger, 1);
  assert (aiger_write_to_string (aiger, aiger_ascii_write_mode, buffer, 100));
  assert (!strcmp (buffer, true_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * and_aig =
"p aig 2 1 0 1\nc inputs\n2\n4\nc latches\nc outputs\n6\nc ands\n6 2 4\n";

static void
write_and (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[200];
  aiger_input (aiger, 2);
  aiger_input (aiger, 4);
  aiger_output (aiger, 6);
  aiger_and (aiger, 6, 2, 4);
  assert (aiger_write_to_string (aiger, aiger_ascii_write_mode, buffer, 200));
  assert (!strcmp (buffer, and_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

int
main (void)
{
  init_and_reset ();
  init_and_reset_mem ();
  only_add_and_reset ();
  latch_undefined ();
  output_undefined ();
  rhs_undefined ();
  cyclic0 ();
  cyclic1 ();
  write_empty ();
  write_false ();
  write_true ();
  write_and ();
  return 0;
}
