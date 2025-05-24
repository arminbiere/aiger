/***************************************************************************
Copyright (c) 2024, Armin Biere, University of Freiburg.
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

#define USAGE \
"usage: aigdd [-h][-v][-r] <src> <dst> <run> [ <run-options> ...]\n" \
"\n" \
"This 'delta debugger' for AIGs has the following options:\n" \
"\n" \
"  -h     prints this command line option summary\n" \
"  -v     increases verbose level (default 0, max 3)\n" \
"  -r     reencode and remove holes even if <dst> is in ASCII format\n" \
"  <src>  source file in AIGER format\n" \
"  <dst>  destination file in AIGER format\n" \
"  <run>  executable\n" \
"\n" \
"The idea is that '<run> <src>' produces a fault, e.g. you know that\n" \
"there is a big AIG saved in '<src>' which produces a wrong behaviour\n" \
"when given as argument to the program '<run>'.\n" \
"\n" \
"You can now use 'aigdd' to produce a copy '<dst>' of '<src>' in which\n" \
"as many literals as possible are removed while still producing\n" \
"the same exit code when running '<run> <dst>'.  Literals are actually\n" \
"removed by assigning them to a constant.  This in effect removes inputs,\n" \
"latches and ANDs.  The number of outputs is currently not changed, but\n" \
"individual outputs are set to constants.\n" \
"\n" \
"If '<dst>' is an AIG in ASCII format, by specifying a '.aag' extension,\n" \
"then the 'holes' left by removed literals are not squeezed out, while\n" \
"in the binary this is enforced.\n" \
"\n" \
"As a typical example consider that you have a new structural SAT solver\n" \
"'solve' that reads AIGs.  On one AIG it fails with an assertion\n" \
"failure.  You save this AIG in '/tmp/fail.aig'.  To shrink\n" \
"this file while still producing a failure you could just use\n" \
"\n" \
"  aigdd /tmp/fail.aig /tmp/shrunken.aig solve\n" \
"\n" \
"Unless your solver produces the same exit code for a correct run, this\n" \
"should give a compact easier to analyze AIG in '/tmp/shrunken.aig'.\n" \
"\n" \
"The '<dst>' will always have the smallest failure inducing input sofar.\n" \

#include "aiger.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>           /* for WEXITSTATUS */

static aiger *src;
static const char *dst_name;
static char tmp_name[80];
static unsigned *unstable;
static unsigned *stable;
static char * fixed;
static char * outputs;
static char * bad;
static char * constraints;
static char * justice;
static char * fairness;
static int verbose;
static int reencode;
static int removeopts;
static int runs;
static int *values;
static char **options;
static int nopts;
static int szopts;

static void
msg (int level, const char *fmt, ...)
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
die (const char *fmt, ...)
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
  unsigned idx = lit / 2, sign = lit & 1, tmp, res, tmp0, tmp1;
  aiger_and * and;

  tmp = unstable[idx];

  if (tmp == 2 * idx)
    {
      if (!fixed [idx])
	{
	  and = aiger_is_and (src, 2 * idx);

	  if (and)
	    {
	      tmp0 = deref (and->rhs0);
	      tmp1 = deref (and->rhs1);

	      if (!tmp0 || !tmp1)
		tmp = 0;
	      else if (tmp0 == 1)
		tmp = tmp1;
	      else if (tmp1 == 1)
		tmp = tmp0;
	    }
	}
    }
  else
    tmp = deref (tmp);

  unstable[idx] = tmp;
  fixed[idx] = 1;
  res = tmp ^ sign;

  return res;
}

