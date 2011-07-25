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

typedef struct Latch { int lit, next; } Latch;
typedef struct Fairness { int lit, sat; } Fairness;
typedef struct Justice { int nlits, sat; Fairness * lits; } Justice;

typedef struct State {
  int time;
  int * inputs;
  Latch * latches;
  int * ands;
  int * bad, onebad;
  int * constraints, sane;
  Justice * justice; int onejustified;
  Fairness * fairness; int allfair;
  int join, loop, assume;
} State;

static State * states;
static int nstates, szstates, * join;
static char * bad, * justice;

static int verbose, move;
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

static void wrn (const char *fmt, ...) {
  va_list ap;
  fputs ("[aigbmc] WARNING ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void add (int lit) { picosat_add (lit); }

static void assume (int lit) { picosat_assume (lit); }

static int sat () { return picosat_sat (-1); }

static int deref (int lit) { return picosat_deref (lit); }

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
  free (join);
  picosat_reset ();
  aiger_reset (model);
}

static int newvar () { return ++nvars; }

static int import (State * s, unsigned ulit) {
  unsigned uidx = ulit/2;
  int res, idx;
  assert (ulit <= 2*model->maxvar + 1);
  if (!uidx) idx = -1;
  else if (uidx < firstlatchidx) idx = s->inputs[uidx - 1];
  else if (uidx < firstandidx) idx = s->latches[uidx - firstlatchidx].lit;
  else idx = s->ands[uidx - firstandidx];
  assert (idx);
  res = (ulit & 1) ? -idx : idx;
  return res;
}

static void unit (int lit) { add (lit); add (0); }

static void binary (int a, int b) { add (a); add (b); add (0); }

static void ternary (int a, int b, int c) {
  add (a); add (b); add (c); add (0);
}

static void quaterny (int a, int b, int c, int d) {
  add (a); add (b); add (c); add (d); add (0);
}

static void and (int lhs, int rhs0, int rhs1) {
  binary (-lhs, rhs0);
  binary (-lhs, rhs1);
  ternary (lhs, -rhs0, -rhs1);
}

static int encode () {
  int time = nstates, lit;
  aiger_symbol * symbol;
  State * res, * prev;
  aiger_and * uand;
  unsigned reset;
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
    prev = 0;
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
    lit = newvar ();
    res->ands[i] = lit;
    uand = model->ands + i;
    and (lit, import (res, uand->rhs0), import (res, uand->rhs1));
  }

  for (i = 0; i < model->num_latches; i++)
    res->latches[i].next = import (res, model->latches[i].next);

  res->assume = newvar ();

  if (model->num_bad) {
    res->bad = malloc (model->num_bad * sizeof *res->bad);
    for (i = 0; i < model->num_bad; i++)
      res->bad[i] = import (res, model->bad[i].lit);
    if (model->num_bad > 1) {
      res->onebad = newvar ();
      add (-res->onebad);
      for (i = 0; i < model->num_bad; i++) add (res->bad[i]);
      add (0);
    } else res->onebad = res->bad[0];
  }

  if (model->num_constraints) {
    res->constraints =
      malloc (model->num_constraints * sizeof *res->constraints);
    for (i = 0; i < model->num_constraints; i++)
      res->constraints[i] = import (res, model->constraints[i].lit);
    res->sane = newvar ();
    for (i = 0; i < model->num_constraints; i++)
      binary (-res->sane, res->constraints[i]);
    if (time) binary (-res->sane, prev->sane);
    binary (-res->assume, res->sane);
  }

  if (model->num_justice) {

    res->justice = malloc (model->num_justice * sizeof *res->justice);
    for (i = 0; i < model->num_justice; i++) {
      res->justice[i].lits = 
	malloc (model->justice[i].size * sizeof *res->justice[i].lits);
      for (j = 0; j < model->justice[i].size; j++)
	res->justice[i].lits[j].lit = import (res, model->justice[i].lits[j]);
    }

    res->join = newvar ();
    if (time) {
      res->loop = newvar ();
      ternary (-res->loop, res->join, prev->loop);
    } else res->loop = res->join;
    for (i = 0; i < model->num_latches; i++) {
      ternary (-res->join, -join[i], res->latches[i].lit);
      ternary (-res->join, join[i], -res->latches[i].lit);
    }
    for (i = 0; i < model->num_latches; i++) {
      ternary (-res->assume, -join[i], res->latches[i].next);
      ternary (-res->assume, join[i], -res->latches[i].next);
    }

    for (i = 0; i < model->num_justice; i++) {
      for (j = 0; j < model->justice[i].size; j++) {
	res->justice[i].lits[j].sat = newvar ();
	add (-res->justice[i].lits[j].sat);
	if (time) add (prev->justice[i].lits[j].sat);
	add (res->justice[i].lits[j].lit);
	add (0);
	add (-res->justice[i].lits[j].sat);
	if (time) add (prev->justice[i].lits[j].sat);
	add (res->loop);
	add (0);
      }
      res->justice[i].sat = newvar ();
      for (j = 0; j < model->justice[i].size; j++)
	binary (-res->justice[i].sat, res->justice[i].lits[j].sat);
    }
    if (model->num_justice > 1) {
      res->onejustified = newvar ();
      add (-res->onejustified);
      for (i = 0; i < model->num_justice; i++) add (res->justice[i].sat);
      add (0);
    } else res->onejustified = res->justice[0].sat;
  }

  if (model->num_justice && model->num_fairness) {
    res->fairness = malloc (model->num_fairness * sizeof *res->fairness);
    for (i = 0; i < model->num_fairness; i++)
      res->fairness[i].lit = import (res, model->fairness[i].lit);
    for (i = 0; i < model->num_fairness; i++) {
      res->fairness[i].sat = newvar ();
      add (-res->fairness[i].sat);
      if (time) add (prev->fairness[i].sat);
      add (res->fairness[i].lit);
      add (0);
      add (-res->fairness[i].sat);
      if (time) add (prev->fairness[i].sat);
      add (res->loop);
      add (0);
    }
    if (model->num_fairness > 1) {
      res->allfair = newvar ();
      for (i = 0; i < model->num_fairness; i++) 
	binary (-res->allfair, res->fairness[i].sat);
    } else res->allfair = res->fairness[0].sat;

    binary (-res->onejustified, res->allfair);
  }

  assert (model->num_bad || model->num_justice);
  add (-res->assume);
  if (model->num_bad) add (res->onebad);
  if (model->num_justice) add (res->onejustified);
  add (0);

  msg (1, "encoded %d", time);
  return res->assume;
}

