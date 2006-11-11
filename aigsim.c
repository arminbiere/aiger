/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <string.h>
#include <stdlib.h>

static FILE *file;
static int close_file;

#define deref(lit) (val[aiger_lit2var(lit)] ^ aiger_sign(lit))

#define USAGE \
"usage: aigsim [-h][-c][-r n] model [vectors]\n" \
"\n" \
"-h      usage\n" \
"-c      check for 1 output and suppress remaining simulation vectors\n" \
"-q      quit after an output became 1\n" \
"-r n    random stimulus of n input vectors\n" \
"model   as AIG\n" \
"vectors file of 0/1 input vectors\n"

int
main (int argc, char **argv)
{
  const char *vectors_file_name, *model_file_name, *error;
  int res, ch, r, check, found, print, quit;
  unsigned char *val;
  unsigned i, j;
  aiger *aiger;

  vectors_file_name = model_file_name = 0;
  quit = check = 0;
  r = -1;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-c"))
	check = 1;
      else if (!strcmp (argv[i], "-q"))
	quit = 1;
      else if (!strcmp (argv[i], "-r"))
	{
	  if (i + 1 == argc)
	    {
	      fprintf (stderr, "*** [aigsim] argument to '-r' missing\n");
	      exit (1);
	    }

	  r = atoi (argv[++i]);
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "*** [aigsim] invalid option '%s'\n", argv[i]);
	  exit (1);
	}
      else if (!model_file_name)
	model_file_name = argv[i];
      else if (!vectors_file_name)
	vectors_file_name = argv[i];
      else
	{
	  fprintf (stderr, "*** [aigsim] more than two files specified\n");
	  exit (1);
	}
    }

  if (!model_file_name)
    {
      fprintf (stderr, "*** [aigsim] no model specified\n");
      exit (1);
    }

  if (r >= 0 && vectors_file_name)
    {
      fprintf (stderr,
	       "*** [aigsim] random simulation and stimulus file specified\n");
      exit (1);
    }

  res = 0;

  aiger = aiger_init ();
  error = aiger_open_and_read_from_file (aiger, model_file_name);

  if (error)
    {
      fprintf (stderr, "*** [aigsim] %s\n", error);
      res = 1;
    }
  else
    {
      if (vectors_file_name)
	{
	  file = fopen (vectors_file_name, "r");
	  if (!file)
	    {
	      fprintf (stderr,
		       "*** [aigsim] failed to open '%s'\n",
		       vectors_file_name);
	      res = 1;
	    }
	  else
	    close_file = 1;
	}
      else
	file = stdin;

      if (!res)
	{
	  aiger_reencode (aiger);

	  val = calloc (aiger->maxvar + 1, sizeof (val[0]));

	  i = 1;
	  while (!res && r)
	    {
	      if (r > 0)
		{
		  for (j = 1; j <= aiger->num_inputs; j++)
		    val[j] = !(rand () & (1 << ((17 * j + i) % 20)));

		  r--;
		}
	      else
		{
		  j = 1;
		  ch = getc (file);

		  if (ch == EOF)
		    break;

		  /* First read and overwrite inputs.
		   */
		  while (j <= aiger->num_inputs)
		    {
		      if (ch == '0')
			val[j] = 0;
		      else if (ch == '1')
			val[j] = 1;
		      else
			{
			  fprintf (stderr,
				   "*** [aigsim] "
				   "line %u: pos %u: expected '0' or '1'\n",
				   i, j);
			  res = 1;
			  break;
			}

		      j++;
		      ch = getc (file);
		    }

		  if (res)
		    break;

		  if (ch != '\n')
		    {
		      fprintf (stderr,
			       "*** [aigsim] "
			       "line %u: pos %u: expected new line\n", i, j);
		      res = 1;
		      break;
		    }
		}

	      /* Simulate AND nodes.
	       */
	      for (j = 0; j < aiger->num_ands; j++)
		{
		  aiger_and *and = aiger->ands + j;
		  val[and->lhs / 2] = deref (and->rhs0) & deref (and->rhs1);
		}

	      found = 0;
	      for (j = 0; !found && j < aiger->num_outputs; j++)
		found = deref (aiger->outputs[j].lit);

	      print = !check || found;

	      /* Print current state of latches.
	       */
	      if (aiger->num_latches && print)
		{
		  for (j = 0; j < aiger->num_latches; j++)
		    fputc ('0' + deref (aiger->latches[j].lit), stdout);
		  fputc (' ', stdout);
		}

	      /* Then update latches.
	       */
	      for (j = 0; j < aiger->num_latches; j++)
		{
		  aiger_symbol *symbol = aiger->latches + j;
		  val[symbol->lit / 2] = deref (symbol->next);
		}

	      if (print)
		{
		  /* Print inputs.
		   */
		  for (j = 0; j < aiger->num_inputs; j++)
		    fputc ('0' + deref (aiger->inputs[j].lit), stdout);

		  /* Print outputs.
		   */
		  if (aiger->num_outputs)
		    {
		      if (aiger->num_inputs)
			fputc (' ', stdout);

		      for (j = 0; j < aiger->num_outputs; j++)
			fputc ('0' + deref (aiger->outputs[j].lit), stdout);
		    }

		  /* Print next state of latches.
		   */
		  if (aiger->num_latches)
		    {
		      if (aiger->num_inputs || aiger->num_outputs)
			fputc (' ', stdout);

		      for (j = 0; j < aiger->num_latches; j++)
			fputc ('0' + deref (aiger->latches[j].lit), stdout);
		    }

		  fputc ('\n', stdout);
		}

	      i++;

	      if (found && quit)
		break;
	    }

	  free (val);
	}

      if (close_file)
	fclose (file);
    }

  aiger_reset (aiger);

  return res;
}
