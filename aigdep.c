/***************************************************************************
Copyright (c) 2014-2014, Armin Biere, Johannes Kepler University, Austria.

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

#ifdef AIGER_HAVE_PICOSAT
#include "../picosat/picosat.h"
static int use_picosat;
static PicoSAT * picosat;
#endif

#ifdef AIGER_HAVE_LINGELING
#include "../lingeling/lglib.h"
static int use_lingeling;
static LGL * lgl;
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

static aiger * model;
static int output_set, input_set;
static const char * input_file_name;
static const char * output_file_name;
static int verbose, strip, symbols, indices;
static FILE * output_file;

static void die (const char * fmt, ...) {
  va_list ap;
  fputs ("*** aigdep: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void msg (int level, const char * fmt, ...) {
  va_list ap;
  if (verbose < level) return;
  fputs ("[aigdep] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void check_symbols () {
  unsigned i, num_outputs_with_symbols = 0, num_inputs_with_symbols = 0;
  int not_enough_symbols;
  for (i = 0; i < model->num_outputs; i++)
    if (model->outputs[i].name) num_outputs_with_symbols++;
  msg (2, "found %u outputs with symbols", num_outputs_with_symbols);
  for (i = 0; i < model->num_inputs; i++)
    if (model->inputs[i].name) num_inputs_with_symbols++;
  msg (2, "found %u inputs with symbols", num_inputs_with_symbols);
  if (!num_outputs_with_symbols && !num_inputs_with_symbols)
    msg (1, "no input nor output symbols found"), symbols = 0;
  else if (strip)
    msg (1, "ignoring symbols as requested (by '-s' option)"), symbols = 0;
  else if (num_inputs_with_symbols &&
           num_inputs_with_symbols < model->num_inputs)
    die ("found only %u of %u inputs with symbols (use '-s' option)",
      num_inputs_with_symbols, model->num_inputs);
  else if (num_outputs_with_symbols &&
           num_outputs_with_symbols < model->num_outputs)
    die ("found only %u of %u outputs with symbols (use '-s' option)",
      num_outputs_with_symbols, model->num_outputs);
  else if (num_outputs_with_symbols && !num_inputs_with_symbols)
    die ("found all output but no input symbols");
  else if (!num_outputs_with_symbols && num_inputs_with_symbols)
    die ("found all input but no output symbols");
  else {
    assert (num_inputs_with_symbols == model->num_inputs);
    assert (num_outputs_with_symbols == model->num_outputs);
    msg (1, "all inputs and outputs have symbols");
    symbols = 1;
  }
}

static const char * USAGE =
"usage: aigdep [<option> ...] [<aigermodel> [<outputfile>]]\n"
"\n"
"-v           increase verbosity level\n"
"-h           print this command line option summary\n"
"-s           do not use symbols (strip all symbols)\n"
#if defined(AIGER_HAVE_PICOSAT) && defined(AIGER_HAVE_LINGELING)
"--picosat    use PicoSAT as SAT solver (default)\n"
"--lingeling  use Lingeling as SAT solver\n"
#endif
"<aigermodel> path to aiger model or '-' for <stdin> (default)\n"
"<outputfile> path to output file or '-' for <sdout< (default)\n"
;

/*------------------------------------------------------------------------*/

static void add_int_lit (int lit) {
#ifdef AIGER_HAVE_PICOSAT
  if (use_picosat) picosat_add (picosat, lit);
#endif
#ifdef AIGER_HAVE_LINGELING
  if (use_lingeling) lgladd (lgl, lit);
#endif
}

static void sat_init () {
#ifdef AIGER_HAVE_PICOSAT
  if (use_picosat) {
    picosat = picosat_init ();
    if (verbose > 3) picosat_set_verbosity (picosat, 1);
  }
#endif
#ifdef AIGER_HAVE_LINGELING
  if (use_lingeling) {
    lgl = lglinit ();
    if (verbose > 3) lglsetopt (lgl, "verbose", 1);
    else lglsetopt (lgl, "trep", 0);
  }
#endif
  add_int_lit (1);
  add_int_lit (0);
}

static int sat_check () {
#ifdef AIGER_HAVE_PICOSAT
  if (use_picosat) return picosat_sat (picosat, -1);
#endif
#ifdef AIGER_HAVE_LINGELING
  if (use_lingeling) return lglsat (lgl);
#endif
  return 0;
}

static void sat_reset () {
#ifdef AIGER_HAVE_PICOSAT
  if (use_picosat) picosat_reset (picosat);
#endif
#ifdef AIGER_HAVE_LINGELING
  if (use_lingeling) lglrelease (lgl);
#endif
}

/*------------------------------------------------------------------------*/

static unsigned forced_input;
static int dual;

static void add_aiger_lit (unsigned lit) {
  int ilit;
  if (forced_input == lit) ilit = dual ? -1 : 1;
  else if (forced_input == aiger_not (lit)) ilit = dual ? 1 : -1;
  else {
    ilit = lit/2 + 1;
    assert (0 < ilit), assert (ilit <= (int) model->maxvar + 1);
    if (dual && !aiger_is_input (model, aiger_strip (lit))) ilit += model->maxvar;
    if (lit & 1) ilit = -ilit;
  }
  add_int_lit (ilit);
}

static void binary (unsigned a, unsigned b) {
  add_aiger_lit (a);
  add_aiger_lit (b);
  add_int_lit (0);
}