static void
write_unstable (const char * name)
{
  int print_progress = (name == dst_name);
  aiger_symbol *symbol;
  unsigned i, j, lit;
  aiger_and *and;
  aiger *dst;
  char comment[120];

  memset (fixed, 0, src->maxvar + 1);

  dst = aiger_init ();

  for (i = 0; i < src->num_inputs; i++)
    {
      symbol = src->inputs + i;
      lit = symbol->lit;
      if (deref (lit) != lit) continue;
      aiger_add_input (dst, lit, symbol->name);
      if (symbol->reset)				// TODO delta debug
        aiger_add_reset (dst, lit, symbol->reset);
    }

  for (i = 0; i < src->num_latches; i++)
    {
      symbol = src->latches + i;
      lit = symbol->lit;
      if (deref (lit) != lit) continue;
      aiger_add_latch (dst, lit, deref (symbol->next), symbol->name);
      if (symbol->reset <= 1)
	aiger_add_reset (dst, lit, symbol->reset);
      else
	{
	  assert (symbol->reset == lit);
	aiger_add_reset (dst, lit, lit);
	}
    }

  for (i = 0; i < src->num_ands; i++)
    {
      and = src->ands + i;
      if (deref (and->lhs) == and->lhs)
	aiger_add_and (dst, and->lhs, deref (and->rhs0), deref (and->rhs1));
    }

  for (i = 0; i < src->num_outputs; i++)
    {
      if (!outputs[i]) continue;
      symbol = src->outputs + i;
      aiger_add_output (dst, deref (symbol->lit), symbol->name);
    }

  for (i = 0; i < src->num_bad; i++)
    {
      if (!bad[i]) continue;
      symbol = src->bad + i;
      aiger_add_bad (dst, deref (symbol->lit), symbol->name);
    }

  for (i = 0; i < src->num_constraints; i++)
    {
      if (!constraints[i]) continue;
      symbol = src->constraints + i;
      aiger_add_constraint (dst, deref (symbol->lit), symbol->name);
    }

  for (i = 0; i < src->num_justice; i++)
    {
      unsigned * lits;
      if (!justice[i]) continue;
      symbol = src->justice + i;
      lits = malloc (symbol->size * sizeof *lits);
      for (j = 0; j < symbol->size; j++)
	lits[j] = deref (symbol->lits[j]);
      aiger_add_justice (dst, symbol->size, lits, symbol->name);
      free (lits);
    }

  for (i = 0; i < src->num_fairness; i++)
    {
      if (!fairness[i]) continue;
      symbol = src->fairness + i;
      aiger_add_fairness (dst, deref (symbol->lit), symbol->name);
    }

  for (i = 0; i < nopts; i++)
    if (options[i]) {
      sprintf (comment, "--%s=%d", options[i], values[i]);
      aiger_add_comment (dst, comment);
    }


  assert (!aiger_check (dst));

  if (reencode)
    aiger_reencode (dst);

  unlink (name);
  if (!aiger_open_and_write_to_file (dst, name))
    die ("failed to write '%s'", name);

  if (print_progress)
    {
      assert (name == dst_name);
      msg (2, "wrote '%s' MILOABCJF %u %u %u %u %u %u %u %u %u",
           name, 
	   dst->maxvar,
	   dst->num_inputs,
	   dst->num_latches,
	   dst->num_outputs,
	   dst->num_ands,
	   dst->num_bad,
	   dst->num_constraints,
	   dst->num_justice,
	   dst->num_fairness);
    }
  else 
    assert (name == tmp_name);

  aiger_reset (dst);
}

static void
copy_stable_to_unstable_and_write_dst_name (void)
{
  unsigned i;

  for (i = 0; i <= src->maxvar; i++)
    unstable[i] = stable[i];
  msg (2, "writing '%s'", dst_name);
  write_unstable (dst_name);
}

#define CMDSUFFIX " 1>/dev/null 2>/dev/null"

static char *
strapp (char * str, const char * suffix) {
  return strcat (realloc (str, strlen (str) + strlen (suffix) + 1), suffix);
}

static int
run (const char * cmd, const char * name)
{
  int res;
  char * fullcmd = strdup (cmd);
  fullcmd = strapp (fullcmd, " ");
  fullcmd = strapp (fullcmd, name);
  fullcmd = strapp (fullcmd, CMDSUFFIX);
  runs++;
  msg (3, "full command '%s'", fullcmd);
  res = system (fullcmd);
  free (fullcmd);
  return WEXITSTATUS (res);
}

