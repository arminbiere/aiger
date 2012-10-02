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

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define USAGE \
"usage: aigmove [-h][-v][-i][<input> [<output>]]\n" \
"\n" \
"   -h   print command line option summary\n" \
"   -v   increase verbosity\n" \
"   -i   ignore justice and fairness and thus produce old style format\n" \
"   -r   reverse move outputs to bad state constraint section\n" \
"\n" \
"Move all non-primary outputs to the ordinary output section.\n" \
"If a file already exists then 'aigmove' aborts unless\n" \
"it is forced to overwrite it by specifying '-f'.\n"

static aiger * src, * dst;
static unsigned valid, bad, latch, prev;
static int verbose, ignore, reverse;

static void die (const char *fmt, ...) {
  va_list ap;
  fputs ("*** [aigmove] ", stderr);
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
  fputs ("[aigmove] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned next () { return 2*(dst->maxvar+1); }

int main (int argc, char ** argv) {
  const char * input, * output, * err;
  aiger_and * a;
  unsigned j;
  int i, ok;

  input = output = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) { printf ("%s", USAGE); exit (0); }
    else if (!strcmp (argv[i], "-v")) verbose = 1;
    else if (!strcmp (argv[i], "-i")) ignore = 1;
    else if (!strcmp (argv[i], "-r")) reverse = 1;
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
    src->num_inputs, src->num_latches, src->num_outputs, src->num_ands,
    src->num_bad, src->num_constraints, src->num_justice, src->num_fairness);

  if (!ignore && src->num_justice) 
    die ("will not ignore justice properties (use '-i')");
  if (!ignore && src->num_fairness)
    die ("will not ignore fairness properties (use '-i')");

  if (!reverse && src->num_outputs &&
      !src->num_bad && !src->num_constraints && 
      !src->num_justice && !src->num_fairness)
    die ("only outputs founds (use '-r' for reverse move)");

  dst = aiger_init ();
  for (j = 0; j < src->num_inputs; j++)
    aiger_add_input (dst, src->inputs[j].lit, src->inputs[j].name);
  for (j = 0; j < src->num_latches; j++) {
    aiger_add_latch (dst, 
      src->latches[j].lit, src->latches[j].next, src->latches[j].name);
    aiger_add_reset (dst, src->latches[j].lit, src->latches[j].reset);
  }
  for (j = 0; j < src->num_ands; j++) {
    a = src->ands + j;
    aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
  }

  if (reverse) {
    for (j = 0; j < src->num_outputs; j++)
      aiger_add_bad (dst, src->outputs[j].lit, src->outputs[j].name);
  } else {
    for (j = 0; j < src->num_outputs; j++)
      aiger_add_output (dst, src->outputs[j].lit, src->outputs[j].name);

    if (src->num_bad && src->num_constraints) {
      if (src->num_latches) {
	latch = next ();
	valid = latch + 2*src->num_constraints;
	aiger_add_latch (dst, latch, 
			 aiger_not (valid), "AIGMOVE_INVALID_LATCH");
	prev = aiger_not (latch);
	for (j = 0; j < src->num_constraints; j++) {
	  unsigned tmp = latch + 2*(j+1);
	  aiger_add_and (dst, tmp, prev, src->constraints[j].lit);
	  prev = tmp;
	}
	assert (prev == valid);
      } else {
	valid = src->constraints[0].lit;
	for (j = 1; j < src->num_constraints; j++) {
	  unsigned tmp = next ();
	  aiger_add_and (dst, tmp, valid, src->constraints[j].lit);
	  valid = tmp;
	}
      }
      for (j = 0; j < src->num_bad; j++) {
	bad = next ();
	aiger_add_and (dst, bad, valid, src->bad[j].lit);
	aiger_add_output (dst, bad, src->bad[j].name);
      }
    } else
      for (j = 0; j < src->num_bad; j++)
	aiger_add_output (dst, src->bad[j].lit, src->bad[j].name);
  }

  aiger_reset (src);

  msg ("write MILOA %u %u %u %u %u BCJF %u %u %u %u", 
    dst->maxvar,
    dst->num_inputs, dst->num_latches, dst->num_outputs, dst->num_ands,
    dst->num_bad, dst->num_constraints, dst->num_justice, dst->num_fairness);
  
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
