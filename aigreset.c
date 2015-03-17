/***************************************************************************
Copyright (c) 2012, Armin Biere, Johannes Kepler University, Austria.

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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define USAGE \
"usage: aigreset [-h][-v][-i][<input> [<output>]]\n" \
"\n" \
"   -h   print command line option summary\n" \
"   -0   replace one reset by zero reset (default)\n" \
"   -1   replace zero reset by one reset\n" \
"   -c   check if there are unitialized latches\n" \
"   -v   increase verbosity\n" \
"\n" \
"Normalize to zero (default) or one reset.\n"

static unsigned reset, normalized;
static int verbose, check;
static aiger * model;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigreset] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void msg (const char *fmt, ...) {
  va_list ap;
  if (!verbose)
    return;
  fputs ("[aigreset] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}


static unsigned normlit (unsigned lit) {
  unsigned idx = aiger_strip (lit);
  aiger_symbol * sym = aiger_is_latch (model, idx);
  if (!sym) return lit;
  if (sym->reset == sym->lit) return lit;
  if (sym->reset == reset) return lit;
  assert (sym->reset == aiger_not (reset));
  return aiger_not (lit);
}

static void normlitptr (unsigned * litptr) {
  *litptr = normlit (*litptr); 
}

static void normalize (void) {
  unsigned i, j, lit;
  aiger_symbol * sym;
  if (check) {
    for (i = 0; i < model->num_latches; i++) {
      sym = model->latches + i;
      if (sym->reset == sym->lit)
	die ("latch %u literal %u uninitialized", i, sym->lit);
    }
  }
  for (i = 0; i < model->num_latches; i++) {
    sym = model->latches + i;
    if (sym->reset == sym->lit) continue;
    if (sym->reset == reset) continue;
    model->latches[i].next = aiger_not (normlit (model->latches[i].next));
  }
  for (i = 0; i < model->num_outputs; i++)
    normlitptr (&model->outputs[i].lit);
  for (i = 0; i < model->num_ands; i++)
    normlitptr (&model->ands[i].rhs0),
    normlitptr (&model->ands[i].rhs1);
  for (i = 0; i < model->num_bad; i++)
    normlitptr (&model->bad[i].lit);
  for (i = 0; i < model->num_constraints; i++)
    normlitptr (&model->constraints[i].lit);
  for (i = 0; i < model->num_justice; i++)
    for (j = 0; j < model->justice[i].size; j++)
      normlitptr (&model->justice[i].lits[j]);
  for (i = 0; i < model->num_fairness; i++)
    normlitptr (&model->fairness[i].lit);
  for (i = 0; i < model->num_latches; i++) {
    sym = model->latches + i;
    if (sym->reset == sym->lit) continue;
    if (sym->reset == reset) continue;
    assert (sym->reset == aiger_not (reset));
    sym->reset = reset;
    normalized++;
  }
  msg ("normalized %u resets of latches to %u", normalized, reset);
}

int main (int argc, char ** argv) {
  const char * input, * output, * err;
  aiger_and * a;
  unsigned j;
  int i, ok;

  reset = aiger_false;
  input = output = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) { printf ("%s", USAGE); exit (0); }
    else if (!strcmp (argv[i], "-v")) verbose = 1;
    else if (!strcmp (argv[i], "-0")) reset = aiger_false;
    else if (!strcmp (argv[i], "-1")) reset = aiger_true;
    else if (!strcmp (argv[i], "-c")) check = 1;
    else if (argv[i][0] == '-')
      die ("invalid command line option '%s'", argv[i]);
    else if (output) die ("too many arguments");
    else if (input) output = argv[i];
    else input = argv[i];
  }

  model = aiger_init ();
  if (input) {
    msg ("reading '%s'", input);
    err = aiger_open_and_read_from_file (model, input);
  } else {
    msg ("reading '<stdin>'");
    err = aiger_read_from_file (model, stdin);
  }

  if (err) die ("read error: %s", err);

  msg ("read MILOA %u %u %u %u %u BCJF %u %u %u %u", 
    model->maxvar,
    model->num_inputs,
    model->num_latches,
    model->num_outputs,
    model->num_ands,
    model->num_bad,
    model->num_constraints,
    model->num_justice,
    model->num_fairness);

  normalize ();

  if (output) {
    msg ("writing '%s'", output);
    ok = aiger_open_and_write_to_file (model, output);
  } else {
    msg ("writing '<stdout>'", output);
    ok = aiger_write_to_file (model, 
           (isatty (1) ? aiger_ascii_mode : aiger_binary_mode), stdout);
  }
  if (!ok) die ("write error");

  aiger_reset (model);

  return 0;
}
