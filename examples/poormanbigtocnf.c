#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

static unsigned m;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** poormanbigtocnf: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static int
u2i (unsigned l)
{
  if (l == 0)
    return -(m+1);
  
  if (l == 1)
    return m+1;

  return ((l & 1) ? -1 : 1) * (l >> 1);
}

static void
c1 (unsigned a)
{
  printf ("%d 0\n", u2i (a));
}

static void
c2 (unsigned a, unsigned b)
{
  printf ("%d %d 0\n", u2i (a), u2i (b));
}

static void
c3 (unsigned a, unsigned b, unsigned c)
{
  printf ("%d %d %d 0\n", u2i (a), u2i (b), u2i (c));
}

static unsigned char
get (FILE * file)
{
  int tmp = getc (file);
  if (tmp == EOF)
    die ("unexpected end of file");
  return (unsigned char) tmp;
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
  unsigned i, l, o, a, sat, lhs, rhs0, rhs1, delta;
  int close_file, pclose_file;
  FILE * file;

  close_file = 0;
  file = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: poormanbigtocnf [-h][file.big[.gz]]\n");
	  exit (0);
	}
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

  if (fscanf (file, "p big %u %u %u %u %u\n", &m, &i, &l, &o, &a) != 5)
    die ("invalid header");

  if (!l)
    die ("can not handle sequential models");

  if (o != 1)
    die ("expected exactly one output");

  if (fscanf (file, "%u\n", &sat) != 1)
    die ("failed to read output");

  printf ("p cnf %u %u\n", m + 1, a * 3 + 2);

  c1 (sat);	/* output */

  for (lhs = 2 * (i + l + 1); a--; lhs += 2)
    {
      delta = decode (file);
      assert (delta <= lhs);
      rhs0 = lhs - delta;
      delta = decode (file);
      assert (delta <= rhs0);
      rhs1 = rhs0 - delta;
      c2 (lhs^1, rhs0);
      c2 (lhs^1, rhs1);
      c3 (lhs, rhs0^1, rh0^1);
    }

  assert (lhs == 2 * (m + 1));

  c1 (lhs);	/* true */

  if (close_file)
    fclose (file);

  if (pclose_file)
    pclose (file);

  return 0;
}
