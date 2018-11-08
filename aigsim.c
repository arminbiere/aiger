/***************************************************************************
Copyright (c) 2006-2018, Armin Biere, Johannes Kepler University, Austria.
Copyright (c) 2011-2012, Siert Wieringa, Aalto University, Finland.

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

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

static FILE *file;
static int close_file;
static unsigned char *current;
static unsigned char *next;
static aiger *model;
static int filter;

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("*** [aigsim] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static unsigned char
deref (unsigned lit)
{
  unsigned res = current[aiger_lit2var (lit)];
  res ^= aiger_sign (lit);
#ifndef NDEBUG
  if (lit == 0)
    assert (res == 0);
  if (lit == 1)
    assert (res == 1);
#endif
  return res;
}

static void
put (unsigned lit)
{
  unsigned v = deref (lit);
  if (v & 2)
    fputc ('x', stdout);
  else
    fputc ('0' + (v & 1), stdout);
}

static const char *
idx_as_vcd_id (char ch, unsigned idx)
{
  static char buffer[20];
  sprintf (buffer, "%c%u", ch, idx);
  return buffer;
}

static const char *
aiger_symbol_as_string (aiger_symbol * s)
{
  static char buffer[20];
  if (s->name)
    return s->name;

  sprintf (buffer, "%u", s->lit / 2);
  return buffer;
}

static void
print_vcd_symbol (const char *symbol)
{
  const char *p;
  char ch;

  for (p = symbol; (ch = *p); p++)
    fputc ((isspace (ch) ? '"' : ch), stdout);
}

static int last = EOF;
static int first = 0;

static int ignore_line_starting_with (int ch)
{
  if (ch == 'c') return 1;
  if (ch == 'u') return 1;
  if (!filter) return 0;
  if (last != '\n') return 0;
  if (ch == '0') return 0;
  if (ch == '1') return 0;
  if (ch == 'b') return 0;
  if (ch == 'x') return 0;
  if (ch == '.') return 0;
  return 1;
}

static int
nxtc (FILE * file)
{
  int start, ch;
RESTART:
  start = getc (file);
  if (start == EOF) return start;
  if (filter && last == EOF) {
    if (start != '0' && start != '1')
      {
IGNORE_REST_OF_LINE:
	assert (last == '\n' || last == EOF);
	while ((ch = getc (file)) != '\n')
	  if (ch == EOF)
	    die ("unexpected EOF after '%c'", start);
	goto RESTART;
      }
    ch = getc (file);
    if (ch != '\n')
      goto IGNORE_REST_OF_LINE;
    ungetc (ch, file);
    return last = start;
  }
  if (ignore_line_starting_with (start))
    goto IGNORE_REST_OF_LINE;
  last = start;
  return start;
}

static const char * USAGE =
"usage: aigsim [<option> ...] [ <model> [<stimulus>] ]\n"
"\n"
"with\n"
"\n"
"<model>         AIG in AIGER format\n"
"<stimulus>      stimulus (file of 0/1/x input vectors)\n"
"\n"
"and <option> one of the following\n"
"\n"
"-h              usage\n"
"-m              copy/move outputs as bad properties\n"
"-c              check witness and do not print trace (implies '-w', '-2')\n"
"-w              assume stimulus is a witness (first line is '1')\n"
"-v              produce VCD output trace instead of transitions\n"
"-d              add delays between input and output changes to VCD\n"
"-f              force smart line filtering\n"
"-2              ground three valued stimulus by setting 'x' to '0'\n"
"-3              enable three valued stimulus in random simulation\n"
"-r <vectors>    random stimulus of <vectors> input vectors\n"
"-s <seed>       set seed of random number generator (default '0')\n"
;

#define ALLOC_STATES 100

int
main (int argc, char **argv)
{
  int vectors, check, move, vcd, print, three, ground, seeded, delay;
  const char *stimulus_file_name, *model_file_name, *error;
  unsigned i, j, s, l, r, tmp, seed, period;
  int witness, ch, res, och, checkpass;
  /* SW110525 Variables for finding fair loops
   */
  unsigned char **states;
  unsigned statesAlloc;
  unsigned int *fair;
  unsigned int *bad;
  unsigned int **justice;
  unsigned int *expected_prop;
  unsigned int *prop_result;

  int findloop, requireloop, constraintViolation;
  int foundfair, looppoint;

  stimulus_file_name = model_file_name = 0;
  delay = seeded = vcd = check = 0;
  move = witness = 0;
  vectors = -1;
  ground = three = 0;
  seed = 0;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stderr);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-c"))
	check = witness = ground = 1;
      else if (!strcmp (argv[i], "-m"))
	move = 1;
      else if (!strcmp (argv[i], "-f"))
	filter = 1;
      else if (!strcmp (argv[i], "-w"))
	witness = 1;
      else if (!strcmp (argv[i], "-v"))
	vcd = 1;
      else if (!strcmp (argv[i], "-d"))
	delay = 1;
      else if (!strcmp (argv[i], "-3"))
	three = 1;
      else if (!strcmp (argv[i], "-2"))
	ground = 1;
      else if (!strcmp (argv[i], "-s"))
	{
	  if (i + 1 == argc)
	    die ("argvument to '-s' missing");

	  seed = atoi (argv[++i]);
	  seeded = 1;
	}
      else if (!strcmp (argv[i], "-r"))
	{
	  if (i + 1 == argc)
	    die ("argument to '-r' missing");

	  vectors = atoi (argv[++i]);
	}
      else if (argv[i][0] == '-')
	die ("invalid option '%s' (try '-h')", argv[i]);
      else if (!model_file_name)
	model_file_name = argv[i];
      else if (!stimulus_file_name)
	stimulus_file_name = argv[i];
      else
	die ("more than two files specified");
    }

  if (!model_file_name && vectors < 0)
    die ("can only read model from <stdin> in random simulation mode");

  if (vectors >= 0 && stimulus_file_name)
    die ("random simulation but also stimulus file specified");

  if (vectors >= 0 && witness)
    die ("random simulation but also witness specified");

  if (seeded && vectors < 0)
    die ("seed given but no random simulation specified");

  if (vectors < 0 && three)
    die ("can not use '-3' without '-r <vectors>'");

  if (vectors >= 0 && ground)
    die ("can not combine '-2' with '-r <vectors>'");

  if (check && vcd)
    die ("can not combine '-v' with '-c'");

  if (!vcd && delay)
    die ("can not use '-d' without '-v'");

  model = aiger_init ();

  if (model_file_name)
    error = aiger_open_and_read_from_file (model, model_file_name);
  else
    error = aiger_read_from_file (model, stdin);

  if (error)
    die ("%s", error);

  if (!move && !model->num_bad && !model->num_justice && model->num_outputs)
    move = 1;

  if (move) 
    {
      for (i = 0; i < model->num_outputs; i++)
	aiger_add_bad (model, 
	               model->outputs[i].lit, 
		       model->outputs[i].name);
    }

  aiger_reencode (model);	/* otherwise simulation incorrect */

  if (stimulus_file_name)
    {
      file = fopen (stimulus_file_name, "r");
      if (!file)
	die ("failed to open '%s'", stimulus_file_name);

      close_file = 1;
    }
  else
    file = stdin;

  if (vcd)
    {
      for (i = 0; i < model->num_inputs; i++)
	{
	  printf ("$var wire 1 %s ", idx_as_vcd_id ('i', i));
	  print_vcd_symbol (aiger_symbol_as_string (model->inputs + i));
	  fputs (" $end\n", stdout);
	}

      for (i = 0; i < model->num_latches; i++)
	{
	  printf ("$var reg 1 %s ", idx_as_vcd_id ('l', i));
	  print_vcd_symbol (aiger_symbol_as_string (model->latches + i));
	  fputs (" $end\n", stdout);
	}

      for (i = 0; i < model->num_outputs; i++)
	{
	  printf ("$var wire 1 %s ", idx_as_vcd_id ('o', i));
	  print_vcd_symbol (aiger_symbol_as_string (model->outputs + i));
	  fputs (" $end\n", stdout);
	}

      printf ("$enddefinitions $end\n");
    }

  period = delay ? 20 : 1;

  if (seeded)
    srand (seed);
  
  if ( witness ) 
    ch = nxtc(file);

  prop_result =
    calloc (model->num_bad + model->num_justice, sizeof(prop_result[0]));
  for( i = 0; i < model->num_bad + model->num_justice; i++ )
    prop_result[i] = 2;

  if ( witness && ch == EOF)
    goto DONE;

