/***************************************************************************
Copyright (c) 2010, Armin Biere, Johannes Kepler University.

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

#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static int verbose;
static const char * iname1, * iname2;
static const char * oname;
static aiger * model1, * model2, * miter;

static const char * USAGE =
"usage: aigmiter [-h][-v][-o <output>] <input1> <input2>\n"
"\n"
"Generate miter for AIGER models in <input1> and <input2>.\n"
;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigmiter] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
msg (int level, const char *fmt, ...)
{
  va_list ap;
  if (verbose < level) return;
  fputs ("[aigmiter] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

void main (int argc, char ** argv) {
  const char * err;
  int i;
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) {
      fputs (USAGE, stdout);
      exit (0);
    } else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-o")) {
      if (i + 1 == argc) die ("argument to '-o' missing (see '-h')");
      oname = argv[i];
    } else if (iname2) die ("too many input files (see '-h')");
    else if (iname1) iname2 = argv[i];
    else iname1 = argv[i];
  }
  if (!iname1) die ("both input files missing");
  if (!iname2) die ("second input file missing");
  msg (1, "reading '%s", iname1);
  model1 = aiger_init ();
  if ((err = aiger_open_and_read_from_file (model1, iname1)))
    die ("parse error in '%s': %s", iname1, err);
  msg (2, "1st MILOA %d %d %d %d %d",
       model1->maxvar,
       model1->num_inputs,
       model1->num_latches,
       model1->num_outputs,
       model1->num_ands);
  msg (1, "reading '%s", iname2);
  if ((err = aiger_open_and_read_from_file (model2, iname2)))
    die ("parse error in '%s': %s", iname2, err);
  msg (2, "2nd MILOA %d %d %d %d %d",
       model2->maxvar,
       model2->num_inputs,
       model2->num_latches,
       model2->num_outputs,
       model2->num_ands);
  if (model1->num_inputs != model2->num_inputs)
    die ("number of inputs does not match");
  if (model1->num_outputs != model2->num_outputs)
    die ("number of outputs does not match");
  miter = aiger_init ();
  aiger_reset (model1);
  aiger_reset (model2);
  aiger_reset (miter);
  return 0;
}
