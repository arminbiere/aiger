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

static int
lit2int (aiger * mgr, unsigned a)
{
  int sign = aiger_sign (a) ? -1 : 1;
  int res = aiger_lit2var (a);

  if (res)
    res *= sign;
  else
    res = -sign * (mgr->maxvar + 1);	/* TRUE and FALSE */

  return res;
}

int
main (int argc, char **argv)
{
  const char *input_name, *output_name, *error;
  int res, close_file;
  aiger *aiger;
  FILE *file;
  unsigned i;

  res = close_file = 0;
  output_name = input_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr,
		   "usage: aigtocnf [-h][<aig-file> [<dimacs-file>]]\n");
	  exit (0);
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
	  fprintf (file,
		   "p cnf %u %u\n",
		   aiger->maxvar + 1, 3 * aiger->num_ands + 2);

	  for (i = 0; i < aiger->num_ands; i++)
	    {
	      aiger_and *and = aiger->ands + i;

	      fprintf (file, "%d %d 0\n",
		       lit2int (aiger, aiger_not (and->lhs)),
		       lit2int (aiger, and->rhs0));

	      fprintf (file, "%d %d 0\n",
		       lit2int (aiger, aiger_not (and->lhs)),
		       lit2int (aiger, and->rhs1));

	      fprintf (file, "%d %d %d 0\n",
		       lit2int (aiger, and->lhs),
		       lit2int (aiger, aiger_not (and->rhs0)),
		       lit2int (aiger, aiger_not (and->rhs1)));
	    }

	  fprintf (file, "%d 0\n", lit2int (aiger, aiger_true));
	  fprintf (file, "%d 0\n", lit2int (aiger, aiger->outputs[0].lit));
	}

      if (close_file)
	fclose (file);
    }

  aiger_reset (aiger);

  return res;
}
