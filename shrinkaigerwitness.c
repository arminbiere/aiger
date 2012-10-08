#include <stdio.h>
#include <stdlib.h>

static char * start, * top, * end;

int main (void) {
  unsigned char uch;
  size_t i, size;
  int ch;
NEXT:
  ch = getc (stdin);
  if (ch == EOF) return;
  if (ch == '0' || ch == '1') {
    top = buffer;
    while ((ch = getc (stdin)) != '\n') {
      if (ch == EOF) {
	fprintf (stderr, "*** shrinkaigerwitness: unexpected EOF\n");
	exit (1);
      }
      if (ch != '0' && ch != '1') {
	fprintf (stderr, "*** shrinkaigerwitness: expected '0' or '1'\n");
	exit (1);
      }
      if (top == end) {
	size_t oldsz = top - bufer, newsz = oldsz ? 2 * oldsz : 8192;
	start = realloc (start, newsz);
	top = start + oldsz;
	end = start + newsz;
      }
      *top++ = ch;
    }
    size = top - start;
    printf ("z%lld\n", size);
    uch = 0;
    for (i = 0; i < size; i++) {
      unsigned s = i & 7;
      uch |= ((unsigned char)(start[i] == '1')) << (s);
      if (s) continue;
      putc (uch, stdout);
      uch = 0;
    }
    if (i & 7) putc (uch, stdout);
    putc ('\n', stdout);
  }
}
