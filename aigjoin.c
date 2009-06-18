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
#include <unistd.h>

typedef struct AIG AIG;

#define USAGE \
"usage: aigjoin [-h][-v][-f][-o <output>][<input> ...]\n" \
"\n" \
"Join AIGER models.\n"

enum Tag
{
  LATCH = -1,
  AND = -2,
  CONST = INT_MIN,
};

typedef enum Tag Tag;

struct AIG
{
  Tag tag;
  unsigned relevant:1, pushed:1, idx, lit;
  AIG * repr, * rper, * parent, * next, * child[2], * link[2];
};

static aiger ** srcs, * dst;
static char ** srcnames;
static int verbose;

static AIG ** table, ** aigs, *** srcaigs;
static unsigned size, count;

static AIG ** stack, ** top, ** end;

static int merged, relevant;

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

static unsigned
sign (AIG * a)
{
  return 1 & (long) a;
}

static AIG *
not (AIG * a)
{
  return (AIG *)(1l ^ (long)a);
}

static AIG *
strip (AIG * a)
{
  return sign (a) ? not (a) : a;
}

static void
connect (AIG * parent, AIG * child, int pos)
{
  AIG * stripped = strip (child);
  assert (!sign (parent));
  parent->child[pos] = child;
  if (!child) return;
  parent->link[pos] = stripped->parent;
  stripped->parent = parent;
}

static AIG *
new (Tag tag, AIG * c0, AIG * c1)
{
  AIG * res = malloc (sizeof *res);
  res->tag = tag;
  res->pushed = 0;
  res->relevant = 0;
  res->idx = count;
  res->repr = res->rper = res->parent = res->next = 0;
  connect (res, c0, 0);
  connect (res, c1, 1);
  return aigs[count++] = res;
}

static unsigned
idx (AIG * a)
{
  unsigned res;
  if (!a) return 0;
  res = 2 * strip (a)->idx;
  res += sign (a);
  return res;
}

static unsigned
hash (Tag tag, AIG * c0, AIG * c1)
{
  unsigned res = tag;
  res *= 864613u;
  res += 124217221u * idx (c0) + 2342879719u * idx (c1);
  return res;
}

static void
enlarge (void)
{
  AIG ** old_table = table, * p, * n;
  unsigned h, i, old_size = size;
  size = old_size ? 2 * old_size : 1;
  table = calloc (size, sizeof *table);
  for (i = 0; i < old_size; i++)
    for (p = old_table[i]; p; p = n)
      {
	n = p->next;
	h = hash (p->tag, p->child[0], p->child[1]);
	h &= size - 1;
	p->next = table[h];
	table[h] = p;
      }
  free (old_table);
  aigs = realloc (aigs, size * sizeof *aigs);
}

static AIG **
find (Tag tag, AIG * c0, AIG * c1)
{
  AIG ** p, * a;
  unsigned h;
  h = hash (tag, c0, c1);
  h &= size - 1;
  for (p = table + h; (a = *p); p = &a->next)
    if (a->tag == tag && a->child[0] == c0 && a->child[1] == c1)
      return p;
  return p;
}

static void
push (AIG * a)
{
  unsigned n, o;
  a = strip (a);
  if (a->pushed) return;
  if (top == end)
    {
      o = top - stack;
      n = o ? 2 * o : 1;
      stack = realloc (stack, n * sizeof *stack);
      top = stack + o;
      end = stack + n;
    }
  a->pushed = 1;
  *top++ = a;
}

static AIG *
pop (void)
{
  AIG * res;
  assert (stack < top);
  res = *--top;
  assert (res->pushed);
  res->pushed = 1;
  return res;
}

static AIG *
derepr (AIG * a)
{
  AIG * r = strip (a)->repr;
  if (!r) r = a;
  else if (sign (a)) r = not (r);
  assert (!strip (r)->repr);
  return r;
}

static unsigned
delit (AIG * a)
{
  unsigned res = strip (a)->lit;
  if (sign (a)) res++;
  return res;
}

static AIG *
insert (Tag tag, AIG * c0, AIG * c1)
{
  AIG ** p;
  if (count >= size) enlarge ();
  p = find (tag, c0, c1);
  if (*p) return *p;
  return *p = new (tag, c0, c1);
}

