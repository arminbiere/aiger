#include "aiger.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static unsigned k;

static aiger * model;
static const char *model_file_name;

static aiger * expansion;
static const char *expansion_file_name;

static const char *stimulus_file_name;
static int close_stimulus_file;
static FILE * stimulus_file;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** [wrapstim] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

#define USAGE \
  "usage: wrapstim [-h] <model> <expansion> [<k>] [<stimulus>]\n"

int
main (int argc, char ** argv)
{
  int i;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
    }

  return 0;
}
