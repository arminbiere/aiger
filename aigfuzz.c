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

typedef struct AIG AIG;
typedef struct Layer Layer;

struct AIG
{
  unsigned lit;
  unsigned child[2];
};

struct Layer
{
  unsigned M, I, L, O, A;
  unsigned * free;
  AIG * aigs;
};

static int combinational, verbosity;
static unsigned rng, depth, width;
static unsigned M, I, L, O, A;
static aiger * model;
static Layer * layer;

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

static int
cmpu (const void * p, const void * q)
{
  unsigned u = *(unsigned*)p;
  unsigned v = *(unsigned*)q;
  if (u < v)
    return -1;
  if (u > v)
    return 1;
  return 0;
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
  int i, seed = -1, ok, merge = 0, small = 0, large = 0;
  unsigned j, k, lit, start, end, pos, out, lhs;
  const char *dst = 0;
  aiger_mode mode;
  char comment[80];
  Layer * l, * m;
  AIG * a;

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
      else if (!strcmp (argv[i], "-m"))
	merge = 1;
      else if (!strcmp (argv[i], "-s"))
	small = 1;
      else if (!strcmp (argv[i], "-l"))
	large = 1;
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

  if (small && large)
    die ("can not combined '-s' and '-l'");

  if (seed < 0)
    seed = abs ((times(0) * getpid()) >> 1);

  rng = seed;

  msg (1, "seed %u", rng);
  depth = pick (large ? 50 : 2, small ? 10 : 200);
  msg (1, "depth %u", depth);
  layer = calloc (depth, sizeof *layer);

  width = pick (large ? 100 : 10, small ? 20 : 1000);
  msg (1, "width %u", width);

  for (l = layer; l < layer + depth; l++)
    {
      assert (10 <= width);
      l->M = pick (10, 10 + width - 1);
      l->I = I ? pick (0, l->M/10) : l->M;
      l->L = 0;
      l->A = l->M - l->I;
      M += l->M;
      I += l->I;
      L += l->L;
      A += l->A;
      l->aigs = calloc (l->M, sizeof *l->aigs);
      l->free = calloc (l->M, sizeof *l->free);
      l->O = l->M;
    }

  assert (M = I + L + A);

  lit = 0;
  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->I; j++)
      l->aigs[j].lit = (lit += 2);

  assert (lit/2 == I);

  for (l = layer; l < layer + depth; l++)
    {
      start = l->I;
      end = start + l->L;
      for (j = start; j < end; j++)
	l->aigs[j].lit = (lit += 2);
    }

  assert (lit/2 == I + L);

  for (l = layer; l < layer + depth; l++)
    {
      start = l->I + l->L;
      end = start + l->A;
      assert (end == l->M);
      for (j = start; j < end; j++)
	l->aigs[j].lit = (lit += 2);
    }

  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->M; j++)
      l->free[j] = l->aigs[j].lit;

  assert (lit/2 == M);

  for (l = layer; l < layer + depth; l++)
    {
      start = l->I + l->L;
      end = start + l->A;
      assert (end == l->M);
      for (j = start; j < end; j++)
	{
	  a = l->aigs + j;
	  for (k = 0; k <= 1; k++)
	    {
	      m = l - 1;
	      if (k)
		while (m > layer && pick (11, 12) == 11)
		  m--;

	      if (m->O > 0)
		{
		  pos = pick (0, m->O - 1);
		  lit = m->free[pos];
		  m->free[pos] = m->free[--m->O];
		}
	      else
		{
		  pos = pick (0, m->M - 1);
		  lit = m->aigs[pos].lit;
		}

	      if (pick (17,18) == 17)
		lit++;

	      if (k && a->child[0]/2 == lit/2)
		k--;
	      else
		a->child[k] = lit;
	    }

	  lit = a->child[1];
	  if (a->child[0] < lit)
	    {
	      a->child[1] = a->child[0];
	      a->child[0] = lit;
	    }

	  assert (a->lit > a->child[0]);
	  assert (a->child[0] > a->child[1]);
	}
    }

  for (l = layer; l < layer + depth; l++)
    {
      qsort (l->free, l->O, sizeof *l->free, cmpu);
      O += l->O;
    }

  for (l = layer + depth - 1; l>= layer; l--)
    msg (2,
         "layer[%u] M I L O A %u %u %u %u %u",
         l-layer, l->M, l->I, l->L, l->O, l->A);

  model = aiger_init ();

  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->I; j++)
      aiger_add_input (model, l->aigs[j].lit, 0);

  for (l = layer; l < layer + depth; l++)
    {
      start = l->I + l->L;
      end = start + l->A;
      assert (end == l->M);
      for (j = start; j < end; j++)
	{
	  a = l->aigs + j;
	  aiger_add_and (model, a->lit, a->child[0], a->child[1]);
	}
    }

  out = 0;
  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->O; j++)
      {
	lit = l->free[j];
	if (pick (3, 4) == 3)
	  lit++;

	if (merge)
	  {
	    if (out)
	      {
		lhs = 2 * ++M;
		aiger_add_and (model, lhs, out, lit);
		out = lhs;
		A++;
	      }
	    else
	      out = lit;
	  }
	else
	  aiger_add_output (model, lit, 0);
      }

  if (merge && out)
    aiger_add_output (model, out, 0);

  for (l = layer; l < layer + depth; l++)
    {
      free (l->aigs);
      free (l->free);
    }
  free (layer);

  sprintf (comment, "aigfuzz%s%s%s%s %d", 
           combinational ? " -c" : "",
           merge ? " -m" : "",
	   small ? " -s" : "",
	   large ? " -l" : "",
	   seed);
  aiger_add_comment (model, comment);

  sprintf (comment, "seed %d", seed);
  aiger_add_comment (model, comment);
  sprintf (comment, "depth %u", depth);
  aiger_add_comment (model, comment);
  sprintf (comment, "width %u", width);
  aiger_add_comment (model, comment);

  aiger_reencode (model);

  msg (1, "M I L O A %u %u %u %u %u", 
      model->maxvar,
      model->num_inputs,
      model->num_latches,
      model->num_outputs,
      model->num_ands);

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
