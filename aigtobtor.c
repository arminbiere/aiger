/***************************************************************************
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.
Copyright (c) 2015 Aina Niemetz, Johannes Kepler University.

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

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>

static int verbose;

static void
msg (const char *fmt, ...)
{
  va_list ap;
  if (!verbose) return;
  fputs ("[aigtobtor] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
wrn (const char *fmt, ...)
{
  va_list ap;
  fflush (stdout);
  fputs ("[aigtobtor] WARNING ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
die (const char *fmt, ...)
{
  va_list ap;
  fflush (stdout);
  fputs ("*** [aigtobtor] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

int
main (int argc, char **argv)
{
  int i, j, lit, btorid, btorid0, btorid1, res, close_file, *map, prtmap, reft;
  const char *input_name, *output_name, *error;
  aiger *aiger;
  FILE *file;

  prtmap = 0; reft = 0;
  res = close_file = 0;
  output_name = input_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: "
	     "aigtobtor [-h][-v][-m][<aig-file> [<btor-file>]]\n");
	  exit (0);
	}
      else if (!strcmp (argv[i], "-m")) prtmap = 1;
      else if (!strcmp (argv[i], "-v")) verbose++;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (!input_name) input_name = argv[i];
      else if (!output_name) output_name = argv[i];
      else die ("more than two files specified");
    }

  aiger = aiger_init ();

  if (input_name)
    error = aiger_open_and_read_from_file (aiger, input_name);
  else
    error = aiger_read_from_file (aiger, stdin);

  if (error)
    die ("%s: %s", input_name ? input_name : "<stdin>", error);

  msg ("read MILOA %u %u %u %u %u", 
       aiger->maxvar,
       aiger->num_inputs,
       aiger->num_latches,
       aiger->num_outputs,
       aiger->num_ands);

  if (aiger->num_latches) die ("can not handle latches");
  if (aiger->num_bad) 
    die ("can not handle bad state properties (use 'aigmove')");
  if (aiger->num_constraints) 
    die ("can not handle environment constraints (use 'aigmove')");
  if (!aiger->num_outputs) die ("no output");
  if (aiger->num_justice) wrn ("ignoring justice properties");
  if (aiger->num_fairness) wrn ("ignoring fairness constraints");

  close_file = 0;
  if (output_name)
    {
      file = fopen (output_name, "w");
      if (!file) die ("failed to write '%s'", output_name);
      close_file = 1;
    }
  else
    file = stdout;

  aiger_reencode (aiger);

  map = calloc (2*(aiger->maxvar+1), sizeof *map);
  map[0] = 1; map[1] = -1; j = 2;
  for (i = 0; i < aiger->num_inputs; i++)
    {
      lit = aiger->inputs[i].lit;
      assert (lit > 1);
      assert (map[lit] == 0);
      assert (map[aiger_not (lit)] == 0);
      assert (j <= aiger->maxvar+1);
      map[lit] = j;
      map[aiger_not (lit)] = -j;
      if (prtmap)
	fprintf (file, "; %d -> %d\n", lit, j);
      j += 1;
    }
  for (i = 0; i < aiger->num_ands; i++)
    {
      lit = aiger->ands[i].lhs;
      assert (map[lit] == 0);
      assert (map[aiger_not (lit)] == 0);
      assert (j <= aiger->maxvar+1);
      map[lit] = lit & 1 ? -j : j;
      map[aiger_not (lit)] = -map[lit];
      if (lit == aiger_true || lit == aiger_false) reft += 1;
      if (prtmap)
	fprintf (file, "; %d -> %d\n", lit & 1 ?  aiger_not (lit) : lit, j);
      j += 1;
    }
  for (i = 0; i < aiger->num_outputs; i++)
    {
      lit = aiger->outputs[i].lit;
      if (lit == aiger_true || lit == aiger_false) reft += 1;
    }

  if ((reft = reft || aiger->outputs[0].lit == 0 || aiger->outputs[0].lit == 1))
    fprintf (file, "1 zero 1\n");
  for (i = 0; i < aiger->num_inputs; i++)
    {
      lit = aiger->inputs[i].lit;
      btorid = reft ? map[lit] : map[lit]-1;
      fprintf (file, "%d var 1\n", btorid);
    }
  for (i = 0; i < aiger->num_ands; i++)
    {
      lit = aiger->ands[i].lhs;
      lit = lit & 1 ? aiger_not (lit) : lit;
      btorid = reft ? map[lit] : map[lit]-1;
      btorid0 = map[aiger->ands[i].rhs0];
      btorid0 = reft ? btorid0 : (btorid0 < 0 ? btorid0+1 : btorid0-1);
      btorid1 = map[aiger->ands[i].rhs1];
      btorid1 = reft ? btorid1 : (btorid1 < 0 ? btorid1+1 : btorid1-1);
      fprintf (file, "%d and 1 %d %d\n", btorid, btorid0, btorid1);
    }
  for (i = 0; i < aiger->num_outputs; i++)
    {
      lit = aiger->outputs[i].lit;
      assert (map[lit]);
      btorid = reft ? map[lit] : (map[lit] < 0 ? map[lit]+1 : map[lit]-1);
      fprintf (file, "%d root 1 %d\n", j++, btorid);
    }

  if (close_file) fclose (file); 
  aiger_reset (aiger);

  return res;
}
