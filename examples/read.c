#include <stdio.h>

unsigned
decode (FILE * file)
{
  unsinged x = 0, i = 0;
  unsigned char ch;

  while ((ch = getc (file)) & 0x80)
    x |= (ch & 0x7f) << (7 * i++);

  return x | (ch << (7 * i));
}

int 
main (void)
{
  while (!feof (stdin))
   printf ("%u\n", decode (stdin));

  return 0;
}
