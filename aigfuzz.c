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

#include "aigfuzz.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/times.h>
#include <limits.h>

static aiger * model;
static unsigned * outputs, O, R, A;
static aigfuzz_opts opts;
static int verbosity;
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

void
aigfuzz_msg (int level, const char *fmt, ...)
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

unsigned
aigfuzz_pick (unsigned from, unsigned to)
{
  unsigned res = rng, prev = rng;
  assert (from <= to);
  rng *= 1664525u;
  rng += 1013904223u;
  if (to < (1<<10)) res >>= 10;
  res %= to - from + 1;
  res += from;
  aigfuzz_msg (3,
               "aigfuzz_pick %u from %u to %u rng %u",
               res, from, to, prev);
  return res;
}

int
aigfuzz_oneoutof (unsigned to)
{
  assert (to > 0);
  return aigfuzz_pick (13, 12 + to) == 13;
}

void
aigfuzz_opt (const char * fmt, ...)
{
  char comment[80];
  va_list ap;
  va_start (ap, fmt);
  vsprintf (comment, fmt, ap);
  va_end (ap);
  aiger_add_comment (model, comment);
  aigfuzz_msg (1, "%s", comment);
}

static unsigned
pick_output (int flip, int remove)
{
  unsigned pos, res;
  assert (O > 0);
  pos = aigfuzz_pick (0, O - 1);
  res = outputs[pos];
  if (remove && pos < --O)
    outputs[pos] = outputs[O];
  if (flip && aigfuzz_oneoutof (2))
    res ^= 1;
  return res;
}

static void
basiclosure (int flip)
{
  unsigned lhs, rhs0, rhs1;
  assert (O > 0);
  while (O > R)
    {
      lhs = 2 * (model->maxvar + 1);
      rhs0 = pick_output (flip, 1);
      rhs1 = pick_output (flip, 1);
      aiger_add_and (model, lhs, rhs0, rhs1);
      if (flip && aigfuzz_oneoutof (2)) lhs ^= 1;
      outputs[O++] = lhs;
    }
}

static void
andclosure (void)
{
  aigfuzz_opt ("and closure");
  basiclosure (0);
}

static void
orclosure (void)
{
  unsigned i;
  aigfuzz_opt ("or closure");
  for (i = 0; i < O; i++) outputs[i] ^= 1;
  basiclosure (0);
  for (i = 0; i < O; i++) outputs[i] ^= 1;
}

static void
mergeclosure (void)
{
  aigfuzz_opt ("merge closure");
  basiclosure (1);
}

static void
cnfclosure (void)
{
  unsigned ratio, *clauses, C, i, j, lhs, lit;
  int ternary_only;
  aigfuzz_opt ("cnf closure");
  ratio = aigfuzz_pick (400, 450);
  aigfuzz_opt ("clause variable ratio %.2f", ratio / (double) 100.0);
  C = (ratio * O) / 100;
  aigfuzz_opt ("clauses %u", C);
  clauses = calloc (C, sizeof *clauses);
  ternary_only = aigfuzz_oneoutof (2);
  aigfuzz_opt ("ternary only %d", ternary_only);
  for (i = 0; i < C; i++)
    {
      clauses[i] = pick_output (1, 0);
      for (j = 0; j < 2 || (!ternary_only && aigfuzz_oneoutof (3)); j++)
	{
	  lhs = 2 * (model->maxvar + 1);
	  lit = pick_output (1, 0);
	  aiger_add_and (model, lhs, clauses[i], lit);
	  clauses[i] = lhs;
	}
      clauses[i] ^= 1;
    }
  free (outputs);
  outputs = clauses;
  O = C;
  aigfuzz_msg (2, "clausal gates %u", model->num_ands - A);
  basiclosure (0);
}

static int
isposnum (const char * str)
{
  const char * p;

  if (str[0] == '0' && !str[1])
    return 1;

  if (str[0] == '0')
    return 0;

  for (p = str; *p; p++)
    if (!isdigit (*p))
      return 0;

  return 1;
}

#define USAGE \
"usage: aigfuzz [-h][-v][-c][-m][-s][-l][-o dst][seed]\n" \
"\n" \
"An AIG fuzzer to generate random AIGs.\n" \
"\n" \
"  -h    print this command line option summary\n" \
"  -v    verbose output on 'stderr'\n" \
"  -c    combinational logic only, e.g. no latches\n" \
"  -m    conjoin all outputs into one\n" \
"  -s    only small circuits\n" \
"  -l    only large circuits\n" \
"\n" \
"  dst   output file with 'stdout' as default\n" \
"\n" \
"  seed  force deterministic random number generation\n"

int
main (int argc, char ** argv)
{
  int i, seed = -1, ok;
  const char *dst = 0;
  unsigned closure;
  char comment[80];
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
	opts.combinational = 1;
      else if (!strcmp (argv[i], "-m"))
	opts.merge = 1;
      else if (!strcmp (argv[i], "-s"))
	opts.small = 1;
      else if (!strcmp (argv[i], "-l"))
	opts.large = 1;
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

  if (opts.small && opts.large)
    die ("can not combined '-s' and '-l'");

  if (seed < 0)
    seed = abs ((times(0) * getpid()) >> 1);

  rng = seed;
  aigfuzz_msg (1, "seed %u", rng);
  model = aiger_init ();

  sprintf (comment, "aigfuzz%s%s%s%s %d", 
           opts.combinational ? " -c" : "",
           opts.merge ? " -m" : "",
	   opts.small ? " -s" : "",
	   opts.large ? " -l" : "",
	   seed);
  aiger_add_comment (model, comment);

  sprintf (comment, "seed %d", seed);
  aiger_add_comment (model, comment);

  outputs = aigfuzz_layers (model, &opts);
  for (O = 0; outputs[O] != UINT_MAX; O++)
    ;
  aigfuzz_msg (2, "got %u outputs from basic fuzzer", O);

  if (O)
    {
      A = model->num_ands;
      if (opts.merge) R = 1;
      else R = aigfuzz_pick (1, O);
      aigfuzz_msg (2, "reducing outputs from %u to %u", O, R);
      closure = aigfuzz_pick (0, 99);
      if (closure < 10) andclosure ();
      else if (closure < 20) orclosure ();
      else if (closure < 50) mergeclosure ();
      else cnfclosure ();
      aigfuzz_msg (2, "closing gates %u", model->num_ands - A);
    }

  while (O > 0)
    aiger_add_output (model, outputs[--O], 0);

  free (outputs);

  aiger_reencode (model);

  aigfuzz_msg (1, "MILOA %u %u %u %u %u", 
      model->maxvar,
      model->num_inputs,
      model->num_latches,
      model->num_outputs,
      model->num_ands);

  aigfuzz_msg (1, "writing %s", dst ? dst : "<stdout>");
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
