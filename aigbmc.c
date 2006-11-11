/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

/*************** !!! INCOMPLETE, NOT WORKING YET !!! **********************/

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
static unsigned verbose;
static simpaigmgr *mgr;
static LatchOrInput *lois;

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
  simpaig *res, * l, * r;
  aiger_and * and;

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
  simpaig *aig, * res, * shifted, * tmp, * lhs, * rhs, * out;
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
  simpaig_dec (mgr, tmp);
  res = tmp;

  return res;
}

static void
expand (simpaig * aig)
{
}

#define USAGE \
  "usage: aigbmc [-h][-v][-a][-s][k][src [dst]]\n"

int
main (int argc, char **argv)
{
  const char *src, *dst, *p, *err;
  int i, ascii, strip;
  aiger_mode mode;
  simpaig * res;

  src = dst = 0;
  strip = ascii = 0;

  for (i = 1; i < argc; i++)
    {
      for (p = argv[0]; isdigit (*p); p++)
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

  if (strip)
    aiger_strip_symbols_and_comments (expansion);

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
