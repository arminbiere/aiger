#include "aiger.h"

#include <string.h>
#include <stdlib.h>

static FILE * file;
static int close_file;

#define deref(lit) (val[aiger_lit2var(lit)] ^ aiger_sign(lit))

int
main (int argc, char ** argv)
{
  const char * vectors_file_name, * model_file_name, * error;
  unsigned char * val;
  unsigned i, j;
  aiger * aiger;
  int res, ch;

  vectors_file_name = model_file_name = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: aigsim model [vectors]\n");
	  exit (0);
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
	  while (!res)
	    {
	      j = 1;
	      ch = getc (file);

	      if (ch == EOF)
		break;

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
			   "line %u: pos %u: expected new line\n",
			   i, j);
		  res = 1;
		  break;
		}

	      for (j = 0; j < aiger->num_inputs; j++)
		fputc ('0' + deref (aiger->inputs[j].lit), stdout);

	      if (aiger->num_latches)
		{
		  fputc (' ', stdout);
		  for (j = 0; j < aiger->num_latches; j++)
		    fputc ('0' + deref (aiger->latches[j].lit), stdout);
		}

	      if (aiger->num_outputs)
		{
		  fputc (' ', stdout);
		  for (j = 0; j < aiger->num_outputs; j++)
		    fputc ('0' + deref (aiger->outputs[j].lit), stdout);
		}

	      fputc ('\n', stdout);

	      i++;
	    }

	  free (val);
	}

      if (close_file)
	fclose (file);
    }

  aiger_reset (aiger);

  return res;
}
