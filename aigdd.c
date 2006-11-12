#include "aiger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static aiger * src;
static aiger * dst;

static unsigned * stable;
static unsigned * unstable;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** [aigdd] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
copy (const char * name)
{
  dst = aiger_init ();
  unlink (name);
  if (!aiger_open_and_write_to_file (dst, name))
    die ("failed to write '%s'", name);
  aiger_reset (dst);
}

#define USAGE \
  "usage: aigdd src dst [run]\n"

int
main (int argc, char ** argv)
{
  const char * src_name, * dst_name, * run_name, * err;
  int i, changed;

  src_name = dst_name = run_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (src_name && dst_name && run_name)
	die ("more than three files");
      else if (dst_name)
	run_name = argv[i];
      else if (src_name)
	dst_name = argv[i];
      else
	src_name = argv[i];
    }

  if (!src_name || !dst_name)
    die ("expected exactly two files");

  if (!run_name)
    run_name = "./run";

  src = aiger_init ();
  if ((err = aiger_open_and_read_from_file (src, src_name)))
    die ("%s: %s", src_name, err);

  aiger_reencode (src);		/* make sure that ands are sorted */

  stable = malloc (sizeof (stable[0]) * (src->maxvar + 1));
  unstable = malloc (sizeof (unstable[0]) * (src->maxvar + 1));

  for (i = 0; i <= src->maxvar; i++)
    stable[i] = i;

  changed = 1;
  while (changed)
    {
      changed = 0;
    }

  for (i = 0; i <= src->maxvar; i++)
    unstable[i] = stable[i];

  copy (dst_name);

  free (stable);
  free (unstable);
  aiger_reset (src);

  return 0;
}
