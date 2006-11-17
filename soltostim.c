#include "aiger.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>

static FILE * solution_file;
static int close_solution_file;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** soltostim: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  exit (1);
}

static const char * USAGE =
"usage: soltostim [-h] <aiger-model> [ <dimacs-solution> ]"
;

int
main (int argc, char ** argv)
{
  const char * model_file_name, * solution_file_name, * err;
  aiger * model;
  int i;

  solution_file_name = model_file_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stdout);
	  exit (1);
	}
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (solution_file_name)
	die ("more than two files on command line");
      else if (model_file_name)
	solution_file_name = argv[i];
      else
	model_file_name = argv[i];
    }

  if (!model_file_name)
    die ("no model specified");

  model = aiger_init ();
  if ((err = aiger_open_and_read_from_file (model, model_file_name)))
    die ("%s: %s", model_file_name, err);

  aiger_reset (model);

  if (close_solution_file)
    fclose (solution_file);

  return 0;
}
