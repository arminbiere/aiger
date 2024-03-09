/***************************************************************************
Copyright (c) 2006-2018, Armin Biere, Johannes Kepler University.
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

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "aiger.h"

static int verbose;

static void msg(const char *fmt, ...) {
  va_list ap;
  if (!verbose)
    return;
  fputs("[aigtocnf] ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}

static void wrn(const char *fmt, ...) {
  va_list ap;
  fflush(stdout);
  fputs("[aigtocnf] WARNING ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}

static void die(const char *fmt, ...) {
  va_list ap;
  fflush(stdout);
  fputs("*** [aigtocnf] ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static int have_same_variable(unsigned a, unsigned b) {
  return aiger_strip(a) == aiger_strip(b);
}

static int distinct_variables(unsigned a, unsigned b, unsigned c) {
  if (aiger_strip(a) == aiger_strip(b))
    return 0;
  if (aiger_strip(a) == aiger_strip(c))
    return 0;
  if (aiger_strip(b) == aiger_strip(c))
    return 0;
  return 1;
}

static int is_xor(aiger *aiger, unsigned lit, unsigned *rhs0ptr,
		  unsigned *rhs1ptr) {
  aiger_and *and, *left, *right;
  unsigned left_rhs0, left_rhs1;
  unsigned right_rhs0, right_rhs1;
  unsigned not_right_rhs0, not_right_rhs1;
  and = aiger_is_and(aiger, lit);
  if (!and)
    return 0;
  if (!aiger_sign(and->rhs0))
    return 0;
  if (!aiger_sign(and->rhs1))
    return 0;
  left = aiger_is_and(aiger, and->rhs0);
  if (!left)
    return 0;
  right = aiger_is_and(aiger, and->rhs1);
  if (!right)
    return 0;
  left_rhs0 = left->rhs0, left_rhs1 = left->rhs1;
  right_rhs0 = right->rhs0, right_rhs1 = right->rhs1;
  not_right_rhs0 = aiger_not(right_rhs0);
  not_right_rhs1 = aiger_not(right_rhs1);
  //      (!l0 | !l1) & (!r0 | !r1)
  // (A): ( r0 |  r1) & (!r0 | !r1)
  // (B): ( r1 |  r0) & (!r0 | !r1)
  //        r0 ^  r1                 // used
  //        l0 ^  l1                 // not used
  if ((left_rhs0 == not_right_rhs0 && left_rhs1 == not_right_rhs1) || //(A)
      (left_rhs0 == not_right_rhs1 && left_rhs1 == not_right_rhs0)) { //(B)
    const unsigned rhs0 = left_rhs0, rhs1 = left_rhs1;
    if (have_same_variable(rhs0, rhs1))
      return 0;
    if (rhs0ptr)
      *rhs0ptr = rhs0;
    if (rhs1ptr)
      *rhs1ptr = rhs1;
    return 1;
  }
  return 0;
}

static int is_ite(aiger *aiger, unsigned lit, unsigned *cond_lit_ptr,
		  unsigned *then_lit_ptr, unsigned *else_lit_ptr) {
  aiger_and *and, *left, *right;
  unsigned left_rhs0, left_rhs1;
  unsigned right_rhs0, right_rhs1;
  unsigned not_left_rhs0, not_left_rhs1;
  unsigned not_right_rhs0, not_right_rhs1;
  unsigned cond_lit, then_lit, else_lit;
  int res = 1;
  and = aiger_is_and(aiger, lit);
  if (!and)
    return 0;
  if (!aiger_sign(and->rhs0))
    return 0;
  if (!aiger_sign(and->rhs1))
    return 0;
  left = aiger_is_and(aiger, and->rhs0);
  if (!left)
    return 0;
  right = aiger_is_and(aiger, and->rhs1);
  if (!right)
    return 0;
  left_rhs0 = left->rhs0, left_rhs1 = left->rhs1;
  right_rhs0 = right->rhs0, right_rhs1 = right->rhs1;
  not_left_rhs0 = aiger_not(left_rhs0);
  not_left_rhs1 = aiger_not(left_rhs1);
  not_right_rhs0 = aiger_not(right_rhs0);
  not_right_rhs1 = aiger_not(right_rhs1);
  if (left_rhs0 == not_right_rhs0) {
    // (!l0 | !l1) & (!r0 | !r1)
    // (!l0 | !l1) & (l0 | !r1)
    // (l0 -> !l1) & (!l0 -> !r1)
    // l0 ? !l1 : !r1
    cond_lit = left_rhs0;
    then_lit = not_left_rhs1;
    else_lit = not_right_rhs1;
  } else if (left_rhs0 == not_right_rhs1) {
    // (!l0 | !l1) & (!r0 | !r1)
    // (!l0 | !l1) & (!r0 | l0)
    // (l0 -> !l1) & (!r0 <- !l0)
    // l0 ? !l1 : !r0
    cond_lit = left_rhs0;
    then_lit = not_left_rhs1;
    else_lit = not_right_rhs0;
  } else if (left_rhs1 == not_right_rhs0) {
    // (!l0 | !l1) & (!r0 | !r1)
    // (!l0 | !l1) & (l1 | !r1)
    // (!l0 <- l1) & (!l1 -> !r1)
    // l1 ? !l0 : !r1
    cond_lit = left_rhs1;
    then_lit = not_left_rhs0;
    else_lit = not_right_rhs1;
  } else if (left_rhs1 == not_right_rhs1) {
    // (!l0 | !l1) & (!r0 | !r1)
    // (!l0 | !l1) & (!r0 | l1)
    // (!l0 <- l1) & (!r0 <- !l1)
    // l1 ? !l0 : !r0
    cond_lit = left_rhs1;
    then_lit = not_left_rhs0;
    else_lit = not_right_rhs0;
  } else
    return 0;
  if (aiger_is_constant(cond_lit))
    return 0;
  if (aiger_is_constant(then_lit))
    return 0;
  if (aiger_is_constant(else_lit))
    return 0;
  if (!distinct_variables(cond_lit, then_lit, else_lit))
    return 0;
  if (cond_lit_ptr)
    *cond_lit_ptr = cond_lit;
  if (then_lit_ptr)
    *then_lit_ptr = then_lit;
  if (else_lit_ptr)
    *else_lit_ptr = else_lit;
  return 1;
}

int main(int argc, char **argv) {
  const char *input_name, *output_name, *error;
  int res, *map, m, n, close_file, nocoi, nopg, noxor, noite, prtmap;
  unsigned i, *refs, lit;
  aiger *aiger;
  FILE *file;

  nocoi = 0;
  nopg = 0;
  noxor = 0;
  noite = 0;
  prtmap = 0;
  res = close_file = 0;
  output_name = input_name = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h")) {
      fprintf(stderr,
	      "usage: "
	      "aigtocnf [-h][-v][-m][--no-coi][--no-pg][--no-xor][--no-ite] "
	      "[ <aig-file> [ <dimacs-file> ] ]\n");
      exit(0);
    } else if (!strcmp(argv[i], "-m"))
      prtmap = 1;
    else if (!strcmp(argv[i], "-v"))
      verbose++;
    else if (!strcmp(argv[i], "--no-coi"))
      nocoi = 1;
    else if (!strcmp(argv[i], "--no-pg"))
      nopg = 1;
    else if (!strcmp(argv[i], "--no-xor"))
      noxor = 1;
    else if (!strcmp(argv[i], "--no-ite"))
      noite = 1;
    else if (argv[i][0] == '-')
      die("invalid command line option '%s'", argv[i]);
    else if (!input_name)
      input_name = argv[i];
    else if (!output_name)
      output_name = argv[i];
    else
      die("more than two files specified");
  }

  aiger = aiger_init();

  if (input_name)
    error = aiger_open_and_read_from_file(aiger, input_name);
  else
    error = aiger_read_from_file(aiger, stdin);

  if (error)
    die("%s: %s", input_name ? input_name : "<stdin>", error);

  msg("read MILOA %u %u %u %u %u", aiger->maxvar, aiger->num_inputs,
      aiger->num_latches, aiger->num_outputs, aiger->num_ands);

  if (aiger->num_latches)
    die("can not handle latches");
  if (aiger->num_bad)
    die("can not handle bad state properties (use 'aigmove')");
  if (aiger->num_constraints)
    die("can not handle environment constraints (use 'aigmove')");
  if (!aiger->num_outputs)
    die("no output");
  if (aiger->num_outputs > 1)
    die("more than one output");
  if (aiger->num_justice)
    wrn("ignoring justice properties");
  if (aiger->num_fairness)
    wrn("ignoring fairness constraints");

  close_file = 0;
  if (output_name) {
    file = fopen(output_name, "w");
    if (!file)
      die("failed to write '%s'", output_name);
    close_file = 1;
  } else
    file = stdout;

  aiger_reencode(aiger);

  if (aiger->outputs[0].lit == 0) {
    msg("p cnf %u 1", aiger->num_inputs);
    fprintf(file, "p cnf %u 1\n0\n", aiger->num_inputs);
  } else if (aiger->outputs[0].lit == 1) {
    msg("p cnf %u 0", aiger->num_inputs);
    fprintf(file, "p cnf %u 0\n", aiger->num_inputs);
  } else {
    refs = calloc(2 * (aiger->maxvar + 1), sizeof *refs);

    lit = aiger->outputs[0].lit;
    refs[lit]++;

    i = aiger->num_ands;
    while (i--) {
      unsigned not_lhs, not_rhs0, not_rhs1;
      unsigned cond_lit, then_lit, else_lit;
      unsigned not_cond, not_then, not_else;
      unsigned lhs, rhs0, rhs1;
      aiger_and *and;
      int xor, ite;
      and = aiger->ands + i;
      lhs = and->lhs;
      not_lhs = aiger_not(lhs);
      if (!refs[lhs] && !refs[not_lhs])
	continue;
      rhs0 = and->rhs0;
      rhs1 = and->rhs1;
      xor = !noxor &&is_xor(aiger, lhs, &rhs0, &rhs1);
      ite = !xor&&!noite &&is_ite(aiger, lhs, &cond_lit, &then_lit, &else_lit);
      not_rhs0 = aiger_not(rhs0);
      not_rhs1 = aiger_not(rhs1);
      not_cond = aiger_not(cond_lit);
      not_then = aiger_not(then_lit);
      not_else = aiger_not(else_lit);
      if (ite) {
	if (refs[lhs] || refs[not_lhs]) {
	  refs[cond_lit]++;
	  refs[not_cond]++;
	}
	if (refs[lhs]) {
	  refs[then_lit]++;
	  refs[else_lit]++;
	}
	if (refs[not_lhs]) {
	  refs[not_then]++;
	  refs[not_else]++;
	}
      } else {
	if (xor || refs[lhs]) {
	  refs[rhs0]++;
	  refs[rhs1]++;
	}
	if (xor || refs[not_lhs]) {
	  refs[not_rhs0]++;
	  refs[not_rhs1]++;
	}
      }
    }

    if (nopg) {
      if (nocoi) {
	for (lit = 2; lit <= 2 * aiger->maxvar + 1; lit++)
	  refs[lit] = INT_MAX;
      } else {
	for (lit = 2; lit != 2 * aiger->maxvar; lit += 2) {
	  unsigned not_lit = lit + 1;
	  if (refs[lit] && !refs[not_lit])
	    refs[not_lit] = UINT_MAX;
	  if (!refs[lit] && refs[not_lit])
	    refs[lit] = UINT_MAX;
	}
      }
    }

    map = calloc(2 * (aiger->maxvar + 1), sizeof *map);
    m = 0;
    n = 1;
    if (refs[0] || refs[1]) {
      map[0] = -1;
      map[1] = 1;
      m++;
      n++;
    }
    for (lit = 2; lit <= 2 * aiger->maxvar; lit += 2) {
      unsigned not_lit;
      not_lit = lit + 1;
      if (!refs[lit] && !refs[not_lit])
	continue;
      map[lit] = ++m;
      map[not_lit] = -m;
      if (prtmap)
	fprintf(file, "c %d -> %d\n", lit, m);
      if (!aiger_is_and(aiger, lit))
	continue;
      if ((!noxor && is_xor(aiger, lit, 0, 0)) ||
	  (!noite && is_ite(aiger, lit, 0, 0, 0))) {
	if (refs[lit])
	  n += 2;
	if (refs[not_lit])
	  n += 2;
      } else {
	if (refs[lit])
	  n += 2;
	if (refs[not_lit])
	  n += 1;
      }
    }

    fprintf(file, "p cnf %u %u\n", m, n);
    msg("p cnf %u %u", m, n);

    if (refs[0] || refs[1])
      fprintf(file, "%d 0\n", map[1]);

    for (i = 0; i < aiger->num_ands; i++) {
      unsigned not_lhs, not_rhs0, not_rhs1;
      unsigned cond_lit, then_lit, else_lit;
      unsigned not_cond, not_then, not_else;
      unsigned lhs, rhs0, rhs1;
      aiger_and *and;
      int xor, ite;
      and = aiger->ands + i;
      lhs = and->lhs;
      rhs0 = and->rhs0;
      rhs1 = and->rhs1;
      xor = !noxor &&is_xor(aiger, lhs, &rhs0, &rhs1);
      ite = !xor&&!noite &&is_ite(aiger, lhs, &cond_lit, &then_lit, &else_lit);
      not_lhs = aiger_not(lhs);
      not_rhs0 = aiger_not(rhs0);
      not_rhs1 = aiger_not(rhs1);
      not_cond = aiger_not(cond_lit);
      not_then = aiger_not(then_lit);
      not_else = aiger_not(else_lit);
      if (refs[lhs]) {
	if (xor) {
	  fprintf(file, "%d %d %d 0\n", map[not_lhs], map[rhs0], map[rhs1]);
	  fprintf(file, "%d %d %d 0\n", map[not_lhs], map[not_rhs0],
		  map[not_rhs1]);
	} else if (ite) {
	  fprintf(file, "%d %d %d 0\n", map[not_lhs], map[not_cond],
		  map[then_lit]);
	  fprintf(file, "%d %d %d 0\n", map[not_lhs], map[cond_lit],
		  map[else_lit]);
	} else {
	  fprintf(file, "%d %d 0\n", map[not_lhs], map[rhs1]);
	  fprintf(file, "%d %d 0\n", map[not_lhs], map[rhs0]);
	}
      }
      if (refs[not_lhs]) {
	if (xor) {
	  fprintf(file, "%d %d %d 0\n", map[lhs], map[rhs0], map[not_rhs1]);
	  fprintf(file, "%d %d %d 0\n", map[lhs], map[not_rhs0], map[rhs1]);
	} else if (ite) {
	  fprintf(file, "%d %d %d 0\n", map[lhs], map[not_cond], map[not_then]);
	  fprintf(file, "%d %d %d 0\n", map[lhs], map[cond_lit], map[not_else]);
	} else
	  fprintf(file, "%d %d %d 0\n", map[lhs], map[not_rhs1], map[not_rhs0]);
      }
    }

    fprintf(file, "%d 0\n", map[aiger->outputs[0].lit]);

    free(refs);
    free(map);
  }

  if (close_file)
    fclose(file);
  aiger_reset(aiger);

  return res;
}
