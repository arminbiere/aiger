/***************************************************************************
Copyright (c) 2006, Armin Biere, Johannes Kepler University.

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

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

static FILE *file;
static int close_file;
static unsigned char *val;
static aiger * model;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** [aigsim] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static unsigned char
deref (unsigned lit)
{
  unsigned res = val[aiger_lit2var(lit)];
  res ^= aiger_sign(lit);
#ifndef NDEBUG
  if (lit == 0)
    assert (res == 0);
  if (lit == 1)
    assert (res == 1);
#endif
  return res;
}

static void
put (unsigned lit)
{
  unsigned v = deref (lit);
  if (v & 2)
    fputc ('x', stdout);
  else
    fputc ('0' + (v & 1), stdout);
}

static const char *
idx_as_vcd_id (char ch, unsigned idx)
{
  static char buffer[20];
  sprintf (buffer, "%c%u", ch, idx);
  return buffer;
}

static const char *
aiger_symbol_as_string (aiger_symbol * s)
{
  static char buffer[20];
  if (s->name)
    return s->name;

  sprintf (buffer, "%u", s->lit/2);
  return buffer;
}

#define USAGE \
"usage: aigsim [-h][-c][-r n] [ <model> [<stimulus>] ]\n" \
"\n" \
"-h              usage\n" \
"-c              check for witness and do not print trace\n" \
"-v              produce VCD output trace instead of transitions\n" \
"-q              quit after an output became 1\n" \
"-2              ground three valued stimulus by setting 'x' to '0'\n" \
"-3              enable three valued stimulus in random simulation\n" \
"-r <vectors>    random stimulus of <vectors> input vectors\n" \
"-s <seed>       set seed of random number generator (default '0')\n" \
"<model>         AIG in AIGER format\n" \
"<stimulus>      stimulus (file of 0/1/x input vectors)\n"

int
main (int argc, char **argv)
{
  int vectors, check, vcd, found, print, quit, three, ground, seeded;
  const char *stimulus_file_name, *model_file_name, *error;
  unsigned i, j, s, l, r, tmp, seed;
  int ch;

  stimulus_file_name = model_file_name = 0;
  seeded = vcd = quit = check = 0;
  vectors = -1;
  ground = three = 0;
  seed = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-c"))
	check = 1;
      else if (!strcmp (argv[i], "-v"))
	vcd = 1;
      else if (!strcmp (argv[i], "-q"))
	quit = 1;
      else if (!strcmp (argv[i], "-3"))
	three = 1;
      else if (!strcmp (argv[i], "-2"))
	ground = 1;
      else if (!strcmp (argv[i], "-s"))
	{
	  if (i + 1 == argc)
	    die ("argvument to '-s' missing");

	  seed = atoi (argv[++i]);
	  seeded = 1;
	}
      else if (!strcmp (argv[i], "-r"))
	{
	  if (i + 1 == argc)
	    die ("argument to '-r' missing");

	  vectors = atoi (argv[++i]);
	}
      else if (argv[i][0] == '-')
	die ("invalid option '%s' (try '-h')", argv[i]);
      else if (!model_file_name)
	model_file_name = argv[i];
      else if (!stimulus_file_name)
	stimulus_file_name = argv[i];
      else
	die ("more than two files specified");
    }

  if (!model_file_name && vectors < 0)
    die ("can only read model from <stdin> in random simulation mode");

  if (vectors >= 0 && stimulus_file_name)
    die ("random simulation but also stimulus file specified");
  
  if (seeded && vectors < 0)
    die ("seed given but no random simulation specified");

  if (vectors < 0 && three)
    die ("can use '-3' without '-r <vectors>'");

  if (vectors >= 0 && ground)
    die ("can not combine '-2' with '-r <vectors>'");

  if (check && vcd)
    die ("can not combine '-v' with '-c'");

  model = aiger_init ();

  if (model_file_name)
    error = aiger_open_and_read_from_file (model, model_file_name);
  else
    error = aiger_read_from_file (model, stdin);

  if (error)
    die ("%s", error);

  aiger_reencode (model);	/* otherwise simulation incorrect */

  if (stimulus_file_name)
    {
      file = fopen (stimulus_file_name, "r");
      if (!file)
	die ("failed to open '%s'", stimulus_file_name);

      close_file = 1;
    }
  else
    file = stdin;

  if (vcd)
    {
      for (i = 0; i < model->num_inputs; i++)
	printf ("$var wire 1 %s %s $end\n",
		idx_as_vcd_id ('i', i),
		aiger_symbol_as_string (model->inputs + i));

      for (i = 0; i < model->num_latches; i++)
	printf ("$var reg 1 %s %s $end\n",
		idx_as_vcd_id ('l', i),
		aiger_symbol_as_string (model->latches + i));

      for (i = 0; i < model->num_outputs; i++)
	printf ("$var wire 1 %s %s $end\n",
		idx_as_vcd_id ('o', i),
		aiger_symbol_as_string (model->outputs + i));

      printf ("$enddefinitions $end\n");
    }

  val = calloc (model->maxvar + 1, sizeof (val[0]));

  if (seeded)
    srand (seed);

  i = 1;

  while (vectors)
    {
      if (vectors > 0)
	{
	  for (j = 1; j <= model->num_inputs; j++)
	    {
	      s = 17 * j + i;
	      s %= 20;
	      tmp = rand() >> s;
	      tmp %= three + 2;
	      val [j] = tmp;
	    }

	  vectors--;
	}
      else
	{
	  j = 1;
	  ch = getc (file);

	  if (ch == EOF)
	    break;

	  /* First read and overwrite inputs.
	   */
	  while (j <= model->num_inputs)
	    {
	      if (ch == '0')
		val[j] = 0;
	      else if (ch == '1')
		val[j] = 1;
	      else if (ch == 'x')
		val[j] = ground ? 0 : 2;
	      else
		die ("line %u: pos %u: expected '0' or '1'", i, j);

	      j++;
	      ch = getc (file);
	    }

	  if (ch != '\n')
	    die ("line %u: pos %u: expected new line", i, j);
	}

      /* Simulate AND nodes.
       */
      for (j = 0; j < model->num_ands; j++)
	{
	  aiger_and *and = model->ands + j;
	  l = deref (and->rhs0);
	  r = deref (and->rhs1);
	  tmp = l & r;
	  tmp |= l & (r << 1);
	  tmp |= r & (l << 1);
	  val[and->lhs / 2] = tmp;
	}

      found = 0;
      for (j = 0; !found && j < model->num_outputs; j++)
	found = (deref (model->outputs[j].lit) == 1);

      print = !vcd && (!check || found);

      /* Print current state of latches.
       */
      if (print)
	{
	  for (j = 0; j < model->num_latches; j++)
	    put (model->latches[j].lit);
	  fputc (' ', stdout);
	}

      if (vcd)
	{
	  printf ("#%u\n", i - 1);

	  if (i == 1)
	    printf ("$dumpvars\n");

	  for (j = 0; j < model->num_latches; j++)
	    {
	      put (model->latches[j].lit);
	      fputs (idx_as_vcd_id ('l', j), stdout);
	      fputc ('\n', stdout);
	    }

	  if (i == 1)
	    printf ("$end\n");
	}

      /* Then update latches.
       */
      for (j = 0; j < model->num_latches; j++)
	{
	  aiger_symbol *symbol = model->latches + j;
	  val[symbol->lit / 2] = deref (symbol->next);
	}

      if (print)
	{
	  /* Print inputs.
	   */
	  for (j = 0; j < model->num_inputs; j++)
	    put (model->inputs[j].lit);
	  fputc (' ', stdout);

	  /* Print outputs.
	   */
	  for (j = 0; j < model->num_outputs; j++)
	    put (model->outputs[j].lit);
	  fputc (' ', stdout);

	  /* Print next state of latches.
	   */
	  for (j = 0; j < model->num_latches; j++)
	    put (model->latches[j].lit);

	  fputc ('\n', stdout);
	}

      if (vcd)
	{
	  for (j = 0; j < model->num_inputs; j++)
	    {
	      put (model->inputs[j].lit);
	      fputs (idx_as_vcd_id ('i', j), stdout);
	      fputc ('\n', stdout);
	    }

	  for (j = 0; j < model->num_outputs; j++)
	    {
	      put (model->outputs[j].lit);
	      fputs (idx_as_vcd_id ('o', j), stdout);
	      fputc ('\n', stdout);
	    }
	}

      i++;

      if (found && quit)
	break;
    }

  free (val);

  if (close_file)
    fclose (file);

  aiger_reset (model);

  return 0;
}
