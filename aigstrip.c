/***************************************************************************
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.

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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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
	  fprintf (stderr, "usage: aigstrip [-h][<file>]\n");
	  return 0;
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "*** [aigstrip] invalid option '%s'\n", argv[i]);
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

  res = 0;

  aiger = aiger_init ();
  if (!name) 
    {
      if((error = aiger_read_from_file (aiger, stdin))) goto PARSE_ERROR;
      (void) aiger_strip_symbols_and_comments (aiger);
      if (!aiger_write_to_file (aiger,
             (isatty (1) ? aiger_ascii_mode : aiger_binary_mode), stdout))
	{
	  fprintf (stderr, "*** [aigstrip] write error\n");
	  res = 1;
	}
    }
  else if ((error = aiger_open_and_read_from_file (aiger, name)))
    {
PARSE_ERROR:
      fprintf (stderr, "*** [aigstrip] read error: %s\n", error);
      res = 1;
    }
  else 
    {
      (void) aiger_strip_symbols_and_comments (aiger);
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

  aiger_reset (aiger);

  return res;
}
