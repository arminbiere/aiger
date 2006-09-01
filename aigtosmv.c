#include "aiger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static FILE * file;
static aiger * mgr;

static void
ps (const char * str)
{
  fputs (str, file);
}

static void
pl (unsigned lit)
{
  if (lit == 0)
    putc ('0', file);
  else if (lit == 1)
    putc ('1', file);
  else if ((lit & 1))
    putc ('!', file), pl (lit - 1);
  else
    {
      aiger_literal * literal = mgr->literals + lit;
      aiger_symbol * symbol = literal->symbol;

      if (symbol && symbol->str)
	{
	  fputs (symbol->str, file);
	}
      else
	{
	  if (literal->input)
	    putc ('i', file);
	  else if (literal->latch)
	    putc ('l', file);
	  else
	    {
	      assert (literal->node);
	      putc ('a', file);
	    }

	  fprintf (file, "%u", lit);
	}
    }
}

int
main (int argc, char ** argv)
{
  const char * src, * dst, * error;
  int res, strip;
  unsigned i;

  src = dst = 0;
  strip = 0;

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

      if (strip)
	aiger_strip_symbols (mgr);

      fputs ("MODULE main\n", file);
      fputs ("VAR\n", file);
      fputs ("--inputs\n", file);
      for (i = 0; i < mgr->num_inputs; i++)
	pl (mgr->inputs[i].lit), ps (":boolean;\n");
      fputs ("--latches\n", file);
      for (i = 0; i < mgr->num_latches; i++)
	pl (mgr->latches[i].lit), ps (":boolean;\n");
      fputs ("ASSIGN\n", file);
      fputs ("DEFINE\n", file);
      fputs ("--ands\n", file);
      for (i = 0; i < mgr->num_nodes; i++)
	{
	  aiger_node * n = mgr->nodes + i;
	  pl (n->lhs);
	  ps (":=");
	  pl (n->rhs0);
	  ps ("&");
	  pl (n->rhs1);
	  ps (";\n");
	}
      fputs ("--outputs\n", file);
      for (i = 0; i < mgr->num_outputs; i++)
	fprintf (file, "o%u:=", i), pl (mgr->outputs[i].lit), ps (";\n");
      if (dst)
	fclose (file);
    }

  aiger_reset (mgr);

  return res;
}
