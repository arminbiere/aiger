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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** aigtodot: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

int
main (int argc, char **argv)
{
  const char *model_name, *dot_name, *err;
  int i, close_dot_file;
  FILE *dot_file;
  aiger *model;

  model_name = dot_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr,
		   "usage: aigtodot [-h] [<aiger-model> [<dot-file>]]\n");
	  exit (0);
	}

      if (argv[i][0] == '-')
	die ("unknown command line option '%s'", argv[i]);

      if (dot_name)
	die ("expected at most two file names on command line");
      else if (model_name)
	dot_name = argv[i];
      else
	model_name = argv[i];
    }

  model = aiger_init ();
  if (model_name)
    err = aiger_open_and_read_from_file (model, model_name);
  else
    err = aiger_read_from_file (model, stdin);

  if (err)
    die ("%s", err);

  if (dot_name)
    {
      dot_file = fopen (dot_name, "w");
      if (!dot_file)
	die ("can not write to '%s'", dot_name);
      close_dot_file = 1;
    }
  else
    {
      dot_file = stdout;
      close_dot_file = 0;
    }

  fputs ("digraph {\n", dot_file);

  for (i = 0; i < model->num_ands; i++)
    {
      aiger_and *and = model->ands + i;

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=", and->lhs / 2,
	       and->rhs0 / 2);
      fputs (((and->rhs0 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=", and->lhs / 2,
	       and->rhs1 / 2);
      fputs (((and->rhs1 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);
    }

  fputs ("}\n", dot_file);

  if (close_dot_file)
    fclose (dot_file);

  aiger_reset (model);


  return 0;
}
