#include "aiger.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static unsigned k;
static int foundk;

static aiger * model;
static const char *model_file_name;
static unsigned ** m2e;

static aiger * expansion;
static const char *expansion_file_name;
static unsigned char * assignment;

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

static void
link (void)
{
  const char * p, * start_of_name, * end_of_name;
  unsigned i, tidx, midx, eidx;
  aiger_symbol * esym, * isym;
  char ch;

  m2e = malloc (sizeof (m2e[0]) * model->num_inputs);
  for (i = 0; i < model->num_inputs; i++)
    m2e[i] = calloc (k + 1, sizeof (m2e[i][0]));

  for (i = 0; i < expansion->num_inputs; i++)
    {
      esym = expansion->inputs + i;
      p = esym->name; 
      if (!p)
INVALID_SYMBOL:
	die ("input %u does not have a valid expanded symbol in '%s'", 
	     i, expansion_file_name);

      ch = *p++;
      if (!isdigit (ch))
	goto INVALID_SYMBOL;

      tidx = ch - '0';
      while (isdigit (ch = *p++))
	tidx = 10 * tidx + (ch - '0');

      if (ch != ' ')
	goto INVALID_SYMBOL;

      start_of_name = p;
      while (*p++)
	;
    }
}

static void
parse (void)
{
  assignment = calloc (expansion->num_inputs, sizeof (assignment[0]));
}


#define USAGE \
  "usage: wrapstim [-h] <model> <expansion> <k> [<stimulus>]\n"

int
main (int argc, char ** argv)
{
  const char * err;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (argv[i][0] = '-')
        die ("invalid command line option '%s'", argv[i]);
      else if (!model_file_name)
	model_file_name = argv[i];
      else if (!expansion_file_name)
	expansion_file_name = argv[i];
      else if (!foundk)
	{
	  const char * p = argv[i];
	  while (*p)
	    if (!isdigit (*p++))
	      die ("expected bound as third argument");

	  k = atoi (argv[i]);
	  foundk = 1;
	}
      else if (!stimulus_file)
	stimulus_file_name = argv[i];
      else
	die ("too many paramaters");
    }

  if (!model_file_name)
    die ("no model specified");

  if (!expansion_file_name)
    die ("no expansion specified");

  if (!foundk)
    die ("unspecified bound");

  model = aiger_init ();
  if ((err = aiger_open_and_read_from_file (model, model_file_name)))
    die ("%s", err);

  expansion = aiger_init ();
  if ((err = aiger_open_and_read_from_file (expansion, expansion_file_name)))
    die ("%s", err);

  link ();

  if (!stimulus_file_name)
    {
      if (!(stimulus_file = fopen (stimulus_file_name, "r")))
	die ("failed to read '%s'", stimulus_file_name);

      close_stimulus_file = 1;
    }
  else
    stimulus_file = stdin;

  parse ();

  if (close_stimulus_file)
    fclose (stimulus_file);

  aiger_reset (expansion);

  for (i = 0; i < model->num_inputs; i++)
    free (m2e[i]);
  free (m2e);

  aiger_reset (model);
   
  return 0;
}
