#include "aiger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int 
main (int argc, char ** argv)
{
  const char * src, * dst, * error;
  unsigned lhs, rhs0, rhs1, lit;
  aiger_node * parent, * child;
  unsigned i, close_file;
  aiger * aiger;
  FILE * file;
  int res;

  src = dst = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: andstoaig [-h][ src [ dst ]]\n");
	  return 0;
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr,
		   "*** [andstoaig] invalid command line option '%s'\n",
		   argv[i]);
	  return 1;
	}
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	{
	  fprintf (stderr, "*** [andstoaig] more than two files specified\n"); 
	  return 1;
	}
    }

  if (src)
    {
      file = fopen (src, "r");
      if (!file)
	{
	  fprintf (stderr, "*** [andstoaig] can not read '%s'\n", src);
	  return 1;
	}

      close_file = 1;
    }
  else
    {
      file = stdin;
      close_file = 0;
    }
  
  aiger = aiger_init ();

  while (fscanf (file, "%d %d %d\n", &lhs, &rhs0, &rhs1) == 3)
    aiger_add_and (aiger, lhs, rhs0, rhs1);

  if (close_file)
    fclose (file);

  for (i = 0; i < aiger->num_nodes; i++)
    {
      parent = aiger->nodes + i;

      child = aiger->literals[parent->rhs0].node;
      if (child)
	child->client_data = parent;		/* mark as used */

      child = aiger->literals[parent->rhs1].node;
      if (child)
	child->client_data = parent;		/* mark as used */
    }

  for (i = 2; i <= aiger->max_literal; i++)
    {
    }

  error = aiger_check (aiger);
  if (error)
    {
      fprintf (stderr, "*** [andstoaig] %s\n", error);
      res = 1;
    } 
  else
    {
      if (dst)
	res = !aiger_open_and_write_to_file (aiger, dst);
      else
	res = !aiger_write_to_file (aiger, aiger_ascii_mode, stdout);

      if (res)
	{
	  fprintf (stderr, 
		   "*** [andstoaig] writing to '%s' failed\n",
		   dst ? dst : "<stdout>");
	  if (dst)
	    unlink (dst);
	}
    }

  aiger_reset (aiger);

  return res;
}
