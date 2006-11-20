/***************************************************************************
Copyright (c) 2006, Armin Biere, Johannes Kepler University.

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
#define USAGE \
"usage: aigdd [-h][-v][-r] <src> <dst> [ <run> ]\n" \
sdf
"\n" \
sdf
"This 'delta debugger' for AIGs has the following options:\n" \
sdf
"\n" \
sdf
"  -h     prints this command line option summary\n" \
sdf
"  -v     increases verbose level (default 0, max 3)\n" \
sdf
"  -r     reencode and remove holes even if <dst> is in ASCII format\n" \
sdf
"  <src>  source file in AIGER format\n" \
sdf
"  <dst>  destination file in AIGER format\n" \
sdf
"  <run>  executable\n" \
sdf
"\n" \
sdf
"The idea is that '<run> <src>' produces a fault, e.g. you know that\n" \
sdf
"there is a big AIG saved in '<src>' which produces a wrong behaviour\n" \
sdf
"when given as argument to the program or shell script '<run>'.\n" \
sdf
"You can now use 'aigdd' to produce a copy '<dst>' of '<src>' in which\n" \
sdf
"as many literals as possible are removed while still producing\n" \
sdf
"the same exit code when running '<run> <dst>'.  Literals are actually\n" \
sdf
"removed by assigning them to a constant.  This in effect removes inputs,\n" \
sdf
"latches and ANDs.  The number of outputs is currently not changed, but\n" \
sdf
"individual outputs are set to constants.\n" \
sdf
"\n" \
sdf
"If '<dst>' is an AIG in ASCII format, by specifying a '.aag' extension,\n" \
sdf
"then the 'holes' left by removed literals are not squeezed out, while\n" \
sdf
"in the binary this is enforced.\n" \
sdf
"\n" \
sdf
"As a typical example consider that you have a new structural SAT solver\n" \
sdf
"'solve' that reads AIGs.  On one AIG it fails with an assertion\n" \
sdf
"failure.  You save this AIG in '/tmp/fail.aig'.  To shrink\n" \
sdf
"this file while still producing a failure you could just use\n" \
sdf
"\n" \
sdf
"  aigdd /tmp/fail.aig /tmp/shrunken.aig solve\n" \
sdf
"\n" \
sdf
"Unless your solver produces the same exit code for a correct run, this\n" \
sdf
"should give a compact easier to analyze AIG in '/tmp/shrunken.aig'.\n"
sdf

sdf
#include "aiger.h"
sdf

sdf
#include <assert.h>
sdf
#include <stdarg.h>
sdf
#include <stdlib.h>
sdf
#include <string.h>
sdf
#include <unistd.h>
sdf

sdf
static aiger * src;
sdf
static const char * dst_name;
sdf
static unsigned * stable;
sdf
static unsigned * unstable;
sdf
static int verbose;
sdf
static int reencode;
sdf

sdf
static void
sdf
msg (int level, const char * fmt, ...)
sdf
{
sdf
  va_list ap;
sdf
  if (verbose < level)
sdf
    return;
sdf
  fputs ("[aigdd] ", stderr);
sdf
  va_start (ap, fmt);
sdf
  vfprintf (stderr, fmt, ap);
sdf
  va_end (ap);
sdf
  fputc ('\n', stderr);
sdf
  fflush (stderr);
sdf
}
sdf

sdf
static void
sdf
die (const char * fmt, ...)
sdf
{
sdf
  va_list ap;
sdf
  fputs ("*** [aigdd] ", stderr);
sdf
  va_start (ap, fmt);
sdf
  vfprintf (stderr, fmt, ap);
sdf
  va_end (ap);
sdf
  fputc ('\n', stderr);
sdf
  fflush (stderr);
sdf
  exit (1);
sdf
}
sdf

sdf
static unsigned
sdf
deref (unsigned lit)
sdf
{
sdf
  unsigned sign = lit & 1;
sdf
  unsigned idx = lit / 2;
sdf
  assert (idx <= src->maxvar);
sdf
  return unstable[idx] ^ sign;
sdf
}
sdf

sdf
static void
sdf
write_unstable_to_dst (void)
sdf
{
sdf
  aiger_symbol * symbol;
sdf
  aiger_and * and;
sdf
  unsigned i, lit;
sdf
  aiger * dst;
sdf
  
sdf
  dst = aiger_init ();
sdf

sdf
  for (i = 0; i < src->num_inputs; i++)
sdf
    {
sdf
      symbol = src->inputs + i;
sdf
      lit = symbol->lit;
sdf
      if (deref (lit) == lit)
sdf
	aiger_add_input (dst, lit, symbol->name);
sdf
    }
sdf

sdf
  for (i = 0; i < src->num_latches; i++)
sdf
    {
sdf
      symbol = src->latches + i;
sdf
      lit = symbol->lit;
sdf
      if (deref (lit) == lit)
sdf
	aiger_add_latch (dst, lit, deref (symbol->next), symbol->name);
sdf
    }
sdf

sdf
  for (i = 0; i < src->num_ands; i++)
sdf
    {
sdf
      and = src->ands + i;
sdf
      if (deref (and->lhs) == and->lhs)
sdf
	aiger_add_and (dst, and->lhs, deref (and->rhs0), deref (and->rhs1));
sdf
    }
sdf

sdf
  for (i = 0; i < src->num_outputs; i++)
sdf
    {
sdf
      symbol = src->outputs + i;
sdf
      aiger_add_output (dst, deref (symbol->lit), symbol->name);
sdf
    }
sdf

sdf
  assert (!aiger_check (dst));
sdf

sdf
  if (reencode)
sdf
    aiger_reencode (dst);
sdf

sdf
  unlink (dst_name);
sdf
  if (!aiger_open_and_write_to_file (dst, dst_name))
sdf
    die ("failed to write '%s'", dst_name);
sdf
  aiger_reset (dst);
sdf
}
sdf

sdf
static void
sdf
copy_stable_to_unstable (void)
sdf
{
sdf
  unsigned i;
sdf

sdf
  for (i = 0; i <= src->maxvar; i++)
sdf
    unstable[i] = stable[i];
sdf
}
sdf

sdf
#if 1
sdf
#define CMD "exec %s %s 1>/dev/null 2>/dev/null"
sdf
#else
sdf
#define CMD "exec %s %s"
sdf
#endif
sdf

sdf
static int
sdf
min (int a, int b)
sdf
{
sdf
  return a < b ? a : b;
sdf
}
sdf

sdf
int
sdf
main (int argc, char ** argv)
sdf
{
sdf
  int i, changed, delta, j, expected, res, last, outof;
sdf
  const char * src_name, * run_name, * err;
sdf
  char * cmd;
sdf

sdf
  src_name = dst_name = run_name = 0;
sdf

sdf
  for (i = 1; i < argc; i++)
sdf
    {
sdf
      if (!strcmp (argv[i], "-h"))
sdf
	{
sdf
	  fprintf (stderr, USAGE);
sdf
	  exit (0);
sdf
	}
sdf
      else if (!strcmp (argv[i], "-v"))
sdf
	verbose++;
sdf
      else if (!strcmp (argv[i], "-r"))
sdf
	reencode = 1;
sdf
      else if (src_name && dst_name && run_name)
sdf
	die ("more than three files");
sdf
      else if (dst_name)
sdf
	run_name = argv[i];
sdf
      else if (src_name)
sdf
	dst_name = argv[i];
sdf
      else
sdf
	src_name = argv[i];
sdf
    }
sdf

sdf
  if (!src_name || !dst_name)
sdf
    die ("expected exactly two files");
sdf

sdf
  if (!run_name)
sdf
    run_name = "./run";
sdf

sdf
  cmd = malloc (strlen (src_name) + strlen (run_name) + strlen (CMD) + 1);
sdf
  sprintf (cmd, CMD, run_name, src_name);
sdf
  expected = system (cmd);
sdf
  msg (1, "'%s' returns %d", cmd, expected);
sdf
  free (cmd);
sdf

sdf
  cmd = malloc (strlen (dst_name) + strlen (run_name) + strlen (CMD) + 1);
sdf
  sprintf (cmd, CMD, run_name, dst_name);
sdf

sdf
  src = aiger_init ();
sdf
  if ((err = aiger_open_and_read_from_file (src, src_name)))
sdf
    die ("%s: %s", src_name, err);
sdf

sdf
  stable = malloc (sizeof (stable[0]) * (src->maxvar + 1));
sdf
  unstable = malloc (sizeof (unstable[0]) * (src->maxvar + 1));
sdf

sdf
  for (i = 0; i <= src->maxvar; i++)
sdf
    stable[i] = 2 * i;
sdf

sdf
  copy_stable_to_unstable ();
sdf
  write_unstable_to_dst ();
sdf

sdf
  res = system (cmd);
sdf
  if (res != expected)
sdf
    die ("return value of copy differs (%d instead of %d)", res, expected);
sdf

sdf
  for (delta = src->maxvar; delta; delta = (delta == 1) ? 0 : (delta + 1)/2)
sdf
    {
sdf
      i = 1;
sdf

sdf
      do {
sdf
	for (j = 1; j < i; j++)
sdf
	  unstable[j] = stable[j];
sdf

sdf
	changed = 0;
sdf
	last = min (i + delta - 1, src->maxvar);
sdf
	outof = last - i + 1;
sdf
	for (j = i; j <= last; j++)
sdf
	  {
sdf
	    if (stable[j])		/* replace '1' by '0' as well */
