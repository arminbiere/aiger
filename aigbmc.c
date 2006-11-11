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
  simpaig *next;
};

static unsigned k;
static aiger *model;
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
  simpaig *res;

  if (!(res = lois[idx].aig))
    {
      if (idx)
	{
	}
      else
	res = simpaig_false (mgr);

      lois[idx].aig = res;
    }

  if (sign)
    res = simpaig_not (res);

  return res;
}

static void
build (void)
{
  aiger_symbol *symbol;
  simpaig *aig;
  unsigned i;

  lois = malloc ((model->maxvar + 1) * sizeof lois[0]);
  for (i = 0; i <= model->maxvar; i++)
    {
      lois[i].idx = i;
      lois[i].aig = 0;
      lois[i].next = 0;
    }

  for (i = 0; i <= model->maxvar; i++)
    {
      aig = build_rec (i * 2);
      assert (aig == lois[i].aig);
    }

  for (i = 0; i <= model->maxvar; i++)
    {
      symbol = aiger_is_latch (model, 2 * i);
      if (symbol)
	lois[i].next = build_rec (symbol->next);
    }
}

#define USAGE \
  "usage: aigbmc [-h][-v][-a][-s][k][src [dst]]\n"

int
main (int argc, char **argv)
{
  const char *src, *dst, *p, *err;
  int i, ascii, strip;

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
    die ("will not write binary file to stdout connected to terminal");

  if (src && dst && !strcmp (src, dst))
    die ("identical 'src' and 'dst' file");

  model = aiger_init ();
  if (src)
    err = aiger_open_and_read_from_file (model, src);
  else
    err = aiger_read_from_file (model, stdin);

  if (err)
    die ("%s: %s", (src ? src : "<stdin>"), err);

  aiger_reencode (model);

  mgr = simpaig_init ();
  build ();

  for (i = 0; i <= model->maxvar; i++)
    simpaig_dec (mgr, lois[i].aig);
  assert (!simpaig_current_nodes (mgr));
  simpaig_reset (mgr);

  aiger_reset (model);

  return 0;
}
