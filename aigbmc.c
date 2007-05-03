/***************************************************************************
Copyright (c) 2006-2007, Armin Biere, Johannes Kepler University.

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
#include "simpaig.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

typedef struct LatchOrInput LatchOrInput;

struct LatchOrInput
{
  unsigned idx;			/* AIGER variable index */
  simpaig *aig;
};

static unsigned k;
static aiger *model;
static aiger *expansion;
static int strip;
static unsigned verbose;
static simpaigmgr *mgr;
static LatchOrInput *lois;
static simpaig **aigs;
static char *buffer;
static unsigned size_buffer;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigbmc] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static simpaig *
build_rec (unsigned lit)
{
  unsigned sign = lit & 1;
  unsigned idx = lit / 2;
  simpaig *res, *l, *r;
  aiger_and *and;

  if (!(res = lois[idx].aig))
    {
      if (idx)
	{
	  if ((and = aiger_is_and (model, 2 * idx)))
	    {
	      assert (and->lhs == 2 * idx);
	      l = build_rec (and->rhs0);
	      r = build_rec (and->rhs1);
	      res = simpaig_and (mgr, l, r);
	    }
	  else
	    res = simpaig_var (mgr, lois + idx, 0);
	}
      else
	res = simpaig_false (mgr);

      lois[idx].aig = res;
    }

  if (sign)
    res = simpaig_not (res);

  return res;
}

static simpaig *
build (void)
{
  simpaig *aig, *res, *shifted, *tmp, *lhs, *rhs, *out;
  aiger_symbol *symbol;
  unsigned i, j;

  lois = malloc ((model->maxvar + 1) * sizeof lois[0]);
  for (i = 0; i <= model->maxvar; i++)
    {
      lois[i].idx = i;
      lois[i].aig = 0;
    }

  for (i = 0; i <= model->maxvar; i++)
    {
      aig = build_rec (i * 2);
      assert (aig == lois[i].aig);
    }

  for (i = 0; i <= model->maxvar; i++)
    symbol = aiger_is_latch (model, 2 * i);

  for (i = 0; i < model->num_latches; i++)
    {
      lhs = build_rec (model->latches[i].lit);
      rhs = simpaig_false (mgr);
      simpaig_assign (mgr, lhs, rhs);
      simpaig_dec (mgr, rhs);
    }

  for (i = 1; i <= k; i++)
    {
      for (j = 0; j < model->num_latches; j++)
	{
	  tmp = build_rec (model->latches[j].lit);
	  lhs = simpaig_shift (mgr, tmp, i);
	  tmp = build_rec (model->latches[j].next);
	  rhs = simpaig_shift (mgr, tmp, i - 1);
	  simpaig_assign (mgr, lhs, rhs);
	  simpaig_dec (mgr, rhs);
	  simpaig_dec (mgr, lhs);
	}
    }

  out = build_rec (model->outputs[0].lit);
  res = simpaig_false (mgr);
  for (i = 0; i <= k; i++)
    {
      shifted = simpaig_shift (mgr, out, i);
      tmp = simpaig_or (mgr, res, shifted);
      simpaig_dec (mgr, shifted);
      simpaig_dec (mgr, res);
      res = tmp;
    }

  tmp = simpaig_substitute (mgr, res);
  simpaig_dec (mgr, res);
  res = tmp;

  return res;
}

static const char *
next_symbol (unsigned idx, int slice)
{
  aiger_symbol *input_symbol;
  const char *unsliced_name;
  unsigned len, pos;

  assert (!strip);
  assert (1 <= idx);
  assert (idx <= model->maxvar);
  assert (slice >= 0);

  input_symbol = aiger_is_input (model, 2 * idx);
  assert (input_symbol);
  unsliced_name = input_symbol->name;

  len = unsliced_name ? strlen (unsliced_name) : 20;
  len += 30;

  if (size_buffer < len)
    {
      if (size_buffer)
	{
	  while (size_buffer < len)
	    size_buffer *= 2;

	  buffer = realloc (buffer, size_buffer);
	}
      else
	buffer = malloc (size_buffer = len);
    }

  pos = input_symbol - model->inputs;
  assert (pos < model->num_inputs);

  if (unsliced_name)
    sprintf (buffer, "%d %s %u", slice, unsliced_name, pos);
  else
    sprintf (buffer, "%d %u %u", slice, 2 * idx, pos);

  return buffer;
}

