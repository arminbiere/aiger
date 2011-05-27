/***************************************************************************
Copyright (c) 2011, Armin Biere, Johannes Kepler University, Austria.

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
#include "../picosat/picosat.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char * name;
static aiger * model;
static unsigned firstlatchidx, firstandidx;

typedef struct And { int lhs, rhs0, rhs1; } And;
typedef struct Latch { int lit, next; } Latch;
typedef struct Fairness { int lit, sat; } Fairness;
typedef struct Justice { int nlits, sat; Fairness * lits; } Justice;

typedef struct State {
  int time;
  int * inputs;
  Latch * latches;
  And * ands;
  int * bad, onebad;
  int * constraints, allconstrained;
  Justice * justice;
  Fairness * fairness; int fairsat;
  int looping, inloop, assumption;
} State;

static State * states;
static int nstates, szstates, * endstate;
static char * bad, * justice;

static int verbose;
static int nvars;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigbmc] ", stderr);
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
  fputs ("[aigbmc] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void init () {
  picosat_init ();
  model = aiger_init ();
}

static void reset () {
  State * s;
  int i, j;
  for (i = 0; i < nstates; i++) {
    s = states + i;
    free (s->inputs);
    free (s->latches);
    free (s->ands);
    free (s->bad);
    free (s->constraints);
    for (j = 0; j < model->num_justice; j++) free (s->justice[j].lits);
    free (s->justice);
    free (s->fairness);
  }
  free (states);
  free (bad);
  free (justice);
  free (endstate);
  picosat_reset ();
  aiger_reset (model);
}

static int newvar () { return ++nvars; }

static int import (State * s, unsigned ulit) {
  unsigned uidx = ulit/2;
  int res, idx;
  assert (ulit <= 2*model->maxvar + 1);
  if (!uidx) idx = 1;
  else if (uidx < firstlatchidx) idx = s->inputs[uidx - 1];
  else if (uidx < firstandidx) idx = s->latches[uidx - firstlatchidx].lit;
  else idx = s->ands[uidx - firstandidx].lhs;
  assert (idx);
  res = (ulit & 1) ? -idx : idx;
  return res;
}

static State * encode () {
  int time = nstates, lit;
  aiger_symbol * symbol;
  State * res, * prev;
  aiger_and * uand;
  unsigned reset;
  And * iand;
  int i, j;
  if (nstates == szstates)
    states = realloc (states, ++szstates * sizeof *states);
  nstates++;
  res = states + time;
  memset (res, 0, sizeof *res);
  res->time = time;
  res->latches = malloc (model->num_latches * sizeof *res->latches);
  if (time) {
    prev = res - 1;
    for (i = 0; i < model->num_latches; i++)
      res->latches[i].lit = prev->latches[i].next;
  } else {
    for (i = 0; i < model->num_latches; i++) {
      symbol = model->latches + i;
      reset = symbol->reset;
      if (!reset) lit = -1;
      else if (reset == 1) lit = 1;
      else {
	if (reset != symbol->lit)
	  die ("can only handle constant or uninitialized reset logic");
	lit = newvar ();
      }
      res->latches[i].lit = lit;
    }
  }
  res->inputs = malloc (model->num_inputs * sizeof *res->inputs);
  for (i = 0; i < model->num_inputs; i++)
    res->inputs[i] = newvar ();
  res->ands = malloc (model->num_ands * sizeof *res->ands);
  for (i = 0; i < model->num_ands; i++) {
    uand = model->ands + i;
    iand = res->ands + i;
    iand->lhs = newvar ();
    iand->rhs0 = import (res, uand->rhs0);
    iand->rhs1 = import (res, uand->rhs1);
  }
  for (i = 0; i < model->num_latches; i++)
    res->latches[i].next = import (res, model->latches[i].next);
  res->bad = malloc (model->num_bad * sizeof *res->bad);
  for (i = 0; i < model->num_bad; i++)
    res->bad[i] = import (res, model->bad[i].lit);
  res->constraints =
    malloc (model->num_constraints * sizeof *res->constraints);
  for (i = 0; i < model->num_constraints; i++)
    res->constraints[i] = import (res, model->constraints[i].lit);
  res->justice = malloc (model->num_justice * sizeof *res->justice);
  for (i = 0; i < model->num_justice; i++) {
    res->justice[i].lits = 
      malloc (model->justice[i].size * sizeof *res->justice[i].lits);
    for (j = 0; j < model->justice[i].size; j++)
      res->justice[i].lits[j].lit = import (res, model->justice[i].lits[j]);
  }
  res->fairness = malloc (model->num_fairness * sizeof *res->fairness);
  for (i = 0; i < model->num_fairness; i++)
    res->fairness[i].lit = import (res, model->fairness[i].lit);
  res->looping = newvar ();
  if (time) {
  } else {
    res->inloop = res->looping;
  }
  msg (1, "encoded %d", time);
  return res;
}

static void unit (int lit) { picosat_add (lit); picosat_add (0); }

static int isnum (const char * str) {
  const char * p = str;
  if (!isdigit (*p++)) return 0;
  while (isdigit (*p)) p++;
  return !*p;
}

static const char * usage =
"usage: aigbmc [-h][-v][<model>][<maxk>]\n";

int main (int argc, char ** argv) {
  int i, k, maxk = -1;
  const char * err;
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) {
      printf ("%s", usage);
      exit (0);
    } else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-v"))
      die ("invalid command line option '%s'", argv[i]);
    else if (name && maxk >= 0) 
      die ("unexpected argument '%s'", argv[i]);
    else if (name && !isnum (argv[i]))
      die ("expected number got '%s'", argv[i]);
    else if (name) maxk = atoi (argv[i]);
    else name = argv[i];
  }
  if (maxk < 0) maxk = 10;
  msg (1, "aigbmc bounded model checker");
  msg (1, "maxk = %d", maxk);
  init ();
  msg (1, "reading from '%s'", name ? name : "<stdin>");
  if (name) err = aiger_open_and_read_from_file (model, name);
  else err = aiger_read_from_file (model, stdin), name = "<stdin>";
  if (err) die ("parse error reading '%s': %s", name, err);
  msg (1, "M I L O A = %u %u %u %u %u",
       model->maxvar,
       model->num_inputs,
       model->num_latches,
       model->num_outputs,
       model->num_ands);
  msg (1, "B C J F = %u %u %u %u",
       model->num_bad,
       model->num_constraints,
       model->num_justice,
       model->num_fairness);
  aiger_reencode (model);
  firstlatchidx = 1 + model->num_inputs;
  firstandidx = firstlatchidx + model->num_latches;
  msg (2, "reencoded model");
  bad = calloc (model->num_bad, 1);
  justice = calloc (model->num_justice, 1);
  unit (newvar ()), assert (nvars == 1);
  endstate = malloc (model->num_latches * sizeof *endstate);
  for (i = 0; i < model->num_latches; i++) endstate[i] = newvar ();
  for (k = 0; k <= maxk; k++) encode ();
  reset ();
  return 0;
}
