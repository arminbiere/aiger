/*------------------------------------------------------------------------*/
/* (C)opyright 2006, Armin Biere, Johannes Kepler University, see LICENSE */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* THIS IS BROKEN, NEEDS TO BE MADE COMPATIBLE WITH NEW AIGER API */

int 
main (int argc, char ** argv)
{
  aiger_and * parent, * child, * node;
  const char * src, * dst, * error;
  unsigned lhs, rhs0, rhs1;
  unsigned i, close_file;
  aiger * aiger;
  FILE * file;
  int res;

  src = dst = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, "usage: andtoaig [-h][ src [ dst ]]\n");
	  return 0;
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
  
  aiger = aiger_init ();

  while (fscanf (file, "%d %d %d\n", &lhs, &rhs0, &rhs1) == 3)
    aiger_add_and (aiger, lhs, rhs0, rhs1);

  if (close_file)
    fclose (file);

  for (i = 0; i < aiger->num_ands; i++)
    {
      parent = aiger->ands + i;

      literal = aiger->literals + aiger_strip (parent->rhs0);
      child = literal->node;
      if (child)
	child->client_data = parent;		/* mark as used */
      else
	literal->client_bit = 1;

      literal = aiger->literals + aiger_strip (parent->rhs1);
      child = literal->node;
      if (child)
	child->client_data = parent;		/* mark as used */
      else
	literal->client_bit = 1;
    }

  for (i = 2; i <= aiger->max_literal; i += 2)
    {
      literal = aiger->literals + i;
      node = aiger->literals[i].node;
      if (node)
	{
	  if (!node->client_data)
	    aiger_add_output (aiger, i, 0);
	}
      else
	{
	  if (literal->client_bit)
	    aiger_add_input (aiger, i, 0);
	}
    }

  error = aiger_check (aiger);
  if (error)
    {
      fprintf (stderr, "*** [andtoaig] %s\n", error);
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
		   "*** [andtoaig] writing to '%s' failed\n",
		   dst ? dst : "<stdout>");
	  if (dst)
	    unlink (dst);
	}
    }

  aiger_reset (aiger);

  return res;
}