static int isnum (const char * str) {
  const char * p = str;
  if (!isdigit (*p++)) return 0;
  while (isdigit (*p)) p++;
  return !*p;
}

static const char * usage =
"usage: aigbmc [-h][-v][-m][<model>][<maxk>]\n";

static void print (int lit) {
  int val = deref (lit), ch;
  if (val < 0) ch = '0';
  else if (val > 0) ch = '1';
  else ch = 'x';
  putc (ch, stdout);
}

static void nl () { putc ('\n', stdout); }

int main (int argc, char ** argv) {
  int i, j, k, maxk, lit, found;
  const char * err;
  maxk = -1;
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) {
      printf ("%s", usage);
      exit (0);
    } else if (!strcmp (argv[i], "-v")) verbose++;
    else if (!strcmp (argv[i], "-m")) move = 1;
    else if (argv[i][0] == '-')
      die ("invalid command line option '%s'", argv[i]);
    else if (name && maxk >= 0) 
      die ("unexpected argument '%s'", argv[i]);
    else if (name && !isnum (argv[i]))
      die ("expected number got '%s'", argv[i]);
    else if (maxk < 0 && isnum (argv[i])) maxk = atoi (argv[i]);
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
  msg (1, "MILOA = %u %u %u %u %u",
       model->maxvar,
       model->num_inputs,
       model->num_latches,
       model->num_outputs,
       model->num_ands);
  if (!model->num_bad && !model->num_justice && model->num_constraints)
    wrn ("%u environment constraints but no bad nor justice properties",
         model->num_constraints);
  if (!model->num_justice && model->num_fairness)
    wrn ("%u fairness constraints but no justice properties",
         model->num_fairness);
  if (move && !model->num_bad && !model->num_justice && model->num_outputs) {
    wrn ("using %u outputs as bad properties", model->num_outputs);
    for (i = 0; i < model->num_outputs; i++)
      aiger_add_bad (model, model->outputs[i].lit, 0);
  }
  msg (1, "BCJF = %u %u %u %u",
       model->num_bad,
       model->num_constraints,
       model->num_justice,
       model->num_fairness);
  if (!model->num_bad && !model->num_justice) {
    wrn ("no properties");
    goto DONE;
  }
  aiger_reencode (model);
  firstlatchidx = 1 + model->num_inputs;
  firstandidx = firstlatchidx + model->num_latches;
  msg (2, "reencoded model");
  bad = calloc (model->num_bad, 1);
  justice = calloc (model->num_justice, 1);
  unit (newvar ()), assert (nvars == 1);
  if (model->num_justice) {
    join = malloc (model->num_latches * sizeof *join);
    for (i = 0; i < model->num_latches; i++)
      join[i] = newvar ();
  }
  for (k = 0; k <= maxk; k++) {
    lit = encode ();
    assume (lit);
    if (sat () == 10) break;
    unit (-lit);
  }
  if (k <= maxk) {
    printf ("1\n");
    fflush (stdout);
    found = 0;
    assert (nstates == k + 1);
    for (i = 0; i < model->num_bad; i++)
      if (deref (states[k].bad[i]) > 0)
	printf ("b%d", i), found++;
    for (i = 0; i < model->num_justice; i++)
      if (deref (states[k].justice[i].sat) > 0)
	printf ("j%d", i), found++;
    assert (found);
    assert (model->num_bad || model->num_justice);
    nl ();
    for (i = 0; i < model->num_latches; i++)
      print (states[0].latches[i].lit);
    nl ();
    for (i = 0; i <= k; i++) {
      for (j = 0; j < model->num_inputs; j++)
	print (states[i].inputs[j]);
      nl ();
    }
    printf (".\n");
    fflush (stdout);
  }
DONE:
  reset ();
  return 0;
}
