/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <string.h>
#include <stdlib.h>

static FILE *file;
static int close_file;
static unsigned char *val;

#define deref(lit) (val[aiger_lit2var(lit)] ^ aiger_sign(lit))

static void
put (unsigned lit)
{
  unsigned v = deref (lit);
  if (v & 2)
    fputc ('x', stdout);
  else
    fputc ('0' + (v & 1), stdout);
}

#define USAGE \
"usage: aigsim [-h][-c][-r n] model [vectors]\n" \
"\n" \
"-h              usage\n" \
"-c              check for 1 output and suppress simulation vectors\n" \
"-q              quit after an output became 1\n" \
"-3              three valued inputs in random simulation\n" \
"-r <vectors>    random stimulus of n input vectors\n" \
"model           as AIG\n" \
"vectors         file of 0/1/x input vectors\n"

int
main (int argc, char **argv)
{
  const char *vectors_file_name, *model_file_name, *error;
  int res, ch, vectors, check, found, print, quit, three;
  unsigned i, j, s, l, r, tmp;
  aiger *aiger;

  vectors_file_name = model_file_name = 0;
  quit = check = 0;
  vectors = -1;
  three = 0;

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
      else if (!strcmp (argv[i], "-3"))
	three = 1;
      else if (!strcmp (argv[i], "-r"))
	{
	  if (i + 1 == argc)
	    {
	      fprintf (stderr, "*** [aigsim] argument to '-r' missing\n");
	      exit (1);
	    }

	  vectors = atoi (argv[++i]);
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

  if (vectors >= 0 && three)
    {
      fprintf (stderr, "*** [aigsim] '-3' without '-r <vectors>'\n");
      exit (1);
    }

  if (!model_file_name)
    {
      fprintf (stderr, "*** [aigsim] no model specified\n");
      exit (1);
    }

  if (vectors >= 0 && vectors_file_name)
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
	  while (!res && vectors)
	    {
	      if (vectors > 0)
		{
		  for (j = 1; j <= aiger->num_inputs; j++)
		    {
		      s = 17 * j + i;
		      s %= 20;
		      tmp = rand() >> s;

		      if (three)
			tmp &= 3;
		      else
			tmp &= 1;

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
		  while (j <= aiger->num_inputs)
		    {
		      if (ch == '0')
			val[j] = 0;
		      else if (ch == '1')
			val[j] = 1;
		      else if (ch == 'x')
			val[j] = 2;
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
		  l = deref (and->rhs0);
		  r = deref (and->rhs1);
		  tmp = l & r;
		  tmp |= l & (r << 1);
		  tmp |= r & (l << 1);
		  val[and->lhs / 2] = tmp;
		}

	      found = 0;
	      for (j = 0; !found && j < aiger->num_outputs; j++)
		found = (deref (aiger->outputs[j].lit) == 1);

	      print = !check || found;

	      /* Print current state of latches.
	       */
	      if (print)
		{
		  for (j = 0; j < aiger->num_latches; j++)
		    put (aiger->latches[j].lit);
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
		    put (aiger->inputs[j].lit);
		  fputc (' ', stdout);

		  /* Print outputs.
		   */
		  for (j = 0; j < aiger->num_outputs; j++)
		    put (aiger->outputs[j].lit);
		  fputc (' ', stdout);

		  for (j = 0; j < aiger->num_latches; j++)
		    put (aiger->latches[j].lit);

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