sdf
	      {
sdf
		unstable[j] = 0;
sdf
		changed++;
sdf
	      }
sdf
	    else
sdf
	      unstable[j] = 0;		/* always favor 'zero' */
sdf
	  }
sdf

sdf
	if (changed)
sdf
	  {
sdf
	    for (j = i + delta; j <= src->maxvar; j++)
sdf
	      unstable[j] = stable[j];
sdf

sdf
	    write_unstable_to_dst ();
sdf
	    res = system (cmd);
sdf
	    if (res == expected)
sdf
	      {
sdf
		msg (1, "[%d,%d] set to 0 (%d out of %d)",
sdf
		     i, last, changed, outof);
sdf

sdf
		for (j = i; j <= last; j++)
sdf
		  stable[j] = unstable[j];
sdf
	      }
sdf
	    else			/* try setting to 'one' */
sdf
	      {
sdf
		msg (2, "[%d,%d] can not be set to 0 (%d out of %d)",
sdf
		     i, last, changed, outof);
sdf

sdf
		for (j = 1; j < i; j++)
sdf
		  unstable[j] = stable[j];
sdf

sdf
		changed = 0;
sdf
		for (j = i; j <= last; j++)
sdf
		  {
sdf
		    if (stable[j])
sdf
		      {
sdf
			if (stable[j] > 1)
sdf
			  {
sdf
			    unstable[j] = 1;
sdf
			    changed++;
sdf
			  }
sdf
			else
sdf
			  unstable[j] = 1;
sdf
		      }
sdf
		    else
sdf
		      unstable[j] = 0;	/* always favor '0' */
sdf
		  }
sdf

sdf
		if (changed)
sdf
		  {
sdf
		    for (j = i + delta; j <= src->maxvar; j++)
sdf
		      unstable[j] = stable[j];
sdf

sdf
		    write_unstable_to_dst ();
sdf
		    res = system (cmd);
sdf
		    if (res == expected)
sdf
		      {
sdf
			msg (1, "[%d,%d] set to 1 (%d out of %d)",
sdf
			     i, last, changed, outof);
sdf

sdf
			for (j = i; j < i + delta && j <= src->maxvar; j++)
sdf
			  stable[j] = unstable[j];
sdf
		      }
sdf
		    else
sdf
		      msg (2, "[%d,%d] can neither be set to 1 (%d out of %d)",
sdf
			   i, last, changed, outof);
sdf
		  }
sdf
	      }
sdf
	  }
sdf
	else
sdf
	  msg (3, "[%d,%d] stabilized to 0", i, last);
sdf

sdf
	i += delta;
sdf
      } while (i <= src->maxvar);
sdf
    }
sdf

sdf
  copy_stable_to_unstable ();
sdf
  write_unstable_to_dst ();
sdf

sdf
  changed = 0;
sdf
  for (i = 1; i <= src->maxvar; i++)
sdf
    if (stable[i] <= 1)
sdf
      changed++;
sdf

sdf
  msg (1, "%.1f%% literals removed (%d out of %d)",
sdf
       src->maxvar ? changed * 100.0 / src->maxvar : 0,
sdf
       changed, src->maxvar);
sdf

sdf
  free (stable);
sdf
  free (unstable);
sdf
  free (cmd);
sdf
  aiger_reset (src);
sdf

sdf
  return 0;
sdf
}
sdf
