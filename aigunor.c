// clang-format off

static const char * usage = 
"usage: aigunor [ -h ] [ <input> [ <output> ]\n"
"\n"
"where '-h' prints this command line option summary and '<input>'\n"
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

static const char *input_name;
static const char *output_name;

static struct aiger *input_model;
static struct aiger *output_model;
static char *kept;

static void die(const char *, ...) __attribute__((format(printf, 1, 2)));

static void die(const char *fmt, ...) {
  fputs(fmt, stderr);
  exit(1);
}

static void traverse(unsigned lit) {
  if (lit <= 0)
    return;
  unsigned idx = aiger_lit2var(lit);
  if (kept[idx])
    return;
  kept[idx] = 1;
}

int main(int argc, char **argv) {

  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h")) {
      fputs(usage, stdout);
      return 0;
    } else if (arg[0] == '-' && arg[1])
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
    const char *read_error =
        aiger_open_and_read_from_file(input_model, input_name);
    if (read_error)
      die("parse error in '%s': %s", input_name, read_error);
  } else {
    const char *read_error = aiger_read_from_file(input_model, stdin);
    die("parse error in '<stdin>': %s", read_error);
  }

  if (input_model->num_outputs != 1)
    die("expected exactly one output (but got '%u')", input_model->num_outputs);

  struct aiger *output_model = aiger_init();

  kept = calloc(1, input_model->maxvar + 1);
  for (unsigned i = 0; i != input_model->num_inputs; i++) {
    aiger_symbol *input = input_model->inputs + i;
    aiger_add_input(output_model, input->lit, input->name);
  }

  aiger_reset(input_model);

  if (output_name) {
    if (aiger_open_and_write_to_file(output_model, output_name))
      die("could not open and write '%s'", output_name);
  } else {
    aiger_mode mode = isatty(1) ? aiger_ascii_mode : aiger_binary_mode;
    if (aiger_write_to_file(output_model, mode, stdout))
      die("failed to write to '<stdout>'");
  }

  aiger_reset(output_model);
  free(kept);

  return 0;
}
