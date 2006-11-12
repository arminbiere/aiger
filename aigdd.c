#include "aiger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static aiger * src;
static aiger * dst;

static unsigned * map;
static unsigned * trie;

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

#define USAGE \
  "usage: aigdd src dst\n"

int
main (int argc, char ** argv)
{
  const char * src_name, * dst_name, * err;
  int i;

  src_name = dst_name = 0;
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (src_name && dst_name)
	die ("more than two files");
      else if (src_name)
	dst_name = argv[i];
      else
	src_name = argv[i];
    }

  if (!src_name || !dst_name)
    die ("expected exactly two files");

  src = aiger_init ();
  if ((err = aiger_open_and_read_from_file (src, src_name)))
    die ("%s: %s", src_name, err);

  aiger_reencode (src);		/* make sure ands topological sorted */

  map = malloc (sizeof (map[0]) * (src->maxvar + 1));
  trie = malloc (sizeof (map[0]) * (src->maxvar + 1));

  free (map);
  free (trie);
  aiger_reset (src);

  return 0;
}
