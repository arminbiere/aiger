/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

unsigned
decode (FILE * file)
{
  unsigned x = 0, i = 0;
  unsigned char ch;

  while ((ch = getc (file)) & 0x80)
    x |= (ch & 0x7f) << (7 * i++);

  return x | (ch << (7 * i));
}

int 
main (void)
{
  for (;;)
   printf ("%u\n", decode (stdin));

  return 0;
}
