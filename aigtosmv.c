#include "aiger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

static FILE * file;
static aiger * mgr;
static int count;

static void
ps (const char * str)
{
  fputs (str, file);
}

static void
pl (unsigned lit)
{
  char ch;
  int i;

  if (lit == 0)
    putc ('0', file);
  else if (lit == 1)
    putc ('1', file);
  else if ((lit & 1))
    putc ('!', file), pl (lit - 1);
  else
    {
      aiger_literal * literal = mgr->literals + lit;
      if (literal->symbol)
	{
	  fputs (literal->symbol, file);
	}
      else
	{
	  if (literal->input)
	    ch = 'i';
	  else if (literal->latch)
	    ch = 'l';
	  else
	    {
	      assert (literal->and);
	      ch = 'a';
	    }

	  for (i = 0; i <= count; i++)
	    fputc (ch, file);

	  fprintf (file, "%u", lit);
	}
    }
}

static int
count_ch_prefix (const char * str, char ch)
{
  const char * p;

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
  const char * symbol;
  unsigned i;
  int tmp;

  count = 0;
  for (i = 0; i <= mgr->max_literal; i++)
    {
      symbol = mgr->literals[i].symbol;
      if (symbol)
	{
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
}

int
main (int argc, char ** argv)
{
  const char * src, * dst, * error;
  int res, strip, ag;
  unsigned i, j;

  src = dst = 0;
  strip = 0;
  ag = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: aigtosmv [-h][-s][src [dst]]\n");
	  exit (0);
	}
      if (!strcmp (argv[i], "-s"))
	strip = 1;
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
      
      ag = (mgr->num_outputs == 1 && 
	    mgr->outputs[0].str && 
	    !strcmp (mgr->outputs[0].str, "NEVER"));

      if (strip)
	aiger_strip_symbols (mgr);
      else
	setupcount ();

      fputs ("MODULE main\n", file);
      fputs ("VAR\n", file);
      fputs ("--inputs\n", file);
      for (i = 0; i < mgr->num_inputs; i++)
	pl (mgr->inputs[i].lit), ps (":boolean;\n");
      fputs ("--latches\n", file);
      for (i = 0; i < mgr->num_latches; i++)
	pl (mgr->latches[i].lit), ps (":boolean;\n");
      fputs ("ASSIGN\n", file);
      for (i = 0; i < mgr->num_latches; i++)
	{
	  ps ("init("), pl (mgr->latches[i].lit), ps ("):=0;\n");
	  ps ("next("), pl (mgr->latches[i].lit), ps ("):=");
	  pl (mgr->next[i]), ps (";\n");
	}
      fputs ("DEFINE\n", file);
      fputs ("--ands\n", file);
      for (i = 0; i < mgr->num_ands; i++)
	{
	  aiger_and * n = mgr->ands + i;
	  pl (n->lhs);
	  ps (":=");
	  pl (n->rhs0);
	  ps ("&");
	  pl (n->rhs1);
	  ps (";\n");
	}

      if (ag)
	fprintf (file, "SPEC AG!"), pl (mgr->outputs[i].lit), ps ("\n");
      else
	{
	  fputs ("--outputs\n", file);
	  for (i = 0; i < mgr->num_outputs; i++)
	    {
	      for (j = 0; j <= count; j++)
		putc ('o', file);

	      fprintf (file, "%u:=", i), pl (mgr->outputs[i].lit), ps (";\n");
	    }

	  ps ("SPEC AG 1\n");
	}

      if (dst)
	fclose (file);
    }

  aiger_reset (mgr);

  return res;
}
