#include "aiger.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PERCENT(a,b) ((b) ? (100.0 * (a))/ (double)(b) : 0.0)

typedef struct stream stream;
typedef struct memory memory;

struct stream
{
  double bytes;
  FILE * file;
};

struct memory
{
  double bytes;
  double max;
};

static void *
aigtoaig_malloc (memory * m, size_t bytes)
{
  m->bytes += bytes;
  assert (m->bytes);
  if (m->bytes > m->max)
    m->max = m->bytes;
  return malloc (bytes);
}

static void
aigtoaig_free (memory * m, void * ptr, size_t bytes)
{
  assert (m->bytes >= bytes);
  m->bytes -= bytes;
  free (ptr);
}

static int
aigtoaig_put (char ch, stream * stream)
{
  int res;
  
  res = putc ((unsigned char) ch, stream->file);
  if (res != EOF)
    stream->bytes++;

  return res;
}

static int
aigtoaig_get (stream * stream)
{
  int res;
  
  res = getc (stream->file);
  if (res != EOF)
    stream->bytes++;

  return res;
}

static double
size_of_file (const char * file_name)
{
  struct stat buf;
  buf.st_size = 0;
  stat (file_name, &buf);
  return buf.st_size;
}

int
main (int argc, char ** argv)
{
  int verbose, binary, compact, res;
  const char * src, * dst, * error;
  stream reader, writer;
  aiger_mode mode;
  memory memory;
  aiger * aiger;
  unsigned i;

  res = verbose = binary = compact = 0;
  src = dst = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (stderr, 
	            "usage: "
		    "aigtoaig [-h][-v][--binary][--compact][src [dst]]\n");
	  exit (0);
	}
      else if (!strcmp (argv[i], "-v"))
	verbose = 1;
      else if (!strcmp (argv[i], "--binary"))
	binary = 1;
      else if (!strcmp (argv[i], "--compact"))
	compact = 1;
      else if (argv[i][0] == '-')
	{
	  fprintf (stderr, "*** [aigtoaig] invalid command line option\n");
	  exit (1);
	}
      else if (!src)
	src = argv[i];
      else if (!dst)
	dst = argv[i];
      else
	{
	  fprintf (stderr, "*** [aigtoaig] more than two files specified\n");
	  exit (1);
	}
    }

  if (dst && binary)
    {
      fprintf (stderr, "*** [aigtoaig] 'dst' file and '--binary' specified\n");
      exit (1);
    }

  if (dst && compact)
    {
      fprintf (stderr, "*** [aigtoaig] 'dst' file and '--compact' specified\n");
      exit (1);
    }

  if (!dst && binary && isatty (1))
    {
      fprintf (stderr,
	  "*** [aigtoaig] "
	  "will not write binary file to stdout connected to terminal\n");
      exit (1);
    }

  if (binary && compact)
    {
      fprintf (stderr,
	       "*** [aigtoaig] '--binary' and '--compact' specified\n");
      exit (1);
    }

  if (src && dst && !strcmp (src, dst))
    {
      fprintf (stderr, "*** [aigtoaig] identical 'src' and 'dst' file\n");
      exit (1);
    }

  memory.max = memory.bytes = 0;
  aiger = aiger_init_mem (&memory,
                          (aiger_malloc) aigtoaig_malloc,
                          (aiger_free) aigtoaig_free);
  if (src)
    {
      error = aiger_open_and_read_from_file (aiger, src);
      if (error)
	{
READ_ERROR:
	  fprintf (stderr, "*** [aigtoaig] %s\n", error);
	  res = 1;
	}
      else
	{
	  reader.bytes = size_of_file (src);

	  if (verbose)
	    {
	      fprintf (stderr,
		       "[aigtoaig] read from '%s' (%.0f bytes)\n",
		       src, (double) reader.bytes);
	      fflush (stderr);
	    }
	         
	  if (dst)
	    {
	      if (aiger_open_and_write_to_file (aiger, dst))
		{
		  writer.bytes = size_of_file (dst);

		  if (verbose)
		    {
		      fprintf (stderr,
			       "[aigtoaig] wrote to '%s' (%.0f bytes)\n",
			       dst, (double) writer.bytes);
		      fflush (stderr);
		    }
		}
	      else
		{
		  unlink (dst);
	WRITE_ERROR:
		  fprintf (stderr, "*** [aigtoai]: write error\n");
		  res = 1;
		}
	    }
	  else
	    {
WRITE_TO_STDOUT:
	      writer.file = stdout;
	      writer.bytes = 0;

	      if (binary)
		mode = aiger_binary_mode;
	      else if (compact)
		mode = aiger_compact_mode;
	      else
		mode = aiger_ascii_mode;

	      if (!aiger_write_generic (aiger, mode,
					&writer, (aiger_put) aigtoaig_put))
		goto WRITE_ERROR;

	      if (verbose)
		{
		  fprintf (stderr,
			   "[aigtoaig] wrote to '<stdout>' (%.0f bytes)\n",
			   (double) writer.bytes);
		  fflush (stderr);
		}
	    }
	}
    }
  else
    {
      reader.file = stdin;
      reader.bytes = 0;

      error = aiger_read_generic (aiger, &reader, (aiger_get) aigtoaig_get);

      if (error)
	goto READ_ERROR;

      if (verbose)
	{
	  fprintf (stderr,
		   "[aigtoaig] read from '<stdin>' (%.0f bytes)\n",
		   (double) reader.bytes);
	  fflush (stderr);
	}

      goto WRITE_TO_STDOUT;
    }

  aiger_reset (aiger);

  if (!res && verbose)
    {
      if (reader.bytes > writer.bytes)
	fprintf (stderr, "[aigtoaig] deflated to %.1f%%\n",
	         PERCENT (writer.bytes, reader.bytes));
      else
	fprintf (stderr, "[aigtoaig] inflated to %.1f%%\n",
	         PERCENT (writer.bytes, reader.bytes));

      fprintf (stderr,
	       "[aigtoaig] allocated %.0f bytes maximum\n", memory.max);

      fflush (stderr);
    }

  return res;
}
