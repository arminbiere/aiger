/***************************************************************************
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

static FILE *file;
static aiger *mgr;
static int count;

static const char * prefix = "";

static void
ps (const char *str)
{
  fputs (str, file);
}

static void
pl (unsigned lit)
{
  const char *name;
  char ch;
  int i;

  if (lit == 0)
    fprintf (file, "FALSE");
  else if (lit == 1)
    fprintf (file, "TRUE");
  else if ((lit & 1))
    putc ('!', file), pl (lit - 1);
  else
    {
      if (prefix)
	fputs (prefix, file);
	 if ((name = aiger_get_symbol (mgr, lit)))
	{
	  /* TODO: check name to be valid SMV name
	   */
	  fputs (name, file);
	}
      else
	{
	  if (aiger_is_input (mgr, lit))
	    ch = 'i';
	  else if (aiger_is_latch (mgr, lit))
	    ch = 'l';
	  else
	    {
	      assert (aiger_is_and (mgr, lit));
	      ch = 'a';
	    }

	  for (i = 0; i <= count; i++)
	    fputc (ch, file);

	  fprintf (file, "%u", lit);
	}
    }
}

static int
count_ch_prefix (const char *str, char ch)
{
  const char *p;

  assert (ch);
  for (p = str; *p == ch; p++)
    ;

  if (*p && !isdigit (*p))
    return 0;

  return p - str;
}

static void
setupcount (void)
{
  const char *symbol;
  unsigned i;
  int tmp;

  count = 0;
  for (i = 1; i <= mgr->maxvar; i++)
    {
      symbol = aiger_get_symbol (mgr, 2 * i);
      if (!symbol)
	continue;

      if ((tmp = count_ch_prefix (symbol, 'i')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'l')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'o')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'a')) > count)
	count = tmp;
    }
}

int
main (int argc, char **argv)
{
  const char *src, *dst, *error;
  int res, strip, bad;
  unsigned i, j;

  src = dst = 0;
  strip = 0;
  res = 0;
  bad = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, 
	           "usage: aigtosmv [-h][-s][-p <prefix>][src [dst]]\n"
		   "\n"
		   "  -h           print this command line option summary\n"
		   "  -b           assume outputs are bad properties\n"
		   "  -p <prefix>  use <prefix> for variable names\n"
		   "  -s           strip symbols\n"
		   );
	  exit (0);
	}
      if (!strcmp (argv[i], "-s"))
	strip = 1;
      else if (!strcmp (argv[i], "-b"))
	bad = 1;
      else if (!strcmp (argv[i], "-p"))
	{
	  if (++i == argc)
	    {
	      fprintf (stderr, "*** [aigtosmv] argument to '-p' missing\n");
	      exit (1);
	    }

	  prefix = argv[i];
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "*** [aigtosmv] invalid option '%s'\n", argv[i]);
	  exit (1);
	}
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	{
	  fprintf (stderr, "*** [aigtosmv] too many files\n");
	  exit (1);
	}
    }

  mgr = aiger_init ();

  if (src)
    error = aiger_open_and_read_from_file (mgr, src);
  else
    error = aiger_read_from_file (mgr, stdin);

  if (error)
    {
      fprintf (stderr, "*** [aigtosmv] %s\n", error);
      res = 1;
    }
  else
    {
      if (dst)
	{
	  if (!(file = fopen (dst, "w")))
	    {
	      fprintf (stderr,
		       "*** [aigtosmv] failed to write to '%s'\n", dst);
	      exit (1);
	    }
	}
      else
	file = stdout;

      if (strip)
	aiger_strip_symbols_and_comments (mgr);
      else
	setupcount ();

      ps ("MODULE main\n");
      ps ("VAR\n");
      ps ("--inputs\n");
      for (i = 0; i < mgr->num_inputs; i++)
	pl (mgr->inputs[i].lit), ps (" : boolean;\n");
      ps ("--latches\n");
      for (i = 0; i < mgr->num_latches; i++)
	pl (mgr->latches[i].lit), ps (" : boolean;\n");
      ps ("ASSIGN\n");
      for (i = 0; i < mgr->num_latches; i++)
	{
	  if (mgr->latches[i].reset != mgr->latches[i].lit) {
	    ps ("init("), pl (mgr->latches[i].lit), ps (") := "); 
	    pl(mgr->latches[i].reset), ps(";\n");
	  }
	  ps ("next("), pl (mgr->latches[i].lit), ps (") := ");
	  pl (mgr->latches[i].next), ps (";\n");
	}
      ps ("DEFINE\n");
      ps ("--ands\n");
      for (i = 0; i < mgr->num_ands; i++)
	{
	  aiger_and *n = mgr->ands + i;

	  unsigned rhs0 = n->rhs0;
	  unsigned rhs1 = n->rhs1;

	  pl (n->lhs);
	  ps (" := ");
	  pl (rhs0);
	  ps (" & ");
	  pl (rhs1);
	  ps (";\n");
	}

      ps ("--outputs\n");
      for (i = 0; i < mgr->num_outputs; i++)
	{
	  for (j = 0; j <= count; j++)
	    putc ('o', file);

	  fprintf (file, "%u := ", i), pl (mgr->outputs[i].lit), ps (";\n");
	}

      ps ("--bad\n");
      if (bad)
	{
	  for (i = 0; i < mgr->num_outputs; i++)
	    {
	      fprintf (file, "SPEC AG ");
	      pl (aiger_not (mgr->outputs[i].lit));
	      fprintf (file, " --o%u as bad property\n", i);
	    }
	}
      for (i = 0; i < mgr->num_bad; i++)
	{
	  ps ("SPEC AG ");
	  pl (aiger_not (mgr->bad[i].lit));
	  fprintf (file, " --b%u\n", i);
	}

      ps ("--constraints\n");
      for (i = 0; i < mgr->num_constraints; i++)
	{
	  fprintf (file, "INVAR ");
	  pl (mgr->constraints[i].lit);
	  fprintf (file, " --c%u\n", i);
	}

      ps ("--justice\n");
      for (i = 0; i < mgr->num_justice; i++)
	{
	  fprintf (file, "LTLSPEC !( --j%u\n", i);
	  for (j = 0; j < mgr->justice[i].size; j++)
	    {
	      if (j) ps (" &\n");
	      ps ("(G F ");
	      pl (mgr->justice[i].lits[j]);
	      ps (")");
	    }
	  ps (")\n");
	}

      ps ("--fairness\n");
      for (i = 0; i < mgr->num_fairness; i++)
	{
	  ps ("FAIRNESS ");
	  pl (mgr->fairness[i].lit);
	  fprintf (file, " --f%u\n", i);
	}

      if (dst)
	fclose (file);
    }

  aiger_reset (mgr);

  return res;
}
