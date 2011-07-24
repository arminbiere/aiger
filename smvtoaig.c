/***************************************************************************
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.
Copyright (c) 2011, Siert Wieringa, Aalto University, Finland.

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

/*------------------------------------------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>

/*------------------------------------------------------------------------*/

#define NEW(p) \
  do { \
    size_t bytes = sizeof (*(p)); \
    (p) = malloc (bytes); \
    memset ((p), 0, bytes); \
  } while (0)

/*------------------------------------------------------------------------*/

#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = malloc (bytes); \
    memset ((p), 0, bytes); \
  } while (0)

/*------------------------------------------------------------------------*/

#define TRUE ((AIG*)(long)1)
#define FALSE ((AIG*)(long)-1)

/*------------------------------------------------------------------------*/

#define VALID_SYMBOL "AIGER_VALID"
#define INVALID_SYMBOL "AIGER_INVALID"
#define INITIALIZED_SYMBOL "AIGER_INITIALIZED"
#define NEVER_SYMBOL "AIGER_NEVER"
#define INVAR_SYMBOL "AIGER_INVAR"
#define FAIR_SYMBOL "AIGER_FAIR" /* SW110131 */
#define JUST_SYMBOL "AIGER_JUST" /* SW110530 */

#define NEXT_PREFIX "AIGER_NEXT_"
#define NOT_PREFIX "AIGER_NOT_"
#define LTL_SYMBOL "IGNORE_LTL_"

/*------------------------------------------------------------------------*/

enum Tag
{
  /* The first group of tags are tokens, which are encoded in the same way
   * as their ASCII representation.  This is just for convenience and
   * debugging purposes.
   */
  AND = '&',
  CLAUSE = ':',
  NOT = '!',
  ONE = '1',
  OR = '|',
  ZERO = '0',

  LTL_FINALLY = 'F',
  LTL_GLOBALLY = 'G',
  LTL_HISTORICALLY = 'H',
  LTL_ONCE = 'O',
  LTL_SINCE = 'S',
  LTL_TRIGGERED = 'T',
  LTL_UNTIL = 'U',
  LTL_RELEASES = 'V',
  LTL_NEXT = 'X',
  LTL_YESTERDAY = 'Y',
  LTL_WEAKYESTERDAY = 'Z',

  AG = 256,
  ASSIGN = 257,
  BECOMES = 258,
  boolean = 259,
  CASE = 260,
  DEFINE = 261,
  ESAC = 262,
  IFF = 263,
  IMPLIES = 264,
  init = 265,
  INIT = 266,
  INVAR = 267,
  MODULE = 268,
  next = 269,
  NOTEQ = 270,
  SPEC = 271,
  SYMBOL = 272,
  TRANS = 273,
  VAR = 274,
  FAIRNESS = 275, /* SW110131 */
  XOR = 276, /* SW110307 */
  XNOR = 277, /* SW110307 */
  LTLSPEC = 288 /* SW110527 */
};

typedef enum Tag Tag;

/*------------------------------------------------------------------------*/

typedef struct AIG AIG;
typedef struct Symbol Symbol;
typedef struct Expr Expr;

/*------------------------------------------------------------------------*/

struct Symbol
{
  char *name;

  unsigned declared:1;
  unsigned nondet:1;
  unsigned latch:1;
  unsigned input:1;
  unsigned flipped:1;

  unsigned mark;

  Expr *init_expr;
  Expr *next_expr;
  Expr *def_expr;

  AIG *init_aig;
  AIG *next_aig;
  AIG *def_aig;

  Symbol *chain;
  Symbol *order;
};

/*------------------------------------------------------------------------*/

struct Expr
{
  Tag tag;
  Symbol *symbol;
  Expr *c0;
  Expr *c1;
  Expr *next;
  AIG * cache[2];
};

/*------------------------------------------------------------------------*/

struct AIG
{
  Symbol *symbol;
  unsigned slice;		/* actually only 0 or 1 */
  AIG *c0;
  AIG *c1;
  unsigned idx;			/* Tseitin index */
  AIG *next;			/* collision chain */
  AIG *cache;			/* cache for shifting and elaboration */
  unsigned id;			/* unique id for hashing/comparing purposes */
};

/*------------------------------------------------------------------------*/

static const char *ltl2smv;
static const char *input_name;
static int close_input;
static FILE *input;

static int verbose;

static int ascii;
static aiger *writer;
static const char *output_name;
static int strip_symbols;

static int lineno;
static int saved_char;
static int char_has_been_saved;
static Tag token;

static unsigned count_buffer;
static char *buffer;
static unsigned size_buffer;

static int next_allowed;
static int temporal_operators_allowed;

/*------------------------------------------------------------------------*/

static Symbol *first_symbol;
static Symbol *last_symbol;

static unsigned size_symbols;
static unsigned count_symbols;
static Symbol **symbols;

static Symbol *initialized_symbol;
static Symbol *invalid_symbol;
static Symbol *valid_symbol;

/*------------------------------------------------------------------------*/

static Expr *first_expr;
static Expr *last_expr;
static unsigned count_exprs;

static unsigned size_expr_stack;
static unsigned count_expr_stack;
static Expr **expr_stack;

static Expr *trans_expr;
static Expr *last_trans_expr;
static Expr *ltl_trans_expr;

static Expr *init_expr;
static Expr *last_init_expr;
static Expr *ltl_init_expr;

/* SW110606 Modified invar handling such that each separate INVAR expression
   will lead to a separate constraint */
static Expr **invar_expr;
static int invar_expr_cnt;

/* SW110131 Changed the expression pointer "spec_expr" into a pointer to
   a pointer, which forms an array of expression pointers of length 
   spec_expr_cnt. Then added the same for fair expressions.
*/
static Expr **spec_expr;
static Expr **ltlspec_expr;
static Expr **fair_expr;
static int *justice_groups;
static int fair_expr_cnt;
static int ltlspec_expr_cnt;
static int spec_expr_cnt;
static int ltlspec;

static int constantinitialized;

/*------------------------------------------------------------------------*/

static unsigned size_aigs;
static unsigned count_aigs;
static AIG **aigs;

static unsigned size_cached;
static unsigned count_cached;
static AIG **cached;

static AIG *trans_aig;
static AIG *init_aig;

/* SW110131 Now there's an array of "bad AIGs" and an array of "fair AIGs"
   SW110606 Now also for "invar AIGs"
 */
static AIG **invar_aig;
static AIG **bad_aig;
static AIG **fair_aig;

/*------------------------------------------------------------------------*/

static unsigned nondets;
static unsigned inputs;
static unsigned latches;
static unsigned ands;
static unsigned idx;

/*------------------------------------------------------------------------*/

static int optimize = 4;

/*------------------------------------------------------------------------*/

static unsigned primes[] = { 21433, 65537, 332623, 1322963, 200000123 };
static unsigned *eoprimes = primes + sizeof (primes) / sizeof (primes[0]);

/*------------------------------------------------------------------------*/

