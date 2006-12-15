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

static aiger * model;

int
main (int argc, char ** argv)
{
  const char * model_name, * dot_name;
  int i;

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
  aiger_reset (model);

  return 0;
}
