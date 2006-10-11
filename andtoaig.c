/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USAGE \
"usage: andtoaig [-a][-h][ src [ dst ]]\n" \
"\n" \
"  -h   print this command line option summary\n" \
"  -a   force ascii format\n" \
"  dst  output file in aiger format (default binary format)\n" \
"  src  file with definitions of AND gates (as in ASCII aiger format)\n"

int 
main (int argc, char ** argv)
{
  const char * src, * dst, * error;
  unsigned lhs, rhs0, rhs1;
  unsigned i, close_file;
  aiger_and * and;
  int res, ascii;
  aiger * aiger;
  char * used;
  FILE * file;

  src = dst = 0;
  ascii = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, USAGE);
	  return 0;
	}
      else if (!strcmp (argv[i], "-a"))
	{
	  ascii = 1;
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr,
		   "*** [andtoaig] invalid command line option '%s'\n",
		   argv[i]);
	  return 1;
	}
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	{
	  fprintf (stderr, "*** [andtoaig] more than two files specified\n"); 
	  return 1;
	}
    }

  if (src)
    {
      file = fopen (src, "r");
      if (!file)
	{
	  fprintf (stderr, "*** [andtoaig] can not read '%s'\n", src);
	  return 1;
	}

      close_file = 1;
    }
  else
    {
      file = stdin;
      close_file = 0;
    }
  
  if (ascii && dst)
    {
      fprintf (stderr, "*** [andtoaig] ascii format and 'dst' specified\n");
      return 1;
    }

  if (!ascii && !dst && isatty (1))
    {
      fprintf (stderr,
	       "*** [andtoaig] will not write binary data to terminal\n");
      return 1;
    }
  
  aiger = aiger_init ();

  while (fscanf (file, "%d %d %d\n", &lhs, &rhs0, &rhs1) == 3)
    aiger_add_and (aiger, lhs, rhs0, rhs1);

  if (close_file)
    fclose (file);

  used = calloc (aiger->maxvar + 1, 1);

  for (i = 0; i < aiger->num_ands; i++)
    {
      and = aiger->ands + i;
      used[aiger_lit2var (and->rhs0)] = 1;
      used[aiger_lit2var (and->rhs1)] = 1;
    }

  for (i = 2; i <= 2 * aiger->maxvar; i += 2)
    {
      and = aiger_is_and (aiger, i);

      if (used[aiger_lit2var (i)])
	{
	  if (!and)
	    aiger_add_input (aiger, i, 0);
	}
      else if (and)
	aiger_add_output (aiger, i, 0);
    }

  free (used);

  error = aiger_check (aiger);
  if (error)
    {
      fprintf (stderr, "*** [andtoaig] %s\n", error);
      res = 1;
    } 
  else
    {
      aiger_add_comment (aiger, "andtoaig");
      aiger_add_comment (aiger, src ? src : "<stdin>");

      if (dst)
	res = !aiger_open_and_write_to_file (aiger, dst);
      else
	{
	  aiger_mode mode = ascii ? aiger_ascii_mode : aiger_binary_mode;
	  res = !aiger_write_to_file (aiger, mode, stdout);
	}

      if (res)
	{
	  fprintf (stderr, 
		   "*** [andtoaig] writing to '%s' failed\n",
		   dst ? dst : "<stdout>");
	}
    }

  aiger_reset (aiger);

  return res;
}
