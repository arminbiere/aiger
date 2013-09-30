/***************************************************************************
Copyright (c) 2010-2013, Armin Biere, Johannes Kepler University.

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
#include <sys/stat.h>

static const char * USAGE =
"usage: aigsplit [-h][-v][-f][-s <seed>][-r <max-rand>] <input> [<prefix>]\n"
"\n"
"Split all outputs of the input AIGER model.  For each output a new file\n"
"will be generated with name '<prefix><type>[0-9]*.aig'.  If a file already\n"
"exists then 'aigsplits' aborts unless it is forced to overwrite it\n"
"by specifying '-f'.  If '<prefix>' is missing, then the base name of\n"
"'<input>' is used as prefix.  If '-r <max-rand>' is specified then\n"
"a random sample of maximum <max-rand> output, <max-rand> bad, and\n"
"<max-rand> justicte properties are printed.\n"
;

typedef enum Type {
  OUTPUT = 0,
  BAD = 1,
  JUSTICE = 2,
} Type;

static unsigned written, lim, max;
static int verbose, force, randomize;
static aiger * src, * dst;
static char * prefix;
static int seed;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigsplit] ", stderr);
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
  if (verbose < level) return;
  fputs ("[aigsplit] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned
ld10 (unsigned i)
{
  int res, exp = 10;
  res = 1;
  while (i >= exp)
    {
      exp *= 10;
      res++;
    }
  return res;
}

static char *
chop (const char * src)
{
  char * res = strdup (src), * last, * p;
  last = 0;
  while ((p = strstr (last ? last + 4 : res, ".aig")))
    last = p;
  if (last) *last = 0;
  return res;
}

static int 
exists (const char * name) 
{
  struct stat buf;
  return !stat (name, &buf);
}

static void 
print (Type type, unsigned idx)
{
  char comment[80], fmt[80], tch;
  unsigned j, lit, l;
  aiger_and * a;
  char * name;
  int ok;

  if (type == OUTPUT) tch = 'o';
  else if (type == BAD) tch = 'b';
  else assert (type == JUSTICE), tch = 'j';

  l = ld10 (max - 1);
  sprintf (fmt, "%%s%c%%0%uu.aig", tch, l);
  name = malloc (strlen (prefix) + l + 10);
  sprintf (name, fmt, prefix, idx);

  if (!force && exists (name))
    die ("output file '%s' already exists (use '-f')", name);

  msg (2, "writing %s", name);

  dst = aiger_init ();
  for (j = 0; j < src->num_inputs; j++)
    aiger_add_input (dst, src->inputs[j].lit, src->inputs[j].name);

  for (j = 0; j < src->num_latches; j++) {
    aiger_add_latch (dst, src->latches[j].lit, 
                          src->latches[j].next,
                          src->latches[j].name);
    aiger_add_reset (dst, src->latches[j].lit, src->latches[j].reset);
  }

  for (j = 0; j < src->num_ands; j++)
    {
      a = src->ands + j;
      aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
    }

  for (j = 0; j < src->num_constraints; j++)
    aiger_add_constraint (dst,
      src->constraints[j].lit, src->constraints[j].name);

  if (type == JUSTICE)
    for (j = 0; j < src->num_fairness; j++)
      aiger_add_fairness (dst,
	src->fairness[j].lit, src->fairness[j].name);

  sprintf (comment, "aigsplit");
  aiger_add_comment (dst, comment);

  switch (type)
    {
      case OUTPUT:
      default:
	assert (type == OUTPUT);
	assert (idx < src->num_outputs);
	lit = src->outputs[idx].lit;
	aiger_add_output (dst, lit, src->outputs[idx].name);
	sprintf (comment, "was output %u", idx);
	break;
      case BAD:
	assert (idx < src->num_bad);
	lit = src->bad[idx].lit;
	aiger_add_bad (dst, lit, src->bad[idx].name);
	sprintf (comment, "was bad %u", idx);
	break;
      case JUSTICE:
	assert (idx < src->num_justice);
	aiger_add_justice (dst, 
	  src->justice[idx].size,
	  src->justice[idx].lits,
	  src->justice[idx].name);
	sprintf (comment, "was justice %u", idx);
	break;
    }
  aiger_add_comment (dst, comment);

  ok = aiger_open_and_write_to_file (dst, name);

  if (!ok)
    die ("writing to %s failed", name);

  free (name);

  msg (2, "wrote MILOA = %u %u %u %u %u", 
       dst->maxvar,
       dst->num_inputs,
       dst->num_latches,
       dst->num_outputs,
       dst->num_ands);

  msg (2, "wrote BCJF = %u %u %u %u",
       dst->num_bad,
       dst->num_constraints,
       dst->num_justice,
       dst->num_fairness);

  aiger_reset (dst);
  written++;
}

static unsigned gcd (unsigned a, unsigned b) {
  while (b) {
    unsigned r = a % b;
    a = b, b = r;
  }
  return a;
}

static void printall (Type type, unsigned num) {
  unsigned s, p, d, l, c, n;
  l = (!randomize || num < lim) ? num : lim;
  s = 0, d = 1;
  if (randomize && l) {
    s = rand () % num;
    if (l > 1) {
      d = rand () % num;
      while (gcd (d, num) != 1)
	if (++d == num) d = 1;
    }
  }
  p = s;
  for (c = 0; c < l; c++) {
    print (type, p);
    p += d;
    if (p >= num) p -= num;
  }
}

int
main (int argc, char ** argv)
{
  const char * input, * err;
  unsigned i, k;

  input = prefix = 0;

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
      else if (!strcmp (argv[i], "-s")) {
	if (++i == argc) die ("argument to '-s' missing");
	seed = atoi (argv[i]);
      } else if (!strcmp (argv[i], "-r")) {
	if (++i == argc) die ("argument to '-r' missing");
	if (randomize) die ("multiple '-r' options");
	if (((int)(lim = atoi (argv[i]))) < 0)
	  die ("invalid argument to '-r'");
	randomize = 1;
      } else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (prefix)
	die ("too many arguments");
      else if (input)
	prefix = chop (argv[i]);
      else
	input = argv[i];
    }

  if (!input) 
    die ("no input specified");

  if (!prefix)
    prefix = chop (input);

  msg (1, "prefix %s", prefix);

  if (randomize) {
    msg (1, "randomize %u", lim);
    msg (1, "seed %d", seed);
    srand (seed);
  }

  msg (1, "reading %s", input);
  src = aiger_init ();
  err = aiger_open_and_read_from_file (src, input);

  if (err)
    die ("read error: %s", err);

  msg (1, "read MILOA = %u %u %u %u %u", 
       src->maxvar,
       src->num_inputs,
       src->num_latches,
       src->num_outputs,
       src->num_ands);
  
  msg (1, "read BCJF = %u %u %u %u",
       src->num_bad,
       src->num_constraints,
       src->num_justice,
       src->num_fairness);
  
  max = src->num_outputs;
  if (max < src->num_bad) max = src->num_bad;
  if (max < src->num_justice) max = src->num_justice;

  printall (OUTPUT, src->num_outputs);
  printall (BAD, src->num_bad);
  printall (JUSTICE, src->num_justice);

  msg (1, "wrote %u files", written);

  aiger_reset (src);
  free (prefix);

  return 0;
}
