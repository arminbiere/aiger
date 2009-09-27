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

#include <string.h>
#include <stdlib.h>
#include <limits.h>

int
main (int argc, char **argv)
{
  const char *input_name, *output_name, *error;
  int res, * map, m, n, close_file, pg;
  unsigned i, * refs, lit;
  aiger *aiger;
  FILE *file;

  pg = 1;
  res = close_file = 0;
  output_name = input_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr,
		   "usage: "
		   "aigtocnf [-h][--no-pg][<aig-file> [<dimacs-file>]]\n");
	  exit (0);
	}

      if (!strcmp (argv[i], "--no-pg"))
	{
	  pg = 0;
	  continue;
	}

      if (argv[i][0] == '-')
	{
	  fprintf (stderr,
		   "*** [aigtocnf] invalid command line option '%s'\n",
		   argv[i]);
	  exit (1);
	}

      if (!input_name)
	input_name = argv[i];
      else if (!output_name)
	output_name = argv[i];
      else
	{
	  fprintf (stderr, "*** [aigtocnf] more than two files specified\n");
	  exit (1);
	}
    }

  aiger = aiger_init ();

  if (input_name)
    error = aiger_open_and_read_from_file (aiger, input_name);
  else
    error = aiger_read_from_file (aiger, stdin);

  if (error)
    {
      fprintf (stderr,
	       "*** [aigtocnf] %s: %s\n",
	       input_name ? input_name : "<stdin>", error);
      res = 1;
    }
  else if (aiger->num_latches)
    {
      fprintf (stderr,
               "*** [aigtocnf] %s: can not handle latches\n",
	       input_name ? input_name : "<stdin>");
      res = 1;
      res = 1;
    }
  else if (aiger->num_outputs != 1)
    {
      fprintf (stderr,
	       "*** [aigtocnf] %s: expected exactly one output\n",
	       input_name ? input_name : "<stdin>");
      res = 1;
    }
  else
    {
      close_file = 0;
      if (output_name)
	{
	  file = fopen (output_name, "w");
	  if (!file)
	    {
	      fprintf (stderr,
		       "*** [aigtocnf] failed to write '%s'\n", output_name);
	      res = 1;
	    }
	  else
	    close_file = 1;
	}
      else
	file = stdout;

      if (file)
	{
	  aiger_reencode (aiger);

	  if (aiger->outputs[0].lit == 0)
	    fprintf (file, "p cnf 0 1\n0\n");
	  else if (aiger->outputs[0].lit == 1)
	    fprintf (file, "p cnf 0 0\n");
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
		  if (lit <= 2*aiger->num_inputs+1) continue;
		  if (refs[lit]) n += 2;
		  if (refs[lit+1]) n += 1;
		}

	      fprintf (file, "p cnf %u %u\n", m, n);

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
	}

      if (close_file)
	fclose (file);
    }

  aiger_reset (aiger);

  return res;
}
