/***************************************************************************
Copyright (c) 2006-2007, Armin Biere, Johannes Kepler University.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
***************************************************************************/

#include "aiger.h"

#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

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
  size_t bytes;			/* only the allocated bytes as state */
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
write_and_read_fmt (aiger * old, const char *name, const char *fmt)
{
  char buffer[100];
  aiger *new;
  assert (strlen (name) + strlen (fmt) + 5 < sizeof (buffer));
  sprintf (buffer, "log/%s%s", name, fmt);
  assert (aiger_open_and_write_to_file (old, buffer));
  new = aiger_init ();
  assert (!aiger_open_and_read_from_file (new, buffer));
  aiger_reset (new);
}

static void
write_and_read (aiger * old, const char *name)
{
  write_and_read_fmt (old, name, ".aag");
  write_and_read_fmt (old, name, ".aag.gz");
  write_and_read_fmt (old, name, ".aig");
  write_and_read_fmt (old, name, ".aig.gz");
}

static char *empty_aig = "aag 0 0 0 0 0\n";

static void
write_empty (void)
{
  aiger *aiger = my_aiger_init ();
  char buffer[100];
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, empty_aig));
  write_and_read (aiger, "empty");
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char *false_aig = "aag 0 0 0 1 0\n0\n";

static void
write_false (void)
{
  aiger *aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 0, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, false_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char *true_aig = "aag 0 0 0 1 0\n1\n";

static void
write_true (void)
{
  aiger *aiger = my_aiger_init ();
  char buffer[100];
  aiger_add_output (aiger, 1, 0);
  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 100));
  assert (!strcmp (buffer, true_aig));
  aiger_reset (aiger);
  assert (!mgr.bytes);
}

static char *and_aig = "aag 3 2 0 1 1\n2\n4\n6\n6 2 4\n";

static void
write_and (void)
{
  aiger *aiger = my_aiger_init ();
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

static char *counter1 =
  "aag 8 2 1 1 4\n"
  "10\n"
  "4\n"
  "6 8\n"
  "8\n"
  "8 12 11\n"
  "12 17 15\n"
  "14 4 6\n"
  "16 5 7\n"
  "i0 reset\n"
  "i1 enable\n"
  "l0 latch\n"
  "o0 AIGER_NEVER\n" "c\n" "1-bit counter with reset and enable\n";

static char *counter1r =
  "aag 7 2 1 1 4\n"
  "2\n"
  "4\n"
  "6 14\n"
  "14\n"
  "8 6 4\n"
  "10 7 5\n"
  "12 11 9\n"
  "14 12 3\n"
  "i0 reset\n"
  "i1 enable\n"
  "l0 latch\n"
  "o0 AIGER_NEVER\n" "c\n" "1-bit counter with reset and enable\n";

static void
reencode_counter1 (void)
{
  aiger *aiger = my_aiger_init ();
  char buffer[200];

  aiger_add_input (aiger, 10, "reset");
  aiger_add_input (aiger, 4, "enable");
  aiger_add_output (aiger, 8, "AIGER_NEVER");

  aiger_add_latch (aiger, 6, 8, "latch");

  aiger_add_and (aiger, 8, 12, 11);	/* active high reset */
  aiger_add_and (aiger, 12, 17, 15);	/* latch ^ enable */
  aiger_add_and (aiger, 14, 4, 6);
  aiger_add_and (aiger, 16, 5, 7);

  aiger_add_comment (aiger, "1-bit counter with reset and enable");

  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, counter1));

  aiger_reencode (aiger);

  assert (aiger_write_to_string (aiger, aiger_ascii_mode, buffer, 200));
  assert (!strcmp (buffer, counter1r));

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
