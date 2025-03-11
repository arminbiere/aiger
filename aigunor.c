// clang-format off

static const char * usage = 
"usage: aigunor [ -h | -v ] [ <input> [ <output> ]\n"
"\n"
"-h  print this command line option summary\n"
"-v  enable verbose message (on 'stderr')\n"
"\n"
"and '<input>'\n"
"is the input AIGER file.  It is assumed to not have exactly one output\n"
"which is then traversed recursively as long it forms an OR gate.  If\n"
"another AIG node is reached, which is not an OR gate it becomes a new\n"
"output.  The AIG with all those outputs is then written to '<output>'.\n"
"Without being specified, the input is taken as '<stdin>' and the output\n"
"as '<stdout>'.\n"

;

// clang-format on

#include "aiger.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int verbosity;
static const char *input_name;
static const char *output_name;
static struct aiger *input_model;
static struct aiger *output_model;
static unsigned num_new_outputs;
static unsigned *new_outputs;
static char *kept;

static void die(const char *, ...) __attribute__((format(printf, 1, 2)));
static void msg(const char *, ...) __attribute__((format(printf, 1, 2)));

static void die(const char *fmt, ...) {
  fputs("aigunor: error: ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void msg(const char *fmt, ...) {
  if (!verbosity)
    return;
  fputs("[aigunor] ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}
static void traverse_rest(unsigned lit) {
  if (lit < 2)
    return;
  unsigned idx = aiger_lit2var(lit);
  if (kept[idx])
    return;
  kept[idx] = 1;
  aiger_and *and = aiger_is_and(input_model, lit);
  assert(and);
  traverse_rest(and->rhs1);
  traverse_rest(and->rhs0);
}

static void add_new_output(unsigned lit) {
  assert(num_new_outputs <= input_model->maxvar);
  new_outputs[num_new_outputs++] = lit;
}

static void traverse_top(unsigned lit) {
  if (lit < 2)
    add_new_output(lit);
  else if (aiger_is_input(input_model, lit))
    add_new_output(lit);
  else {
    aiger_and *and = aiger_is_and(input_model, lit);
    assert(and);
    if (aiger_sign(lit)) {
      traverse_top(aiger_not(and->rhs1));
      traverse_top(aiger_not(and->rhs0));
    } else {
      add_new_output(lit);
      traverse_rest(and->rhs1);
      traverse_rest(and->rhs0);
      unsigned idx = aiger_lit2var(lit);
      kept[idx] = 1;
    }
  }
}

int main(int argc, char **argv) {

  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h")) {
      fputs(usage, stdout);
      return 0;
    } else if (!strcmp(arg, "-v"))
      verbosity = 1;
    else if (arg[0] == '-' && arg[1])
      die("invalid option '%s' (try '-h')", arg);
    else if (!input_name)
      input_name = arg;
    else if (!output_name)
      output_name = arg;
    else
      die("too many files '%s', '%s' and '%s' (try '-h')", input_name,
          output_name, arg);
  }

  if (input_name && !strcmp(input_name, "-"))
    input_name = 0;
  if (output_name && !strcmp(output_name, "-"))
    output_name = 0;
  if (input_name && output_name && !strcmp(input_name, output_name))
    die("can not read and write to the same file '%s'", input_name);

  input_model = aiger_init();

  if (input_name) {
    msg("reading input model from '%s'", input_name);
    const char *read_error =
        aiger_open_and_read_from_file(input_model, input_name);
    if (read_error)
      die("parse error in '%s': %s", input_name, read_error);
  } else {
    msg("reading input model from '<stdin>'");
    const char *read_error = aiger_read_from_file(input_model, stdin);
    die("parse error in '<stdin>': %s", read_error);
  }

  if (input_model->num_outputs != 1)
    die("expected exactly one output (but got '%u')", input_model->num_outputs);

  if (input_model->num_latches)
    die("unsupported '%u' latches", input_model->num_latches);
  if (input_model->num_bad)
    die("unsupported '%u' bad properties", input_model->num_bad);
  if (input_model->num_constraints)
    die("unsupported '%u' environment constraints",
        input_model->num_constraints);
  if (input_model->num_fairness)
    die("unsupported '%u' fairness constraints", input_model->num_fairness);
  if (input_model->num_justice)
    die("unsupported '%u' justice properties", input_model->num_justice);

  msg("parsed 'MILOA' header '%u %u %u %u %u'", input_model->maxvar,
      input_model->num_inputs, input_model->num_latches,
      input_model->num_outputs, input_model->num_ands);

  struct aiger *output_model = aiger_init();

  kept = calloc(input_model->maxvar + 1, 1);
  for (unsigned i = 0; i != input_model->num_inputs; i++) {
    aiger_symbol *input = input_model->inputs + i;
    aiger_add_input(output_model, input->lit, input->name);
    unsigned idx = aiger_lit2var(input->lit);
    kept[idx] = 1;
  }

  new_outputs = calloc(input_model->maxvar + 1, sizeof *new_outputs);
  for (unsigned i = 0; i != input_model->num_outputs; i++) {
    aiger_symbol *output = input_model->outputs + i;
    traverse_top(output->lit);
  }

  for (unsigned idx = 1; idx <= input_model->maxvar; idx++) {
    if (!kept[idx])
      continue;
    unsigned lit = aiger_var2lit(idx);
    if (aiger_is_input(input_model, lit))
      continue;
    aiger_and *and = aiger_is_and(input_model, lit);
    assert(and);
    aiger_add_and(output_model, and->lhs, and->rhs0, and->rhs1);
  }

  assert(num_new_outputs);
  msg("split single output into '%u' new outputs", num_new_outputs);

  for (unsigned i = 0; i != num_new_outputs; i++) {
    unsigned lit = new_outputs[i];
    aiger_add_output(output_model, lit, 0);
  }

  aiger_reset(input_model);

  if (output_name) {
    msg("writing output model to '%s'", output_name);
    if (!aiger_open_and_write_to_file(output_model, output_name))
      die("could not open and write '%s'", output_name);
  } else {
    msg("writing output model to '<stdout>'");
    aiger_mode mode = isatty(1) ? aiger_ascii_mode : aiger_binary_mode;
    if (!aiger_write_to_file(output_model, mode, stdout))
      die("failed to write to '<stdout>'");
  }

  msg("wrote 'MILOA' header '%u %u %u %u %u'", output_model->maxvar,
      output_model->num_outputs, output_model->num_latches,
      output_model->num_outputs, output_model->num_ands);

  aiger_reset(output_model);
  free(new_outputs);
  free(kept);

  return 0;
}
