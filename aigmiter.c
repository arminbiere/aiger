/***************************************************************************
Copyright (c) 2011, Armin Biere, Johannes Kepler University.

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
static unsigned latches, ands2, latches2exported, ands2exported;

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

static void msg (int level, const char *fmt, ...) {
  va_list ap;
  if (verbose < level) return;
  fputs ("[aigmiter] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned export (int model, unsigned lit) {
  unsigned idx = lit/2, res;
  if (idx < latches || model == 1) return lit;
  assert (model == 2);
  if (idx < ands2) {
    res = idx - latches;
    assert (idx < model2->num_latches);
    res += latches2exported;
  } else {
    res = idx - ands2;
    assert (idx < model2->num_latches);
    res += ands2exported;
  }
  res *= 2;
  res ^= lit & 1;
  return res;
}

static unsigned output (int model, unsigned idx) {
  unsigned res;
  assert (idx < model->num_outputs);
  if (model == 1) res = model1->outputs[idx].lit;
  else assert (model == 2) res = model2->outputs[idx].lit;
  return export (model, res);
}

void main (int argc, char ** argv) {
  const char * err, * sym, * n1, * n2;
  unsigned lit, i, lhs, rhs0, rhs1;
  aiger_and * and;
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
  aiger_reencode (model1), aiger_reencode (model2);
  msg ("both models reencoded");
  latches = 1 + model1->num_inputs;
  ands2 = latches + model2->num_latches;
  latches2exported = model1->maxvar + 1;
  ands2exported = latches2exported + model2->num_latches;
  miter = aiger_init ();
  aiger_reset (model1), aiger_reset (model2);
  for (i = 0; i < model1->num_inputs; i++) {
    lit = model1->inputs[i].lit;
    assert (export (1, lit) == lit);
    assert (export (2, lit) == lit);
    n1 = model1->inputs[i].name;
    n2 = model2->inputs[i].name;
    sym = (n1 && n2 && !strcmp (n1, n2)) ? n1 : 0;
    aiger_add_input (miter, lit, sym);
  }
  for (i = 0; i < model1->num_ands; i++) {
    and = model1->ands + i;
    lhs = and->lhs;  assert (export (1, lhs) == lhs);
    rhs0 = and->rhs0; assert (export (1, rhs0) == rhs0);
    rhs1 = and->rhs1; assert (export (1, rhs1) == rhs1);
    aiger_add_and (miter, lhs, rhs0, rhs1);
  }
  for (i = 0; i < model1->num_ands; i++) {
    and = model1->ands + i;
    lhs = and->lhs;  assert (export (1, lhs) == lhs);
    rhs0 = and->rhs0; assert (export (1, rhs0) == rhs0);
    rhs1 = and->rhs1; assert (export (1, rhs1) == rhs1);
    aiger_add_and (miter, lhs, rhs0, rhs1);
  }
  aiger_reset (miter);
  return 0;
}
