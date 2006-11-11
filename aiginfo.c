/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <stdio.h>
#include <string.h>

int
main (int argc, char **argv)
{
  const char *file_name, *error;
  aiger *aiger;
  int i, res;

  file_name = 0;
  res = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: aiginfo [-h][input]\n");
	  return 0;
	}
      else if (file_name)
	{
	  fprintf (stderr, "*** [aiginfo] multiple files\n");
	  return 1;
	}
      else
	file_name = argv[i];
    }

  aiger = aiger_init ();

  if (file_name)
    error = aiger_open_and_read_from_file (aiger, file_name);
  else
    error = aiger_read_from_file (aiger, stdin);

  if (error)
    {
      fprintf (stderr, "*** [aiginfo] %s\n", error);
      res = 1;
    }
  else
    res = (aiger_write_comments_to_file (aiger, stdout) == EOF);

  aiger_reset (aiger);

  return res;
}
