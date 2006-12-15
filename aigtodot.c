#include "aiger.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static void
die (const char * fmt, ...)
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
main (int argc, char ** argv)
{
  const char * model_name, * dot_name, * err;
  int i, close_dot_file;
  FILE * dot_file;
  aiger * model;

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
      aiger_and * and = model->ands + i;

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=", and->lhs/2, and->rhs0/2);
      fputs (((and->rhs0 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);

      fprintf (dot_file, "\"%u\"->\"%u\"[arrowhead=", and->lhs/2, and->rhs1/2);
      fputs (((and->rhs1 & 1) ? "dot" : "none"), dot_file);
      fputs ("];\n", dot_file);
    }

  fputs ("}\n", dot_file);

  if (close_dot_file)
    fclose (dot_file);

  aiger_reset (model);


  return 0;
}
