/***************************************************************************
Copyright (c) 2013, Armin Biere, Johannes Kepler University.

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

static const char * USAGE =
"usage: aigunconstraint [-h][-v] [<input> [<output>]]\n"
"\n"
"  -h   print this command line option summary\n"
"  -v   increase verbosity\n"
"\n"
"The input is assumed to have bad state and constraint properties.\n"
"The constraints are eliminated by adding a latch.\n"
;

static aiger * model;
static int verbosity;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigunconstraint] ", stderr);
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
  fputs ("[aigunconstraint] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned newlit () { return 2*(model->maxvar + 1); }

int main (int argc, char ** argv) {
  unsigned invalid_state, constraints_valid;
  const char * input, * output, * err;
  unsigned j, tmp;
  aiger_and * a;
  int i, ok;

  reset = aiger_false;
  input = output = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) { printf ("%s", USAGE); exit (0); }
    else if (!strcmp (argv[i], "-v")) verbose = 1;
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
  
  if (!model->num_constraints)
    die ("no environment constraints in '%s'", input);

  invalid_state = newlit ();
  constraints_valid = aiger_not (invalid_state);

  for (j = 0; j < model->num_constraints; j++) {
    tmp = newlit ();
    aiger_add_and (model, tmp, constraints_valid, model->constraint[j].lit);
    constraints_valid = tmp;
  }

  aiger_add_latch (model,
    invalid_state,
    aiger_not (constraints_valid),
    "AIGUNCONSTRAINT_INVALID_STATE");

  msg ("added one latch and %u AND gates for its next state function",
    model->num_constraints);

  for (j = 0; j < model->num_bad; j++) {
    tmp = newlit ();
    aiger_add_and (model, tmp, constraints_valid, model->bad[j].lit);
    model->bad[j].lit = tmp;
  }

  msg ("added %u AND gates as guards for %u bad state properties",
   model->num_bad, model->num_bad);

  for (j = 0; j < model->num_justice; j++) {
    tmp = newlit ();
    aiger_add_and (model, tmp, constraints_valid, model->justice[j].lit);
    model->justice[j].lit = tmp;
  }

  msg ("added %u AND gates as guards for %u justice state properties",
   model->num_justice, model->num_justice);

  aiger_reencode (model);

  tmp = model->num_constraints;
  model->num_constraints = 0;

  msg ("write MILOA %u %u %u %u %u BCJF %u %u %u %u", 
    model->maxvar,
    model->num_inputs,
    model->num_latches,
    model->num_outputs,
    model->num_ands,
    model->num_bad,
    model->num_constraints,
    model->num_justice,
    model->num_fairness);
  
  if (output) {
    msg ("writing '%s'", output);
    ok = aiger_open_and_write_to_file (model, output);
  } else {
    msg ("writing '<stdout>'", output);
    ok = aiger_write_to_file (model, 
           (isatty (1) ? aiger_ascii_mode : aiger_binary_mode), stdout);
  }
  if (!ok) die ("write error");

  model->num_constraints = tmp;
  aiger_reset (model);

  return 0;
}
