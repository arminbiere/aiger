#ifndef aigfuzz_h_INCLUDED
#define aigfuzz_h_INCLUDED
/***************************************************************************
Copyright (c) 2009-2011, Armin Biere, Johannes Kepler University.

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

typedef struct aigfuzz_opts aigfuzz_opts;

struct aigfuzz_opts
{
  int merge;
  int small;
  int large;
  int combinational;
  int version;
  int safety;
  int liveness;
  int bad;
  int justice;
  int zero;
};

void aigfuzz_msg (int level, const char *fmt, ...);
void aigfuzz_opt (const char *fmt, ...);
unsigned aigfuzz_pick (unsigned from, unsigned to);
int aigfuzz_oneoutof (unsigned to);

unsigned * aigfuzz_layers (aiger *, aigfuzz_opts *);

#endif
