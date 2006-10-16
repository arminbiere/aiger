/*------------------------------------------------------------------------*/
/* Copyright (c) 2006 Armin Biere, Johannes Kepler University             */
/* Copyright (c) 2006 Marc Herbsttritt, University of Freiburg            */
/* See LICENSE for full details on Copyright and LICENSE.                 */
/*------------------------------------------------------------------------*/

#include "aiger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

static FILE * file;
static aiger * mgr;
static int count;
static int verbose;

static void
ps (const char * str)
{
  fputs (str, file);
}

static void
pl (unsigned lit)
{
  const char * name;
  char ch;
  int i;

  if (lit == 0)
    putc ('0', file);
  else if (lit == 1)
    putc ('1', file);
  else if ((lit & 1))
    putc ('!', file), pl (lit - 1);
  else if ((name = aiger_get_symbol (mgr, lit)))
    {
      /* TODO: check name to be valid SMV name
       */
      fputs (name, file);
    }
  else
    {
      if (aiger_is_input (mgr, lit))
	ch = 'i';
      else if (aiger_is_latch (mgr, lit))
	ch = 'l';
      else
	{
	  assert (aiger_is_and (mgr, lit));
	  ch = 'a';
	}

      for (i = 0; i <= count; i++)
	fputc (ch, file);

      fprintf (file, "%u", lit);
    }
}

static int
count_ch_prefix (const char * str, char ch)
{
  const char * p;

  assert (ch);
  for (p = str; *p == ch; p++)
    ;

  if (*p && !isdigit (*p))
    return 0;

  return p - str;
}

static void
setupcount (void)
{
  const char * symbol;
  unsigned i;
  int tmp;

  count = 0;
  for (i = 1; i <= mgr->maxvar; i++)
    {
      symbol = aiger_get_symbol (mgr, 2 * i);
      if (!symbol)
	continue;

      if ((tmp = count_ch_prefix (symbol, 'i')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'l')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'o')) > count)
	count = tmp;

      if ((tmp = count_ch_prefix (symbol, 'a')) > count)
	count = tmp;
    }
}

