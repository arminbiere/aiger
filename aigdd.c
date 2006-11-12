#include "aiger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static aiger * src;
static const char * dst_name;
static unsigned * stable;
static unsigned * unstable;
static int verbose;

static void
msg (int level, const char * fmt, ...)
{
  va_list ap;
  if (verbose < level)
    return;
  fputs ("[aigdd] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** [aigdd] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static unsigned
deref (unsigned lit)
{
  unsigned sign = lit & 1;
  unsigned idx = lit / 2;
  assert (idx <= src->maxvar);
  return unstable[idx] ^ sign;
}

static void
write_unstable_to_dst (void)
{
  aiger_symbol * symbol;
  aiger_and * and;
  unsigned i, lit;
  aiger * dst;
  
  dst = aiger_init ();

  for (i = 0; i < src->num_inputs; i++)
    {
      symbol = src->inputs + i;
      lit = symbol->lit;
      if (deref (lit) == lit)
	aiger_add_input (dst, lit, symbol->name);
    }

  for (i = 0; i < src->num_latches; i++)
    {
      symbol = src->latches + i;
      lit = symbol->lit;
      if (deref (lit) == lit)
	aiger_add_latch (dst, lit, deref (symbol->next), symbol->name);
    }

  for (i = 0; i < src->num_ands; i++)
    {
      and = src->ands + i;
      if (deref (and->lhs) == and->lhs)
	aiger_add_and (dst, and->lhs, deref (and->rhs0), deref (and->rhs1));
    }

  for (i = 0; i < src->num_outputs; i++)
    {
      symbol = src->outputs + i;
      aiger_add_output (dst, deref (symbol->lit), symbol->name);
    }

  assert (!aiger_check (dst));

  unlink (dst_name);
  if (!aiger_open_and_write_to_file (dst, dst_name))
    die ("failed to write '%s'", dst_name);
  aiger_reset (dst);
}

static void
copy_stable_to_unstable (void)
{
  unsigned i;

  for (i = 0; i <= src->maxvar; i++)
    unstable[i] = stable[i];
}

#define USAGE \
"usage: aigdd [-h][-v] <src> <dst> [ <run> ]\n" \
"\n" \
"This 'delta debugger' for AIGs has the following options:\n" \
"\n" \
"  -h     prints this command line option summary\n" \
"  -v     increases verbose level (default 0, max 3)\n" \
"  <src>  source file in AIGER format\n" \
"  <dst>  destination file in AIGER format\n" \
"  <run>  executable\n" \
"\n" \
"The idea is that '<run> <src>' produces a fault, e.g. you know that\n" \
"there is a big AIG saved in '<src>' which produces a wrong behaviour\n" \
"when given as argument to the program or shell script '<run>'.\n" \
"You can now use 'aigdd' to produce a copy '<dst>' of '<src>' in which\n" \
"as many literals as possible are removed while still producing\n" \
"the same exit code when running '<run> <dst>'.  Literals are actually\n" \
"removed by assigning them to a constant.  This in effect removes inputs,\n" \
"latches and ANDs.  The number of outputs is currently not changed, but\n" \
"individual outputs are set to constants.\n" \
"\n" \
"If '<dst>' is an AIG\n" \
"in ASCII format, by specifying a '.aag' extension, then the 'holes'\n" \
"left by removed literals are not squeezed out, while in the binary\n" \
"this is enforced.\n" \
"\n" \
"As a typical example consider that you have a new structural SAT solver\n" \
"'solve' that reads AIGs.  On one AIG it fails with an assertion\n" \
"failure.  You save this AIG in '/tmp/fail.aig'.  To shrink\n" \
"this file while still producing a failure you could just use\n" \
"\n" \
"  aigdd /tmp/fail.aig /tmp/shrunken.aig solve\n" \
"\n" \
"Unless your solver produces the same exit code for a correct run, this\n" \
"should give a compact easier to analyze AIG in '/tmp/shrunken.aig'.\n"

#if 1
#define CMD "exec %s %s 1>/dev/null 2>/dev/null"
#else
#define CMD "exec %s %s"
#endif

static int
min (int a, int b)
{
  return a < b ? a : b;
}

int
main (int argc, char ** argv)
{
  int i, changed, delta, j, expected, res, last, outof;
  const char * src_name, * run_name, * err;
  char * cmd;

  src_name = dst_name = run_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-v"))
	{
	  verbose++;
	}
      else if (src_name && dst_name && run_name)
	die ("more than three files");
      else if (dst_name)
	run_name = argv[i];
      else if (src_name)
	dst_name = argv[i];
      else
	src_name = argv[i];
    }

  if (!src_name || !dst_name)
    die ("expected exactly two files");

  if (!run_name)
    run_name = "./run";

  cmd = malloc (strlen (src_name) + strlen (run_name) + strlen (CMD) + 1);
  sprintf (cmd, CMD, run_name, src_name);
  expected = system (cmd);
  msg (1, "'%s' returns %d", cmd, expected);
  free (cmd);

  cmd = malloc (strlen (dst_name) + strlen (run_name) + strlen (CMD) + 1);
  sprintf (cmd, CMD, run_name, dst_name);

  src = aiger_init ();
  if ((err = aiger_open_and_read_from_file (src, src_name)))
    die ("%s: %s", src_name, err);

  stable = malloc (sizeof (stable[0]) * (src->maxvar + 1));
  unstable = malloc (sizeof (unstable[0]) * (src->maxvar + 1));

  for (i = 0; i <= src->maxvar; i++)
    stable[i] = 2 * i;

  copy_stable_to_unstable ();
  write_unstable_to_dst ();

  res = system (cmd);
  if (res != expected)
    die ("return value of copy differs (%d instead of %d)", res, expected);

  for (delta = src->maxvar; delta; delta = (delta == 1) ? 0 : (delta + 1)/2)
    {
      i = 1;

      do {
	for (j = 1; j < i; j++)
	  unstable[j] = stable[j];

	changed = 0;
	last = min (i + delta - 1, src->maxvar);
	outof = last - i + 1;
	for (j = i; j <= last; j++)
	  {
	    if (stable[j])		/* replace '1' by '0' as well */
	      {
		unstable[j] = 0;
		changed++;
	      }
	    else
	      unstable[j] = 0;		/* always favor 'zero' */
	  }

	if (changed)
	  {
	    for (j = i + delta; j <= src->maxvar; j++)
	      unstable[j] = stable[j];

	    write_unstable_to_dst ();
	    res = system (cmd);
	    if (res == expected)
	      {
		msg (1, "[%d,%d] set to 0 (%d out of %d)",
		     i, last, changed, outof);

		for (j = i; j <= last; j++)
		  stable[j] = unstable[j];
	      }
	    else			/* try setting to 'one' */
	      {
		msg (2, "[%d,%d] can not be set to 0 (%d out of %d)",
		     i, last, changed, outof);

		for (j = 1; j < i; j++)
		  unstable[j] = stable[j];

		changed = 0;
		for (j = i; j <= last; j++)
		  {
		    if (stable[j])
		      {
			if (stable[j] > 1)
			  {
			    unstable[j] = 1;
			    changed++;
			  }
			else
			  unstable[j] = 1;
		      }
		    else
		      unstable[j] = 0;	/* always favor '0' */
		  }

		if (changed)
		  {
		    for (j = i + delta; j <= src->maxvar; j++)
		      unstable[j] = stable[j];

		    write_unstable_to_dst ();
		    res = system (cmd);
		    if (res == expected)
		      {
			msg (1, "[%d,%d] set to 1 (%d out of %d)",
			     i, last, changed, outof);

			for (j = i; j < i + delta && j <= src->maxvar; j++)
			  stable[j] = unstable[j];
		      }
		    else
		      msg (2, "[%d,%d] can neither be set to 1 (%d out of %d)",
			   i, last, changed, outof);
		  }
	      }
	  }
	else
	  msg (3, "[%d,%d] stabilized to 0", i, last);

	i += delta;
      } while (i <= src->maxvar);
    }

  copy_stable_to_unstable ();
  write_unstable_to_dst ();

  changed = 0;
  for (i = 1; i <= src->maxvar; i++)
    if (stable[i] <= 1)
      changed++;

  msg (1, "%.1f%% literals removed (%d out of %d)",
       src->maxvar ? changed * 100.0 / src->maxvar : 0,
       changed, src->maxvar);

  free (stable);
  free (unstable);
  free (cmd);
  aiger_reset (src);

  return 0;
}