static void
copyaig (simpaig * aig)
{
  LatchOrInput *input;
  simpaig *c0, *c1;
  const char *name;
  unsigned idx;
  int slice;

  assert (aig);

  aig = simpaig_strip (aig);
  idx = simpaig_index (aig);
  if (!idx || aigs[idx])
    return;

  aigs[idx] = aig;
  if (simpaig_isand (aig))
    {
      c0 = simpaig_child (aig, 0);
      c1 = simpaig_child (aig, 1);
      copyaig (c0);
      copyaig (c1);
      aiger_add_and (expansion,
		     2 * idx,
		     simpaig_unsigned_index (c0),
		     simpaig_unsigned_index (c1));
    }
  else
    {
      name = 0;
      if (!strip)
	{
	  input = simpaig_isvar (aig);
	  assert (input);
	  slice = simpaig_slice (aig);

	  name = next_symbol (input->idx, slice);
	}

      aiger_add_input (expansion, 2 * idx, name);
    }
}

static void
expand (simpaig * aig)
{
  unsigned maxvar;
  simpaig_assign_indices (mgr, aig);
  maxvar = simpaig_max_index (mgr);
  aigs = calloc (maxvar + 1, sizeof aigs[0]);
  copyaig (aig);
  aiger_add_output (expansion, simpaig_unsigned_index (aig), 0);
  free (aigs);
  simpaig_reset_indices (mgr);
  free (buffer);
}

#define USAGE \
"usage: aigbmc [-h][-v][-a][-s][<k>][<src>[<dst>]]\n" \
"\n" \
"where\n" \
"\n" \
"  -h     prints this command line option summary\n" \
"  -v     increase verbose level\n" \
"  -a     force ASCII output\n" \
"  -s     strip symbols from target model\n" \
"  <k>    bound (default 0)\n" \
"  <src>  sequential source model in AIGER format\n" \
"  <dst>  combinational target model in AIGER format\n"

int
main (int argc, char **argv)
{
  const char *src, *dst, *p, *err;
  aiger_mode mode;
  simpaig *res;
  int i, ascii;

  src = dst = 0;
  ascii = 0;

  for (i = 1; i < argc; i++)
    {
      for (p = argv[i]; isdigit (*p); p++)
	;

      if (!*p)
	k = atoi (argv[i]);
      else if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-a"))
	ascii = 1;
      else if (!strcmp (argv[i], "-s"))
	strip = 1;
      else if (!strcmp (argv[i], "-v"))
	verbose++;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	die ("too many files");
    }

  if (ascii && dst)
    die ("'dst' file and '-a' specified");

  if (!ascii && !dst && isatty (1))
    ascii = 1;

  if (src && dst && !strcmp (src, dst))
    die ("identical 'src' and 'dst' file");

  model = aiger_init ();
  if (src)
    err = aiger_open_and_read_from_file (model, src);
  else
    err = aiger_read_from_file (model, stdin);

  if (!src)
    src = "<stdin>";

  if (err)
    die ("%s: %s", src, err);

  if (!model->num_outputs)
    die ("%s: no output");

  if (model->num_outputs > 1)
    die ("%s: more than one output");

  aiger_reencode (model);

  mgr = simpaig_init ();
  res = build ();
  expansion = aiger_init ();
  expand (res);
  simpaig_dec (mgr, res);

  for (i = 0; i <= model->maxvar; i++)
    simpaig_dec (mgr, lois[i].aig);
  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);
  aiger_reset (model);

  free (lois);

  if (dst)
    {
      if (!aiger_open_and_write_to_file (expansion, dst))
	{
	  unlink (dst);
	WRITE_ERROR:
	  die ("%s: write error", dst);
	}
    }
  else
    {
      mode = ascii ? aiger_ascii_mode : aiger_binary_mode;
      if (!aiger_write_to_file (expansion, mode, stdout))
	goto WRITE_ERROR;
    }

  aiger_reset (expansion);

  return 0;
}
