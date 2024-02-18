/***************************************************************************
Copyright (c) 2011, Armin Biere, Johannes Kepler University.
Copyright (c) 2024, Armin Biere, University of Freiburg.

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
#include <unistd.h>

static int verbose, combinational;
static const char * iname1, * iname2;
static const char * oname;
static aiger * model1, * model2, * miter;
static unsigned latches, ands2, latches2exported, ands2exported;

static const char * USAGE =
"usage: aigmiter [-h][-v][-c][-o <output>] <input1> <input2>\n"
"\n"
"Generate miter for AIGER models in <input1> and <input2>.\n"
"\n"
"   -h   prints this command line option summary\n"
"   -v   increase verbosity level\n"
"   -c   treat latches as shared combinational inputs\n"
"\n"
"Output is written to '<stdout>' or to the specified '<output>' file.\n"
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
    assert (res < model2->num_latches);
    res += latches2exported;
  } else {
    res = idx - ands2;
    assert (res < model2->num_ands);
    res += ands2exported;
  }
  res *= 2;
  res ^= lit & 1;
  return res;
}

static unsigned output (int model, unsigned idx) {
  unsigned res;
  assert (idx < model1->num_outputs);
  if (model == 1) res = model1->outputs[idx].lit;
  else assert (model == 2), res = model2->outputs[idx].lit;
  return export (model, res);
}

static unsigned not (unsigned a) { return a^1; }

static unsigned and (unsigned a, unsigned b) {
  unsigned res;
  if (!a || !b || a == not (b)) return 0;
  if (a == 1 || a == b) return b;
  if (b == 1) return a;
  res = 2*(miter->maxvar + 1);
  assert (a < res), assert (b < res);
  aiger_add_and (miter, res, a, b);
  return res;
}

static unsigned implies (unsigned a, unsigned b) {
  return not (and (a, not (b)));
}

static unsigned xnor (unsigned a, unsigned b) {
  return and (implies (a, b), implies (b, a));
}

int main (int argc, char ** argv) {
  unsigned lit, i, lhs, rhs0, rhs1, next, reset, next1, next2, out;
  const char * err, * sym, * n1, * n2;
  aiger_and * a;
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) {
      fputs (USAGE, stdout);
      exit (0);
    } else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-c")) combinational = 1;
    else if (!strcmp (argv[i], "-o")) {
      if (++i == argc) die ("argument to '-o' missing (see '-h')");
      oname = argv[i];
    } else if (iname2) 
      die ("too many input files '%s', '%s' and '%s' (see '-h')",
           iname1, iname2, argv[i]);
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
  if (!combinational && model1->num_outputs < 1) 
    die ("first model in '%s' without outputs", iname1);
  if (combinational && model1->num_outputs < 1 && model1->num_latches < 1)
    die ("first model in '%s' without outputs nor latches", iname1);
  model2 = aiger_init ();
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
    die ("number of inputs in '%s' and '%s' do not match", iname1, iname2);
  if (model1->num_outputs != model2->num_outputs)
    die ("number of outputs in '%s' and '%s' do not match", iname1, iname2);
  if (combinational && model1->num_latches != model2->num_latches)
    die ("number of latches in '%s' and '%s' do not match", iname1, iname2);
  if (combinational)
    for (i = 0; i < model1->num_latches; i++)
      if (model1->latches[i].reset != model2->latches[i].reset)
	die ("reset of latch %u does not match", i);
  aiger_reencode (model1), aiger_reencode (model2);
  msg (2, "both models reencoded");
  latches = 1 + model1->num_inputs;
  ands2 = latches + model2->num_latches;
  if (combinational) {
    latches2exported = latches;
    ands2exported = model1->maxvar + 1;;
  } else {
    latches2exported = model1->maxvar + 1;
    ands2exported = latches2exported + model2->num_latches;
  } 
  miter = aiger_init ();
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
    a = model1->ands + i;
    lhs = export (1, a->lhs);
    rhs0 = export (1, a->rhs0);
    rhs1 = export (1, a->rhs1);
    aiger_add_and (miter, lhs, rhs0, rhs1);
  }
  for (i = 0; i < model2->num_ands; i++) {
    a = model2->ands + i;
    lhs = export (2, a->lhs);
    rhs0 = export (2, a->rhs0);
    rhs1 = export (2, a->rhs1);
    aiger_add_and (miter, lhs, rhs0, rhs1);
  }
  out = 1;
  if (combinational) {
    for (i = 0; i < model1->num_latches; i++) {
      lit = export (1, model1->latches[i].lit);
      assert (lit == export (2, model2->latches[i].lit));
      n1 = model1->latches[i].name;
      n2 = model2->latches[i].name;
      sym = (n1 && n2 && !strcmp (n1, n2)) ? n1 : 0;
      aiger_add_input (miter, lit, sym);
    }
    for (i = 0; i < model1->num_latches; i++) {
      next1 = export (1, model1->latches[i].next);
      next2 = export (2, model2->latches[i].next);
      out = and (out, xnor (next1, next2));
    }
  } else {
    for (i = 0; i < model1->num_latches; i++) {
      lit = export (1, model1->latches[i].lit);
      next = export (1, model1->latches[i].next);
      reset = export (1, model1->latches[i].reset);
      n1 = model1->latches[i].name;
      n2 = model2->latches[i].name;
      sym = (n1 && n2 && !strcmp (n1, n2)) ? n1 : 0;
      aiger_add_latch (miter, lit, next, sym);
      aiger_add_reset (miter, lit, reset);
    }
    for (i = 0; i < model2->num_latches; i++) {
      lit = export (2, model2->latches[i].lit);
      next = export (2, model2->latches[i].next);
      reset = export (2, model2->latches[i].reset);
      n1 = model1->latches[i].name;
      n2 = model2->latches[i].name;
      sym = (n1 && n2 && !strcmp (n1, n2)) ? n1 : 0;
      aiger_add_latch (miter, lit, next, sym);
      aiger_add_reset (miter, lit, reset);
    }
  }
  for (i = 0; i < model1->num_outputs; i++)
    out = and (out, xnor (output (1, i), output (2, i)));
  aiger_reset (model1), aiger_reset (model2);
  aiger_add_output (miter, not (out), "miter");
  aiger_add_comment (miter, "aigmiter");
  aiger_add_comment (miter, iname1);
  aiger_add_comment (miter, iname2);
  msg (2, "created miter");
  aiger_reencode (miter);
  msg (2, "reencoded miter");
  msg (2, "miter MILOA %d %d %d %d %d",
       miter->maxvar,
       miter->num_inputs,
       miter->num_latches,
       miter->num_outputs,
       miter->num_ands);
  msg (1, "writing miter to '%s'", oname ? oname : "<stdout>");
  if ((oname && !aiger_open_and_write_to_file (miter, oname)) ||
      (!oname && !aiger_write_to_file (miter, 
                    isatty (1) ? aiger_ascii_mode : aiger_binary_mode,
		    stdout)))
    die ("failed to write miter '%s'", oname ? oname : "<stdout>");
  aiger_reset (miter);
  return 0;
}
