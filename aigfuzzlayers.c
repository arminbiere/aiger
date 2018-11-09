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
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

typedef struct AIG AIG;
typedef struct Layer Layer;

struct AIG
{
  unsigned lit;
  union {
    unsigned child[2];
    unsigned next;
  };
};

struct Layer
{
  unsigned M, I, L, O, A;
  unsigned * unused;
  AIG * aigs;
};

static int monotonicity;
static unsigned depth, width;
static unsigned input_fraction, latch_fraction, lower_fraction;
static unsigned M, I, L, O, A;
static Layer * layer;

static unsigned
fraction (unsigned f, unsigned of)
{
  unsigned res;
  assert (0 <= f && f <= 100);
  res = (f * of) / 100;
  assert (res <= of);
  return res;
}

unsigned *
aigfuzz_layers (aiger * model, aigfuzz_opts * opts)
{
  unsigned j, k, lit, start, end, pos, * res;
  Layer * l, * m;
  AIG * a;

  aigfuzz_opt ("fuzzer layers");

  depth = aigfuzz_pick (opts->large ? 50 : 2, opts->small ? 10 : 200);
  aigfuzz_opt ("depth %u", depth);

  layer = calloc (depth, sizeof *layer);

  width = aigfuzz_pick (opts->large ? 50 : 10, opts->small ? 20 : 200);
  aigfuzz_opt ("width %u", width);

  input_fraction = aigfuzz_pick (0, 20);
  aigfuzz_opt ("input fraction %u%%", input_fraction);

  latch_fraction = opts->combinational ? 0 : aigfuzz_pick (0, 100);
  aigfuzz_opt ("latch fraction %u%%", latch_fraction);

  lower_fraction = 10 * aigfuzz_pick (0, 5);
  aigfuzz_opt ("lower fraction %u%%", lower_fraction);

  monotonicity = aigfuzz_pick (0, 2) - 1;
  aigfuzz_opt ("monotonicity %d", monotonicity);

  for (l = layer; l < layer + depth; l++)
    {
      assert (10 <= width);
      if (monotonicity < 0 && l == layer + 1)
	l->M = aigfuzz_pick (layer[0].M, 2 * layer[0].M);
      else
	{
	  l->M = aigfuzz_pick (10, 10 + width - 1);
	  if (monotonicity > 0 && l > layer && l->M < l[-1].M) l->M = l[-1].M;
	  else if (monotonicity < 0 && l > layer + 1 &&
		   l->M > l[-1].M) l->M = l[-1].M;
	}
      if (!I) l->I = l->M;
      else if (input_fraction) 
	l->I = aigfuzz_pick (0, fraction (input_fraction, l->M));
      if (latch_fraction)
	{
	  l->L = aigfuzz_pick (0, fraction (latch_fraction, l->I));
	  l->I -= l->L;
	}
      l->A = l->M;
      l->A -= l->I;
      l->A -= l->L;
      M += l->M;
      I += l->I;
      L += l->L;
      A += l->A;
      l->aigs = calloc (l->M, sizeof *l->aigs);
      l->unused = calloc (l->M, sizeof *l->unused);
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
      l->unused[j] = l->aigs[j].lit;

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
		{
		  while (m > layer && aigfuzz_pick (1, 100) <= lower_fraction)
		    m--;
		}

	      if (m->O > 0)
		{
		  pos = aigfuzz_pick (0, m->O - 1);
		  lit = m->unused[pos];
		  m->unused[pos] = m->unused[--m->O];
		}
	      else
		{
		  pos = aigfuzz_pick (0, m->M - 1);
		  lit = m->aigs[pos].lit;
		}

	      if (aigfuzz_oneoutof (2))
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

  for (l = layer + depth - 1; l>= layer; l--)
    {
      start = l->I;
      end = start + l->L;
      for (j = start; j < end; j++)
	{
	  a = l->aigs + j;
	  m = l + 1;
	  if (m >= layer + depth)
	    m -= depth;
	  while (aigfuzz_oneoutof (2))
	    {
	      m++;
	      if (m >= layer + depth)
		m -= depth;
	    }

	  if (m->O > 0)
	    {
	      pos = aigfuzz_pick (0, m->O - 1);
	      lit = m->unused[pos];
	      m->unused[pos] = m->unused[--m->O];
	    }
	  else
	    {
	      pos = aigfuzz_pick (0, m->M - 1);
	      lit = m->aigs[pos].lit;
	    }

	  if (aigfuzz_oneoutof (2))
	    lit++;

	  a->next = lit;
	}
    }

  for (l = layer + depth - 1; l>= layer; l--)
    aigfuzz_msg (2,
         "layer[%u] MILOA %u %u %u %u %u",
         l-layer, l->M, l->I, l->L, l->O, l->A);

  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->I; j++)
      aiger_add_input (model, l->aigs[j].lit, 0);

  for (l = layer; l < layer + depth; l++)
    {
      start = l->I;
      end = start + l->L;
      for (j = start; j < end; j++) {
	aiger_add_latch (model, l->aigs[j].lit, l->aigs[j].next, 0);
	if (opts->version < 2 || opts->zero) continue;
	if (aigfuzz_pick (0, 3)) continue;
	aiger_add_reset (model, l->aigs[j].lit, 
	                 aigfuzz_pick (0, 1) ? l->aigs[j].lit : 1);
      }
    }

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

  for (l = layer; l < layer + depth; l++)
    O += l->O;

  res = calloc (O + 1, sizeof *res);
  O = 0;
  for (l = layer; l < layer + depth; l++)
    for (j = 0; j < l->O; j++)
      {
	lit = l->unused[j];
	if (aigfuzz_oneoutof (2))
	  lit ^= 1;
	res[O++] = lit;
      }
  res[O] = UINT_MAX;

#if 0
  if (opts->merge)
    {
      int * unused, lhs, rhs0, rhs1, out;
      aigfuzz_msg (1, "merging %u unused outputs", O);

      unused = calloc (O, sizeof *unused);
      O = 0;
      for (l = layer; l < layer + depth; l++)
	for (j = 0; j < l->O; j++)
	  unused[O++] = l->unused[j];

      while (O > 1)
	{
	  pos = aigfuzz_pick (0, O - 1);
	  rhs0 = unused[pos];
	  unused[pos] = unused[--O];
	  if (aigfuzz_pick (7, 8) == 7)
	    rhs0++;
	  assert (O > 0);
	  pos = aigfuzz_pick (0, O - 1);
	  rhs1 = unused[pos];
	  if (aigfuzz_pick (11, 12) == 11)
	    rhs1++;
	  lhs = 2 * ++M;
	  aiger_add_and (model, lhs, rhs0, rhs1);
	  unused[pos] = lhs;
	}

      if (O == 1)
	{
	  out = unused[0];
	  if (aigfuzz_pick (3, 4) == 3)
	    out++;
	  aiger_add_output (model, out, 0);
	}
    }
  else
    {
      for (l = layer; l < layer + depth; l++)
	for (j = 0; j < l->O; j++)
	  {
	    lit = l->unused[j];
	    if (aigfuzz_pick (17, 18) == 17)
	      lit++;

	    aiger_add_output (model, lit, 0);
	  }
    }
#endif

  for (l = layer; l < layer + depth; l++)
    {
      free (l->aigs);
      free (l->unused);
    }
  free (layer);
  return res;
}
