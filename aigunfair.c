/***************************************************************************
Copyright (c) 2025 Armin Biere, University of Freiburg.
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

static const char *USAGE =
    "usage: aigunfair [-h][-v] [<input> [<output>]]\n"
    "\n"
    "  -h   print this command line option summary\n"
    "  -q   quiet (no warnings)\n"
    "  -v   increase verbosity\n"
    "\n"
    "Each justice property is reduced to a single literal.\n"
    "All fairness constraints are removed and merged into the justice "
    "literal.\n";

static aiger *src, *dst;
static int verbose;

static void die(const char *fmt, ...) {
    va_list ap;
    fputs("*** [aigunfair] error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    exit(1);
}

static void warn(const char *fmt, ...) {
    if (verbose < 0)
        return;
    va_list ap;
    fputs("*** [aigunfair] warning: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static void msg(const char *fmt, ...) {
    va_list ap;
    if (verbose <= 0)
        return;
    fputs("[aigunfair] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static unsigned maxvar;

static unsigned newlit() {
    unsigned res;
    if (!maxvar) {
        assert(dst);
        maxvar = dst->maxvar;
    }
    res = ++maxvar;
    return 2 * res;
}

static unsigned new_and(unsigned rhs0, unsigned rhs1) {
    unsigned lhs = newlit();
    aiger_add_and(dst, lhs, rhs0, rhs1);
    return lhs;
}

static unsigned new_or(unsigned rhs0, unsigned rhs1) {
    unsigned tmp = new_and(aiger_not(rhs0), aiger_not(rhs1));
    return aiger_not(tmp);
}

int main(int argc, char **argv) {
    const char *input, *output, *err;
    unsigned i, j, k, all_violated;
    unsigned total_latches;
    unsigned *lits, *latches;
    int ok;

    input = output = 0;

    for (i = 1; i < (unsigned)argc; i++) {
        if (!strcmp(argv[i], "-h")) {
            printf("%s", USAGE);
            exit(0);
        } else if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp(argv[i], "-q"))
            verbose = -1;
        else if (argv[i][0] == '-')
            die("invalid command line option '%s'", argv[i]);
        else if (output)
            die("too many arguments");
        else if (input)
            output = argv[i];
        else
            input = argv[i];
    }

    src = aiger_init();
    if (input) {
        msg("reading '%s'", input);
        err = aiger_open_and_read_from_file(src, input);
    } else {
        msg("reading '<stdin>'");
        err = aiger_read_from_file(src, stdin);
    }

    if (err)
        die("read error: %s", err);

    msg("read MILOA %u %u %u %u %u BCJF %u %u %u %u", src->maxvar,
        src->num_inputs, src->num_latches, src->num_outputs, src->num_ands,
        src->num_bad, src->num_constraints, src->num_justice,
        src->num_fairness);

    const char *input_path = input ? input : "<stdin>";
    if (!src->num_justice && !src->num_fairness) {
        warn("no fairness nor justice properties in '%s'", input_path);
        dst = src;
        goto COPY;
    }

    dst = aiger_init();

    for (j = 0; j < src->num_inputs; j++)
        aiger_add_input(dst, src->inputs[j].lit, src->inputs[j].name);

    for (j = 0; j < src->num_ands; j++) {
        aiger_and *a = src->ands + j;
        aiger_add_and(dst, a->lhs, a->rhs0, a->rhs1);
    }

    for (j = 0; j < src->num_latches; j++) {
        aiger_symbol *s = src->latches + j;
        aiger_add_latch(dst, s->lit, s->next, s->name);
        if (s->reset)
            aiger_add_reset(dst, s->lit, s->reset);
    }

    for (j = 0; j < src->num_outputs; j++)
        aiger_add_output(dst, src->outputs[j].lit, src->outputs[j].name);

    for (j = 0; j < src->num_bad; j++)
        aiger_add_bad(dst, src->bad[j].lit, src->bad[j].name);

    for (j = 0; j < src->num_constraints; j++)
        aiger_add_constraint(dst, src->constraints[j].lit,
                             src->constraints[j].name);

    msg("initialized copy of original aiger model");

    total_latches = 0;
    for (j = 0; j < src->num_justice; j++) {
        unsigned total = src->num_fairness + src->justice[j].size;
        unsigned single[1];

        if (!total) {
            single[0] = aiger_false;
            aiger_add_justice(dst, 1, single, src->justice[j].name);
            continue;
        }

        if (src->num_fairness == 0 && src->justice[j].size == 1) {
            single[0] = src->justice[j].lits[0];
            aiger_add_justice(dst, 1, single, src->justice[j].name);
            msg("justice property %u is already single-literal", j);
            continue;
        }

        lits = calloc(total, sizeof *lits);
        latches = calloc(total, sizeof *latches);

        k = 0;
        for (i = 0; i < src->num_fairness; i++) {
            lits[k] = src->fairness[i].lit;
            latches[k++] = newlit();
        }

        for (i = 0; i < src->justice[j].size; i++) {
            lits[k] = src->justice[j].lits[i];
            latches[k++] = newlit();
        }

        assert(k == total);

        all_violated = latches[0];
        for (i = 1; i < total; i++)
            all_violated = new_and(all_violated, latches[i]);

        for (i = 0; i < total; i++) {
            unsigned latch_or = new_or(latches[i], lits[i]);
            unsigned next = new_and(aiger_not(all_violated), latch_or);
            aiger_add_latch(dst, latches[i], next, 0);
        }

        single[0] = all_violated;
        aiger_add_justice(dst, 1, single, src->justice[j].name);

        total_latches += total;

        free(lits);
        free(latches);
    }

    if (total_latches)
        msg("added %u latches", total_latches);

    msg("reduced to %u single-literal justice properties", dst->num_justice);

    aiger_reset(src);

COPY:

    aiger_reencode(dst);

    msg("write MILOA %u %u %u %u %u BCJF %u %u %u %u", dst->maxvar,
        dst->num_inputs, dst->num_latches, dst->num_outputs, dst->num_ands,
        dst->num_bad, dst->num_constraints, dst->num_justice,
        dst->num_fairness);

    if (output) {
        msg("writing '%s'", output);
        ok = aiger_open_and_write_to_file(dst, output);
    } else {
        msg("writing '<stdout>'", output);
        ok = aiger_write_to_file(
            dst, (isatty(1) ? aiger_ascii_mode : aiger_binary_mode), stdout);
    }
    if (!ok)
        die("write error");

    aiger_reset(dst);

    return 0;
}
