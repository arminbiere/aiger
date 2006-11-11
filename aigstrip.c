/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int
main (int argc, char **argv)
{
  const char *name, *error;
  char *renamed;
  aiger *aiger;
  int i, res;

  name = 0;
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: aigstrip [-h] file\n");
	  return 0;
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "*** [aigstrip] invalid option\n");
	  return 1;
	}
      else if (name)
	{
	  fprintf (stderr, "*** [aigstrip] multiple files\n");
	  return 1;
	}
      else
	name = argv[i];
    }

  if (!name)
    {
      fprintf (stderr, "*** [aigstrip] file name missing\n");
      return 1;
    }

  res = 0;

  aiger = aiger_init ();
  error = aiger_open_and_read_from_file (aiger, name);

  if (error)
    {
      fprintf (stderr, "*** [aigstrip] %s\n", error);
      res = 1;
    }
  else if (aiger_strip_symbols_and_comments (aiger))
    {
      renamed = malloc (strlen (name) + 2);
      sprintf (renamed, "%s~", name);

      if (rename (name, renamed))
	{
	  fprintf (stderr, "*** [aigstrip] failed to rename '%s'\n", name);
	  res = 1;
	}
      else if (aiger_open_and_write_to_file (aiger, name))
	{
	  if (unlink (renamed))
	    {
	      fprintf (stderr,
		       "*** [aigstrip] failed to remove '%s'\n", renamed);

	      res = 0;		/* !!! */
	    }
	}
      else
	{

	  fprintf (stderr, "*** [aigstrip] failed to write '%s'\n", name);
	  res = 1;

	  if (rename (renamed, name))
	    fprintf (stderr, "*** [aigstrip] backup in '%s'\n", renamed);
	  else
	    fprintf (stderr, "*** [aigstrip] original file restored\n");
	}

      free (renamed);
    }
  else
    {
      fprintf (stderr, "*** [aigstrip] no symbols in '%s'\n", name);

      res = 0;			/* !!! */
    }

  aiger_reset (aiger);

  return res;
}
