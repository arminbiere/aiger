/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/* Uncomment the following line to test a little endian 32 bit binary
 * encoding without deltas.  This is supposed to read faster.
 *
 */
#define DELTA_CODEC

#ifdef DELTA_CODEC
static unsigned char * data;
static unsigned char * next;
static unsigned char * end_of_data;
#else
static unsigned * data;
static unsigned * next;
#endif

static unsigned M, I, L, O, A;

static int read_only;

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
    return -(M + 1);
  
  if (l == 1)
    return M + 1;

  return ((l & 1) ? -1 : 1) * (l >> 1);
}

static void
c1 (unsigned a)
{
  if (!read_only)
    printf ("%d 0\n", u2i (a));
}

static void
c2 (unsigned a, unsigned b)
{
  if (!read_only)
    printf ("%d %d 0\n", u2i (a), u2i (b));
}

static void
c3 (unsigned a, unsigned b, unsigned c)
{
  if (!read_only)
    printf ("%d %d %d 0\n", u2i (a), u2i (b), u2i (c));
}

#ifdef DELTA_CODEC

static unsigned char
get (void)
{
  if (next == end_of_data)
    die ("unexpected end of file");
  return (unsigned char) *next++;
}

static unsigned
decode (void)
{
  unsigned x = 0, i = 0;
  unsigned char ch;

  while ((ch = get ()) & 0x80)
    x |= (ch & 0x7f) << (7 * i++);

  return x | (ch << (7 * i));
}

#endif

int
main (int argc, char ** argv)
{
  int close_file = 0, pclose_file = 0, verbose = 0;
  unsigned i, l, sat, lhs, rhs0, rhs1;
  FILE * file = 0;
  unsigned sum = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, 
	           "usage: "
		   "poormanbigtocnf [-h][-v][--read-only][file.big[.gz]]\n");
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

  if (fscanf (file, "big %u %u %u %u %u\n", &M, &I, &L, &O, &A) != 5)
    die ("invalid header");

  if (verbose)
    fprintf (stderr, "[poormanbigtocnf] big %u %u %u %u %u\n", M, I, L, O, A);

  if (L)
    die ("can not handle sequential models");

  if (O != 1)
    die ("expected exactly one output");

  if (fscanf (file, "%u\n", &sat) != 1)
    die ("failed to read single output literal");

  if (!read_only)
    printf ("p cnf %u %u\n", M + 1, A * 3 + 2);

  assert (sizeof (unsigned) == 4);
#ifdef DELTA_CODEC
  {
    unsigned bytes = A * 8 + (M >= (1 << 27));
    next = data = malloc (bytes);
    end_of_data = data + fread (data, 1, bytes, file);
  }
#else
  next = data = malloc (A * 8);
  if (fread (data, 4, A * 2, file) != A * 2)
    die ("failed to read binary data");
#endif
  for (lhs = 2 * (I + L + 1); A--; lhs += 2)
    {
#ifdef DELTA_CODEC
      unsigned delta = decode ();
      if (delta >= lhs)
	die ("invalid byte encoding");
      rhs0 = lhs - delta;

      delta = decode ();
      if (delta > rhs0)
	die ("invalid byte encoding");
      rhs1 = rhs0 - delta;
#else
      rhs0 = *next++;
      rhs1 = *next++;
#endif
      c2 (lhs^1, rhs0);
      c2 (lhs^1, rhs1);
      c3 (lhs, rhs0^1, rhs1^1);

      sum += rhs0;
      sum += rhs1;
    }

  assert (lhs == 2 * (M + 1));

  c1 (lhs);	/* true */
  c1 (sat);	/* output */

  if (close_file)
    fclose (file);

  if (pclose_file)
    pclose (file);

  free (data);
#if 1
  if (verbose)
    fprintf (stderr, "[poormanbigtocnf] sum %08x\n", sum);
#endif

  return 0;
}
