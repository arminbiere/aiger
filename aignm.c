/***************************************************************************
Copyright (c) 2006-2007, Armin Biere, Johannes Kepler University.

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
	  fprintf (stderr, "usage: aignm [-h][input]\n");
	  return 0;
	}
      else if (file_name)
	{
	  fprintf (stderr, "*** [aignm] multiple files\n");
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
      fprintf (stderr, "*** [aignm] %s\n", error);
      res = 1;
    }
  else
    res = (aiger_write_symbols_to_file (aiger, stdout) == EOF);

  aiger_reset (aiger);

  return res;
}
