/***************************************************************************
Copyright (c) 2009-2018, Armin Biere, Johannes Kepler University.

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
static int verbosity, ascii;
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

static unsigned
pick_and (int flip)
{
  unsigned res = 2 * (model->maxvar + 1);
  unsigned rhs0 = pick_output (flip, 1);
  unsigned rhs1 = pick_output (flip, 1);
  aiger_add_and (model, res, rhs0, rhs1);
  if (flip && aigfuzz_oneoutof (2)) res ^= 1;
  return res;
}

static unsigned
pick_xor (void)
{
  unsigned res, x, y, l, r;
  x = pick_output (1, 1);
  y = pick_output (1, 1);
  l = 2 * (model->maxvar + 1); 
  aiger_add_and (model, l, x, 1^y);
  r = 2 * (model->maxvar + 1); 
  aiger_add_and (model, r, 1^x, y);
  res = 2 * (model->maxvar + 1); 
  aiger_add_and (model, res, 1^l, 1^r);
  res ^= 1;
  return res;
}

static void
basiclosure (int flip)
{
  unsigned a;
  assert (O > 0);
  while (O > R)
    {
      a = pick_and (flip);
      outputs[O++] = a;
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

static void
xorclosure (void)
{
  unsigned x;
  aigfuzz_opt ("xor closure");
  assert (O > 0);
  while (O > R)
    {
      x = pick_xor ();
      outputs[O++] = x;
    }
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
"  -a    force ASCII output\n" \
"  -c    combinational logic only, e.g. no latches\n" \
"  -m    conjoin all outputs into one\n" \
"  -s    only small circuits\n" \
"  -l    only large circuits\n" \
"  -S    only safety no liveness properties\n" \
"  -L    only liveness no safety properties\n" \
"  -b    only bad part of safety properties\n" \
"  -j    only justice part of liveness properties\n" \
"  -z    force all latches to be initialized by zero\n" \
"  -1    only old AIGER version 1 format\n" \
"  -2    AIGER version 1 and version 2 format\n" \
"\n" \
"  dst   output file with 'stdout' as default\n" \
"\n" \
"  seed  force deterministic random number generation\n"

int
main (int argc, char ** argv)
{
  unsigned closure, lit, lits[2], choices[5], nchoices;
  int i, seed = -1, ok;
  const char *dst = 0;
  char comment[80];
  aiger_mode mode;

  opts.version = 1;
  opts.liveness = 1;
  opts.safety = 1;

  for (i = 1; i < argc; i++) 
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbosity++;
      else if (!strcmp (argv[i], "-a"))
	ascii = 1;
      else if (!strcmp (argv[i], "-c"))
	opts.combinational = 1;
      else if (!strcmp (argv[i], "-m"))
	opts.merge = 1;
      else if (!strcmp (argv[i], "-s"))
	opts.small = 1;
      else if (!strcmp (argv[i], "-l"))
	opts.large = 1;
      else if (!strcmp (argv[i], "-S"))
	opts.liveness = 0;
      else if (!strcmp (argv[i], "-L"))
	opts.safety = 0;
      else if (!strcmp (argv[i], "-b"))
	opts.bad = 1;
      else if (!strcmp (argv[i], "-j"))
	opts.justice = 1;
      else if (!strcmp (argv[i], "-z"))
	opts.zero = 1;
      else if (!strcmp (argv[i], "-1"))
        opts.version = 1;
      else if (!strcmp (argv[i], "-2"))
        opts.version = 2;
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

  if (!opts.safety && !opts.liveness)
    die ("can not combined '-S' and '-L'");

  if (!opts.safety && opts.bad)
    die ("can not combined '-L' and '-b'");

  if (!opts.liveness && opts.justice)
    die ("can not combined '-S' and '-j'");

  if (seed < 0)
    seed = abs ((times(0) * getpid()) >> 1);

  rng = seed;
  aigfuzz_msg (1, "seed %u", rng);
  model = aiger_init ();

  sprintf (comment, "aigfuzz%s%s%s%s%s%s%s%s%s%s%s %d", 
           opts.combinational ? " -c" : "",
           opts.merge ? " -m" : "",
	   opts.small ? " -s" : "",
	   opts.large ? " -l" : "",
	   !opts.liveness ? " -S" : "",
	   !opts.safety ? " -L" : "",
	   opts.bad ? " -b" : "",
	   opts.justice ? " -j" : "",
	   opts.zero ? " -z" : "",
	   opts.version == 1 ? " -1" : "",
	   opts.version == 2 ? " -2" : "",
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
      else if (closure < 80) xorclosure ();
      else cnfclosure ();
      aigfuzz_msg (2, "closing gates %u", model->num_ands - A);
    }

  choices[nchoices = 0] = 0;
  if (opts.safety) choices[++nchoices] = 1;
  if (opts.safety && !opts.bad) choices[++nchoices] = 2;
  if (opts.liveness) choices[++nchoices] = 3;
  if (opts.liveness && !opts.justice) choices[++nchoices] = 4;

  while (O > 0) {
    lit = outputs[--O];
    if (nchoices && opts.version >= 2)
      {
	switch (choices[aigfuzz_pick (0, nchoices)])
	  {
	    case 1:
	      assert (opts.safety);
	      if (lit == 1) lit = 0;
	      aiger_add_bad (model, lit, 0);
	      break;
	    case 2:
	      assert (opts.safety && !opts.bad);
	      if (!lit) lit = 1;
	      aiger_add_constraint (model, lit, 0);
	      break;
	    case 3:
	      assert (opts.liveness);
	      if (!lit) lit = 1;
	      lits[0] = lit;
	      if (O > 0)
	        {
		  lits[1] = outputs[--O];
		  if (!lits[1]) lits[1] = 1;
		  aiger_add_justice (model, 2, lits, 0);
		}
	      else
		aiger_add_justice (model, 1, lits, 0);
	      break;
	    case 4:
	      assert (opts.liveness && !opts.justice);
	      if (!lit) lit = 1;
	      aiger_add_fairness (model, lit, 0);
	      break;
	    default:
	      aiger_add_output (model, lit, 0);
	      continue;
	  }

	if (aigfuzz_pick (0, 4)) continue;
      }
    aiger_add_output (model, lit, 0);
  }

  free (outputs);

  aiger_reencode (model);

  aigfuzz_msg (1, "MILOA %u %u %u %u %u", 
      model->maxvar,
      model->num_inputs,
      model->num_latches,
      model->num_outputs,
      model->num_ands);

  if (opts.version == 2)
    {
      aigfuzz_msg (1, "BCJF %u %u %u %u", 
	  model->num_bad,
	  model->num_constraints,
	  model->num_justice,
	  model->num_fairness);
    }

  aigfuzz_msg (1, "writing %s", dst ? dst : "<stdout>");
  if (dst)
    ok = aiger_open_and_write_to_file (model, dst);
  else
    {
      mode = (ascii || isatty (1)) ? aiger_ascii_mode : aiger_binary_mode;
      ok = aiger_write_to_file (model, mode, stdout);
    }

  if (!ok)
    die ("write error");

  aiger_reset (model);

  return 0;
}
