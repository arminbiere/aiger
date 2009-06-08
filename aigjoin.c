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
#include <limits.h>

#define USAGE \
"usage: aigjoin [-h][-v][-f][-o <output>][<input> ...]\n" \
"\n" \
"Join AIGER models.\n"

static aiger ** srcs, * dst;
static char ** names;
static int verbose;

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
msg (int level, const char *fmt, ...)
{
  va_list ap;
  if (verbose < level)
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
  unsigned inputs = UINT_MAX;
  aiger ** q, * src;
  int i, force = 0;
  char ** p;

  p = names = calloc (argc, sizeof *names);
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbose++;
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
	  assert (p < names + argc);
	  *p++ = argv[i];
	}
    }

  if (p == names)
    die ("no input model specified");

  msg (1, "specified %d models for merging", p - names);
  assert (p < names + argc);
  *p = 0;

  q = srcs = calloc (argc, sizeof *srcs);
  for (p = names; *p; p++)
    {
      msg (1, "reading %s", *p);
      src = *q = aiger_init ();
      err = aiger_open_and_read_from_file (src, *p);
      if (err)
	die ("read error on %s: %s", *p, err);
      msg (2, "found MILOA %u %u %u %u %u",
           src->maxvar,
           src->num_inputs,
           src->num_latches,
           src->num_outputs,
           src->num_ands);
      if (inputs != UINT_MAX)
	{
	  if (src->num_inputs != inputs)
	    {
	      if (force)
		msg (1, "%s: expected %u inputs but got %u", 
		     *p, inputs, src->num_inputs);
	      else
		die ("%s: expected %u inputs but got %u", 
		     *p, inputs, src->num_inputs);
	    }
	}
      else
	inputs = src->num_inputs;
    }

  free (names);

  dst = aiger_init ();

  for (q = srcs; (src = *q); q++)
    aiger_reset (src);
  free (srcs);

  aiger_reset (dst);

  return 0;
}