static void ternary (unsigned a, unsigned b, unsigned c) {
  add_aiger_lit (a);
  add_aiger_lit (b);
  add_aiger_lit (c);
  add_int_lit (0);
}

static void miter (unsigned i, unsigned j) {
  unsigned k, olit = model->outputs[i].lit;
  int l = 2*model->maxvar + 1;
  int r = 2*model->maxvar + 2;
  dual = 0;
  forced_input = model->inputs[j].lit;
  for (k = 0; k < model->num_ands; k++) {
    aiger_and * a = model->ands + k;
    binary (aiger_not (a->lhs), a->rhs0);
    binary (aiger_not (a->lhs), a->rhs1);
    ternary (a->lhs, aiger_not (a->rhs0), aiger_not (a->rhs1));
  }
  dual = 1;
  for (k = 0; k < model->num_ands; k++) {
    aiger_and * a = model->ands + k;
    binary (aiger_not (a->lhs), a->rhs0);
    binary (aiger_not (a->lhs), a->rhs1);
    ternary (a->lhs, aiger_not (a->rhs0), aiger_not (a->rhs1));
  }

  add_int_lit (-l);
  dual = 0, add_aiger_lit (olit);
  add_int_lit (0);

  add_int_lit (-l);
  dual = 1, add_aiger_lit (aiger_not (olit));
  add_int_lit (0);

  add_int_lit (-r);
  dual = 0, add_aiger_lit (aiger_not (olit));
  add_int_lit (0);

  add_int_lit (-r);
  dual = 1, add_aiger_lit (olit);
  add_int_lit (0);

  add_int_lit (l);
  add_int_lit (r);
  add_int_lit (0);
}

static void check_dependency (unsigned i, unsigned j) {
  const char * dep;
  int res;
  if (symbols)
    msg (3, "checking dependency of output %u '%s' on input %u '%s'",
      i, model->outputs[i].name, j, model->inputs[j].name);
  else msg (3, "checking dependency of output %u on input %u", i, j);
  sat_init ();
  miter (i, j);
  res = sat_check ();
  if (res == 10) dep = "dependent";
  else {
    assert (res == 20);
    dep = "independent";
  }
  sat_reset ();
  if (symbols)
    printf ("%s %s %s\n",
      model->outputs[i].name, dep, model->inputs[j].name);
  else
    printf ("%u %s %u\n", i, dep, j);
}

static void extract_output (unsigned i) {
  unsigned j;
  assert (i < model->num_outputs);
  if (symbols)
    msg (2, "extracting dependencies of output %u '%s'",
      i, model->outputs[i].name);
  else msg (2, "extracting dependencies of output %u", i);
  for (j = 0; j < model->num_inputs; j++)
    check_dependency (i, j);
}

int main (int argc, char ** argv) {
  const char * err;
  unsigned i;

  fprintf (stderr, "[WARNING] this program is not working yet\n");
  fflush (stderr);

  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) fputs (USAGE, stdout), exit (0);
    else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-s")) strip = 1;
#if defined(AIGER_HAVE_PICOSAT) && defined(AIGER_HAVE_LINGELING)
    else if (!strcmp (argv[i], "--lingeling")) use_lingeling = 1;
#endif
    else if (argv[i][0] == '-') {
      if (!argv[i][1]) {
	if (output_set) die ("multiple output files");
	else if (input_set) output_set = 1;
	else input_set = 1;
      } else die ("invalid option '%s' (try '-h')", argv[i]);
    } else if (output_set) die ("multiple output files");
    else if (input_set) output_file_name = argv[i], output_set = 1;
    else input_file_name = argv[i], input_set = 1;
  }
  model = aiger_init ();
  if (!input_set || !input_file_name) {
    msg (1, "reading '<stdin>'");
    err = aiger_read_from_file (model, stdin);
    if (err) die ("reading from '<stdin>' failed: %s", err);
  } else {
    msg (1, "reading from '%s'", input_file_name);
    err = aiger_open_and_read_from_file (model, input_file_name);
    if (err) die ("reading '%s' failed: %s", input_file_name, err);
  }
  msg (1, "M I L O A = %u %u %u %u %u",
    model->maxvar,
    model->num_inputs,
    model->num_latches,
    model->num_outputs,
    model->num_ands);
  if (model->num_latches) die ("can not handle latches yet");
  if (!model->num_outputs) die ("not outputs found");
  check_symbols ();
  if (output_file_name) {
    if (!(output_file = fopen (output_file_name, "w")))
      die ("can not open '%s' for writing", output_file_name);
    msg (1, "writing dependencies to '%s'", output_file_name);
  } else {
    output_file = stdout;
    msg (1, "writing dependencies to '<stdout>'");
  }
#if defined(AIGER_HAVE_PICOSAT) && defined(AIGER_HAVE_LINGELING)
  if (!use_lingeling) use_picosat = 1;
#elif defined (AIGER_HAVE_LINGELING)
  use_lingeling = 1;
#else
  use_picosat = 1;
#endif
#ifdef AIGER_HAVE_LINGELING
  if (use_lingeling) msg (1, "using Lingeling as SAT solver");
#endif
#ifdef AIGER_HAVE_PICOSAT
  if (use_picosat) msg (1, "using PicoSAT as SAT solver");
#endif
  for (i = 0; i < model->num_outputs; i++) extract_output (i);
  if (output_file_name) fclose (output_file);
  aiger_reset (model);
  return 0;
}
