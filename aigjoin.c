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
  AIG * repr, * parent, * next, * child[2], * link[2];
};

static aiger ** srcs, * dst;
static char ** names;
static int verbose;

static AIG ** table, *** aigs;
static unsigned size, count;

static AIG ** stack, ** top, ** end;

static int merged;

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
  res->idx = count++;
  res->repr = res->parent = res->next = 0;
  connect (res, c0, 0);
  connect (res, c1, 1);
  return res;
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

static AIG *
chase (AIG * a)
{
  AIG * res, * stripped, * repr;
  res = a;
  stripped = strip (res);
  repr = stripped->repr;
  while (repr)
    {
      res = sign (res) ? not (repr) : repr;
      stripped = strip (res);
      repr = stripped->repr;
    }
  return res;
}

static void
shrink (AIG * a, AIG * repr)
{
  AIG * p = a, * next;
  while (p != repr)
    {
      if (sign (p))
	{
	  p = not (p);
	  repr = not (repr);
	}

      assert (p);
      next = p->repr;
      p->repr = repr;
      p = next;
    }
}

static AIG *
deref (AIG * a)
{
  AIG * r = chase (a);
  shrink (a, r);
  return r;
}

static AIG *
insert (Tag tag, AIG * c0, AIG * c1)
{
  AIG ** p;
  if (count >= size) enlarge ();
  if (c0) c0 = deref (c0);
  if (c1) c1 = deref (c1);
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

static void
merge (AIG * a, AIG * b)
{
  AIG * c = deref (a), * d = deref (b), * tmp;
  if (c == d) return;
  assert (c != not (d));
  if (strip (c)->idx > strip (d)->idx ||
      (strip (c)->tag != LATCH && strip (d)->tag == LATCH))
    { tmp = c; c = d; d = tmp; }
  if (sign (d)) { c = not (c); d = not (d); }
  d->repr = c;
  merged++;
  push (d);
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

int
main (int argc, char ** argv)
{
  unsigned inputs = UINT_MAX, j, k, models, lit;
  const char * output = 0, * err;
  AIG * a, * n, * r0, * r1, * l;
  int i, force = 0, ok;
  aiger ** q, * src;
  aiger_mode mode;
  aiger_and * b;
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

  models = p - names;
  if (!models)
    die ("no input model specified");

  msg (1, "specified %d models for merging", p - names);
  assert (p < names + argc);
  *p = 0;

  q = srcs = calloc (models, sizeof *srcs);
  for (p = names; *p; p++)
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

  free (names);

  assert (inputs < UINT_MAX);

  msg (2, "reencoding models");
  for (j = 0; j < models; j++)
    aiger_reencode (srcs[j]);

  msg (2, "building aigs");
  aigs = calloc (models, sizeof *aigs);
  for (j = 0; j < models; j++)
    aigs[j] = calloc (2 * (srcs[j]->maxvar + 1), sizeof *aigs[j]);

  dst = aiger_init ();

  for (j = 0; j < models; j++)
    {
      src = srcs [j];

      for (k = 0; k < src->num_inputs; k++)
	{
	  a = input (k);
	  lit = 2 * (k + 1);
	  assert (lit == src->inputs[k].lit);
	  aigs[j][lit] = a,
	  aigs[j][lit + 1] = not (a);
	}

      for (k = 0; k < src->num_latches; k++)
	{
	  a = input (inputs + k);
	  lit = src->latches[k].lit;
	  aigs[j][lit] = a;
	  aigs[j][lit + 1] = not (a);
	}

      lit = 0;
      a = constant ();
      aigs[j][lit] = a;
      aigs[j][lit + 1] = not (a);

      for (k = 0; k < src->num_ands; k++)
	{
	  b = src->ands + k;
	  lit = b->lhs;
	  r0 = aigs[j][b->rhs0];
	  r1 = aigs[j][b->rhs1];
	  a = and (r0, r1);
	  aigs[j][lit] = a;
	  aigs[j][lit + 1] = not (a);
	}

      for (k = 0; k < src->num_latches; k++)
	{
	  lit = src->latches[k].lit;
	  a = aigs[j][lit];
	  assert (a == input (inputs + k));
	  n = aigs[j][src->latches[k].next];
	  l = latch (n);
	  merge (a, l);
	}

      inputs += src->num_latches;
    }

  msg (2, "merged %d aigs", merged);

  msg (2, "cleaning up models");
  for (j = 0; j < models; j++)
    aiger_reset (srcs[j]), free (aigs[j]);
  free (srcs), free (aigs);

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
