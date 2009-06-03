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
#include <ctype.h>
#include <unistd.h>

static int combinational, verbosity;
static unsigned M, I, L, O, A;
static aiger * model;
static unsigned rng;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigfuzz] ", stderr);
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
  if (verbosity < level)
    return;
  fputs ("[aigfuzz] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned
pick (unsigned from, unsigned to)
{
  unsigned res = rng;
  msg (3, "rng %u", rng);
  assert (from <= to);
  rng *= 1664525u;
  rng += 1013904223u;
  res %= to - from + 1;
  res += from;
  return res;
}

static int
isposnum (const char * str)
{
  const char * p;

  if (str[0] == '0' && !str[1])
    return 0;

  for (p = str; *p; p++)
    if (!isdigit (*p))
      return 0;

  return 1;
}

#define USAGE \
"usage: aigfuzz [-h][-v][-c][-o dst][seed]\n" \
"\n" \
"An AIG fuzzer to generate random AIGs.\n" \
"\n" \
"  -h    print this command line option summary\n" \
"  -v    verbose output on 'stderr'\n" \
"  -c    combinational logic only, e.g. no latches\n" \
"\n" \
"  dst   output file with 'stdout' as default\n" \
"\n" \
"  seed  force deterministic random number generation\n"

int
main (int argc, char ** argv)
{
  int i, seed = -1, ok;
  const char *dst = 0;
  aiger_mode mode;

  for (i = 1; i < argc; i++) 
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbosity++;
      else if (!strcmp (argv[i], "-c"))
	combinational = 1;
      else if (!strcmp (argv[i], "-o"))
	{
	  if (dst)
	    die ("multiple output '%s' and '%s'", dst, argv[i]);

	  if (++i == argc)
	    die ("argument to '-o' missing");

	  dst = argv[i];
	}
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (isposnum (argv[i]))
	{
	  seed = atoi (argv[i]);
	  if (seed < 0)
	    die ("invalid seed '%s'", argv[i]);
	}
      else
	die ("invalid command line argument '%s'", argv[i]);
    }

  rng = (seed >= 0) ? seed : abs ((times(0) * getpid ()) >> 1);

  msg (1, "seed %u", rng);

  model = aiger_init ();

  msg (1, "writing %s", dst ? dst : "<stdout>");
  if (dst)
    ok = aiger_open_and_write_to_file (model, dst);
  else
    {
      mode = isatty (1) ? aiger_ascii_mode : aiger_binary_mode;
      ok = aiger_write_to_file (model, mode, stdout);
    }

  if (!ok)
    die ("write error");

  aiger_reset (model);

  return 0;
}