static int
write_and_run_unstable (const char * cmd)
{
  write_unstable (tmp_name);
  return run (cmd, tmp_name);
}

static int
min (int a, int b)
{
  return a < b ? a : b;
}

int
main (int argc, char **argv)
{
  int i, changed, delta, j, expected, res, last, outof;
  const char *src_name, *err;
  char * cmd;

  src_name = dst_name = cmd = 0;

  for (i = 1; i < argc; i++)
    {
      if (!src_name && !strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (!src_name && !strcmp (argv[i], "-v"))
	verbose++;
      else if (!src_name && !strcmp (argv[i], "-r"))
	reencode = 1;
      else if (!src_name && !strcmp(argv[i], "-o"))
	removeopts = 1;
      else if (src_name && dst_name && cmd)
	cmd = strapp (strapp (cmd, " "), argv[i]);
      else if (dst_name)
	cmd = strdup (argv[i]);
      else if (src_name)
	dst_name = argv[i];
      else
	src_name = argv[i];
    }

  if (!src_name || !dst_name)
    die ("expected exactly two files");

  if (!cmd)
    die ("name of executable missing");

  expected = run (cmd, src_name);
  msg (1, "'%s %s' returns %d", cmd, src_name, expected);

  src = aiger_init ();
  if ((err = aiger_open_and_read_from_file (src, src_name)))
    die ("%s: %s", src_name, err);

  msg (2, "read '%s' MILOABCJF %u %u %u %u %u %u %u %u %u",
       src_name, 
       src->maxvar,
       src->num_inputs,
       src->num_latches,
       src->num_outputs,
       src->num_ands,
       src->num_bad,
       src->num_constraints,
       src->num_justice,
       src->num_fairness);

  stable = malloc (sizeof (stable[0]) * (src->maxvar + 1));
  unstable = malloc (sizeof (unstable[0]) * (src->maxvar + 1));
  fixed = malloc (src->maxvar + 1);

  outputs = malloc (src->num_outputs + sizeof *outputs);
  bad = malloc (src->num_bad + sizeof *bad);
  constraints = malloc (src->num_constraints + sizeof *constraints);
  justice = malloc (src->num_justice + sizeof *justice);
  fairness = malloc (src->num_fairness + sizeof *fairness);

  for (i = 0; i <= src->maxvar; i++)
    stable[i] = 2 * i;
  for (i = 0; i < src->num_outputs; i++)
    outputs[i] = 1;
  for (i = 0; i < src->num_bad; i++)
    bad[i] = 1;
  for (i = 0; i < src->num_constraints; i++)
    constraints[i] = 1;
  for (i = 0; i < src->num_justice; i++)
    justice[i] = 1;
  for (i = 0; i < src->num_fairness; i++)
    fairness[i] = 1;

  {
    nopts = szopts = 0;
    int  val, szbuf = 32, nbuf = 0;
    char *buffer = 0, **p, *c;
    options = malloc (nopts * sizeof *options);
    values = malloc (nopts * sizeof *values);
    buffer = malloc (szbuf);
    for (p = src->comments; (c = *p); p++) {
      if (*c++ != '-' || *c++ != '-')
        continue;
      nbuf = 0;
      while (isalnum(*c) || *c == '-' || *c == '_') {
        if (nbuf + 1 >= szbuf)
          buffer = realloc(buffer, szbuf = szbuf ? 2 * szbuf : 2);
        buffer[nbuf++] = *c++;
        buffer[nbuf] = 0;
      }
      if (*c++ != '=')
        continue;
      val = 0;
      while (isdigit(*c)) {
        val = 10 * val + (*c++ - '0');
      }
      msg(2, "parsed embedded option: --%s=%u", buffer, val);
      if (nopts >= szopts) {
        szopts = szopts ? 2 * szopts : 1;
        options = realloc(options, szopts * sizeof *options);
        values = realloc(values, szopts * sizeof *values);
      }
      options[nopts] = strdup(buffer);
      values[nopts] = val;
      nopts++;
    }
    free (buffer);
  }

  copy_stable_to_unstable_and_write_dst_name ();

  res = run (cmd, dst_name);
  msg (1, "'%s %s' returns %d", cmd, dst_name, res);

  if (res != expected)
    die ("exec code on copy '%s' of '%s' differs (%d instead of %d)", 
         dst_name, src_name, res, expected);

  sprintf (tmp_name, "/tmp/aigdd%d.aig", getpid ());

  msg (1, "using temporary file '%s'", tmp_name);

  for (delta = src->maxvar; delta; delta = (delta == 1) ? 0 : (delta + 1) / 2)
    {
      i = 1;

      do
	{
	  for (j = 1; j < i; j++)
	    unstable[j] = stable[j];

	  changed = 0;
	  last = min (i + delta - 1, src->maxvar);
	  outof = last - i + 1;
	  for (j = i; j <= last; j++)
	    {
	      if (stable[j])	/* replace '1' by '0' as well */
		{
		  unstable[j] = 0;
		  changed++;
		}
	      else
		unstable[j] = 0;	/* always favor 'zero' */
	    }

	  if (changed)
	    {
	      for (j = i + delta; j <= src->maxvar; j++)
		unstable[j] = stable[j];

	      res = write_and_run_unstable (cmd);
	      if (res == expected)
		{
		  msg (1, "[%d,%d] set to 0 (%d out of %d)",
		       i, last, changed, outof);

		  for (j = i; j <= last; j++)
		    stable[j] = unstable[j];

		  copy_stable_to_unstable_and_write_dst_name ();
		}
	      else		/* try setting to 'one' */
		{
		  msg (3, "[%d,%d] can not be set to 0 (%d out of %d)",
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

		      res = write_and_run_unstable (cmd);
		      if (res == expected)
			{
			  msg (1, "[%d,%d] set to 1 (%d out of %d)",
			       i, last, changed, outof);

			  for (j = i; j < i + delta && j <= src->maxvar; j++)
			    stable[j] = unstable[j];

			  copy_stable_to_unstable_and_write_dst_name ();
			}
		      else
			msg (3,
			     "[%d,%d] can neither be set to 1 (%d out of %d)",
			     i, last, changed, outof);
		    }
		}
	    }
	  else
	    msg (3, "[%d,%d] stabilized to 0", i, last);

	  i += delta;
	}
      while (i <= src->maxvar);
    }

  copy_stable_to_unstable_and_write_dst_name ();

  if (src->num_outputs)
    {
      changed = 0;
      for (i = 0; i < src->num_outputs; i++)
	{
	  assert (outputs[i]);
	  outputs[i] = 0;
	  res = write_and_run_unstable (cmd);
	  if (res == expected)
	    {
	      msg (2, "removed output %d", i);
	      changed++;
	    }
	  else
	    {
	      msg (3, "can not remove output %d", i);
	      outputs[i] = 1;
	    }
	}
      if (changed)
	msg (1, "removed %d outputs", changed);

      copy_stable_to_unstable_and_write_dst_name ();
    }

  if (src->num_bad)
    {
      changed = 0;
      for (i = 0; i < src->num_bad; i++)
	{
	  assert (bad[i]);
	  bad[i] = 0;
	  res = write_and_run_unstable (cmd);
	  if (res == expected)
	    {
	      msg (2, "removed bad state property %d", i);
	      changed++;
	    }
	  else
	    {
	      msg (3, "can not remove bad state property %d", i);
	      bad[i] = 1;
	    }
	}
      if (changed)
	msg (1, "removed %d bad state properties", changed);

      copy_stable_to_unstable_and_write_dst_name ();
    }

  if (src->num_constraints)
    {
      changed = 0;
      for (i = 0; i < src->num_constraints; i++)
	{
	  assert (constraints[i]);
	  constraints[i] = 0;
	  res = write_and_run_unstable (cmd);
	  if (res == expected)
	    {
	      msg (2, "removed environment constraint %d", i);
	      changed++;
	    }
	  else
	    {
	      msg (3, "can not remove environment constraint %d", i);
	      constraints[i] = 1;
	    }
	}
      if (changed)
	msg (1, "removed %d environment constraints", changed);

      copy_stable_to_unstable_and_write_dst_name ();
    }

  if (src->num_justice)
    {
      changed = 0;
      for (i = 0; i < src->num_justice; i++)
	{
	  assert (justice[i]);
	  justice[i] = 0;
	  res = write_and_run_unstable (cmd);
	  if (res == expected)
	    {
	      msg (2, "removed justice property %d", i);
	      changed++;
	    }
	  else
	    {
	      msg (3, "can not remove justice property %d", i);
	      justice[i] = 1;
	    }
	}
      if (changed)
	msg (1, "removed %d justice property", changed);

      copy_stable_to_unstable_and_write_dst_name ();
    }

  if (src->num_fairness)
    {
      changed = 0;
      for (i = 0; i < src->num_fairness; i++)
	{
	  assert (fairness[i]);
	  fairness[i] = 0;
	  res = write_and_run_unstable (cmd);
	  if (res == expected)
	    {
	      msg (2, "removed fairness constraint %d", i);
	      changed++;
	    }
	  else
	    {
	      msg (3, "can not remove fairness constraint %d", i);
	      fairness[i] = 1;
	    }
	}
      if (changed)
	msg (1, "removed %d fairness constraint", changed);

      copy_stable_to_unstable_and_write_dst_name ();
    }

  if (nopts)
    {
      int i, val, removed, reduced, reductions, once, c, n, shift, delta;
      char * opt;

      n = 0;
      for (i = 0; i < nopts; i++)
        if (options[i])
          n++;

      removed = 0;
      if (removeopts)
        {
          c = 0;
          for (i = 0; i < nopts; i++)
            {
              if (!options[i])
                continue;

              c++;
              msg(2, "removed %d completed %d/%d\r", removed, c, n);

              opt = options[i];
              options[i] = 0;
              res = write_and_run_unstable (cmd);
              if (res != expected)
                {
                  removed++;
                  free (opt);
                }
              else
                options[i] = opt;
            }

          if (removed)
            {
              msg (2, "removed %d options", removed);
            }
          copy_stable_to_unstable_and_write_dst_name ();
        }

      c = 0;
      n -= removed;
      reductions = reduced = 0;

      for (i = 0; i < nopts; i++)
        {
          if (!options[i])
            continue;
          msg(2, "reduced %d completed %d/%d in %d reductions\r", reduced, c, n,
              reductions);
          shift = 1;
          once = 0;
          for (;;)
            {
              val = values[i];
              delta = val / (1 << shift);
              if (!delta) break;
              assert (abs (delta) < abs (val));
              values[i] -= delta;
              res = write_and_run_unstable (cmd);
              if (res != expected)
                {
                  values[i] = val;
                  shift++;
                }
              else
                {
                  once = 1;
                  reductions++;
                }
            }

          for (;;)
            {
              val = values[i];
              if (val > 0) values[i]--;
              else if (val < 0) values[i]++;
              else break;
              res = write_and_run_unstable (cmd);
              msg(1, "got %d expected %d", res, expected);
              if (res != expected)
                {
                  values[i] = val;
                  break;
                }
              else
                {
                  once = 1;
                  reductions++;
                }
            }

            if (once)
              reduced++;
        }

      copy_stable_to_unstable_and_write_dst_name ();

      if (reduced)
        msg (1, "reduced %d option values in %d reductions",
              reduced, reductions);
  }

  changed = 0;
  for (i = 1; i <= src->maxvar; i++)
    if (stable[i] <= 1)
      changed++;

  msg (1, "%.1f%% literals removed (%d out of %d)",
       src->maxvar ? changed * 100.0 / src->maxvar : 0, changed, src->maxvar);

  free (fairness);
  free (justice);
  free (constraints);
  free (bad);
  free (stable);
  free (unstable);
  free (fixed);
  free (cmd);
  free (values);
  for (i = 0; i < nopts; i++)
    free(options[i]);
  free (options);
  aiger_reset (src);
  unlink (tmp_name);

  return 0;
}
