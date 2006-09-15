/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>

void
encode (FILE * file, unsigned x)
{
  unsigned char ch;

  while (x & ~0x7f)
    {
      ch = (x & 0x7f) | 0x80;
      putc (ch, file);
      x >>= 7;
    }
 
  ch = x;
  putc (ch, file);
}

int
main (void)
{
  unsigned x;

  if (isatty (1))
    {
      fprintf (stderr, "*** write: will not write to terminal\n");
      return 1;
    }

  while (scanf ("%u", &x) != EOF)
    encode (stdout, x);

  return 0;
}
