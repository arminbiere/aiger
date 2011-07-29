/***************************************************************************
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.
Copyright (c) 2006, Marc Herbstritt, University of Freiburg

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
static int verbose;
static char buffer[20];

static const char *
on (unsigned i)
{
  assert (mgr && i < mgr->num_outputs);
  if (mgr->outputs[i].name)
    return mgr->outputs[i].name;

  sprintf (buffer, "o%u", i);

  return buffer;
}

static void
ps (const char *str)
{
  fputs (str, file);
}

static int
isblifsymchar (int ch)
{
  if (isspace (ch)) return 0;
  if (ch == '.') return 0;
  if (ch == '\\') return 0;
  if (!isprint (ch)) return 0;
  return 1;
}

static void
print_mangled (const char * name, FILE * file) 
{
  const char * p;
  char ch;
  for (p = name; (ch = *p); p++)
    if (isblifsymchar (ch))
      fputc (ch, file);
    else
      fprintf (file, "\\%0X", ch);
}

static int require_const0;
static int require_const1;

static void
pl (unsigned lit)
{
  const char *name;
  char ch;
  int i;

  if (lit == 0) {
    ps ("c0"); require_const0 = 1;
  } else if (lit == 1) {
    ps ("c1"); require_const1 = 1;
  } else if ((lit & 1))
    putc ('!', file), pl (lit - 1);
  else if ((name = aiger_get_symbol (mgr, lit)))
    {
      print_mangled (name, file);
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
  unsigned i, j, latch_helper_cnt;
  const char *src, *dst, *error;
  int *latch_helper = 0;
  int res, strip, ag;
  require_const0 = 0;
  require_const1 = 0;
  latch_helper_cnt = 0;
  src = dst = 0;
  strip = 0;
  res = 0;
  ag = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr,
		   "usage: aigtoblif [-p <prefix>][-h][-s][src [dst]]\n");
	  exit (0);
	}
      if (!strcmp (argv[i], "-s"))
	strip = 1;
      else if (!strcmp (argv[i], "-v"))
	verbose++;
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "=[aigtoblif] invalid option '%s'\n", argv[i]);
	  exit (1);
	}
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	{
	  fprintf (stderr, "=[aigtoblif] too many files\n");
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
      fprintf (stderr, "=[aigtoblif] %s\n", error);
      res = 1;
    }
  else
    {
      if (dst)
	{
	  if (!(file = fopen (dst, "w")))
	    {
	      fprintf (stderr, "=[aigtoblif] failed to write to '%s'\n", dst);
	      exit (1);
	    }
	}
      else
	file = stdout;

      if (strip)
	aiger_strip_symbols_and_comments (mgr);
      else
	setupcount ();

      ps (".model "), ps (src ? src : "stdin"), ps ("\n");
      fputs (".inputs ", file);
      for (i = 0; i < mgr->num_inputs; i++)
	{
	  pl (mgr->inputs[i].lit), ps (" ");

	  if ((i + 1) % 10 == 0 && (i < (mgr->num_inputs - 1)))
	    ps ("\\\n");
	}
      ps ("\n");
      fputs (".outputs ", file);
      for (i = 0; i < mgr->num_outputs; i++)
	{
	  if (verbose > 1)
	    fprintf (stderr,
		     "=[aigtoblif] output '%d' "
		     "has symbol '%s' with AIGER index %d\n",
		     (i + 1), on (i), mgr->outputs[i].lit);

	  ps (on (i)), ps (" ");

	  if ((i + 1) % 10 == 0 && (i < (mgr->num_outputs - 1)))
	    ps ("\\\n");
	}
      ps ("\n");

      /* this is a non-efficient hack for assuring that BLIF-inverters
       * are only inserted once even when multiple latches have the same
       * next-state-function!
       * latch_helper[i] stores the i-th AIG-index for which a INV has to
       * be inserted. checking duplicates is simply done by comparing 
       * a new potential INV with all other INV that must already be inserted!
       */
      latch_helper = calloc (mgr->num_latches, sizeof (latch_helper[0]));
      for (i = 0; i < mgr->num_latches; i++)
	{
	  latch_helper[i] = 0;
	  if (mgr->latches[i].next == aiger_false)
	    {
	      require_const0 = 1;
	    }
	  if (mgr->latches[i].next == aiger_true)
	    {
	      require_const1 = 1;
	    }

	  /* this case normally makes no sense, but you never know ... */
	  if (mgr->latches[i].next == aiger_false ||
	      mgr->latches[i].next == aiger_true)
	    {
	      ps (".latch "),
		(mgr->latches[i].next == aiger_false) ? ps ("c0") : ps ("c1"),
		ps (" "), pl (mgr->latches[i].lit), ps (" 0\n");
	    }
	  /* this should be the general case! */
	  else
	    {
	      if (!aiger_sign (mgr->latches[i].next))
		{
		  ps (".latch "), pl (mgr->latches[i].next), ps (" "),
		    pl (mgr->latches[i].lit), ps (" 0\n");
		}
	      else
		{
		  /* add prefix 'n' to inverted AIG nodes. 
		   * corresponding inverters are inserted below!
		   */
		  ps (".latch n"), pl (aiger_strip (mgr->latches[i].next)),
		    ps (" "), pl (mgr->latches[i].lit), ps (" 0\n");
		  int already_done = 0;
		  for (j = 0; j < latch_helper_cnt && !already_done; j++)
		    {
		      if (mgr->latches[latch_helper[j]].next ==
			  mgr->latches[i].next)
			{
			  already_done = 1;
			}
		    }
		  if (!already_done)
		    {
		      latch_helper[latch_helper_cnt] = i;
		      latch_helper_cnt++;
		    }
		}
	    }
	}

      for (i = 0; i < mgr->num_ands; i++)
	{
	  aiger_and *n = mgr->ands + i;

	  unsigned rhs0 = n->rhs0;
	  unsigned rhs1 = n->rhs1;

	  ps (".names "), pl (aiger_strip (rhs0)), ps (" "),
	    pl (aiger_strip (rhs1)), ps (" "), pl (n->lhs), ps ("\n");
	  aiger_sign (rhs0) ? ps ("0") : ps ("1");
	  aiger_sign (rhs1) ? ps ("0") : ps ("1");
	  ps (" 1\n");
	}

      /* for those outputs having an inverted AIG node, insert an INV,
       * otherwise just a BUF
       */
      for (i = 0; i < mgr->num_outputs; i++)
	{
	  if (verbose > 1)
	    fprintf (stderr,
		     "=[aigtoblif] output '%d' "
		     "has symbol '%s' with AIGER index %d\n",
		     (i + 1), on (i), mgr->outputs[i].lit);

	  /* this case normally makes no sense, but you never know ... */
	  if (mgr->outputs[i].lit == aiger_false ||
	      mgr->outputs[i].lit == aiger_true)
	    {
	      ps (".names ");
	      ((mgr->outputs[i].lit ==
		aiger_false) ? ps ("c0 ") : ps ("c1 ")),
		ps (on (i)), ps ("\n"), ps ("1 1\n");
	      (mgr->outputs[i].lit == aiger_false) ? (require_const0 =
						      1) : (require_const1 =
							    1);
	    }
	  /* this should be the general case! */
	  else if (aiger_sign (mgr->outputs[i].lit))
	    {
	      ps (".names ");
	      pl (aiger_strip (mgr->outputs[i].lit));
	      ps (" ");
	      ps (on (i));
	      ps ("\n");
	      ps ("0 1\n");
	    }
	  else
	    {
	      ps (".names "), pl (aiger_strip (mgr->outputs[i].lit)),
		ps (" "), ps (on (i)), ps ("\n"), ps ("1 1\n");
	    }
	}

      /* for those latches having an inverted AIG node as next-state-function, 
       * insert an INV. these latches were already saved in latch_helper!
       */
      for (i = 0; i < latch_helper_cnt; i++)
	{
	  unsigned l = latch_helper[i];
	  if (mgr->latches[l].next != aiger_false &&
	      mgr->latches[l].next != aiger_true)
	    {
	      assert (aiger_sign (mgr->latches[l].next));
	      ps (".names "), pl (aiger_strip (mgr->latches[l].next)),
		ps (" n"), pl (aiger_strip (mgr->latches[l].next)),
		ps ("\n"), ps ("0 1\n");
	    }
	}

      /* insert constants when necessary */
      if (require_const0)
	{
	  ps (".names c0\n");
	}
      if (require_const1)
	{
	  ps (".names c1\n"), ps ("1\n");
	}
      fputs (".end\n", file);

      /* close file */
      if (dst)
	fclose (file);
    }

  aiger_reset (mgr);

  free (latch_helper);

  return res;
}
