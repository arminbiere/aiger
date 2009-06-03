#include "aiger.h"

#include <stdarg.h>
#include <stdlib.h>

static int combinational, verbosity;
static int M, I, L, O, A;
static aiger * model;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigfuzz] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

#define USAGE \
"usage: aigfuzz [-h][-v][-c][dst]\n" \
"\n" \
"An AIG fuzzer to generate random aigs.\n" \
"\n" \
"  -h   print this command line option summary\n" \
"  -v   verbose output on 'stderr'\n" \
"  -c   combinational logic only, e.g. no latches\n" \
"  dst  output file with 'stdout' as default\n"

int
main (int argc, char ** argv)
{
  const char *dst = 0;
  int i;

  for (i = 1; i < argc; i++) 
    {
      if (!strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}

      if (!strcmp (argv[i], "-v"))
	verbosity = 1;
      else if (!strcmp (argv[i], "-c"))
	combinational = 1;
      else if (argv[i][0] == '-')
	die ("invalid command line option '%s'", argv[i]);
      else if (dst)
	die ("multiple output names '%s' and '%s'", dst, argv[i]);
      else
	dst = argv[i];
    }

  model = aiger_init ();
  aiger_reset (model);
  return 0;
}
