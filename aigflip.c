/***************************************************************************
Copyright (c) 2011, Armin Biere, Johannes Kepler University.

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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USAGE \
"usage: aigflip [-h][-v][<input> [<output>]]\n" \
"\n" \
"Flip (negate) all outputs of an AIGER model.\n"

static aiger * src, * dst;
static int verbose = 0;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigflip] ", stderr);
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
  fputs ("[aigflip] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

int
main (int argc, char ** argv)
{
  const char * input, * output, * err, * srcname;
  unsigned out, tmp;
  char comment[80];
  aiger_mode mode;
  char * dstname;
  aiger_and * a;
  int i, ok;

  input = output = 0;

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
      else if (input)
	output = argv[i];
      else
	input = argv[i];
    }

  msg ("reading %s", input ? input : "<stdin>");
  src = aiger_init ();
  if (input)
    err = aiger_open_and_read_from_file (src, input);
  else
    err = aiger_read_from_file (src, stdin);

  if (err)
    die ("read error: %s", err);

  msg ("read MILOA %u %u %u %u %u", 
       src->maxvar,
       src->num_inputs,
       src->num_latches,
       src->num_outputs,
       src->num_ands);

  if (src->num_bad) die ("can not handle bad state properties");
  if (src->num_constraints) 
    die ("can not handle environment state constraints");
  if (src->num_justice) die ("can not handle justice properties");
  if (src->num_fairness) die ("can not handle fairness constraints");

  dst = aiger_init ();
  for (i = 0; i < src->num_inputs; i++)
    aiger_add_input (dst, src->inputs[i].lit, src->inputs[i].name);

  for (i = 0; i < src->num_latches; i++) {
    aiger_add_latch (dst, src->latches[i].lit, 
                          src->latches[i].next,
                          src->latches[i].name);
    aiger_add_reset (dst, src->latches[i].lit, src->latches[i].reset);
  }

  for (i = 0; i < src->num_ands; i++)
    {
      a = src->ands + i;
      aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
    }

  for (i = 0; i < src->num_outputs; i++)
    {
      out = src->outputs[i].lit;
      srcname = src->outputs[i].name;
      if (srcname)
	{
	  dstname = malloc (strlen (srcname) + 20);
	  sprintf (dstname, "AIGFLIP_%s", srcname);
	}
      else
	dstname = 0;
      aiger_add_output (dst, aiger_not (out), dstname);
      free (dstname);
    }

  sprintf (comment, "aigflip");
  aiger_add_comment (dst, comment);
  sprintf (comment, "flipped/negated all original outputs");
  aiger_add_comment (dst, comment);

  aiger_reset (src);

  msg ("writing %s", output ? output : "<stdout>");

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
