#include "aiger.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int
main (int argc, char ** argv)
{
  int i;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  return 0;
	}
    }

  return 0;
}
