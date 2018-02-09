/***************************************************************************
Copyright (c) 2006-2018, Armin Biere, Johannes Kepler University.

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

static int verbose;

static void
msg (const char *fmt, ...)
{
  va_list ap;
  if (!verbose) return;
  fputs ("[aigtocnf] ", stderr);
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
  fputs ("[aigtocnf] WARNING ", stderr);
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
  fputs ("*** [aigtocnf] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

int
main (int argc, char **argv)
{
  const char *input_name, *output_name, *error;
  int res, * map, m, n, close_file, pg, prtmap;
  unsigned i, * refs, lit;
  aiger *aiger;
  FILE *file;

  pg = 1;
  prtmap = 0;
  res = close_file = 0;
  output_name = input_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: "
	     "aigtocnf [-h][-v][--no-pg][-m][<aig-file> [<dimacs-file>]]\n");
	  exit (0);
	}
      else if (!strcmp (argv[i], "-m")) prtmap = 1;
      else if (!strcmp (argv[i], "-v")) verbose++;
      else if (!strcmp (argv[i], "--no-pg")) pg = 0;
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
  if (aiger->num_outputs > 1) die ("more than one output");
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

  if (aiger->outputs[0].lit == 0)
    msg ("p cnf %u 1", aiger->num_inputs),
    fprintf (file, "p cnf %u 1\n0\n", aiger->num_inputs);
  else if (aiger->outputs[0].lit == 1)
    msg ("p cnf %u 0", aiger->num_inputs),
    fprintf (file, "p cnf %u 0\n", aiger->num_inputs);
  else 
    {
      refs = calloc (2*(aiger->maxvar+1), sizeof *refs);

      lit = aiger->outputs[0].lit;
      refs[lit]++;

      i = aiger->num_ands; 
      while (i--)
	{
	  lit = aiger->ands[i].lhs;
	  if (refs[lit]) 
	    {
	      refs[aiger->ands[i].rhs0]++;
	      refs[aiger->ands[i].rhs1]++;
	    }
	  if (refs[aiger_not (lit)]) 
	    {
	      refs[aiger_not (aiger->ands[i].rhs0)]++;
	      refs[aiger_not (aiger->ands[i].rhs1)]++;
	    }
	}

      if (!pg)
	{
	  for (lit = 2; lit <= 2*aiger->maxvar+1; lit++)
	    refs[lit] = INT_MAX;
	}

      map = calloc (2*(aiger->maxvar+1), sizeof *map);
      m = 0;
      n = 1;
      if (refs[0] || refs[1]) 
	{
	  map[0] = -1;
	  map[1] = 1;
	  m++;
	  n++;
	}
      for (lit = 2; lit <= 2*aiger->maxvar; lit += 2)
	{
	  if (!refs[lit] && !refs[lit+1]) continue;
	  map[lit] = ++m;
	  map[lit+1] = -m;
	  if (prtmap) fprintf (file, "c %d -> %d\n", lit, m);
	  if (lit <= 2*aiger->num_inputs+1) continue;
	  if (refs[lit]) n += 2;
	  if (refs[lit+1]) n += 1;
	}

      fprintf (file, "p cnf %u %u\n", m, n);
      msg ("p cnf %u %u", m, n);

      if (refs[0] || refs[1]) fprintf (file, "%d 0\n", map[1]);

      for (i = 0; i < aiger->num_ands; i++)
	{
	  lit = aiger->ands[i].lhs;
	  if (refs[lit])
	    {
	      fprintf (file, 
		       "%d %d 0\n",
		       map[aiger_not (lit)],
		       map[aiger->ands[i].rhs1]);
	      fprintf (file, 
		       "%d %d 0\n",
		       map[aiger_not (lit)],
		       map[aiger->ands[i].rhs0]);
	    }
	  if (refs[lit+1])
	    {
	      fprintf (file, 
		       "%d %d %d 0\n",
		       map[lit],
		       map[aiger_not (aiger->ands[i].rhs1)],
		       map[aiger_not (aiger->ands[i].rhs0)]);
	    }
	}

      fprintf (file, "%d 0\n", map[aiger->outputs[0].lit]);

      free (refs);
      free (map);
    }

  if (close_file) fclose (file); 
  aiger_reset (aiger);

  return res;
}