readNextWitness:
  /* SW110526 Initialize
   */
  foundfair = 0;
  constraintViolation = 0;
  checkpass = 1;
  res = check;

  expected_prop =
    calloc (model->num_bad + model->num_justice, sizeof(expected_prop[0]));  
  findloop = model->num_fairness || model->num_justice;
  requireloop = 0;

  print = !vcd && !check;
  if (witness)
    {
      int expectTrace = ch == '1';
      int knownResult = ch != '2';

      if ((ch != '0' && ch != '1' && ch != '2') || nxtc (file) != '\n')
	die ("expected '0', '1' or '2' as first line");	      

      if (ch == '0' || ch == '2')
	{ 
	  res = 0;
	  print = 0;
	}

      /* Read specification of properties witnessed */
      ch = nxtc (file);
      if ( ch != 'b' && ch != 'j' ) 
	die("expected 'b' or 'j' in witness");
      
      if (print)
	  printf("Grounded instance of this trace should be a witness for: {");      
	      	     
      do 
	{   
	  /* If we're checking then a loop should only be found if 
	     there is a justice constraint. */
	  requireloop|= ch == 'j';
	  och = ch;
	  ch = nxtc(file);
	  if ( ch < '0' || ch > '9' )
	    die ("expected integer after '%c' in witness", ch);
	  
	  j=0;
	  do 
	    {
	      j*= 10;
	      j+= ch - '0';
	      ch = nxtc(file);
	    }
	  while( ch >= '0' && ch <= '9' );

	  if ( ch != 'b' && ch != 'j' && ch != '\n' )
	    die ("expected digit, 'b', 'j' or new line in witness");
		
	  if ( ( och == 'b' && j >= model->num_bad ) ||
	       ( och == 'j' && j >= model->num_justice ) )
	    die ("'%c%d' specified in witness does not exist in model", 
	         och, j);
	  else if ( knownResult ) {
	    i = j + ((och == 'j') ? model->num_bad : 0);
	    if ( expectTrace && 
		 prop_result[i] != 2 &&
		 prop_result[i] != (expectTrace?1:0) )
	      die ("Inconsistent results specified for %c%d", och, j);	    

	    expected_prop[i] = 1;
	    prop_result[i] = expectTrace ? 1 : 0;
	  }
	  
	  if (print)
	    printf(" %c%d", och, j);
	}
      while( ch == 'b' || ch == 'j' );
	      
      if (print)
	printf(" }\n");

      if (ch != '\n')
	die("expected new line after \"%c%d\" in witness", och, j);
      
      if (!expectTrace) {
	ch = nxtc(file);
	if (ch != '.')
	  die("expected '.' after witness without trace");
	goto skipWitness;
      }
    }

  states = NULL;
  statesAlloc = 0;
  current = calloc (model->maxvar + 1, sizeof (current[0]));
  next = calloc (model->num_latches, sizeof (next[0]));
  fair = calloc (model->num_fairness, sizeof (fair[0]));
  bad = calloc (model->num_bad, sizeof(bad[0]));
  justice = calloc (model->num_justice, sizeof(justice[0]));
  for( i = 0; i < model->num_justice; i++ )
    justice[i] = calloc (model->justice[i].size, sizeof(justice[0][0]));

  if (witness) {
    /* Read initial state */
    for (j = 0; j < model->num_latches; j++) 
      {
	aiger_symbol *symbol = model->latches + j;
	assert(symbol->reset <= 1 || symbol->reset == symbol->lit );
	
	ch = nxtc(file);
	if ( ch != '0' && ch != '1' && ch != 'x' ) 
	  die("expected '0', '1' or 'x' in initial state in witness");
	    
	if (symbol->reset <= 1)
	  {
	    /* Norbert Manthey observed that it might be also OK, to have 'x'
	     * for an initialized latch, since in this case the simulator can
	     * just pick up the value from the model.  So we support this now
	     * (for him ;-).
	     */
	    if (ch == 'x')
	      ch = '0' + symbol->reset;
	    else if (symbol->reset != (ch - '0'))
	      die("witness specifies invalid initial state for latch l%d", j);
	  }

	current[symbol->lit/2] = (ch != 'x') ? (ch - '0') : (ground ? 0 : 2);
      }
      
    if ( nxtc(file) != '\n' )
      die("expected new line after initial state in witness");
  }
  /* Set initial state */
  else
    {
      for (j = 0; j < model->num_latches; j++) 
	{
	  aiger_symbol *symbol = model->latches + j;
	  assert(symbol->reset <= 1 || symbol->reset == symbol->lit );
	  
	  current[symbol->lit/2] =
	    (symbol->reset <= 1) ? symbol->reset : (ground ? 0 : 2);
	}
    }

  i = 1;
  while (vectors)
    {
      if (vectors > 0)
	{
	  for (j = 1; j <= model->num_inputs; j++)
	    {
	      s = 17 * j + i;
	      s %= 20;
	      tmp = rand () >> s;
	      tmp %= three + 2;
	      current[j] = tmp;
	    }

	  vectors--;
	}
      else
	{
	  ch = nxtc (file);
	  j = 1;

	  if (ch == '.')
	    break;

	  /* First read and overwrite inputs.
	   */
	  while (j <= model->num_inputs)
	    {
	      if (ch == '0')
		current[j] = 0;
	      else if (ch == '1')
		current[j] = 1;
	      else if (ch == 'x')
		current[j] = ground ? 0 : 2;
	      else
		die ("line %u: pos %u: expected '0' or '1'", i, j);

	      j++;
	      ch = nxtc (file);
	    }

	  if (ch != '\n')
	    die ("line %u: pos %u: expected new line", i, j);
	}

      /* Simulate AND nodes.
       */
      for (j = 0; j < model->num_ands; j++)
	{
	  aiger_and *and = model->ands + j;
	  l = deref (and->rhs0);
	  r = deref (and->rhs1);
	  tmp = l & r;
	  tmp |= l & (r << 1);
	  tmp |= r & (l << 1);
	  current[and->lhs / 2] = tmp;
	}
      
      /* SW110525 "constraint" outputs */
      for (j = 0; j < model->num_constraints; j++) {
	if (deref (model->constraints[j].lit) == 0) {
	  constraintViolation = 1;	
	  printf("Constraint c%d was violated at timepoint %d\n", j, i-1);
	}
      }
      if ( constraintViolation ) break;

      /* SW110524 Handling loops */
      if ( findloop ) 
	{
	  /* SW110525 Storing the last time point at which fairness constraint
	     was satisfied */
	  for (j = 0; j < model->num_fairness; j++)  {
	    if (deref (model->fairness[j].lit) == 1) fair[j] = i;
	  }

	  /* SW110525 Storing the last time point in which each literal 
	     in every justice constraint was satisfied */
	  for (j = 0; j < model->num_justice; j++)  {
	    int k;
	    for (k = 0; k < model->justice[j].size; k++)  {
	      if (deref (model->justice[j].lits[k]) == 1) justice[j][k] = i;
	    }
	  }

	  /* Store the current state vector in the list. 
	     Allocate (more) memory for the list if necessary */
	  if ( i > statesAlloc ) {
	    statesAlloc+= ALLOC_STATES;
	    states = (unsigned char**) 
	      realloc( states, statesAlloc * sizeof(states[0]) );
	  }
	  states[i-1] = calloc (model->num_latches, sizeof (states[0][0]));
	  for (j = 0; j < model->num_latches; j++)
	    states[i-1][j] = deref( model->latches[j].lit );
	}

      /* SW110525 "bad" outputs */
      for (j = 0; j < model->num_bad; j++)
	bad[j]|= (deref (model->bad[j].lit) == 1);

      /* Print current state of latches.
       */
      if (print)
	{
	  for (j = 0; j < model->num_latches; j++)
	    put (model->latches[j].lit);
	  fputc (' ', stdout);
	}

      if (vcd)
	{
	  printf ("#%u\n", period * (i - 1));

	  if (i == 1)
	    printf ("$dumpvars\n");

	  for (j = 0; j < model->num_latches; j++)
	    {
	      put (model->latches[j].lit);
	      fputs (idx_as_vcd_id ('l', j), stdout);
	      fputc ('\n', stdout);
	    }

	  if (i == 1 && delay)
	    {
	      for (j = 0; j < model->num_inputs; j++)
		{
		  fputc ('x', stdout);
		  fputs (idx_as_vcd_id ('i', j), stdout);
		  fputc ('\n', stdout);
		}

	      for (j = 0; j < model->num_outputs; j++)
		{
		  fputc ('x', stdout);
		  fputs (idx_as_vcd_id ('o', j), stdout);
		  fputc ('\n', stdout);
		}
	    }

	  if (i == 1)
	    printf ("$end\n");
	}

      /* Then first calculate next state values of latches in  parallel.
       */
      for (j = 0; j < model->num_latches; j++)
	{
	  aiger_symbol *symbol = model->latches + j;
	  next[j] = deref (symbol->next);
	}

      /* Then update new values of latches.
       */
      for (j = 0; j < model->num_latches; j++)
	{
	  aiger_symbol *symbol = model->latches + j;
	  current[symbol->lit / 2] = next[j];
	}

      if (print)
	{
	  /* Print inputs.
	   */
	  for (j = 0; j < model->num_inputs; j++)
	    put (model->inputs[j].lit);
	  fputc (' ', stdout);

	  /* Print outputs.
	   */
	  for (j = 0; j < model->num_outputs; j++)
	    put (model->outputs[j].lit);
	  fputc (' ', stdout);

	  /* Print next state of latches.
	   */
	  for (j = 0; j < model->num_latches; j++)
	    put (model->latches[j].lit);

	  fputc ('\n', stdout);
	}

      if (vcd)
	{
	  if (delay)
	    printf ("#%u\n", period * (i - 1) + 1);

	  for (j = 0; j < model->num_inputs; j++)
	    {
	      put (model->inputs[j].lit);
	      fputs (idx_as_vcd_id ('i', j), stdout);
	      fputc ('\n', stdout);
	    }

	  if (delay)
	    printf ("#%u\n", period * (i - 1) + 2);

	  for (j = 0; j < model->num_outputs; j++)
	    {
	      put (model->outputs[j].lit);
	      fputs (idx_as_vcd_id ('o', j), stdout);
	      fputc ('\n', stdout);
	    }
	}

      i++;
    }

  if (vcd)
    printf ("#%u\n", period * (i - 1));

  if (print)
    printf("Trace is a witness for: {");

  /* SW110525 Check & print reachable bad states */
  for (j = 0; j < model->num_bad; j++) {
    if (!bad[j])
      checkpass &= !expected_prop[j];
    else if (print)
      printf(" b%d", j);

    if ( !prop_result[j] && bad[j] )
      die(
       "Trace witnesses b%d which was previously specified unsatisfiable\n", j);    
  }
  free (bad);

  /* SW110303 Loop handling */
  if (findloop)
    {
      /* For all timepoints ... */
      tmp = i-1;
      for ( i = 0; i < tmp; i++ ) {
	/* If we haven't "found fair" yet, check if this timepoint 
	 * is the looppoint */
	if ( !foundfair && !constraintViolation ) {
	  foundfair = 1;
	  /* State at this timepoint should be next state at endpoint */
	  for (j = 0; foundfair && j < model->num_latches; j++)	    
	    foundfair&= (states[i][j] == deref( model->latches[j].lit ));
	  /* The last time a fairness constraint held should be at 
	     the earliest at the looppoint */
	  for (j = 0; foundfair && j < model->num_fairness; j++)
	    foundfair&= fair[j] > i;

	  if ( foundfair )
	    looppoint = i;
	}

	/* Remove stored state */
	free (states[i]);
      }

      checkpass &= foundfair || !requireloop;
	   	        
      /* For all justice constraints... */
      for (i = 0; i < model->num_justice; i++) {
	/* If we found a fair loop then check if justice constraint i is
	   satisfied */
	int foundjust = foundfair;
	for (j = 0; foundjust && j < model->justice[i].size; j++)
	  foundjust&= justice[i][j] > looppoint;
	
	if ( !foundjust ) 
	  checkpass&= !expected_prop[model->num_bad+i];
	else if (print)
	  printf(" j%d", i);

	if ( !prop_result[model->num_bad+i] && foundjust )
	  die("Trace witnesses j%d which was previously specified unsatisfiable\n", i);	

	/* Free memory for this just constraint */
	free (justice[i]);
      }
      free (states);      
    }

  if (print) {
    printf(" }\n");
    if ( foundfair )
      printf("Loop starts at timepoint: %d\n", looppoint);
  }
  /* It is possible to have a constraint violation AND a check pass, if this
     is a witness of a bad state output and the constraint violation happens
     after the bad state output becomes high */
  if (checkpass)
    res = 0;

  free (fair);
  free (justice);
  free (current);
  free (next);

skipWitness:;
  free (expected_prop);

  if (witness && res == 0 && ch == '.') {
    ch = nxtc(file);
    if ( ch != '\n' ) 
      die("expected new line after '.'\n");
    ch = nxtc(file);
    if ( ch != EOF )
      goto readNextWitness;
  }

DONE:
  
  free (prop_result);

  if (close_file)
    fclose (file);

  aiger_reset (model);

  return res;
}
