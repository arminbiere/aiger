/***************************************************************************
Copyright (c) 2010-2011, Armin Biere, Johannes Kepler University.

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

#define USAGE \
"usage: aigsplit [-h][-v][-f] <input> [<prefix>]\n" \
"\n" \
"Split all outputs of the input AIGER model.  For each output a new file\n" \
"will be generated with name '<prefix>[0-9]*.aig'.  If a file already\n" \
"exists then 'aigsplits' aborts unless it is forced to overwrite it\n" \
"by specifying '-f'.  If '<prefix>' is missing, then the base name of\n" \
"'<input>' is used as prefix.\n"

static aiger * src, * dst;
static int verbose, force;
static char * prefix;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigsplit] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
msg (const char *fmt, ...)
{
  va_list ap;
  if (!verbose)
    return;
  fputs ("[aigsplit] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static unsigned
ld10 (unsigned i)
{
  int res, exp = 10;
  res = 1;
  while (i >= exp)
    {
      exp *= 10;
      res++;
    }
  return res;
}

static char *
chop (const char * src)
{
  char * res = strdup (src), * p = strrchr (res, '.');
  if (p) *p = 0;
  return res;
}

static int 
exists (const char * name) 
{
  struct stat buf;
  return !stat (name, &buf);
}

static void 
print (unsigned i)
{
  char comment[80], fmt[80];
  unsigned j, out, l;
  aiger_and * a;
  char * output;
  int ok;

  l = ld10 (src->num_outputs - 1);
  sprintf (fmt, "%%s%%0%uu.aig", l);
  output = malloc (strlen (prefix) + l + 5);
  sprintf (output, fmt, prefix, i);

  if (!force && exists (output))
    die ("output file '%s' already exists (use '-f')", output);

  msg ("writing %s", output);

  dst = aiger_init ();
  for (j = 0; j < src->num_inputs; j++)
    aiger_add_input (dst, src->inputs[j].lit, src->inputs[j].name);

  for (j = 0; j < src->num_latches; j++)
    aiger_add_latch (dst, src->latches[j].lit, 
                          src->latches[j].next,
                          src->latches[j].name);

  for (j = 0; j < src->num_ands; j++)
    {
      a = src->ands + j;
      aiger_add_and (dst, a->lhs, a->rhs0, a->rhs1);
    }

  assert (i < src->num_outputs);
  out = src->outputs[i].lit;
  aiger_add_output (dst, out, src->outputs[i].name);

  sprintf (comment, "aigsplit");
  aiger_add_comment (dst, comment);
  sprintf (comment, "output %u", i);
  aiger_add_comment (dst, comment);

  ok = aiger_open_and_write_to_file (dst, output);

  if (!ok)
    die ("writing to %s failed", output);

  free (output);

  msg ("wrote M I L O A %u %u %u %u %u", 
       dst->maxvar,
       dst->num_inputs,
       dst->num_latches,
       dst->num_outputs,
       dst->num_ands);

  aiger_reset (dst);
}

int
main (int argc, char ** argv)
{
  const char * input, * err;
  unsigned j;
  int i;

  input = prefix = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (!strcmp (argv[i], "-f"))
	force = 1;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (prefix)
	die ("too many arguments");
      else if (input)
	prefix = chop (argv[i]);
      else
	input = argv[i];
    }

  if (!input) 
    die ("no input specified");

  if (!prefix)
    prefix = chop (input);

  msg ("reading %s", input);
  src = aiger_init ();
  err = aiger_open_and_read_from_file (src, input);

  if (err)
    die ("read error: %s", err);

  msg ("read M I L O A %u %u %u %u %u", 
       src->maxvar,
       src->num_inputs,
       src->num_latches,
       src->num_outputs,
       src->num_ands);

  msg ("prefix %s", prefix);

  for (j = 0; j < src->num_outputs; j++)
    print (j);

  aiger_reset (src);
  free (prefix);

  return 0;
}
