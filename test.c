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
  aiger_add_input (aiger, 2, 0);
  aiger_add_output (aiger, 2, 0);
  aiger_add_output (aiger, 6, 0);
  aiger_add_latch (aiger, 6, 5, 0);
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
latch_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_latch (aiger, 2, 5, 0);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
output_undefined (void)
{
  aiger *aiger = my_aiger_init ();
  aiger_add_output (aiger, 2, 0);
  aiger_add_output (aiger, 6, 0);
  aiger_add_latch (aiger, 2, 5, 0);
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
  aiger_add_input (aiger, 2, 0);
  aiger_add_and (aiger, 4, 6, 2);
  aiger_add_and (aiger, 6, 4, 2);
  aiger_add_output (aiger, 5, 0);
  assert (aiger_check (aiger));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static void
write_and_read_fmt (aiger * old, const char * name, const char * fmt)
{
  char buffer[100];
  aiger * new;
  assert (strlen (name) + strlen (fmt) + 5 < sizeof (buffer));
  sprintf (buffer, "log/%s%s", name, fmt);
  assert (aiger_open_and_write_to_file (old, buffer));
  new = aiger_init ();
  assert (!aiger_open_and_read_from_file (new, buffer));
  aiger_reset (new);
}

static void
write_and_read (aiger * old, const char * name)
{
  write_and_read_fmt (old, name, ".aig");
  write_and_read_fmt (old, name, ".aig.gz");
  write_and_read_fmt (old, name, ".big");
  write_and_read_fmt (old, name, ".big.gz");
  write_and_read_fmt (old, name, ".cig");
  write_and_read_fmt (old, name, ".cig.gz");
}

static char * empty_aig = 
"p aig 0 0 0 0 0\n";

static void
write_empty (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, empty_aig));
  write_and_read (aiger, "empty");
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * false_aig =
"p aig 0 0 0 1 0\n0\n";

static void
write_false (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 0, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, false_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * true_aig =
"p aig 0 0 0 1 0\n1\n";

static void
write_true (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 1, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, true_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * and_aig =
"p aig 3 2 0 1 1\n2\n4\n6\n6 2 4\n";

static void
write_and (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[200];
  aiger_add_input (aiger, 2, 0);
  aiger_add_input (aiger, 4, 0);
  aiger_add_output (aiger, 6, 0);
  aiger_add_and (aiger, 6, 2, 4);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, and_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char * counter1 =
"p aig 9 2 1 1 4\n"
"10\n"
"4\n"
"6 18\n"
"19\n"
"12 6 4\n"
"14 7 5\n"
"16 15 13\n"
"18 16 11\n"
"i 0 10 reset\n"
"i 1 4 enable\n"
"l 0 6 latch\n"
"o 0 19 output\n"
;

static void
reencode_counter1 (void)
{
  aiger * aiger = my_aiger_init ();
  char buffer[200];

  aiger_add_input (aiger, 10, "reset");
  aiger_add_input (aiger, 4, "enable");
  aiger_add_output (aiger, 9, "output");

  aiger_add_latch (aiger, 6, 8, "latch");

  aiger_add_and (aiger, 8, 12, 11);	/* active high reset */
  aiger_add_and (aiger, 12, 17, 15);	/* latch ^ enable */
  aiger_add_and (aiger, 14, 4, 6);
  aiger_add_and (aiger, 16, 5, 7);

  aiger_reencode (aiger, 0);

  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, counter1));

  write_and_read (aiger, "counter1");

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
