/***************************************************************************
Copyright (c) 2023, Armin Biere, University of Freiburg.
Copyright (c) 2018, Armin Biere, Johannes Kepler University.

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

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USAGE \
"usage: aigselect [-h][-v][-r][<output-idx> ...][<input-aig> [<output-aig>]]\n" \
"\n" \
"Selects the outputs '<output-idx>' from '<input-aig>' and writes the\n" \
"resulting AIG with only those outputs to '<output-aig>'.  If '-r' is\n" \
"specified then the inputs of the resulting AIG are reduced to those\n" \
"remaining in the cone-of-influence of one output.\n"

static int verbose = 0;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigselect] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
msg (const char *fmt, ...)
{
  va_list ap;
  if (!verbose)
    return;
  fputs ("[aigselect] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static int
contains_only_digits (const char * str)
{
  const char * p;

  if (!*str) return 0;
  for (p = str; *p; p++)
    if (!isdigit (*p))
      return 0;

  return 1;
}

static void
invalid_number (const char * str)
{
  die ("invalid number '%s'", str);
}

static unsigned
parse_number (const char * str)
{
  unsigned res = 0, digit;
  const char * p;
  int ch;

  for (p = str; (ch = *p) ;p++)
    {
      if (!isdigit (ch)) invalid_number (str);
      if (UINT_MAX/10 < res) invalid_number (str);
      res *= 10;
      digit = ch - '0';
      if (UINT_MAX - digit < res) invalid_number (str);
      res += digit;
    }

  return res;
}

#define PUSH(STACK,ELEMENT) \
do { \
  if (size_ ## STACK == capacity_ ## STACK) \
    { \
      capacity_ ## STACK = \
        capacity_ ## STACK ? 2 * capacity_ ## STACK : 1; \
      STACK = realloc (STACK, capacity_ ## STACK * sizeof *STACK); \
      if (!STACK) \
	die ("out-of-memory resizing '%s'", #STACK); \
    } \
  STACK[size_ ## STACK ++] = ELEMENT; \
} while (0)

#define COI(LIT) \
do { \
  unsigned VAR; \
  if (aiger_is_constant (LIT)) \
    break; \
  VAR = aiger_lit2var (LIT); \
  if (coi[VAR]) \
    break; \
  coi[VAR] = 1; \
  PUSH (stack, VAR); \
} while (0)

#define KEEP(LIT) \
  (aiger_is_constant (LIT) || coi[aiger_lit2var (LIT)])

#define MAP(LIT) \
do { \
  unsigned TMP_LIT = (LIT); \
  unsigned NOT_LIT = aiger_not (TMP_LIT); \
  map[TMP_LIT] = mapped++; \
  map[NOT_LIT] = mapped++; \
} while (0)

int
main (int argc, char ** argv)
{
  size_t size_selected, capacity_selected;
  size_t size_stack, capacity_stack;
  const char * input, * output, * err;
  static aiger * src, * dst;
  unsigned j, out, tmp;
  unsigned * selected;
  unsigned * stack;
  char comment[128];
  aiger_mode mode;
  unsigned mapped;
  unsigned * map;
  aiger_and * a;
  char * coi;
  int reduce;
  size_t i;
  int ok;

  input = output = 0;

  size_selected = capacity_selected = 0;
  selected = 0;

  size_stack = capacity_stack = 0;
  stack = 0;

  reduce = 0;

  for (i = 1; i != argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (!strcmp (argv[i], "-r"))
	reduce = 1;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (output)
	die ("too many arguments");
      else if (contains_only_digits (argv[i]))
	{
	  int selection = parse_number (argv[i]);
	  msg ("selecting output '%u'", selection);
	  PUSH (selected, selection);
	}
      else if (input)
	output = argv[i];
      else
	input = argv[i];
    }

  src = aiger_init ();

  if (input)
    {
      msg ("reading '%s'", input);
      err = aiger_open_and_read_from_file (src, input);
    }
  else
    {
      input = "<stdin>";
      msg ("reading '%s'", input);
      err = aiger_read_from_file (src, stdin);
    }

  if (err)
    die ("read error: %s", err);

  msg ("read MILOA %u %u %u %u %u", 
       src->maxvar,
       src->num_inputs,
       src->num_latches,
       src->num_outputs,
       src->num_ands);

  if (!src->num_outputs)
    die ("can not find any outputs in '%s'", input);

  coi = calloc (src->maxvar + 1, 1);
  if (!coi)
    die ("out-of-memory allocating 'coi'");

  for (i = 0; i != size_selected; i++)
    {
      unsigned selection, lit;

      selection = selected[i];
      if (src->num_outputs <= selection)
	die ("selected output index '%u' too large (expected range '0..%u')",
	     selection, src->num_outputs-1);

      lit = src->outputs[selection].lit;
      COI (lit);
    }

  for (i = 0; i != size_stack; i++)
    {
      unsigned var, lit;
      aiger_symbol * s;
      aiger_and * a;

      var = stack[i];
      assert (coi[var]);
      lit = aiger_var2lit (var);

      a = aiger_is_and (src, lit);
      if (a)
	{
	  COI (a->rhs0);
	  COI (a->rhs1);
	}
      else
	{
	  s = aiger_is_latch (src, lit);
	  if (s)
	    {
	      COI (s->next);
	      COI (s->reset);
	    }
	}
    }

  msg ("found %zu literals in cone-of-influence", size_stack);

  map = calloc (2*(src->maxvar + 1), sizeof *map);
  if (!map)
    die ("out-of-memory allocating 'map'");

  mapped = 0;
  MAP (aiger_false);
  assert (mapped == 2);

  for (j = 0; j != src->num_inputs; j++)
    {
      unsigned lit = src->inputs[j].lit;
      if (!reduce || KEEP (lit))
	MAP (lit);
    }

  for (j = 0; j != src->num_latches; j++)
    {
      unsigned lit = src->latches[j].lit;
      if (KEEP (lit))
	MAP (lit);
    }

  for (j = 0; j != src->num_ands; j++)
    {
      unsigned lhs;
      a = src->ands + j;
      lhs = a->lhs;
      if (KEEP (lhs))
	MAP (lhs);
    }

  msg ("mapped '%zu' literals", mapped);

  dst = aiger_init ();
  for (j = 0; j != src->num_inputs; j++) {
    unsigned lit;
    lit = src->inputs[j].lit;
    if (reduce && !KEEP (lit))
      continue;
    aiger_add_input (dst, map[lit], src->inputs[j].name);
  }

  for (j = 0; j != src->num_latches; j++)
    {
      unsigned lit;
      lit = src->latches[j].lit;
      if (!KEEP (lit))
	continue;
      aiger_add_latch (dst, map[lit],
                       map[src->latches[j].next], src->latches[j].name);
      aiger_add_reset (dst, map[lit], map[src->latches[j].reset]);
    }

  for (j = 0; j != src->num_ands; j++)
    {
      unsigned lhs;
      a = src->ands + j;
      lhs = a->lhs;
      if (!KEEP (lhs))
	continue;
      aiger_add_and (dst, map[lhs], map[a->rhs0], map[a->rhs1]);
    }

  sprintf (comment, "aigselect");
  aiger_add_comment (dst, comment);

  for (int i = 0; i != size_selected; i++)
    {
      unsigned selection = selected[i];
      unsigned lit = src->outputs[selection].lit;
      aiger_add_output (dst, map[lit], src->outputs[selection].name);

      sprintf (comment,
	"selected output index %u of %u original outputs",
	selection, src->num_outputs);
      aiger_add_comment (dst, comment);
    }

  aiger_reset (src);

  aiger_reencode (dst);

  msg ("writing '%s'", output ? output : "<stdout>");

  if (output)
    ok = aiger_open_and_write_to_file (dst, output);
  else
    {
      mode = isatty (1) ? aiger_ascii_mode : aiger_binary_mode;
      ok = aiger_write_to_file (dst, mode, stdout);
    }

  if (!ok)
    die ("writing failed");

  msg ("wrote MILOA %u %u %u %u %u", 
       dst->maxvar,
       dst->num_inputs,
       dst->num_latches,
       dst->num_outputs,
       dst->num_ands);

  aiger_reset (dst);
  free (selected);
  free (coi);
  free (map);

  return 0;
}
