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

#define LIT(L) (aiger_strip(L)/intlits)

int
main (int argc, char **argv)
{
  const char *model_name, *dot_name, *err, * p;
  int i, close_dot_file, zero, strip, intlits;
  FILE *dot_file;
  aiger *model;
  char ch;

  model_name = dot_name = 0;
  intlits = 1;
  strip = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr,
	    "usage: aigtodot [-h][-s][-i] [<aiger-model> [<dot-file>]]\n"
	    "\n"
	    "  -h  print this command line option summary\n"
	    "  -s  strip and do not show symbols\n"
	    "  -i  use integer indices (divide literals by two)\n");
	  exit (0);
	}
      else if (!strcmp (argv[i], "-s"))
	strip = 1;
      else if (!strcmp (argv[i], "-i"))
	intlits = 2;
      else if (argv[i][0] == '-')
	die ("unknown command line option '%s'", argv[i]);
      else if (dot_name)
	die ("expected at most two file names on command line");
      else if (model_name)
	dot_name = argv[i];
      else
	model_name = argv[i];
    }

  if (model_name && dot_name && !strcmp (model_name, dot_name))
    die ("two identical file names given");

  model = aiger_init ();
  if (model_name)
    err = aiger_open_and_read_from_file (model, model_name);
  else
    err = aiger_read_from_file (model, stdin);

  if (err)
    die ("%s", err);

  if (strip)
    aiger_strip_symbols_and_comments (model);

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

  fputs ("digraph \"", dot_file);
  if (model_name)
    {
      for (p = model_name; (ch = *p); p++)
	{
	  if (ch == '"' || ch == '\\')		/* mangle */
	    fputc ('\\', dot_file);

	  fputc (ch, dot_file);
	}
    }
  else
    fputs ("<stdin>", dot_file);

  fputs ("\" {\n", dot_file);

  for (i = 0; i < model->num_inputs; i++)
    {
      fprintf (dot_file, "\"%u\"[shape=box];\n", LIT(model->inputs[i].lit));

      fprintf (dot_file, "I%u[shape=triangle,color=blue];\n", i);

      if (model->inputs[i].name)
	fprintf (dot_file,
	         "I%u[label=\"%s\"];\n", 
	         i, model->inputs[i].name);

      fprintf (dot_file,
	       "\"%u\"->I%u[arrowhead=none];\n",
	       LIT(model->inputs[i].lit), i);
    }

  zero = 0;

  for (i = 0; i < model->num_ands; i++)
    {
      aiger_and *and = model->ands + i;

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=", 
	       LIT (and->lhs), LIT (and->rhs0));
      fputs (((and->rhs0 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=",
               LIT (and->lhs), LIT (and->rhs1));
      fputs (((and->rhs1 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      if (and->rhs0 <= 1)
	zero = 1;

      if (and->rhs1 <= 1)
	zero = 1;
    }

  for (i = 0; i < model->num_outputs; i++)
    {
      fprintf (dot_file, "O%u[shape=triangle,color=blue];\n", i);

      if (model->outputs[i].name)
	fprintf (dot_file,
	         "O%u[label=\"%s\"];\n", 
	         i, model->outputs[i].name);

      fprintf (dot_file,
	       "O%u -> \"%u\"[arrowhead=",
	       i, LIT (model->outputs[i].lit));
      fputs ((((model->outputs[i].lit) & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      if (model->outputs[i].lit <= 1)
	zero = 1;
    }

  for (i = 0; i < model->num_bad; i++)
    {
      fprintf (dot_file, "B%u[shape=triangle,color=red];\n", i);

      if (model->bad[i].name)
	fprintf (dot_file,
	         "B%u[label=\"%s\"];\n", 
	         i, model->bad[i].name);

      fprintf (dot_file,
	       "B%u -> \"%u\"[arrowhead=",
	       i, LIT (model->bad[i].lit));
      fputs ((((model->bad[i].lit) & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      if (model->bad[i].lit <= 1)
	zero = 1;
    }

  for (i = 0; i < model->num_constraints; i++)
    {
      fprintf (dot_file, "C%u[shape=triangle,color=green];\n", i);

      if (model->constraints[i].name)
	fprintf (dot_file,
	         "C%u[label=\"%s\"];\n", 
	         i, model->constraints[i].name);

      fprintf (dot_file,
	       "C%u -> \"%u\"[arrowhead=",
	       i, LIT (model->constraints[i].lit));
      fputs ((((model->constraints[i].lit) & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      if (model->constraints[i].lit <= 1)
	zero = 1;
    }

  for (i = 0; i < model->num_latches; i++)
    {
      fprintf (dot_file, 
	       "\"%u\"[shape=box,color=magenta];\n",
	       LIT(model->latches[i].lit));

      fprintf (dot_file, "L%u [shape=diamond,color=magenta];\n", i);

      if (model->latches[i].name)
	fprintf (dot_file,
	         "L%u[label=\"%s\"];\n", 
	         i, model->latches[i].name);

      fprintf (dot_file,
	       "L%u -> \"%u\"[arrowhead=",
	       i, LIT (model->latches[i].next));
      fputs ((((model->latches[i].next) & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      fprintf (dot_file,
	       "L%u -> \"%u\"[style=dashed,color=magenta,arrowhead=none];\n",
	       i, LIT(model->latches[i].lit));

      if (model->latches[i].next <= 1)
	zero = 1;
    }

  if (zero)
    fputs ("\"0\"[color=red,shape=box];\n", dot_file);

  fputs ("}\n", dot_file);

  if (close_dot_file)
    fclose (dot_file);

  aiger_reset (model);


  return 0;
}
