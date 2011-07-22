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

/* This utility 'poormanaigtocnf' is an example on how an AIG in binary
 * AIGER format can be read easily if a third party tool can not use the
 * AIGER library.  It even supports files compressed with 'gzip'.  Error
 * handling is complete but diagnostics could be more detailed.
 *
 * In principle reading can be further speed up, by for instance using
 * 'fread'.  In our experiments this gave a factor of sometimes 5 if no
 * output is produced ('--read-only').  However, we want to keep this
 * implementation simple and clean and writing the CNF dominates the overall
 * run time clearly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

static unsigned M, I, L, O, A;

static int read_only;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** poormanaigtocnf: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static int
u2i (unsigned l)
{
  /* We need one more literal in the CNF for TRUE.  This is the first
   * after the original literals in the AIG file.  Signs of literals in the
   * AIGER format are given by the LSB, while for DIMACS it is the sign.
   */
  if (l == 0)
    return -(M + 1);
  
  if (l == 1)
    return M + 1;	

  return ((l & 1) ? -1 : 1) * (l >> 1);
}

/* Print a unary clause.
 */
static void
c1 (unsigned a)
{
  if (!read_only)
    printf ("%d 0\n", u2i (a));
}

/* Print a binary clause.
 */
static void
c2 (unsigned a, unsigned b)
{
  if (!read_only)
    printf ("%d %d 0\n", u2i (a), u2i (b));
}

/* Print a ternary clause.
 */
static void
c3 (unsigned a, unsigned b, unsigned c)
{
  if (!read_only)
    printf ("%d %d %d 0\n", u2i (a), u2i (b), u2i (c));
}

static unsigned char
get (FILE * file)
{
  int ch = getc (file);
  if (ch == EOF)
    die ("unexpected end of file");
  return (unsigned char) ch;
}

static unsigned
decode (FILE * file)
{
  unsigned x = 0, i = 0;
  unsigned char ch;

  while ((ch = get (file)) & 0x80)
    x |= (ch & 0x7f) << (7 * i++);

  return x | (ch << (7 * i));
}

int
main (int argc, char ** argv)
{
  int close_file = 0, pclose_file = 0, verbose = 0;
  unsigned i, l, sat, lhs, rhs0, rhs1, delta;
  FILE * file = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, 
	           "usage: "
		   "poormanaigtocnf [-h][-v][--read-only][file.aig[.gz]]\n");
	  exit (0);
	}
      else if (!strcmp (argv[i], "--read-only"))
	read_only = 1;
      else if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (file)
	die ("more than one file specified");
      else if ((l = strlen (argv[i])) > 2 && !strcmp (argv[i] + l - 3, ".gz"))
	{
	  char * cmd = malloc (l + 20);
	  sprintf (cmd, "gunzip -c %s", argv[i]);
          if (!(file = popen (cmd, "r")))
	    die ("failed to open gzipped filed '%s' for reading", argv[i]);
	  free (cmd);
	  pclose_file = 1;
	}
      else if (!(file = fopen (argv[i], "r")))
	die ("failed to open '%s' for reading", argv[i]);
      else
	close_file = 1;
    }

  if (!file)
    file = stdin;

  if (fscanf (file, "aig %u %u %u %u %u\n", &M, &I, &L, &O, &A) != 5)
    die ("invalid header");

  if (verbose)
    fprintf (stderr, "[poormanaigtocnf] aig %u %u %u %u %u\n", M, I, L, O, A);

  if (L)
    die ("can not handle sequential models");

  if (O != 1)
    die ("expected exactly one output");

  if (fscanf (file, "%u", &sat) != 1)
    die ("failed to read single output literal");

  /* NOTE: do not put the '\n' in the 'fscanf' format string above, since it
   * results in skipping additional white space if by chance the first
   * binary character is actually a white space character.
   */
  if (getc (file) != '\n')
    die ("no new line after output");

  if (!read_only)
    printf ("p cnf %u %u\n", M + 1, A * 3 + 2);

  for (lhs = 2 * (I + L + 1); A--; lhs += 2)
    {
      delta = decode (file);
      if (delta >= lhs)
	die ("invalid byte encoding of 1st RHS of %u", lhs);
      rhs0 = lhs - delta;

      delta = decode (file);
      if (delta > rhs0)
	die ("invalid byte encoding of 2nd RHS of %u", lhs);
      rhs1 = rhs0 - delta;

      c2 (lhs^1, rhs0);
      c2 (lhs^1, rhs1);
      c3 (lhs, rhs0^1, rhs1^1);
    }

  assert (lhs == 2 * (M + 1));

  c1 (lhs);	/* true */
  c1 (sat);	/* output */

  if (close_file)
    fclose (file);

  if (pclose_file)
    pclose (file);

  return 0;
}