static void
die (const char *msg, ...)
{
  va_list ap;
  fputs ("*** [smvtoaig] ", stderr);
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

/*------------------------------------------------------------------------*/

static void
perr (const char *msg, ...)
{
  va_list ap;
  fprintf (stderr, "%s:%d: ", input_name, lineno);
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

/*------------------------------------------------------------------------*/

static void
msg (int level, const char *msg, ...)
{
  va_list ap;

  if (level > verbose)
    return;

  fprintf (stderr, "[smvtoaig] ");
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

/*------------------------------------------------------------------------*/

static void
enlarge_buffer (void)
{
  size_buffer = size_buffer ? 2 * size_buffer : 1;
  buffer = realloc (buffer, size_buffer);
}

/*------------------------------------------------------------------------*/

static int
full_buffer (void)
{
  return count_buffer >= size_buffer;
}

/*------------------------------------------------------------------------*/

static void
push_buffer (int ch)
{
  if (full_buffer ())
    enlarge_buffer ();

  buffer[count_buffer++] = ch;
}

/*------------------------------------------------------------------------*/

static void
pop_buffer (void)
{
  assert (count_buffer > 0);
  count_buffer--;
}

/*------------------------------------------------------------------------*/

static void
enlarge_expr_stack (void)
{
  size_expr_stack = size_expr_stack ? 2 * size_expr_stack : 1;
  expr_stack = realloc (expr_stack, size_expr_stack * sizeof (expr_stack[0]));
}

/*------------------------------------------------------------------------*/

static void
push_expr (Expr * expr)
{
  if (count_expr_stack >= size_expr_stack)
    enlarge_expr_stack ();

  expr_stack[count_expr_stack++] = expr;
}

/*------------------------------------------------------------------------*/

static Expr *
pop_expr (void)
{
  assert (count_expr_stack > 0);
  return expr_stack[--count_expr_stack];
}

/*------------------------------------------------------------------------*/

static int
next_char (void)
{
  int res;

  if (char_has_been_saved)
    {
      res = saved_char;
      char_has_been_saved = 0;
    }
  else
    res = getc (input);
 
  if (res != EOF)
    push_buffer (res);

  if (res == '\n')
    lineno++;

  return res;
}

/*------------------------------------------------------------------------*/

static void
save_char (int ch)
{
  assert (!char_has_been_saved);

  if (ch == '\n')
    {
      assert (lineno > 0);
      lineno--;
    }

  saved_char = ch;
  char_has_been_saved = 1;

  if (ch != EOF && count_buffer)
    pop_buffer ();
}

/*------------------------------------------------------------------------*/

static void
invalid_token (void)
{
  push_buffer (0);
  perr ("invalid token '%s'", buffer);
}

/*------------------------------------------------------------------------*/

static unsigned char issymbol[256];

/*------------------------------------------------------------------------*/

static void
init_issymbol (void)
{
  unsigned char ch;

  memset (issymbol, 0, sizeof (issymbol));

  for (ch = '0'; ch <= '9'; ch++)
    issymbol[ch] = 1;

  for (ch = 'a'; ch <= 'z'; ch++)
    issymbol[ch] = 1;

  for (ch = 'A'; ch <= 'Z'; ch++)
    issymbol[ch] = 1;

  issymbol['-'] = 1;
  issymbol['_'] = 1;
  issymbol['.'] = 1;
  issymbol['@'] = 1;
  issymbol['['] = 1;
  issymbol[']'] = 1;
}

/* SW110527 */
static int
is_temporal_operator (Tag tag)
{
  return ( tag == AG ) || 
    ( tag == LTL_NEXT ) ||
    ( tag == LTL_GLOBALLY ) ||
    ( tag == LTL_FINALLY ) ||
    ( tag == LTL_YESTERDAY ) ||
    ( tag == LTL_WEAKYESTERDAY ) ||
    ( tag == LTL_HISTORICALLY ) ||
    ( tag == LTL_ONCE ) ||
    ( tag == LTL_UNTIL ) ||
    ( tag == LTL_SINCE ) ||
    ( tag == LTL_TRIGGERED ) ||
    ( tag == LTL_RELEASES );
}

/*------------------------------------------------------------------------*/

static Tag
get_next_token (void)
{
  int ch;

SKIP_WHITE_SPACE:

  while (isspace (ch = next_char ()))
    ;

  save_char (ch);
  count_buffer = 0;		/* start of new token */

  ch = next_char ();

  assert (!isspace (ch));

  if (ch == '-')
    {
      ch = next_char ();
      if (ch == '-')
	{
	  while ((ch = next_char ()) != '\n' && ch != EOF)
	    ;

	  goto SKIP_WHITE_SPACE;
	}
      else if (ch != '>')
	invalid_token ();

      return IMPLIES;
    }

  if (ch == '!')
    {
      ch = next_char ();
      if (ch == '=')
	return NOTEQ;

      save_char (ch);
      return '!';
    }

  if (ch == '=' || ch == '|' || ch == '(' || ch == ')' ||
      ch == ';' || ch == '&' || ch == '0' || ch == '1')
    return ch;

  if (ch == ':')
    {
      ch = next_char ();
      if (ch == '=')
	return BECOMES;

      save_char (ch);
      return ':';
    }
  
  if (ch == '<')
    {
      if (next_char () != '-' || next_char () != '>')
	invalid_token ();

      return IFF;
    }
  
  if (issymbol[ch])
    {
      while (issymbol[ch = next_char ()])
	;
      save_char (ch);
      push_buffer (0);

      if ( strlen(buffer) == 1 && 
	   is_temporal_operator(buffer[0]) )
	return buffer[0];

      if (!strcmp (buffer, "AG"))
	return AG;
      if (!strcmp (buffer, "ASSIGN"))
	return ASSIGN;
      if (!strcmp (buffer, "boolean"))
	return boolean;
      if (!strcmp (buffer, "case"))
	return CASE;
      if (!strcmp (buffer, "DEFINE"))
	return DEFINE;
      if (!strcmp (buffer, "esac"))
	return ESAC;
      if (!strcmp (buffer, "INIT"))
	return INIT;
      if (!strcmp (buffer, "init"))
	return init;
      if (!strcmp (buffer, "INVAR"))
	return INVAR;
      if (!strcmp (buffer, "MODULE"))
	return MODULE;
      if (!strcmp (buffer, "next"))
	return next;
      if (!strcmp (buffer, "SPEC") || 
	  /* SW110309 Added CTLSPEC which is a synonym for SPEC 
	   */
	  !strcmp (buffer, "CTLSPEC"))
	return SPEC;
      if (!strcmp (buffer, "LTLSPEC"))
	return LTLSPEC;
      if (!strcmp (buffer, "TRANS"))
	return TRANS;
      if (!strcmp (buffer, "TRUE"))
	return ONE;
      /* SW110308 Added IVAR as synonym for VAR
	 TODO Handle IVAR seperately to include restrictions imposed on
	 input variables (e.g. can't be left hand side of assignment).
      */
      if (!strcmp (buffer, "VAR") || !strcmp (buffer, "IVAR"))
	return VAR;
      if (!strcmp (buffer, "FALSE"))
	return ZERO;
      /* SW110131 
       */
      if (!strcmp (buffer, "FAIRNESS") || !strcmp(buffer, "JUSTICE"))
        return FAIRNESS;
      /* SW110307
       */
      if (!strcmp (buffer, "xor"))
        return XOR;
      if (!strcmp (buffer, "xnor"))
        return XNOR;

      return SYMBOL;
    }

  if (ch != EOF)
    {
      if (isprint (ch))
	perr ("invalid character '%c'", ch);
      else
	perr ("invalid character");
    }

  return EOF;
}

/*------------------------------------------------------------------------*/

void
print_token (FILE *out, Tag tag)
{
  assert( tag != SYMBOL );
  switch (tag)
    {
    case AG:
      fputs ("AG",out);
      break;
    case ASSIGN:
      fputs ("ASSIGN",out);
      break;
    case BECOMES:
      fputs (":=",out);
      break;
    case boolean:
      fputs ("boolean",out);
      break;
    case CASE:
      fputs ("case",out);
      break;
    case DEFINE:
      fputs ("DEFINE",out);
      break;
    case ESAC:
      fputs ("esac",out);
      break;
    case IFF:
      fputs ("<->",out);
      break;
    case IMPLIES:
      fputs ("->",out);
      break;
    case INIT:
      fputs ("INIT",out);
      break;
    case init:
      fputs ("init",out);
      break;
    case INVAR:
      fputs ("INVAR",out);
      break;
    case MODULE:
      fputs ("MODULE",out);
      break;
    case next:
      fputs ("next",out);
      break;
    case SPEC:
      fputs ("SPEC",out);
      break;
    case TRANS:
      fputs ("TRANS",out);
      break;
    case VAR:
      fputs ("VAR",out);
      break;
    /* SW110131
     */
    case FAIRNESS:
      fputs ("FAIRNESS",out);
      break;
    /* SW110307
     */
    case XOR:
      fputs ("xor",out);
      break;
    case XNOR:
      fputs ("xnor",out);
      break;
    /* SW110527 */
    case LTLSPEC:
      fputs ("LTLSPEC",out);
      break;
    default:
      fprintf (out,"%c", tag);
      break;
    case EOF:
      fputs ("<EOF>",out);
      break;
    }
}

static void
next_token (void)
{
  token = get_next_token ();
#if 0
  /* Dump all tokens to 'stdout' to debug scanner and parser.
   */
  fprintf (stderr, "%s:%d: ", input_name, lineno);
  if (token == SYMBOL)
    fputs (buffer,stderr);
  else
    print_token(stderr, token);
  fprintf(stderr,"\n");
  fflush(stderr);
#endif
}

/*------------------------------------------------------------------------*/

static unsigned
hash_str (const char *str)
{
  const unsigned *q;
  unsigned char ch;
  const char *p;
  unsigned res;

  res = 0;

  p = str;
  q = primes;

  while ((ch = *p++))
    {
      res *= *q++;
      res += ch;

      if (q >= eoprimes)
	q = primes;
    }

  res *= *q;

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
hash_symbol (const char *str)
{
  unsigned res = hash_str (str) & (size_symbols - 1);
  assert (res < size_symbols);
  return res;
}

/*------------------------------------------------------------------------*/

static void
enlarge_symbols (void)
{
  Symbol **old_symbols, *p, *chain;
  unsigned old_size_symbols, h, i;

  old_size_symbols = size_symbols;
  old_symbols = symbols;

  size_symbols = size_symbols ? 2 * size_symbols : 1;
  NEWN (symbols, size_symbols);

  for (i = 0; i < old_size_symbols; i++)
    {
      for (p = old_symbols[i]; p; p = chain)
	{
	  chain = p->chain;
	  h = hash_symbol (p->name);
	  p->chain = symbols[h];
	  symbols[h] = p;
	}
    }

  free (old_symbols);
}

/*------------------------------------------------------------------------*/

static Symbol **
find_symbol (const char * name)
{
  Symbol **res, *s;

  for (res = symbols + hash_symbol (name);
       (s = *res) && strcmp (s->name, name); res = &s->chain)
    ;

  return res;
}

/*------------------------------------------------------------------------*/

static Symbol *
have_symbol (const char * name)
{
  return *find_symbol (name);
}

/*------------------------------------------------------------------------*/

static Symbol *
new_symbol (const char * name)
{
  Symbol **p, *res;

  if (size_symbols <= count_symbols)
    enlarge_symbols ();

  p = find_symbol (name);
  res = *p;

  if (!res)
    {
      NEW (res);
      res->name = strdup (name);
      if (last_symbol)
	last_symbol->order = res;
      else
	first_symbol = res;
      last_symbol = res;
      *p = res;

      count_symbols++;
    }

  return res;
}

/*------------------------------------------------------------------------*/

static Symbol *
new_internal_symbol (const char *name)
{
  if (have_symbol (name))
    die ("duplicate internal symbol '%s'", name);

  return new_symbol (name);
}

/*------------------------------------------------------------------------*/

static void
enqueue_expr (Expr * expr)
{
  if (last_expr)
    last_expr->next = expr;
  else
    first_expr = expr;

  last_expr = expr;
  count_exprs++;
}

/*------------------------------------------------------------------------*/

static Expr *
sym2expr (Symbol * symbol)
{
  Expr *res;

  assert (symbol);
  NEW (res);
  res->tag = SYMBOL;
  res->symbol = symbol;
  enqueue_expr (res);

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
new_expr (Tag tag, Expr * c0, Expr * c1)
{
  Expr *res;

  NEW (res);
  res->tag = tag;
  res->c0 = c0;
  res->c1 = c1;
  enqueue_expr (res);

  return res;
}

/*------------------------------------------------------------------------*/

static void
eat_token (Tag expected)
{
  if (token != expected)
    perr ("expected '%c'", expected);

  next_token ();
}

/*------------------------------------------------------------------------*/

static void
eat_symbolic_token (Tag expected, const char *str)
{
  if (token != expected)
    perr ("expected '%s'", str);

  next_token ();
}

/*------------------------------------------------------------------------*/

static Symbol *
eat_symbol (void)
{
  Symbol *res;

  if (token != SYMBOL)
    perr ("expected variable");

  res = new_symbol (buffer);
  next_token ();

  return res;
}

/*------------------------------------------------------------------------*/

static void
print_expr(FILE *out, Expr *expr)
{
  if ( expr->c0 && expr->c1 ) 
    {
      fprintf(out, "( ");
      print_expr(out, expr->c0);
    }

  assert( expr->tag != SYMBOL || 
	  (expr->symbol && expr->symbol->name) );

  if ( expr->tag != SYMBOL )
    print_token(out, expr->tag);
  else
    fputs(expr->symbol->name, out);
  fputc(' ', out);
  
  if ( expr->c0 && !expr->c1 )
    print_expr(out, expr->c0);

  if ( expr->c0 && expr->c1 ) 
    {
      print_expr(out, expr->c1);
      fprintf(out, " ) ");
    }
}

/*------------------------------------------------------------------------*/

static void
parse_vars (void)
{
  Symbol *symbol;

  assert (token == VAR);

  next_token ();

  while (token == SYMBOL)
    {
      symbol = new_symbol (buffer);
      if (symbol->declared)
	perr ("variable '%s' declared twice", symbol->name);
      if (symbol->def_expr)
	perr ("can not declare already defined variable '%s'", symbol->name);
      symbol->declared = 1;
      next_token ();

      eat_token (':');
      eat_symbolic_token (boolean, "boolean");
      eat_token (';');
    }
}

/*------------------------------------------------------------------------*/

static Expr *parse_expr (void);

/*------------------------------------------------------------------------*/

static Expr *
parse_next (void)
{
  int next_was_allowed = next_allowed;
  Expr *res;

  if (!next_allowed)
    perr ("'next' not allowed here");

  assert (token == next);
  next_token ();

  eat_token ('(');
  next_allowed = 0;
  res = parse_expr ();
  next_allowed = next_was_allowed;
  eat_token (')');

  return new_expr (next, res, 0);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_case (void)
{
  Expr *res, *clause, *lhs, *rhs;
  int count = count_expr_stack;

  assert (token == CASE);
  next_token ();

  lhs = 0;

  while (token != ESAC)
    {
      if (token == EOF)
	perr ("'esac' missing");

      lhs = parse_expr ();
      eat_token (':');
      rhs = parse_expr ();
      eat_token (';');
      clause = new_expr (':', lhs, rhs);
      push_expr (clause);
    }

  if (!lhs)
    perr ("case statemement without clauses");

  res = 0;

  while (count < count_expr_stack)
    res = new_expr (CASE, pop_expr (), res);

  next_token ();

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_basic (void)
{
  Expr *res = 0;

  if (token == ZERO || token == ONE)
    {
      res = new_expr (token, 0, 0);
      next_token ();
    }
  else if (token == SYMBOL)
    res = sym2expr (eat_symbol ());
  else if (token == next)
    res = parse_next ();
  else if (token == CASE)
    res = parse_case ();
  else if (token == '(')
    {
      next_token ();
      res = parse_expr ();
      eat_token (')');
    }
  else
    perr ("expected basic expression");

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_eq (void)
{
  Expr *res = parse_basic ();

  if (token == '=')
    {
      next_token ();
      res = new_expr (IFF, res, parse_basic ());
    }
  else if (token == NOTEQ)
    {
      next_token ();
      res = new_expr ('!', new_expr (IFF, res, parse_basic ()), 0);
    }

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_right_associative (Tag tag, Expr * (*f) (void))
{
  Expr *res = f ();

  while (token == tag)
    {
      next_token ();
      res = new_expr (tag, res, f ());
    }

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_left_associative (Tag tag, Expr * (*f) (void))
{
  int count = count_expr_stack;
  Expr *res = f ();

  push_expr (res);

  while (token == tag)
    {
      next_token ();
      push_expr (f ());
    }

  res = pop_expr ();
  while (count < count_expr_stack)
    res = new_expr (tag, pop_expr (), res);

  return res;
}

static Expr *parse_not (void);
/*------------------------------------------------------------------------*/
static Expr *
parse_temporal_postfix (void)
{
  if (is_temporal_operator(token))
    {
      Tag tag = token;
      next_token ();
      return new_expr (tag, parse_not (), 0);
    }
  return parse_eq ();
}

/*------------------------------------------------------------------------*/

static Expr *
parse_not (void)
{
  int count = 0;
  Expr *res;

  while (token == '!')
    {
      next_token ();
      count = !count;
    }

  res = parse_temporal_postfix ();
  if (count)
    res = new_expr ('!', res, 0);

  return res;
}

/*------------------------------------------------------------------------*/
static Expr *
parse_ltl_triggered (void)
{
  return parse_right_associative (LTL_TRIGGERED, parse_not);
}
static Expr *
parse_ltl_since (void)
{
  return parse_right_associative (LTL_SINCE, parse_ltl_triggered);
}
static Expr *
parse_ltl_releases (void)
{
  return parse_right_associative (LTL_RELEASES, parse_ltl_since);
}
static Expr *
parse_ltl_until (void)
{
  if ( is_temporal_operator(token) && !temporal_operators_allowed)
    perr ("Temporal operator not allowed here");
  return parse_right_associative (LTL_UNTIL, parse_ltl_releases);
}

static Expr *
parse_and (void)
{
  return parse_left_associative ('&', parse_ltl_until);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_or (void)
{
  return parse_left_associative ('|', parse_and);
}

/* SW110307 Inserted xor and xnor as left associative. 
   They should have the same priority as or.
 */
static Expr *
parse_xor (void)
{
  return parse_left_associative (XOR, parse_or);
}

static Expr *
parse_xnor (void)
{
  return parse_left_associative (XNOR, parse_xor);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_iff (void)
{
  return parse_left_associative (IFF, parse_xnor);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_implies (void)
{
  return parse_right_associative (IMPLIES, parse_iff);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_expr (void)
{
  return parse_implies ();
}

/*------------------------------------------------------------------------*/

static int
contains_temporal_operator (Expr * expr)
{
  if (!expr)
    return 0;

  if (is_temporal_operator(expr->tag))
    return 1;

  if (contains_temporal_operator (expr->c0))
    return 1;

  return contains_temporal_operator (expr->c1);
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications
 */
static void
parse_spec (void)
{
  const int s = spec_expr_cnt;
  assert (token == SPEC);

  if (ltlspec)
    perr("Error: SPEC not allowed in SMV translation of LTL formula\n");

  next_token ();

  spec_expr = (Expr**) realloc (spec_expr, ++spec_expr_cnt * sizeof(Expr*) );

  temporal_operators_allowed = 1;
  spec_expr[s] = parse_expr ();
  temporal_operators_allowed = 0;

  if (spec_expr[s]->tag != AG || 
      contains_temporal_operator (spec_expr[s]->c0))
    perr ("can handle only simple 'AG' safety properties");
}

/* SW110527 Handling LTLSPEC
 */
static void
parse_ltlspec (void)
{
  const int s = ltlspec_expr_cnt++;
  assert (token == LTLSPEC);

  if (ltlspec)
    perr("Error: LTLSPEC not allowed in SMV translation of LTL formula\n");

  next_token ();
  ltlspec_expr = (Expr**) realloc (ltlspec_expr, ltlspec_expr_cnt * sizeof(Expr*) );

  temporal_operators_allowed = 1;
  ltlspec_expr[s] = parse_expr ();
  temporal_operators_allowed = 0;
}

/*------------------------------------------------------------------------*/

static void
parse_assigns (void)
{
  Symbol *symbol;
  Expr *rhs;
  Tag tag;

  assert (token == ASSIGN);
  next_token ();

  while (token == init || token == next || token == SYMBOL)
    {
      tag = token;
      if (token != SYMBOL)
	{
	  next_token ();
	  eat_token ('(');
	}

      symbol = eat_symbol ();
      if (symbol->def_expr)
	perr ("can not assign or define variable '%s' twice", symbol->name);

      if (tag == SYMBOL)
	{
	  if (symbol->init_expr || symbol->next_expr)
	    perr ("can not define already assigned variable '%s'",
		  symbol->name);
	}
      else
	{
	  if (tag == init && symbol->init_expr)
	    perr ("multiple 'init' assignments for '%s'", symbol->name);

	  if (tag == next && symbol->next_expr)
	    perr ("multiple 'next' assignments for '%s'", symbol->name);

	  eat_token (')');
	}

      eat_symbolic_token (BECOMES, ":=");
      rhs = parse_expr ();
      eat_token (';');

      if (tag == init)
	symbol->init_expr = rhs;
      else if (tag == next)
	symbol->next_expr = rhs;
      else
	{
	  assert (tag == SYMBOL);
	  symbol->def_expr = rhs;
	}
    }
}

/*------------------------------------------------------------------------*/

static void
parse_defines (void)
{
  Symbol *symbol;
  Expr *rhs;

  assert (token == DEFINE);
  next_token ();

  while (token == SYMBOL)
    {
      symbol = eat_symbol ();
      if (symbol->declared)
	perr ("can not define already declared variable '%s'", symbol->name);
      if (symbol->def_expr)
	perr ("multiple definitions for '%s'", symbol->name);
      if (symbol->init_expr || symbol->next_expr)
	perr ("can not define already assigned variable '%s'", symbol->name);
      eat_symbolic_token (BECOMES, ":=");

      next_allowed = 1;
      rhs = parse_expr ();
      eat_token (';');
      next_allowed = 0;
      symbol->def_expr = rhs;
    }
}

/*------------------------------------------------------------------------*/
static void
add_to_init_expr (Expr * expr)
{
  Expr **p;

  p = last_init_expr ? &last_init_expr->c1 : &init_expr;
  assert ((*p)->tag == '1');
  last_init_expr = *p = new_expr ('&', expr, *p);
}

static void
parse_init (void)
{
  Expr *res;

  assert (token == INIT);
  next_token ();

  res = parse_expr ();

  if ( !ltlspec )
    add_to_init_expr(res);
  else
    ltl_init_expr = new_expr('&', ltl_init_expr, res);
}

/*------------------------------------------------------------------------*/

static void
add_to_trans_expr (Expr * expr)
{
  Expr **p;

  p = last_trans_expr ? &last_trans_expr->c1 : &trans_expr;
  assert ((*p)->tag == '1');
  last_trans_expr = *p = new_expr ('&', expr, *p);
}

/*------------------------------------------------------------------------*/

static void
parse_trans (void)
{
  Expr *res;

  assert (token == TRANS);
  next_token ();

  next_allowed = 1;
  res = parse_expr ();
  next_allowed = 0;
 
  if ( !ltlspec )
    add_to_trans_expr (res);
  else
    ltl_trans_expr = new_expr( '&', ltl_trans_expr, res );
}

/*------------------------------------------------------------------------*/

static void
parse_invar (void)
{
  Expr *res, **p;

  if (ltlspec)
    perr("Error: INVAR not allowed in SMV translation of LTL formula\n");

  assert (token == INVAR);
  next_token ();

  invar_expr = (Expr**) realloc (invar_expr, ++invar_expr_cnt * sizeof(Expr*) );
  invar_expr[invar_expr_cnt-1] = parse_expr ();
}

/*------------------------------------------------------------------------*/

/* SW110531 Added for handling of fairness/justice properties
 */
static void
add_fair_expr (Expr *expr)
{
  fair_expr = (Expr**) realloc (fair_expr, ++fair_expr_cnt * sizeof(Expr*) );
  fair_expr[fair_expr_cnt-1] = expr;
  
  if (ltlspec)
    justice_groups[ltlspec]++;
}

/* SW110131 Added for handling of fairness/justice properties
 */
static void
parse_fairness (void)
{  
  assert (token == FAIRNESS);
  next_token ();

  add_fair_expr( parse_expr() );
}

/*------------------------------------------------------------------------*/

static void
parse_section (void)
{
  assert (token != EOF);

  if (token == VAR)
    parse_vars ();
  else if (token == SPEC)
    parse_spec ();
  else if (token == LTLSPEC)
    parse_ltlspec ();
  else if (token == ASSIGN)
    parse_assigns ();
  else if (token == DEFINE)
    parse_defines ();
  else if (token == TRANS)
    parse_trans ();
  else if (token == INVAR)
    parse_invar ();
  else if (token == INIT)
    parse_init ();
  /* SW110131
   */
  else if (token == FAIRNESS)
    parse_fairness ();
  /* SW110310 This will allow ; add the end of a section
   */
  else if (token == ';')
    eat_token (';');
  else
    perr ("expected EOF or section start");
}

/*------------------------------------------------------------------------*/

static void
parse_main (void)
{
  if (token != MODULE)
    perr ("expected 'MODULE'");

  next_token ();

  if (token != SYMBOL)
    perr ("expected module name");

  if (!ltlspec && strcmp (buffer, "main"))
    perr ("expected 'main'");

  next_token ();

  while (token != EOF)
    parse_section ();
}

/*------------------------------------------------------------------------*/

static Expr *
true_expr (void)
{
  return new_expr ('1', 0, 0);
}

static void
handle_ltlspec (Expr *expr) 
{
  char *ltlexpr_filename, *smv_filename, tmp_name[80];
  char buf[strlen(LTL_SYMBOL)+10];
  int pid, status, fid;
  Expr *symexpr, *tmp;
  Symbol *sym;
  FILE *file;
  
  strcpy (tmp_name, "/tmp/smvtoaig-ltlexpr-XXXXXX");
  fid = mkstemp (tmp_name);
  ltlexpr_filename = strdup (tmp_name);
  if (fid < 0 || !(file = fdopen (fid, "wt")))
    perr("Failed to open temporary file for writing LTL specification\n"); 
  
  print_expr(file, new_expr('!', expr, 0));
  print_expr(stderr, new_expr('!', expr, 0)); fprintf(stderr,"\n");
  fclose (file);
  close (fid);
  
  strcpy (tmp_name, "/tmp/smvtoaig-ltl2smv-result-XXXXXX");
  fid = mkstemp (tmp_name);
  smv_filename = strdup (tmp_name);
  sprintf(buf, "%d", ltlspec);
  close (fid);
  
  msg(1, "Calling LTL to SMV translator \"%s\" on property %d",
      ltl2smv, ltlspec);

  pid = fork ();
  if ( pid == 0 ) 
    {
      execlp (ltl2smv, ltl2smv, buf, ltlexpr_filename, smv_filename, NULL);
    }
  else if ( pid > 0 ) 
    {
      waitpid(pid, &status, 0);
      if (!WIFEXITED(status))
	perr("Failed to launch \"%s\"\n", ltl2smv);
      else if (WEXITSTATUS(status))
	perr("\"%s\" returned non-zero value %d\n",
	     ltl2smv, WEXITSTATUS(status));
    }
  else perr("Failed to fork process for launching \"%s\"\n", ltl2smv);  
  
  input = fopen(smv_filename, "rt");
  if ( !input )
    perr("Failed to open output file of LTL to SMV translator (\"%s\") for reading\n", ltl2smv);
  
  ltl_trans_expr = true_expr();
  ltl_init_expr = true_expr();

  next_token();
  parse_main();

  // Close and delete file
  fclose(input);
  unlink(ltlexpr_filename);
  free(ltlexpr_filename);
  unlink(smv_filename);
  free(smv_filename);

  // Create an internal symbol AIGER_LTL_p for this LTL property
  sprintf(buf,"%s%d", LTL_SYMBOL, ltlspec);
  sym = new_internal_symbol(buf);
  sym->declared = 1;
  symexpr = sym2expr(sym);

  // ltl_trans | IGNORE_LTL_p
  ltl_trans_expr = new_expr('|', ltl_trans_expr, symexpr);
  ltl_init_expr = new_expr('|', ltl_init_expr, symexpr);
  
  // (ltl_trans | IGNORE_LTL_p) & (IGNORE_LTL_p -> next(IGNORE_LTL_p))
  tmp = new_expr(next, symexpr, 0);
  tmp = new_expr(IMPLIES, symexpr, tmp);
  ltl_trans_expr = new_expr('&', ltl_trans_expr, tmp);

  // Add the literal !IGNORE_LTL_p to the justice constraint
  // The idea is that by setting IGNORE_LTL_p executions of the model are possible
  // that do not satisfy LTL property #p, but in that case this justice contraint
  // will not be satisfied.
  tmp = new_expr('!', symexpr, 0);
  add_fair_expr( tmp );

  // Add ltl_trans to model trans
  add_to_trans_expr(ltl_trans_expr);  
  add_to_init_expr( ltl_init_expr );
}

void 
handle_ltlspecs(void)
{
  justice_groups = calloc( ltlspec_expr_cnt+1, sizeof(justice_groups[0]) );
  justice_groups[0] = fair_expr_cnt;

  assert( !ltlspec );
  while( ltlspec++ < ltlspec_expr_cnt )
    handle_ltlspec(ltlspec_expr[ltlspec-1]);
}

/* SW110131 Modified for handling fairness properties
 */
static void
parse (void)
{
  lineno = 1;
  init_issymbol ();
  next_token ();
  init_expr = true_expr ();
  trans_expr = true_expr ();
  invar_expr = NULL;
  invar_expr_cnt = 0;
  fair_expr = NULL;
  fair_expr_cnt = 0;
  justice_groups = NULL;
  parse_main ();

  if (!spec_expr_cnt && !ltlspec_expr_cnt)
    msg (0, "Warning: SMV model contains no properties");
}

/*------------------------------------------------------------------------*/

static void
check_all_variables_are_defined_or_declared (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    {
#ifndef NDEBUG
      if (p->def_expr)
	{
	  /* NOTE: it may be 'declared'. */
	  assert (!p->init_expr);
	  assert (!p->next_expr);
	}

      if (p->init_expr || p->next_expr)
	assert (!p->def_expr);
#endif
      if (!p->declared && !p->def_expr)
	perr ("undeclared and undefined variable '%s'", p->name);
    }
}

/*------------------------------------------------------------------------*/

static void
unmark_symbols (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    p->mark = 0;
}

/*------------------------------------------------------------------------*/

static void check_non_cyclic_symbol (Symbol *);

/*------------------------------------------------------------------------*/

static void
check_non_cyclic_expr (Expr * expr)
{
  if (!expr)
    return;

  if (expr->tag != SYMBOL)
    {
      check_non_cyclic_expr (expr->c0);
      check_non_cyclic_expr (expr->c1);
    }
  else
    check_non_cyclic_symbol (expr->symbol);
}

/*------------------------------------------------------------------------*/

static void
check_non_cyclic_symbol (Symbol * s)
{
  if (s->mark == 2)
    perr ("cyclic definition of '%s'", s->name);

  if (s->mark)
    return;

  s->mark = 2;
  if (s->def_expr)
    check_non_cyclic_expr (s->def_expr);
  if (s->init_expr)
    check_non_cyclic_expr (s->init_expr);
  s->mark = 1;
}

/*------------------------------------------------------------------------*/

static void
check_non_cyclic_definitions (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    check_non_cyclic_symbol (p);

  unmark_symbols ();
}

/*------------------------------------------------------------------------*/

static unsigned count_next_nesting_level_symbol (Symbol *);

/*------------------------------------------------------------------------*/

static unsigned
count_next_nesting_level_expr (Expr * expr)
{
  unsigned res, tmp;

  if (!expr)
    return 1;

  if (expr->tag == next)
    return count_next_nesting_level_expr (expr->c0) + 1;

  if (expr->tag == SYMBOL)
    return count_next_nesting_level_symbol (expr->symbol);

  res = count_next_nesting_level_expr (expr->c0);
  tmp = count_next_nesting_level_expr (expr->c1);

  if (tmp > res)
    res = tmp;

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
count_next_nesting_level_symbol (Symbol * p)
{
  if (!p->mark)
    {
      if (p->def_expr)
	p->mark = count_next_nesting_level_expr (p->def_expr);
      else
	p->mark = 1;
    }

  return p->mark;
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
check_next_not_nested (void)
{
  unsigned nesting, moved;
  Expr *lhs, *iff;
  Symbol *p;
  int i;

  for (p = first_symbol; p; p = p->order)
    count_next_nesting_level_symbol (p);

  for (i = 0; i < spec_expr_cnt; i++ )
    if (count_next_nesting_level_expr (spec_expr[i]->c0) > 1)
      perr ("SPEC depends on 'next' operator through definitions");

  for (i = 0; i < fair_expr_cnt; i++ )
    if (count_next_nesting_level_expr (fair_expr[i]->c0) > 1)
      perr ("FAIRNESS depends on 'next' operator through definitions");

  for (i = 0; i < invar_expr_cnt; i++ )
    if (count_next_nesting_level_expr (invar_expr[i]) > 1)
      perr ("INVAR depends on 'next' operator through definitions");

  if (init_expr && count_next_nesting_level_expr (init_expr) > 1)
    perr ("INIT depends on 'next' operator through definitions");

  if (trans_expr && count_next_nesting_level_expr (trans_expr) > 2)
    perr ("TRANS depends too deeply on 'next' operator through definitions");

  moved = 0;
  for (p = first_symbol; p; p = p->order)
    {
      if (p->init_expr && count_next_nesting_level_expr (p->init_expr) > 1)
	perr ("right hand side of 'init (%s)' depends on next state",
	      p->name);

      if (p->next_expr)
	{
	  nesting = count_next_nesting_level_expr (p->next_expr);
	  if (nesting > 2)
	    perr ("'next (%s)' depends on next after next state", p->name);

	  if (nesting > 1)
	    {
	      lhs = new_expr (next, sym2expr (p), 0);
	      iff = new_expr (IFF, lhs, p->next_expr);
	      add_to_trans_expr (iff);
	      p->next_expr = 0;

	      msg (2, "moving to TRANS: next (%s)", p->name);
	      moved++;
	    }
	}
    }

  if (moved)
    msg (1, "moved %u next state assignments to TRANS", moved);

  unmark_symbols ();
}

/*------------------------------------------------------------------------*/

static void
analyze (void)
{
  check_all_variables_are_defined_or_declared ();
  check_non_cyclic_definitions ();
  check_next_not_nested ();
}

/*------------------------------------------------------------------------*/

static unsigned
hash_aig (Symbol * symbol, unsigned slice, AIG * c0, AIG * c1)
{
  const unsigned *q;
  unsigned long tmp;
  unsigned res;

  q = primes;

  tmp = (unsigned long) symbol;
  res = *q++ * tmp;

  res += *q++ * slice;

  tmp = (unsigned long) c0;
  res += *q++ * tmp;

  tmp = (unsigned long) c1;
  res += *q++ * tmp;

  res *= *q;

  res &= size_aigs - 1;

  assert (q <= eoprimes);
  assert (res < size_aigs);

  return res;
}

/*------------------------------------------------------------------------*/

static void
enlarge_aigs (void)
{
  unsigned old_size_aigs, i, h;
  AIG **old_aigs, *p, *next;

  old_aigs = aigs;
  old_size_aigs = size_aigs;

  size_aigs = size_aigs ? 2 * size_aigs : 1;
  NEWN (aigs, size_aigs);

  for (i = 0; i < old_size_aigs; i++)
    for (p = old_aigs[i]; p; p = next)
      {
	next = p->next;
	h = hash_aig (p->symbol, p->slice, p->c0, p->c1);
	p->next = aigs[h];
	aigs[h] = p;
      }

  free (old_aigs);
}

/*------------------------------------------------------------------------*/

static int
sign_aig (AIG * aig)
{
  long aig_as_long = (long) aig;
  int res = aig_as_long < 0 ? -1 : 1;
  return res;
}

/*------------------------------------------------------------------------*/

static int
eq_aig (AIG * aig, Symbol * symbol, unsigned slice, AIG * c0, AIG * c1)
{
  assert (sign_aig (aig) > 0);

  if (symbol)
    return aig->symbol == symbol && aig->slice == slice;
  else
    return aig->c0 == c0 && aig->c1 == c1;
}

/*------------------------------------------------------------------------*/

AIG **
find_aig (Symbol * s, unsigned l, AIG * c0, AIG * c1)
{
  AIG **p, *a;

  for (p = aigs + hash_aig (s, l, c0, c1);
       (a = *p) && !eq_aig (a, s, l, c0, c1); p = &a->next)
    ;

  return p;
}

/*------------------------------------------------------------------------*/

static AIG *
not_aig (AIG * aig)
{
  long aig_as_long, res_as_long;
  AIG *res;

  aig_as_long = (long) aig;
  res_as_long = -aig_as_long;
  res = (AIG *) res_as_long;

  assert (sign_aig (aig) * sign_aig (res) == -1);

  return res;
}

/*------------------------------------------------------------------------*/

#define strip_aig(sign,aig) \
  do { \
    (sign) = sign_aig (aig); \
    if ((sign) < 0) \
      (aig) = not_aig (aig); \
  } while (0)

/*------------------------------------------------------------------------*/

#define swap_aig(a,b) \
  do { \
    AIG * tmp_swap_aig = (a); \
    (a) = (b); \
    (b) = tmp_swap_aig; \
  } while(0)

/*------------------------------------------------------------------------*/

static AIG *
stripped_aig (AIG * aig)
{
  return sign_aig (aig) < 0 ? not_aig (aig) : aig;
}

/*------------------------------------------------------------------------*/
#ifndef NDEBUG
/*------------------------------------------------------------------------*/

static void
print_aig (AIG * aig)
{
  int sign;

  if (aig == TRUE)
    fputc ('1', stdout);
  else if (aig == FALSE)
    fputc ('0', stdout);
  else
    {
      strip_aig (sign, aig);

      if (sign < 0)
	fputc ('!', stdout);

      if (aig->symbol)
	{
	  fputs (aig->symbol->name, stdout);
	}
      else
	{
	  if (sign < 0)
	    fputc ('(', stdout);

	  print_aig (aig->c0);
	  fputc ('&', stdout);
	  print_aig (aig->c1);

	  if (sign < 0)
	    fputc (')', stdout);
	}
    }
}

/*------------------------------------------------------------------------*/

void
printnl_aig (AIG * aig)
{
  print_aig (aig);
  fputc ('\n', stdout);
}

/*------------------------------------------------------------------------*/
#endif
/*------------------------------------------------------------------------*/

static AIG *
simplify_aig_one_level (AIG * a, AIG * b)
{
  assert (optimize >= 1);

  if (a == FALSE || b == FALSE)
    return FALSE;

  if (b == TRUE || a == b)
    return a;

  if (a == TRUE)
    return b;

  if (a == not_aig (b))
    return FALSE;

  return 0;
}

/*------------------------------------------------------------------------*/

static AIG *
simplify_aig_two_level (AIG * a, AIG * b)
{
  AIG *a0, *a1, *b0, *b1, *signed_a, *signed_b;
  int s, t;

  assert (optimize >= 2);

  signed_a = a;
  signed_b = b;

  strip_aig (s, a);
  strip_aig (t, b);

  a0 = (a->symbol) ? a : a->c0;
  a1 = (a->symbol) ? a : a->c1;
  b0 = (b->symbol) ? b : b->c0;
  b1 = (b->symbol) ? b : b->c1;

  if (s > 0)
    {
      /* Assymetric Contradiction.
       *
       * (a0 & a1) & signed_b
       */
      if (a0 == not_aig (signed_b))
	return FALSE;
      if (a1 == not_aig (signed_b))
	return FALSE;

      /* Assymetric Idempotence.
       *
       * (a0 & a1) & signed_b
       */
      if (a0 == signed_b)
	return signed_a;
      if (a1 == signed_b)
	return signed_a;
    }
  else
    {
      /* Assymetric Subsumption.
       *
       * (!a0 | !a1) & signed_b
       */
      if (a0 == not_aig (signed_b))
	return signed_b;
      if (a1 == not_aig (signed_b))
	return signed_b;
    }

  if (t > 0)
    {
      /* Assymetric Contradiction.
       *
       * signed_a & (b0 & b1)
       */
      if (b0 == not_aig (signed_a))
	return FALSE;
      if (b1 == not_aig (signed_a))
	return FALSE;

      /* Assymetric Idempotence.
       *
       * signed_a & (b0 & b1)
       */
      if (b0 == signed_a)
	return signed_b;
      if (b1 == signed_a)
	return signed_b;
    }
  else
    {
      /* Assymetric Subsumption.
       *
       * signed_a & (!b0 | !b1)
       */
      if (b0 == not_aig (signed_a))
	return signed_a;
      if (b1 == not_aig (signed_a))
	return signed_a;
    }

  if (s > 0 && t > 0)
    {
      /* Symmetric Contradiction.
       *
       * (a0 & a1) & (b0 & b1)
       */
      if (a0 == not_aig (b0))
	return FALSE;
      if (a0 == not_aig (b1))
	return FALSE;
      if (a1 == not_aig (b0))
	return FALSE;
      if (a1 == not_aig (b1))
	return FALSE;
    }
  else if (s < 0 && t > 0)
    {
      /* Symmetric Subsumption.
       *
       * (!a0 | !a1) & (b0 & b1)
       */
      if (a0 == not_aig (b0))
	return b;
      if (a1 == not_aig (b0))
	return b;
      if (a0 == not_aig (b1))
	return b;
      if (a1 == not_aig (b1))
	return b;
    }
  else if (s > 0 && t < 0)
    {
      /* Symmetric Subsumption.
       *
       * a0 & a1 & (!b0 | !b1)
       */
      if (b0 == not_aig (a0))
	return a;
      if (b1 == not_aig (a0))
	return a;
      if (b0 == not_aig (a1))
	return a;
      if (b1 == not_aig (a1))
	return a;
    }
  else
    {
      assert (s < 0 && t < 0);

      /* Resolution.
       *
       * (!a0 | !a1) & (!b0 | !b1)
       */
      if (a0 == b0 && a1 == not_aig (b1))
	return not_aig (a0);
      if (a0 == b1 && a1 == not_aig (b0))
	return not_aig (a0);
      if (a1 == b0 && a0 == not_aig (b1))
	return not_aig (a1);
      if (a1 == b1 && a0 == not_aig (b0))
	return not_aig (a1);
    }

  return 0;
}

/*------------------------------------------------------------------------*/

static AIG *
new_aig (Symbol * symbol, unsigned slice, AIG * a, AIG * b)
{
  AIG **p, *res;

  assert (!symbol == (a && b));
  assert (!slice || symbol);

  if (count_aigs >= size_aigs)
    enlarge_aigs ();

  if (!symbol)
    {
    TRY_TO_SIMPLIFY_AGAIN:
      assert (optimize >= 1);
      if ((res = simplify_aig_one_level (a, b)))
	return res;

      if (optimize >= 2 && (res = simplify_aig_two_level (a, b)))
	return res;

      if (optimize >= 3)
	{
	  AIG *not_a = not_aig (a);
	  AIG *not_b = not_aig (b);

	  if (sign_aig (a) < 0 && !not_a->symbol)
	    {
	      /* Assymetric Substitution
	       *
	       * (!a0 | !a1) & b
	       */
	      if (not_a->c0 == b)
		{
		  a = not_aig (not_a->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_a->c1 == b)
		{
		  a = not_aig (not_a->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (b) < 0 && !not_b->symbol)
	    {
	      /* Assymetric Substitution
	       *
	       * a & (!b0 | !b1)
	       */
	      if (not_b->c0 == a)
		{
		  b = not_aig (not_b->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_b->c1 == a)
		{
		  b = not_aig (not_b->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (a) > 0 && !a->symbol &&
	      sign_aig (b) < 0 && !not_b->symbol)
	    {
	      /* Symmetric Substitution.
	       *
	       * (a0 & a1) & (!b0 | !b1)
	       */
	      if (not_b->c0 == a->c0 || not_b->c0 == a->c1)
		{
		  b = not_aig (not_b->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_b->c1 == a->c0 || not_b->c1 == a->c1)
		{
		  b = not_aig (not_b->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (a) < 0 && !not_a->symbol &&
	      sign_aig (b) > 0 && !b->symbol)
	    {
	      /* Symmetric Substitution.
	       *
	       * (!a0 | !a1) & (b0 & b1)
	       */
	      if (not_a->c0 == b->c0 || not_a->c0 == b->c1)
		{
		  a = not_aig (not_a->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_a->c1 == b->c0 || not_a->c1 == b->c1)
		{
		  a = not_aig (not_a->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }
	}

      if (optimize >= 4)
	{
	  if (sign_aig (a) > 0 && !a->symbol &&
	      sign_aig (b) > 0 && !b->symbol)
	    {
	      /* Symmetric Idempotence.
	       *
	       * (a0 & a1) & (b0 & b1)
	       */
	      if (a->c0 == b->c0)
		{
		  a = a->c1;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c0 == b->c1)
		{
		  a = a->c1;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c1 == b->c0)
		{
		  a = a->c0;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c1 == b->c1)
		{
		  a = a->c0;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }
	}

      if (stripped_aig (a)->id > stripped_aig (b)->id)
	swap_aig (a, b);
    }


  p = find_aig (symbol, slice, a, b);
  res = *p;
  if (!res)
    {
      NEW (res);

      assert (sign_aig (res) > 0);

      res->symbol = symbol;
      res->slice = slice;
      res->c0 = a;
      res->c1 = b;

      res->id = count_aigs++;
      *p = res;
    }

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *build_expr (Expr *, unsigned);

/*------------------------------------------------------------------------*/

AIG *
symbol_aig (Symbol * symbol, unsigned slice)
{
  if ( symbol->def_expr )
    return build_expr(symbol->def_expr, slice);
  else
    return new_aig (symbol, slice, 0, 0);
}

/*------------------------------------------------------------------------*/

static AIG *
and_aig (AIG * a, AIG * b)
{
  return new_aig (0, 0, a, b);
}

/*------------------------------------------------------------------------*/

static AIG *
or_aig (AIG * a, AIG * b)
{
  return not_aig (and_aig (not_aig (a), not_aig (b)));
}

/* SW110307 
 */
static AIG *
xor_aig (AIG * a, AIG * b)
{
  return and_aig (not_aig (and_aig (a, b)), not_aig (and_aig (not_aig (a), not_aig (b))));
}

/*------------------------------------------------------------------------*/

static AIG *
implies_aig (AIG * a, AIG * b)
{
  return not_aig (and_aig (a, not_aig (b)));
}

/*------------------------------------------------------------------------*/

static AIG *
iff_aig (AIG * a, AIG * b)
{
  return and_aig (implies_aig (a, b), implies_aig (b, a));
}

/*------------------------------------------------------------------------*/

static AIG *
ite_aig (AIG * c, AIG * t, AIG * e)
{
  return and_aig (implies_aig (c, t), implies_aig (not_aig (c), e));
}

static AIG *
build_cases (Expr * expr, unsigned slice)
{
  AIG *c, *t, *e;
  Expr *clause;

  if (!expr)
    return TRUE;

  assert (expr->tag == CASE);

  clause = expr->c0;
  assert (clause->tag == ':');

  c = build_expr (clause->c0, slice);
  t = build_expr (clause->c1, slice);
  e = build_cases (expr->c1, slice);

  return ite_aig (c, t, e);
}

/*------------------------------------------------------------------------*/

static AIG *
build_expr_uncached (Expr * expr, unsigned slice)
{
  Tag tag = expr->tag;
  AIG *l, *r;

  assert (expr);
  assert (slice <= 1);

  if (tag == '0')
    return FALSE;

  if (tag == '1')
    return TRUE;

  if (tag == SYMBOL)
    return symbol_aig (expr->symbol, slice);

  if (tag == next)
    return build_expr (expr->c0, slice + 1);

  if (tag == CASE)
    return build_cases (expr, slice);

  l = build_expr (expr->c0, slice);

  if (tag == NOT)
    return not_aig (l);

  r = build_expr (expr->c1, slice);

  if (tag == AND)
    return and_aig (l, r);

  if (tag == OR)
    return or_aig (l, r);

  /* SW110307 
   */
  if (tag == XOR) {
    return xor_aig (l, r);
  }
  if (tag == XNOR) {
    return not_aig( xor_aig (l, r) );
  }

  if (tag == IMPLIES)
    return implies_aig (l, r);

  assert (tag == IFF);

  return iff_aig (l, r);
}

/*------------------------------------------------------------------------*/

static AIG *
build_expr (Expr * expr, unsigned slice) 
{
  AIG * res;
  assert (0 <= slice && slice < 2);
  if ((res = expr->cache[slice])) return res;
  res = build_expr_uncached (expr, slice);
  expr->cache[slice] = res;
  return res;
}

/*------------------------------------------------------------------------*/

static void
cache (AIG * aig, AIG * res)
{
  assert (sign_aig (aig) > 0);

  aig->cache = res;

  if (count_cached >= size_cached)
    {
      size_cached = size_cached ? 2 * size_cached : 1;
      cached = realloc (cached, size_cached * sizeof (cached[0]));
    }

  cached[count_cached++] = aig;
}

/*------------------------------------------------------------------------*/

static void
reset_cache (void)
{
  unsigned i;
  AIG *aig;

  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig);
      assert (aig->cache);
      aig->cache = 0;
    }

  count_cached = 0;
}

/*------------------------------------------------------------------------*/

static AIG *
shift_aig_aux (AIG * aig, unsigned delta)
{
  AIG *res, *l, *r;
  int sign;

  strip_aig (sign, aig);

  if (!aig->cache)
    {
      if (!aig->symbol)
	{
	  l = shift_aig_aux (aig->c0, delta);
	  r = shift_aig_aux (aig->c1, delta);
	  res = and_aig (l, r);
	}
      else
	res = symbol_aig (aig->symbol, aig->slice + delta);

      cache (aig, res);
    }
  else
    res = aig->cache;

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
shift_aig (AIG * aig, unsigned delta)
{
  AIG *res;

  if (delta == 0 || aig == FALSE || aig == TRUE)
    return aig;

  res = shift_aig_aux (aig, delta);
  reset_cache ();

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
next_aig (AIG * aig)
{
  return shift_aig (aig, 1);
}

/*------------------------------------------------------------------------*/

static void
build_assignments (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    {
      if (p->def_expr)
	p->def_aig = build_expr (p->def_expr, 0);

      if (p->init_expr)
	p->init_aig = build_expr (p->init_expr, 0);

      if (p->next_expr)
	p->next_aig = build_expr (p->next_expr, 0);
    }
}

/*------------------------------------------------------------------------*/

static AIG *
substitute_def_next_aig_delta (AIG * aig, unsigned delta)
{
  AIG *res, *l, *r;
  Symbol *symbol;
  int sign;

  if (aig == TRUE || aig == FALSE)
    return aig;

  assert (delta == 0 || delta == 1);

  strip_aig (sign, aig);

  if (!aig->cache)
    {
      symbol = aig->symbol;
      if (symbol)
	{
	  if (symbol->def_aig)
	    res = substitute_def_next_aig_delta (symbol->def_aig, delta);
	  else if (aig->slice)
	    {
	      assert (!delta);
	      assert (aig->slice == 1);

	      if (symbol->next_aig)
		res = substitute_def_next_aig_delta (symbol->next_aig, 0);
	      else
		res = aig;
	    }
	  else
	    res = aig;
	}
      else
	{
	  l = substitute_def_next_aig_delta (aig->c0, delta);
	  r = substitute_def_next_aig_delta (aig->c1, delta);
	  res = and_aig (l, r);
	}

      cache (aig, res);
    }
  else
    res = aig->cache;

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
substitute_def_next_aig (AIG * node)
{
  return substitute_def_next_aig_delta (node, 0);
}

/*------------------------------------------------------------------------*/

static void
substitute_def_next_symbol (Symbol * symbol)
{
  if (symbol->def_aig)
    symbol->def_aig = substitute_def_next_aig (symbol->def_aig);

  if (symbol->init_aig)
    symbol->init_aig = substitute_def_next_aig (symbol->init_aig);

  if (symbol->next_aig)
    symbol->next_aig = substitute_def_next_aig (symbol->next_aig);
}

/*------------------------------------------------------------------------*/

static void
substitute_def_next_symbols (void)
{
  Symbol *p;
  for (p = first_symbol; p; p = p->order)
    substitute_def_next_symbol (p);
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
substitute_def_next (void)
{
  int i;
  substitute_def_next_symbols ();
  init_aig = substitute_def_next_aig (init_aig);
  trans_aig = substitute_def_next_aig (trans_aig);

  for( i = 0; i < invar_expr_cnt; i++ ) 
    invar_aig[i] = substitute_def_next_aig (invar_aig[i]);
 
  for( i = 0; i < spec_expr_cnt; i++ ) 
    bad_aig[i] = substitute_def_next_aig (bad_aig[i]);
  
  for( i = 0; i < fair_expr_cnt; i++ ) 
    fair_aig[i] = substitute_def_next_aig (fair_aig[i]);  

  reset_cache ();
}

/*------------------------------------------------------------------------*/

static AIG *
substitute_init_aig (AIG * aig)
{
  AIG *res, *l, *r;
  Symbol *symbol;
  int sign;

  if (aig == TRUE || aig == FALSE)
    return aig;

  strip_aig (sign, aig);

  if (!aig->cache)
    {
      symbol = aig->symbol;
      if (symbol)
	{
	  assert (!aig->slice);
	  assert (!symbol->def_aig);	/* substituted before */

	  if (symbol->init_aig)
	    res = substitute_init_aig (symbol->init_aig);
	  else
	    res = aig;
	}
      else
	{
	  l = substitute_init_aig (aig->c0);
	  r = substitute_init_aig (aig->c1);
	  res = and_aig (l, r);
	}

      cache (aig, res);
    }
  else
    res = aig->cache;

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static void
substitute_init_symbol (Symbol * symbol)
{
  if (symbol->init_aig)
    symbol->init_aig = substitute_init_aig (symbol->init_aig);
}

/*------------------------------------------------------------------------*/

static void
substitute_init_symbols (void)
{
  Symbol *p;
  for (p = first_symbol; p; p = p->order)
    substitute_init_symbol (p);
}

/*------------------------------------------------------------------------*/

static void
substitute_init (void)
{
  substitute_init_symbols ();
  init_aig = substitute_init_aig (init_aig);
  reset_cache ();
}

/*------------------------------------------------------------------------*/

static void
substitute (void)
{
  substitute_def_next ();
  substitute_init ();
}

/*------------------------------------------------------------------------*/

static AIG *
flip_aux (AIG * aig)
{
  AIG *res, *l, *r;
  Symbol *symbol;
  int sign;

  if (aig == TRUE || aig == FALSE)
    return aig;

  strip_aig (sign, aig);

  res = aig->cache;
  if (!res)
    {
      symbol = aig->symbol;
      if (!symbol)
	{
	  l = flip_aux (aig->c0);
	  r = flip_aux (aig->c1);
	  res = and_aig (l, r);
	}
      else
	res = (symbol->init_aig == TRUE) ? not_aig (aig) : aig;

      cache (aig, res);
    }

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static char *
strdup2 (const char *a, const char *b)
{
  char *res = malloc (strlen (a) + strlen (b) + 1);
  strcpy (res, a);
  strcat (res, b);
  return res;
}

/*------------------------------------------------------------------------*/

static void
classify_nondet_aux (AIG * aig, const char *context)
{
  Symbol *symbol;
  int sign;

  if (aig == TRUE || aig == FALSE)
    return;

  strip_aig (sign, aig);

  symbol = aig->symbol;
  if (symbol && aig->slice)
    aig = symbol_aig (symbol, 0);	/* normalize to slice 0 */

  if (aig->cache)
    return;

  if (symbol)
    {
      if (!symbol->nondet && !symbol->latch)
	{
	  assert (!symbol->input);

	  nondets++;

	  inputs++;
	  latches++;

	  symbol->nondet = 1;
	  symbol->next_aig = symbol_aig (symbol, 1);
	  symbol->init_aig = FALSE;

	  msg (2, "nondet: %s  (found in %s)", symbol->name, context);
	}
    }
  else
    {
      classify_nondet_aux (aig->c0, context);
      classify_nondet_aux (aig->c1, context);
    }

  cache (aig, aig);
}

/*------------------------------------------------------------------------*/

static void
classify_nondet (AIG * aig, const char *context)
{
  classify_nondet_aux (aig, context);
  reset_cache ();
}

/*------------------------------------------------------------------------*/

static void
new_latch (Symbol * symbol)
{
  assert (!symbol->latch);
  assert (!symbol->input);

  latches++;
  symbol->latch = 1;
  msg (2, "latch: %s", symbol->name);
}

/*------------------------------------------------------------------------*/

static Symbol *
get_initialized_symbol (void)
{
  Symbol * res;

  res = have_symbol (INITIALIZED_SYMBOL);
  if (res)
    {
      if (res->def_aig || res->init_aig != FALSE || res->next_aig != TRUE)
	die ("name clash for '%s'", INITIALIZED_SYMBOL);

      assert (!res->input);
      if (!res->latch)
	new_latch (res);

      return res;
    }

  res = new_internal_symbol (INITIALIZED_SYMBOL);
  new_latch (res);

  res->init_aig = FALSE;
  res->next_aig = TRUE;

  return res;
}

/*------------------------------------------------------------------------*/

static void
classify_states (void)
{
  Symbol *p, * initialized_symbol = 0;
  unsigned oldndets, newndets;

  for (p = first_symbol; p; p = p->order)
    {
      if (p == initialized_symbol)
	continue;

      if (p->next_aig &&
	  (p->init_aig == FALSE || p->init_aig == TRUE || !p->init_aig))
	{
	  new_latch (p);
	}
      else if (p->init_aig)
	{
	  if (p->next_aig)
	    {
	      assert (p->init_aig != TRUE);
	      assert (p->init_aig != FALSE);

	      trans_aig = and_aig (trans_aig,
				   iff_aig (symbol_aig (p, 1), p->next_aig));
	    }

	  if (p->init_aig == FALSE)
	    {
	      msg (2, "zero initialized latch without next: %s", p->name);
	    }
	  else if (p->init_aig == TRUE)
	    {
	      msg (2, "true initialized latch without next: %s", p->name);
	    }
	  else
	    {
	      assert (p->init_aig != TRUE);
	      msg (2, "non constant initialized latch: %s", p->name);
	    }

	  init_aig = and_aig (init_aig,
			      iff_aig (symbol_aig (p, 0), p->init_aig));
	}
      else
	{
	  assert (!p->init_aig);
	  assert (!p->next_aig);
	}
    }

  if (latches && (init_aig != TRUE || trans_aig != TRUE))
    msg (1, "%u deterministic latches", latches);

  classify_nondet (init_aig, "INIT");
  if (nondets)
    msg (1, "found %u non determistic inputs/latches in INIT", nondets);

  oldndets = nondets;
  classify_nondet (trans_aig, "TRANS");

  newndets = nondets - oldndets;
  if (newndets)
    msg (1, "found %u %snon deterministic inputs/latches in TRANS",
	 newndets, oldndets ? "additional " : "");

  for (p = first_symbol; p; p = p->order)
    {
      if (p->def_aig || p->nondet || p->latch)
	continue;

      assert (!p->input);
      assert (!p->init_aig);
      assert (!p->next_aig);

      p->input = 1;
      inputs++;

      msg (2, "input: %s", p->name);
    }
}

/*------------------------------------------------------------------------*/

static void
check_deterministic (void)
{
  Symbol *p;

  assert (init_aig == TRUE);
  assert (trans_aig == TRUE);

  for (p = first_symbol; p; p = p->order)
    {
      if (p->nondet)
	assert (!p->input && !p->latch);

      if (p->input)
	{
	  assert (!p->init_aig);
	  assert (!p->next_aig);
	}
      else if (p->latch)
	{
	  assert (p->init_aig == TRUE || p->init_aig == FALSE || !p->init_aig);
	  assert (p->next_aig);
	}
      else if (p->nondet)
	{
	  assert (p->init_aig == FALSE);
	  assert (p->next_aig);
	}
      else
	assert (p->def_aig);
    }
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
choueka (void)
{
  AIG *tmp, *initialized;
  Symbol *p;

  if (init_aig != TRUE || trans_aig != TRUE)
    {
      if (init_aig == TRUE && !have_symbol (INITIALIZED_SYMBOL))
	{
	  int i;
	  invalid_symbol = new_internal_symbol (INVALID_SYMBOL);
	  new_latch (invalid_symbol);

	  tmp = symbol_aig (invalid_symbol, 0);

	  for( i = 0; i < spec_expr_cnt; i++ ) 
	    bad_aig[i] = and_aig (bad_aig[i], not_aig (tmp));
  
	  for( i = 0; i < fair_expr_cnt; i++ )
	    fair_aig[i] = and_aig (fair_aig[i], not_aig (tmp));

	  tmp = or_aig (tmp, not_aig (trans_aig));

	  invalid_symbol->init_aig = FALSE;
	  invalid_symbol->next_aig = tmp;
	}
      else
	{
	  int i;
	  valid_symbol = new_internal_symbol (VALID_SYMBOL);
	  new_latch (valid_symbol);

	  tmp = symbol_aig (valid_symbol, 0);

	  for( i = 0; i < spec_expr_cnt; i++ ) 
	    bad_aig[i] = and_aig (bad_aig[i], tmp);
  
	  for( i = 0; i < fair_expr_cnt; i++ )
	    fair_aig[i] = and_aig (fair_aig[i], tmp);

	  tmp = and_aig (tmp, trans_aig);

	  initialized_symbol = get_initialized_symbol ();
	  initialized = symbol_aig (initialized_symbol, 0);
	  tmp = ite_aig (initialized, tmp, next_aig (init_aig));

	  for( i = 0; i < invar_expr_cnt; i++ )
	    invar_aig[i] = or_aig(invar_aig[i], not_aig(initialized));

	  valid_symbol->init_aig = FALSE;
	  valid_symbol->next_aig = tmp;

	  for (p = first_symbol; p; p = p->order)
	    {
	      if (!p->latch || !p->init_aig ||
		  p->nondet || p == initialized_symbol || p == valid_symbol)
		continue;

	      if (p->init_aig == FALSE)
		p->next_aig = and_aig (initialized, p->next_aig);
	      else 
		{
		  assert (p->init_aig == TRUE);
		  p->next_aig = or_aig (not_aig (initialized), p->next_aig);
		}
	    }
	}

      init_aig = TRUE;
      trans_aig = TRUE;
    }

  check_deterministic ();

  msg (1, "%u inputs", inputs);
  msg (1, "%u latches", latches);
}

/*------------------------------------------------------------------------*/

static void
check_rebuild_aig (AIG * aig)
{
  AIG *tmp;
  int s;

  if (aig == TRUE || aig == FALSE)
    return;

  strip_aig (s, aig);
  if (aig->cache)
    return;

  if (aig->symbol)
    return;

  check_rebuild_aig (aig->c0);
  check_rebuild_aig (aig->c1);

  tmp = and_aig (aig->c0, aig->c1);
  assert (tmp == aig);
  cache (aig, tmp);
}

/*------------------------------------------------------------------------*/

static void
check_rebuild (void)
{
  Symbol *p;
  for (p = first_symbol; p; p = p->order)
    if (p->next_aig)
      check_rebuild_aig (p->next_aig);
  reset_cache ();
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
build (void)
{
  init_aig = build_expr (init_expr, 0);
  trans_aig = build_expr (trans_expr, 0);
  
  if ( invar_expr_cnt )
    {
      int i;
      invar_aig = (AIG**) malloc (invar_expr_cnt * sizeof(AIG*));
      for (i = 0; i < invar_expr_cnt; i++)
	invar_aig[i] = build_expr (invar_expr[i], 0);
    }

  if ( spec_expr_cnt )
    {
      int i;
      bad_aig = (AIG**) malloc (spec_expr_cnt * sizeof(AIG*));
      for (i = 0; i < spec_expr_cnt; i++)
	bad_aig[i] = not_aig (build_expr (spec_expr[i]->c0, 0));
    }

  if ( fair_expr_cnt )
    {
      int i;
      fair_aig = (AIG**) malloc (fair_expr_cnt * sizeof(AIG*));
      for (i = 0; i < fair_expr_cnt; i++)
	fair_aig[i] = build_expr (fair_expr[i], 0);
    }

  build_assignments ();
  substitute ();
  
  classify_states ();
  
  choueka ();
  
  check_rebuild ();
}

/*------------------------------------------------------------------------*/

static void
tseitin_symbol (Symbol * p, unsigned slice)
{
  AIG *aig = symbol_aig (p, slice);
  assert (!aig->idx);
  idx += 2;
  aig->idx = idx;
}

/*------------------------------------------------------------------------*/

static void
tseitin_inputs (void)
{
  Symbol *p;
  assert (!idx);
  for (p = first_symbol; p; p = p->order)
    {
      if (p->input)
	tseitin_symbol (p, 0);

      if (p->nondet)
	tseitin_symbol (p, 1);
    }

  assert (idx == 2 * inputs);
}

/*------------------------------------------------------------------------*/

static void
tseitin_latches (void)
{
  Symbol *p;

  assert (idx == 2 * inputs);
  for (p = first_symbol; p; p = p->order)
    {
      if (p->latch || p->nondet)
	tseitin_symbol (p, 0);
    }

  assert (idx == 2 * (inputs + latches));
}

/*------------------------------------------------------------------------*/

static void
tseitin_aig (AIG * aig)
{
  int sign;

  if (aig == TRUE || aig == FALSE)
    return;

  strip_aig (sign, aig);

  if (aig->symbol)
    {
      assert (aig->idx);
      return;
    }

  if (aig->idx)
    return;

  tseitin_aig (aig->c0);
  tseitin_aig (aig->c1);

  idx += 2;
  aig->idx = idx;
  ands++;
  cache (aig, aig);
}

/*------------------------------------------------------------------------*/

static void
tseitin_next (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    {
      if (!p->latch && !p->nondet)
	continue;

      assert (p->next_aig);
      assert (p->init_aig == FALSE||p->init_aig==TRUE||!p->init_aig);

      tseitin_aig (p->next_aig);
    }
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
tseitin (void)
{
  int i;
  ands = 0;
  tseitin_inputs ();
  tseitin_latches ();
  tseitin_next ();

  for( i = 0; i < invar_expr_cnt; i++ ) 
    tseitin_aig (invar_aig[i]);
  for( i = 0; i < spec_expr_cnt; i++ ) 
    tseitin_aig (bad_aig[i]);
  for( i = 0; i < fair_expr_cnt; i++ ) 
    tseitin_aig (fair_aig[i]);

  msg (1, "%u ands", ands);
  assert (inputs + latches + ands == idx / 2);
}

/*------------------------------------------------------------------------*/

static unsigned
aig_idx (AIG * aig)
{
  unsigned res;
  int sign;

  assert (aig);

  if (aig == TRUE)
    return 1;

  if (aig == FALSE)
    return 0;

  strip_aig (sign, aig);
  res = aig->idx;
  assert (res > 1);
  assert (!(res & 1));
  if (sign < 0)
    res++;

  return res;
}

/*------------------------------------------------------------------------*/

static void
add_inputs (void)
{
  Symbol *p;
  unsigned i;
  char *tmp;

  i = 2;
  for (p = first_symbol; p; p = p->order)
    {
      if (p->input)
	{
	  assert (aig_idx (symbol_aig (p, 0)) == i);
	  aiger_add_input (writer, i, strip_symbols ? 0 : p->name);
	  i += 2;
	}

      if (p->nondet)
	{
	  assert (aig_idx (symbol_aig (p, 1)) == i);
	  tmp = strip_symbols ? 0 : strdup2 (NEXT_PREFIX, p->name);
	  aiger_add_input (writer, i, tmp);
	  if (tmp)
	    free (tmp);
	  i += 2;
	}
    }
}

/*------------------------------------------------------------------------*/

static void
add_latches (void)
{
  Symbol *p;
  unsigned i;

  i = 2 * (inputs + 1);
  for (p = first_symbol; p; p = p->order)
    {
      if (p->latch || p->nondet)
	{
	  assert (aig_idx (symbol_aig (p, 0)) == i);
	  assert (p->next_aig);

	  aiger_add_latch (writer,
			   i, aig_idx (p->next_aig),
			   strip_symbols ? 0 : p->name);

	  if (p->init_aig == TRUE)
	    aiger_add_reset (writer, i, 1);
	  else if (!p->init_aig)
	    aiger_add_reset (writer, i, i);
	  else
	    assert (p->init_aig == FALSE);

	  i += 2;
	}
    }
}

/*------------------------------------------------------------------------*/

static void
add_ands (void)
{
  unsigned i, j;
  AIG *aig;

  j = 2 * (inputs + latches + 1);
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (sign_aig (aig) > 0);
      assert (aig_idx (aig) == j);
      aiger_add_and (writer,
		     aig_idx (aig), aig_idx (aig->c0), aig_idx (aig->c1));
      j += 2;
    }
}

/*------------------------------------------------------------------------*/

/* SW110131 Modified for handling multiple specifications and fairness 
   properties
 */
static void
print (void)
{
  char symb[strlen(FAIR_SYMBOL)+strlen(NEVER_SYMBOL)+strlen(INVAR_SYMBOL)+10]; /* "Large enough" */
  int i;
  AIG *init;

  tseitin ();

  writer = aiger_init ();

  add_inputs ();
  add_latches ();
  add_ands ();

  for( i = 0; i < invar_expr_cnt; i++ ) {
    sprintf(symb, "%s_%d", INVAR_SYMBOL, i);
    aiger_add_constraint (writer, aig_idx (invar_aig[i]),
			  strip_symbols ? 0 : symb);  
  }
  
  for( i = 0; i < spec_expr_cnt; i++ ) {
    sprintf(symb, "%s_%d", NEVER_SYMBOL, i);
    aiger_add_bad (writer, aig_idx (bad_aig[i]),
		   strip_symbols ? 0 : symb);  
  }

  for( i = 0; i < justice_groups[0]; i++ ) {
    sprintf(symb, "%s_%d", FAIR_SYMBOL, i);
    aiger_add_fairness (writer, aig_idx (fair_aig[i]),
			strip_symbols ? 0 : symb);
  }

  unsigned *lits = calloc( fair_expr_cnt - justice_groups[0], 
			   sizeof(unsigned) );
  unsigned *l = lits;
  for( i = justice_groups[0]; i < fair_expr_cnt; i++ )
    *(l++) = aig_idx (fair_aig[i]);

  l = lits;
  for( i = 1; i <= ltlspec_expr_cnt; i++ ) 
    {
      sprintf(symb, "%s_%d", JUST_SYMBOL, i-1 );
      aiger_add_justice (writer, justice_groups[i], 
			 l, strip_symbols ? 0 : symb);
      l+= justice_groups[i];
    }
  
  free(lits);
  
  reset_cache ();
  if (!strip_symbols)
    {
      aiger_add_comment (writer, "smvtoaig");
      aiger_add_comment (writer, aiger_version ());
      aiger_add_comment (writer, input_name);
    }
  
  if (output_name)
    {
      if (!aiger_open_and_write_to_file (writer, output_name))
	die ("failed to write to %s", output_name);
    }
  else
    {
      aiger_mode mode = ascii ? aiger_ascii_mode : aiger_binary_mode;
      if (!aiger_write_to_file (writer, mode, stdout))
	die ("failed to write to <stdout>");
    }

  aiger_reset (writer);
}

/*------------------------------------------------------------------------*/

static void
release_symbols (void)
{
  Symbol *p, *order;

  for (p = first_symbol; p; p = order)
    {
      order = p->order;
      free (p->name);
      free (p);
    }

  free (symbols);
}

/*------------------------------------------------------------------------*/

static void
release_exprs (void)
{
  Expr *p, *next;

  for (p = first_expr; p; p = next)
    {
      next = p->next;
      free (p);
    }
}

/*------------------------------------------------------------------------*/

static void
release_aig_chain (AIG * aig)
{
  AIG *p, *next;

  for (p = aig; p; p = next)
    {
      next = p->next;
      free (p);
    }
}

/*------------------------------------------------------------------------*/

static void
release_aigs (void)
{
  unsigned i;

  for (i = 0; i < size_aigs; i++)
    release_aig_chain (aigs[i]);

  free (aigs);
  free (cached);
}

/*------------------------------------------------------------------------*/

static void
release (void)
{
  msg (2, "%u symbols", count_symbols);
  msg (2, "%u expressions", count_symbols);
  msg (2, "%u aigs", count_aigs);

  release_symbols ();
  release_exprs ();
  release_aigs ();

  /* SW110606
   */
  if ( invar_expr_cnt )
    {
      free( invar_aig );
      free( invar_expr );
    }
  /* SW110131     
   */
  if ( spec_expr_cnt ) 
    {
      free( bad_aig );
      free( spec_expr );
    }
  if ( fair_expr_cnt ) 
    {
      free( fair_aig );
      free( fair_expr );
    }
  if ( ltlspec_expr_cnt )
    free( ltlspec_expr );
  free( justice_groups );
    
  free (expr_stack);
  free (buffer);
}

/*------------------------------------------------------------------------*/

#define USAGE \
"usage: smvtoaig [-h][-v][-s][-a][-L<ltl2smv>][-O(1|2|3|4)][src [dst]]\n" \
"\n" \
"  -h  print this command line option summary\n" \
"  -v  increase verbosity level\n" \
"  -s  strip symbols (default is to produce a symbol table)\n" \
"  -a  generate AIG in ASCII AIGER format (default is binary format)\n" \
"  -L  path to ltl2smv translator\n" \
"  -Oi AIG optimization level (default is maximum of 4)\n" \
"\n" \
"  src input file in flat SMV\n" \
"      (default is stdin, one module, only boolean variables)\n" \
"\n" \
"  dst output file in AIGER format (default stdout, binary format)\n"

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  int i;
  ltlspec = 0;
  ltl2smv = "ltl2smv";

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stdout);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-v"))
	verbose++;
      else if (!strcmp (argv[i], "-s"))
	strip_symbols = 1;
      else if (!strcmp (argv[i], "-a"))
	ascii = 1;
      else if (!strcmp (argv[i], "-L"))
	{
	  if ( ++i >= argc )
	    die("expected path to ltl2smv translator after -L\n");
	  ltl2smv = argv[i];
	}
      else if (argv[i][0] == '-' && argv[i][1] == 'O')
	{
	  optimize = atoi (argv[i] + 2);
	  if (optimize != 1 && optimize != 2 && optimize != 3
	      && optimize != 4)
	    die ("can only use 1, 2, 3 or 4 as argument to '-O'");
	}
      else if (argv[i][0] == '-')
	die ("unknown command line option '%s' (try '-h')", argv[i]);
      else if (output_name)
	die ("too many files");
      else if (input)
	output_name = argv[i];
      else if (!(input = fopen (argv[i], "r")))
	die ("can not read '%s'", argv[i]);
      else
	{
	  input_name = argv[i];
	  close_input = 1;
	}
    }

  if (ascii && output_name)
    die ("'-a' in combination with 'dst'");

  if (!ascii && !output_name && isatty (1))
    ascii = 1;

  if (!input)
    {
      input = stdin;
      input_name = "<stdin>";
    }

  parse ();
  if (close_input)
    fclose (input);

  handle_ltlspecs();

  analyze ();
  build ();

  print ();
  release ();

  return 0;
}
