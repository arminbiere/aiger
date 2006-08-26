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
  aiger_add_and (aiger, 4, 0, 1);
  aiger_add_input (aiger, 2);
  aiger_add_output (aiger, 2);
  aiger_add_output (aiger, 6);
  aiger_add_latch (aiger, 6, 5);
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
latch_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_latch (aiger, 2, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
output_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_output (aiger, 2);
  aiger_add_output (aiger, 6);
  aiger_add_latch (aiger, 2, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
rhs_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_and (aiger, 4, 2, 1);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
cyclic0 (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_and (aiger, 4, 4, 1);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
cyclic1 (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_input (aiger, 2);
  aiger_add_and (aiger, 4, 6, 2);
  aiger_add_and (aiger, 6, 4, 2);
  aiger_add_output (aiger, 5);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * empty_aig = 
"p aig 0 0 0 0\n";

static void
write_empty (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, empty_aig));
  assert (aiger_open_and_write_to_file (aiger, "log/empty.aig"));
  assert (aiger_open_and_write_to_file (aiger, "log/empty.aig.gz"));
  assert (aiger_open_and_write_to_file (aiger, "log/empty.big"));
  assert (aiger_open_and_write_to_file (aiger, "log/empty.big.gz"));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * false_aig =
"p aig 0 0 1 0\nc outputs 1\n0\n";

static void
write_false (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, false_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * true_aig =
"p aig 0 0 1 0\nc outputs 1\n1\n";

static void
write_true (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 1);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, true_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * and_aig =
"p aig 2 0 1 1\nc inputs 2 from 2 to 4\n0\nc outputs 1\n6\nc ands 1\n6 2 4\n";

static void
write_and (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[200];
  aiger_add_input (aiger, 2);
  aiger_add_input (aiger, 4);
  aiger_add_output (aiger, 6);
  aiger_add_and (aiger, 6, 2, 4);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, and_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * counter1 =
"p aig 2 1 1 4\n"
"c inputs 2\n"
"10\n"
"4\n"
"c latches 1\n"
"6 18\n"
"c outputs 1\n"
"19\n"
"c ands 4\n"
"12 6 4\n"
"14 7 5\n"
"16 15 13\n"
"18 16 11\n"
"c symbols 4\n"
"10 reset\n"
"4 enable\n"
"19 output\n"
"6 latch\n"
;

static void
reencode_counter1 (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[200];

  aiger_add_input (aiger, 10);
  aiger_add_symbol (aiger, 10, "reset");

  aiger_add_input (aiger, 4);
  aiger_add_symbol (aiger, 4, "enable");

  aiger_add_output (aiger, 9);
  aiger_add_symbol (aiger, 9, "output");

  aiger_add_latch (aiger, 6, 8);
  aiger_add_symbol (aiger, 6, "latch");

  aiger_add_and (aiger, 8, 12, 11);	/* active high reset */
  aiger_add_and (aiger, 12, 17, 15);	/* latch ^ enable */
  aiger_add_and (aiger, 14, 4, 6);
  aiger_add_and (aiger, 16, 5, 7);

  aiger_reencode (aiger);

  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, counter1));

  assert (aiger_open_and_write_to_file (aiger, "log/counter1.aig"));
  assert (aiger_open_and_write_to_file (aiger, "log/counter1.aig.gz"));
  assert (aiger_open_and_write_to_file (aiger, "log/counter1.big"));
  assert (aiger_open_and_write_to_file (aiger, "log/counter1.big.gz"));

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
  reencode_counter1 ();
  return 0;
}
