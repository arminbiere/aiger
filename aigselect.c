/***************************************************************************
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
"usage: aigselect [-h][-v][<output-idx>][<input-aig> [<output-aig>]]\n" \
"\n" \
"Selects output '<output-idx>' from '<input-aig>' and writes the resulting\n" \
"AIG with only one output to '<output-aig>'\n"

static aiger * src, * dst;
static int verbose = 0;
static unsigned selection;

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

int
main (int argc, char ** argv)
{
  const char * input, * output, * err;
  int i, ok, already_selected;
  unsigned j, out, tmp;
  char comment[80];
  aiger_mode mode;
  aiger_and * a;

  input = output = 0;
  already_selected = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (output)
	die ("too many arguments");
      else if (contains_only_digits (argv[i]))
	{
	  if (already_selected)
	    die ("multiple selections '%u' and '%s'", select, argv[i]);

	  selection = parse_number (argv[i]);
	  msg ("selecting output '%u'", selection);
	  already_selected = 1;
	}
      else if (input)
	output = argv[i];
      else
	input = argv[i];
    }

  if (!already_selected)
    msg ("selecting default output '%u'", selection);

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

  if (src->num_outputs <= selection)
    die ("selected output index '%u' too large (expected range '0..%u')",
      selection, src->num_outputs-1);

  dst = aiger_init ();
  for (j = 0; j < src->num_inputs; j++)
    aiger_add_input (dst, src->inputs[j].lit, src->inputs[j].name);

  for (j = 0; j < src->num_latches; j++)
    {
      aiger_add_latch (dst, src->latches[j].lit, 
			    src->latches[j].next,
			    src->latches[j].name);
      aiger_add_reset (dst, src->latches[j].lit, 
			    src->latches[j].reset);
    }

  for (j = 0; j < src->num_ands; j++)
    {
      a = src->ands + j;
      aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
    }

  aiger_add_output (dst,
    src->outputs[selection].lit,
    src->outputs[selection].name);

  sprintf (comment, "aigselect");
  aiger_add_comment (dst, comment);
  sprintf (comment,
    "selected output index %u of %u original outputs",
    selection, src->num_outputs);
  aiger_add_comment (dst, comment);

  aiger_reset (src);

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

  return 0;
}
