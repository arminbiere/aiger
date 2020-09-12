/***************************************************************************
Copyright (c) 2013 - 2020 Armin Biere, Johannes Kepler University.

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
#include <unistd.h>

static const char * USAGE =
"usage: aigunconstraint [-h][-v] [<input> [<output>]]\n"
"\n"
"  -h   print this command line option summary\n"
"  -v   increase verbosity\n"
"\n"
"The input is assumed to have bad state and constraint properties.\n"
"The constraints are eliminated by adding a latch.\n"
;

static aiger * src, * dst;
static int verbose;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigunconstraint] error: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void warn (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigunconstraint] warning: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void msg (const char *fmt, ...) {
  va_list ap;
  if (!verbose) return;
  fputs ("[aigunconstraint] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned maxvar;

static unsigned newlit () {
  unsigned res;
  if (!maxvar) { assert (dst); maxvar = dst->maxvar; }
  res = ++maxvar;
  return 2*res;
}

int main (int argc, char ** argv) {
  unsigned invalid_state, constraints_valid;
  const char * input, * output, * err;
  unsigned j, tmp;
  aiger_and * a;
  int i, ok;

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

  src = aiger_init ();
  if (input) {
    msg ("reading '%s'", input);
    err = aiger_open_and_read_from_file (src, input);
  } else {
    msg ("reading '<stdin>'");
    err = aiger_read_from_file (src, stdin);
  }

  if (err) die ("read error: %s", err);

  msg ("read MILOA %u %u %u %u %u BCJF %u %u %u %u", 
    src->maxvar,
    src->num_inputs,
    src->num_latches,
    src->num_outputs,
    src->num_ands,
    src->num_bad,
    src->num_constraints,
    src->num_justice,
    src->num_fairness);
  
  if (src->num_outputs)
    die ("can not handle outputs in '%s'", input);
  if (!src->num_bad)
    die ("no bad state properties in '%s'", input);

  if (!src->num_constraints) {
    warn ("no environment constraints in '%s'", input);
    dst = src;
    goto COPY;
  }

  if (src->num_justice)
    die ("can not handle justice properties in '%s'", input);

  dst = aiger_init ();

  for (j = 0; j < src->num_inputs; j++)
    aiger_add_input (dst, src->inputs[j].lit, src->inputs[j].name);

  for (j = 0; j < src->num_ands; j++) {
    aiger_and * a = src->ands + j;
    aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
  }

  for (j = 0; j < src->num_latches; j++) {
    aiger_symbol * s = src->latches + j;
    aiger_add_latch (dst, s->lit, s->next, s->name);
    if (s->reset) aiger_add_reset (dst, s->lit, s->reset);
  }

  msg ("initialized copy of original aiger model");

  invalid_state = newlit ();
  constraints_valid = aiger_not (invalid_state);

  for (j = 0; j < src->num_constraints; j++) {
    tmp = newlit ();
    aiger_add_and (dst, tmp, constraints_valid, src->constraints[j].lit);
    constraints_valid = tmp;
  }

  aiger_add_latch (dst,
    invalid_state,
    aiger_not (constraints_valid),
    "AIGUNCONSTRAINT_INVALID_STATE");

  msg ("added one latch and %u AND gates for its next state function",
    src->num_constraints);

  for (j = 0; j < src->num_bad; j++) {
    tmp = newlit ();
    aiger_add_and (dst, tmp, constraints_valid, src->bad[j].lit);
    aiger_add_bad (dst, tmp, src->bad[j].name);
  }

  msg ("added %u AND gates as guards for %u bad state properties",
   src->num_bad, src->num_bad);

  aiger_reset (src);

COPY:

  aiger_reencode (dst);

  msg ("write MILOA %u %u %u %u %u BCJF %u %u %u %u", 
    dst->maxvar,
    dst->num_inputs,
    dst->num_latches,
    dst->num_outputs,
    dst->num_ands,
    dst->num_bad,
    dst->num_constraints,
    dst->num_justice,
    dst->num_fairness);
  
  if (output) {
    msg ("writing '%s'", output);
    ok = aiger_open_and_write_to_file (dst, output);
  } else {
    msg ("writing '<stdout>'", output);
    ok = aiger_write_to_file (dst, 
           (isatty (1) ? aiger_ascii_mode : aiger_binary_mode), stdout);
  }
  if (!ok) die ("write error");

  aiger_reset (dst);

  return 0;
}