static AIG *
constant (void)
{
  return insert (CONST, 0, 0);
}

static AIG *
input (int i)
{
  assert (i >= 0);
  return insert (i, 0, 0);
}

static AIG *
and (AIG * a, AIG * b)
{
  return insert (AND, a, b);
}

static AIG *
latch (AIG * next)
{
  return insert (LATCH, next, 0);
}

static void
assign (AIG * b, AIG * a)
{
  AIG * c = derepr (a), * d = derepr (b), * p;
  if (c == d) return;
  assert (c != not (d));
  if (sign (d)) { c = not (c); d = not (d); }
  for (p = strip (c); p->rper; p = p->rper)
    ;
  p->rper = d;
  d->repr = c;
  merged++;
  push (d);
  for (p = d->rper; p; p = p->rper)
    {
      if (p->repr == d)
	p->repr = c;
      else
	{
	  assert (p->repr == not (d));
	  p->repr = not (c);
	}
      push (p);
      merged++;
    }
}

static void
msg (int level, const char *fmt, ...)
{
  va_list ap;
  if (verbose < level) return;
  fputs ("[aigjoin] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
join (void)
{
  AIG * a, * b, * p, * s, * c, * n;
  int pos;
  while (top > stack)
    {
      a = pop ();
      assert (!sign (a));
      for (p = a->parent; p; p = n)
	{
	  s = strip (p);
	  pos = (strip (s->child[1]) == strip (a));
	  assert (strip (s->child[pos]) == strip (a));
	  n = s->link[pos];
	  c = derepr (s->child[0]);
	  if (s->tag == AND)
	    b = and (c, derepr (s->child[1]));
	  else 
	    {
	      assert (p->tag == LATCH);
	      b = latch (c);
	    }

	  b = derepr (b);
	  assign (s, b);
	}
    }
}

static void
coi (AIG * r)
{
  AIG * a;
  assert (top == stack);
  push (strip (r));
  while (top > stack)
    {
      a = strip (pop ());
      assert (!a->repr);
      if (a->relevant)
	continue;
      a->relevant = 1;
      relevant++;
      if (a->tag == AND)
	{
	  push (a->child[0]);
	  push (a->child[1]);
	}
      else if (a->tag == LATCH)
	{
	  push (a->next);
	}
      else
	assert (a->tag == CONST);
    }
}

int
main (int argc, char ** argv)
{
  unsigned inputs = UINT_MAX, j, k, models, lit, latches;
  const char * output = 0, * err;
  AIG * a, * n, * r0, * r1, * l;
  int i, force = 0, ok;
  aiger ** q, * src;
  aiger_mode mode;
  aiger_and * b;
  char ** p;

  p = srcnames = calloc (argc, sizeof *srcnames);
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
	  assert (p < srcnames + argc);
	  *p++ = argv[i];
	}
    }

  models = p - srcnames;
  if (!models)
    die ("no input model specified");

  msg (1, "specified %d models for merging", p - srcnames);
  assert (p < srcnames + argc);
  *p = 0;

  q = srcs = calloc (models, sizeof *srcs);
  for (p = srcnames; *p; p++)
    {
      msg (1, "reading %s", *p);
      src = *q++ = aiger_init ();
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
		{
		  msg (1, "%s: expected %u inputs but got %u", 
		       *p, inputs, src->num_inputs);
		  if (inputs < src->num_inputs)
		    inputs = src->num_inputs;
		}
	      else
		die ("%s: expected %u inputs but got %u", 
		     *p, inputs, src->num_inputs);
	    }
	}
      else
	inputs = src->num_inputs;
    }

  free (srcnames);

  assert (inputs < UINT_MAX);

  msg (2, "reencoding models");
  for (j = 0; j < models; j++)
    aiger_reencode (srcs[j]);

  msg (2, "building aigs");
  srcaigs = calloc (models, sizeof *srcaigs);
  for (j = 0; j < models; j++)
    srcaigs[j] = calloc (2 * (srcs[j]->maxvar + 1), sizeof *srcaigs[j]);

  latches = inputs;
  for (j = 0; j < models; j++)
    {
      src = srcs [j];

      for (k = 0; k < src->num_inputs; k++)
	{
	  a = input (k);
	  lit = 2 * (k + 1);
	  assert (lit == src->inputs[k].lit);
	  srcaigs[j][lit] = a,
	  srcaigs[j][lit + 1] = not (a);
	}

      for (k = 0; k < src->num_latches; k++)
	{
	  a = input (latches + k);
	  lit = src->latches[k].lit;
	  srcaigs[j][lit] = a;
	  srcaigs[j][lit + 1] = not (a);
	}

      lit = 0;
      a = constant ();
      srcaigs[j][lit] = a;
      srcaigs[j][lit + 1] = not (a);

      for (k = 0; k < src->num_ands; k++)
	{
	  b = src->ands + k;
	  lit = b->lhs;
	  r0 = srcaigs[j][b->rhs0];
	  r1 = srcaigs[j][b->rhs1];
	  a = and (r0, r1);
	  srcaigs[j][lit] = a;
	  srcaigs[j][lit + 1] = not (a);
	}

      for (k = 0; k < src->num_latches; k++)
	{
	  lit = src->latches[k].lit;
	  a = srcaigs[j][lit];
	  assert (a == input (latches + k));
	  n = srcaigs[j][src->latches[k].next];
	  l = latch (n);
	  assign (a, l);
	}

      latches += src->num_latches;
    }

  msg (2, "starting merge phase with %d AIGs to be joined", top - stack);

  join ();

  msg (2, "merged %d aigs", merged);

  for (j = 0; j < inputs; j++)
    {
      a = input (j);
      a->relevant = 1;
      relevant++;
    }

  for (j = 0; j < models; j++)
    {
      src = srcs [j];
      for (k = 0; k < src->num_outputs; k++)
	{
	  lit = src->outputs[k].lit;
	  a = derepr (srcaigs[j][lit]);
	  coi (a);
	}
    }
  msg (2, "found %d relevant AIGs in COI", relevant);

  dst = aiger_init ();

  lit = 0;
  for (j = 0; j < inputs; j++)
    {
      a = input (j);
      assert (a->relevant);
      assert (!a->repr);
      lit += 2;
      a->lit = lit;
      aiger_add_input (dst, lit, 0);
    }
  assert (2 * inputs == lit);
  msg (2, "joined model has %u inputs", inputs);

  for (j = 0; j < count; j++)
    {
      a = aigs[j];
      if (!a->relevant) continue;
      assert (!a->repr);
      if (a->tag != LATCH) continue;
      lit += 2;
      a->lit = lit;
    }
  assert (2 * latches >= lit);
  msg (2, "joined model has %u latches", lit/2 - inputs);

  for (j = 0; j < count; j++)
    {
      a = aigs[j];
      if (!a->relevant) continue;
      assert (!a->repr);
      if (a->tag != AND) continue;
      lit += 2;
      a->lit = lit;
      assert (lit > delit (a->child[0]));
      assert (lit > delit (a->child[1]));
      aiger_add_and (dst, lit, delit (a->child[0]), delit (a->child[1]));
    }

  msg (1,
       "target MILOA %u %u %u %u %u", 
       dst->maxvar,
       dst->num_inputs,
       dst->num_latches,
       dst->num_outputs,
       dst->num_ands);

  msg (2, "cleaning up models");
  for (j = 0; j < models; j++)
    aiger_reset (srcs[j]), free (srcaigs[j]);
  free (srcs), free (aigs), free (srcaigs);

  msg (2, "cleaning up %u aigs", count);
  for (j = 0; j < size; j++)
    for (a = table[j]; a; a = n)
      {
	n = a->next;
	free (a);
      }
  free (table);

  msg (1, "writing %s", output ? output : "<stdout>");

  if (output)
    ok = aiger_open_and_write_to_file (dst, output);
  else
    {
      mode = isatty (1) ? aiger_ascii_mode : aiger_binary_mode;
      ok = aiger_write_to_file (dst, mode, stdout);
    }

  aiger_reset (dst);

  if (!ok)
    die ("writing failed");

  free (stack);

  return 0;
}
