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
#include <stdlib.h>

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

static unsigned depth, width;
static unsigned M, I, L, O, A;
static unsigned * unused;
static Layer * layer;

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

void
aigfuzz_layers (aiger * model, aigfuzz_opts * opts)
{
  unsigned j, k, lit, start, end, pos, out, lhs, rhs0, rhs1;
  char comment[80];
  Layer * l, * m;
  AIG * a;

  depth = aigfuzz_pick (opts->large ? 50 : 2, opts->small ? 10 : 200);
  aigfuzz_msg (1, "depth %u", depth);
  layer = calloc (depth, sizeof *layer);

  width = aigfuzz_pick (opts->large ? 50 : 10, opts->small ? 20 : 200);
  aigfuzz_msg (1, "width %u", width);

  for (l = layer; l < layer + depth; l++)
    {
      assert (10 <= width);
      l->M = aigfuzz_pick (10, 10 + width - 1);
      if (!I) l->I = l->M;
      else l->I = aigfuzz_pick (0, l->M/10);
      if (!opts->combinational) 
	{
	  l->L = aigfuzz_pick (0, l->I);
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
		while (m > layer && aigfuzz_oneoutof (2))
		  m--;

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
      for (j = start; j < end; j++)
	aiger_add_latch (model, l->aigs[j].lit, l->aigs[j].next, 0);
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
    {
      qsort (l->unused, l->O, sizeof *l->unused, cmpu);
      O += l->O;
    }

  if (opts->merge)
    {
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

  for (l = layer; l < layer + depth; l++)
    {
      free (l->aigs);
      free (l->unused);
    }
  free (layer);
  free (unused);

  sprintf (comment, "depth %u", depth);
  aiger_add_comment (model, comment);
  sprintf (comment, "width %u", width);
  aiger_add_comment (model, comment);

}