int
main (int argc, char ** argv)
{
  const char * src, * dst, * error;
  int res, strip, ag;
  unsigned i,j,latch_helper_cnt;
  int require_const0;
  int require_const1;
  require_const0 = 0;
  require_const1 = 1;
  latch_helper_cnt = 0;
  src = dst = 0;
  strip = 0;
  res = 0;
  ag = 0;

  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h")) {
      fprintf (stderr, "usage: aigtoblif [-h][-s][src [dst]]\n");
      exit (0);
    }
    if (!strcmp (argv[i], "-s"))
      strip = 1;
    else if (!strcmp (argv[i], "-v"))
      verbose++;
    else if (argv[i][0] == '-') {
      fprintf (stderr, "=[aigtoblif] invalid option '%s'\n", argv[i]);
      exit (1);
    }
    else if (!src)
      src = argv[i];
    else if (!dst)
      dst = argv[i];
    else {
      fprintf (stderr, "=[aigtoblif] too many files\n");
      exit (1);
    }
  }

  mgr = aiger_init ();

  if (src)
    error = aiger_open_and_read_from_file (mgr, src);
  else
    error = aiger_read_from_file (mgr, stdin);

  if (error) {
    fprintf (stderr, "=[aigtoblif] %s\n", error);
    res = 1;
  }
  else {
    if (dst) {
      if (!(file = fopen (dst, "w"))) {
        fprintf (stderr,
                 "=[aigtoblif] failed to write to '%s'\n", dst);
        exit (1);
      }
    }
    else
      file = stdout;
      
    if (strip)
      aiger_strip_symbols_and_comments (mgr);
    else
      setupcount ();

    ps (".model "), ps (src ? src : "stdin"), ps("\n");
    fputs (".inputs ", file);
    for (i = 0; i < mgr->num_inputs; i++) {
      pl (mgr->inputs[i].lit), ps (" ");
      if( (i+1)%10 == 0 && (i<(mgr->num_inputs -1) )) ps( "\\\n" );
      if( i == (mgr->num_inputs -1) ) ps( "\n" );
    }
    fputs (".outputs ", file);
    for (i = 0; i < mgr->num_outputs; i++) {
      if( ! mgr->outputs[i].name ) {
        fprintf (stderr,
          "=[aigtoblif] expected to get a symbol of output #'%d' (aig-idx) %d\n", 
          (i+1), mgr->outputs[i].lit );
        fprintf (stderr, "=[aigtoblif] sorry, cannot dump BLIF file.\n" );
        exit(1);
      }
      if( verbose > 1 ) {
        fprintf (stderr,
          "=[aigtoblif] output '%d' has symbol '%s' with AIGER index %d\n", 
          (i+1), mgr->outputs[i].name, mgr->outputs[i].lit );
      }
      ps (mgr->outputs[i].name), ps (" ");
      if( (i+1)%10 == 0 && (i<(mgr->num_outputs -1) )) ps( "\\\n" );
      if( i == (mgr->num_outputs -1) ) ps( "\n" );
    }

    /* this is a non-efficient hack for assuring that BLIF-inverters
     * are only inserted once even when multiple latches have the same
     * next-state-function!
     * latch_helper[i] stores the i-th AIG-index for which a INV has to
     * be inserted. checking duplicates is simply done by comparing 
     * a new potential INV with all other INV that must already be inserted!
     */
    int latch_helper[mgr->num_latches];
    for( i=0; i< mgr->num_latches; i++ ) {
      latch_helper[i]=0;
    }
    for (i = 0; i < mgr->num_latches; i++) {
      latch_helper[i] = 0;
      if( mgr->latches[i].next == aiger_false ) {
        require_const0 = 1;
      }
      if( mgr->latches[i].next == aiger_true ) {
        require_const1 = 1;
      }

      /* this case normally makes no sense, but you never know ... */
      if( mgr->latches[i].next == aiger_false ||
          mgr->latches[i].next == aiger_true ) {
        ps(".latch "), 
        (mgr->latches[i].next == aiger_false) ? ps("c0") : ps("c1"), 
        ps(" "), pl(mgr->latches[i].lit), ps(" 0\n");
      } 
      /* this should be the general case! */
      else {
        if( ! aiger_sign( mgr->latches[i].next ) ) {
          ps(".latch "), pl(mgr->latches[i].next), ps(" "), 
          pl(mgr->latches[i].lit), ps(" 0\n");
	}
        else {
          /* add prefix 'n' to inverted AIG nodes. 
           * corresponding inverters are inserted below!
	   */
          ps(".latch n"), pl(aiger_strip(mgr->latches[i].next)), ps(" "), 
          pl(mgr->latches[i].lit), ps(" 0\n");
          int already_done=0;
          for( j=0; j<latch_helper_cnt && !already_done;j++) {
            if( mgr->latches[latch_helper[j]].next == mgr->latches[i].next ) {
              already_done=1;
	    }
	  } 
          if( ! already_done ) {
            latch_helper[latch_helper_cnt]=i;
            latch_helper_cnt++;
	  }
	}
      }
    }

    for (i = 0; i < mgr->num_ands; i++) {
      aiger_and * n = mgr->ands + i;

      unsigned rhs0 = n->rhs0;
      unsigned rhs1 = n->rhs1;

      ps(".names "), pl(aiger_strip(rhs0)), ps(" "), 
      pl(aiger_strip(rhs1)), ps(" "), pl(n->lhs), ps("\n");
      aiger_sign(rhs0) ? ps("0") : ps("1");
      aiger_sign(rhs1) ? ps("0") : ps("1");
      ps(" 1\n");
    }

    /* for those outputs having an inverted AIG node, insert an INV,
     * otherwise just a BUF
     */
    for (i = 0; i < mgr->num_outputs; i++) {
      if( ! mgr->outputs[i].name ) {
        fprintf (stderr,
          "=[aigtoblif] expected to get a symbol of output #'%d' (aig-idx) %d\n", 
          (i+1), mgr->outputs[i].lit );
        fprintf (stderr, "=[aigtoblif] sorry, cannot dump BLIF file.\n" );
        exit(1);
      }
      if( verbose > 1 ) {
        fprintf (stderr,
          "=[aigtoblif] output '%d' has symbol '%s' with AIGER index %d\n", 
          (i+1), mgr->outputs[i].name, mgr->outputs[i].lit );
      }
      /* this case normally makes no sense, but you never know ... */
      if( mgr->outputs[i].lit == aiger_false || 
          mgr->outputs[i].lit == aiger_true ) {
        ps(".names "), 
        ( (mgr->outputs[i].lit == aiger_false) ? ps("c0 ") : ps("c1 ") ), 
        ps (mgr->outputs[i].name), ps("\n"), ps("1 1\n");
        (mgr->outputs[i].lit == aiger_false) ? 
          (require_const0 = 1) : (require_const1 = 1);
      }
      /* this should be the general case! */
      else if( aiger_sign( mgr->outputs[i].lit ) ) {
        ps(".names "), pl(aiger_strip(mgr->outputs[i].lit)), 
        ps(" "), ps (mgr->outputs[i].name), ps("\n"), ps("0 1\n");
      }
      else {
        ps(".names "), pl(aiger_strip(mgr->outputs[i].lit)), 
        ps(" "), ps (mgr->outputs[i].name), ps("\n"), ps("1 1\n");
      }
    }

    /* for those latches having an inverted AIG node as next-state-function, 
     * insert an INV. these latches were already saved in latch_helper!
     */
    for( i=0; i<latch_helper_cnt; i++ ) {
      unsigned l=latch_helper[i];
      if( mgr->latches[l].next != aiger_false &&
          mgr->latches[l].next != aiger_true ) {
        assert( aiger_sign( mgr->latches[l].next ) );
        ps(".names "), pl(aiger_strip(mgr->latches[l].next)), 
        ps(" n"), pl (aiger_strip(mgr->latches[l].next)), 
        ps("\n"), ps("0 1\n");
      }
    }

    /* insert constants when necessary */
    if( require_const0 ) {
      ps(".names c0\n"), ps("0\n");
    }
    if( require_const1 ) {
      ps(".names c1\n"), ps("1\n");
    }
    fputs(".end\n", file);

    /* close file */
    if (dst)
      fclose (file);
  }

  aiger_reset (mgr);

  return res;
}
