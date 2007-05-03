/***************************************************************************
Copyright (c) 2006-2007, Armin Biere, Johannes Kepler University.

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
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

static aiger *model;

static const char *solution_file_name;
static FILE *solution_file;
static int close_solution_file;
static int lineno;
static int prev;

static int *solution;
static unsigned size_solution;
static unsigned count_solution;

static int size_assignment;
static int *assignment;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** soltostim: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  exit (1);
}

static void
push (int lit)
{
  if (count_solution == size_solution)
    {
      size_solution = size_solution ? 2 * size_solution : 1;
      solution = realloc (solution, sizeof (solution[0]) * size_solution);
    }

  solution[count_solution++] = lit;
}

static int
next (void)
{
  int ch = getc (solution_file);
  if (prev == '\n')
    lineno++;
  prev = ch;
  return ch;
}

static int
match (const char *str)
{
  const char *p;

  for (p = str; *p; p++)
    if (*p != next ())
      return 0;

  return 1;
}

static void
perr (const char *msg)
{
  die ("%s: line %d: %s", solution_file_name, lineno, msg);
}

static void
parse (void)
{
  int ch, lit, sign;

  lineno = 0;
  prev = '\n';

SKIP_COMMENTS_UNTIL_SOLUTION_START:

  ch = next ();
  if (ch == 's')
    goto SOLUTION_START;

  if (ch == 'c')
    {
      while ((ch = next ()) != '\n' && ch != EOF)
	;

      if (ch == EOF)
	die ("%s: no solution line found", solution_file_name);

      goto SKIP_COMMENTS_UNTIL_SOLUTION_START;
    }

  perr ("expected 's' or 'c' at line start");

SOLUTION_START:

  ch = next ();
  if (ch != ' ')
  INVALID_SOLUTION_LINE:
    perr ("invalid solution line");

  ch = next ();

  if (ch != 'S')
    {
      if (ch == 'U')
	{
	  ch = next ();
	  if (ch != 'N')
	    goto INVALID_SOLUTION_LINE;

	  ch = next ();
	  if (ch == 'K')
	    {
	      if (!match ("NOWN\n"))
		goto INVALID_SOLUTION_LINE;

	    EXPECTED_SATISFIABLE_SOLUTION:
	      perr ("expected 's SATISFIABLE'");
	    }
	  else if (ch == 'S')
	    {
	      if (!match ("ATISFIABLE\n"))
		goto INVALID_SOLUTION_LINE;

	      goto EXPECTED_SATISFIABLE_SOLUTION;
	    }
	  else
	    goto INVALID_SOLUTION_LINE;
	}
      else
	goto INVALID_SOLUTION_LINE;
    }

  if (!match ("ATISFIABLE\n"))
    goto INVALID_SOLUTION_LINE;

SCAN_SOLUTION:
  ch = next ();
  if (ch != 'v')
  UNTERMINATED_SOLUTION:
    perr ("terminating '0' missing");

SCAN_LITERAL:
  ch = next ();

SCAN_LITERAL_AFTER_READING_CH:
  if (ch == ' ' || ch == '\t')
    goto SCAN_LITERAL;

  if (ch == EOF)
    goto UNTERMINATED_SOLUTION;

  if (ch == '\n')
    goto SCAN_SOLUTION;

  if (ch == '-')
    {
      sign = -1;
      ch = next ();
    }
  else
    sign = 1;

  if (!isdigit (ch))
    perr ("expected literal");

  lit = ch - '0';
  while (isdigit (ch = next ()))
    lit = 10 * lit + (ch - '0');

  if (!lit)
    return;

  lit *= sign;
  push (lit);

  goto SCAN_LITERAL_AFTER_READING_CH;
}

static void
assign (void)
{
  int i, tmp;

  size_assignment = 0;
  for (i = 0; i < count_solution; i++)
    {
      tmp = abs (solution[i]);
      if (size_assignment < tmp)
	size_assignment = tmp;
    }

  size_assignment++;
  assignment = calloc (size_assignment, sizeof (assignment[0]));

  for (i = 0; i < count_solution; i++)
    {
      tmp = solution[i];
      assignment[abs (tmp)] = tmp;
    }
}

static int
deref (unsigned idx)
{
  assert (idx);
  assert (idx <= model->maxvar);

  return idx >= size_assignment ? 0 : assignment[idx];
}

static void
print (void)
{
  unsigned i, idx;
  char ch;
  int tmp;

  for (i = 0; i < model->num_inputs; i++)
    {
      idx = model->inputs[i].lit / 2;
      tmp = deref (idx);
      if (tmp < 0)
	ch = '0';
      else if (tmp > 0)
	ch = '1';
      else
	ch = 'x';

      fputc (ch, stdout);
    }

  fputc ('\n', stdout);
}

static const char *USAGE =
  "usage: soltostim [-h] <aiger-model> [ <dimacs-solution> ]\n";

int
main (int argc, char **argv)
{
  const char *model_file_name, *err;
  int i;

  solution_file_name = model_file_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stdout);
	  exit (1);
	}
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (solution_file_name)
	die ("more than two files on command line");
      else if (model_file_name)
	solution_file_name = argv[i];
      else
	model_file_name = argv[i];
    }

  if (!model_file_name)
    die ("no model specified");

  model = aiger_init ();
  if ((err = aiger_open_and_read_from_file (model, model_file_name)))
    die ("%s: %s", model_file_name, err);

  if (solution_file_name)
    {
      if (!(solution_file = fopen (solution_file_name, "r")))
	die ("failed to read '%s'", solution_file_name);

      close_solution_file = 1;
    }
  else
    {
      solution_file = stdin;
      solution_file_name = "<stdin>";
    }

  parse ();
  assign ();
  print ();

  if (close_solution_file)
    fclose (solution_file);

  aiger_reset (model);
  free (solution);
  free (assignment);

  return 0;
}
