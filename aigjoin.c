/***************************************************************************
Copyright (c) 2009, Armin Biere, Johannes Kepler University.

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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define USAGE \
"usage: aigjoin [-h][-v][-f][-o <output>][<input> ...]\n" \
"\n" \
"Join AIGER models.\n"

static aiger ** srcs, * dst;
static char ** inputs;
static int verbose = 0;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigjoin] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
msg (const char *fmt, ...)
{
  va_list ap;
  if (!verbose)
    return;
  fputs ("[aigjoin] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

int
main (int argc, char ** argv)
{
  const char * output = 0, * err;
  aiger ** q, * src;
  int i, force = 0;
  char ** p;

  p = inputs = calloc (argc, sizeof *inputs);
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (!strcmp (argv[i], "-f"))
	force = 1;
      else if (!strcmp (argv[i], "-o"))
	{
	  if (++i == argc)
	    die ("argument to '-o' missing");

	  if (output)
	    die ("multiple output files specified");

	  output = argv[i];
	}
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else
	{
	  assert (p < inputs + argc);
	  *p++ = argv[i];
	}
    }

  if (p == inputs)
    die ("no input model specified");

  msg ("specified %d models for merging", p - inputs);
  assert (p < inputs + argc);
  *p = 0;

  q = srcs = calloc (argc, sizeof *srcs);
  for (p = inputs; *p; p++)
    {
      msg ("reading %s", *p);
      src = *q = aiger_init ();
      err = aiger_open_and_read_from_file (src, *p);
      if (err)
	die ("read error on %s: %s", *p, err);
    }

  free (inputs);

  dst = aiger_init ();

  for (q = srcs; (src = *q); q++)
    aiger_reset (src);
  free (srcs);

  aiger_reset (dst);

  return 0;
}
