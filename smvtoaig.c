#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

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

  AG = 256 ,
  ASSIGN = 257 ,
  BECOMES = 258 ,
  boolean = 259 ,
  CASE = 260 ,
  DEFINE = 261 ,
  ESAC = 262 ,
  IFF = 263 ,
  IMPLIES = 264 ,
  init = 265 ,
  INIT = 266 ,
  INVAR = 267 ,
  MODULE = 268 ,
  next = 269 ,
  SPEC = 270 ,
  SYMBOL = 271 ,
  TRANS = 272 ,
  VAR = 273 ,
};

typedef enum Tag Tag;

/*------------------------------------------------------------------------*/

enum Mode
{
  BMC_MODE = 0,			/* default */
  BMC_QBF_MODE,
  DIAMETER_MODE,
  DIAMETER_QBF_MODE,
  DIAMETER_SQR_QBF_MODE,
  SQR_QBF_MODE,
  REOCCURRENCE_MODE,
  REOCCURRENCE_QBF_MODE,
  FIXPOINT_MODE,
  INDUCTION_MODE,
  LINEARINDUCTION_MODE,
  LINEARINDUCTION_TRANS_MODE,
  LINEARINDUCTION_BIN_TRANS_MODE,
};

typedef enum Mode Mode;

/*------------------------------------------------------------------------*/

enum Format
{
  EMPTY_FORMAT = 0,		/* default */
  CNF_FORMAT,
  AIG_FORMAT,
};

typedef enum Format Format;

/*------------------------------------------------------------------------*/

typedef struct AIG AIG;
typedef struct Symbol Symbol;
typedef struct Expr Expr;
typedef struct Scope Scope;

/*------------------------------------------------------------------------*/

struct Symbol
{
  char * name;

  unsigned declared : 1;
  unsigned state : 1;		/* state, e.g. not a primary input */
  unsigned input : 1;		/* primary input, e.g. not a state */
  unsigned output : 1;		/* primary output, e.g. not a state */
  unsigned mark : 2;

  unsigned occurs_in_current_state : 1;
  unsigned occurs_in_next_state : 1;

  unsigned occurrence_in_current_state_checked : 1;
  unsigned occurrence_in_next_state_checked : 1;

  unsigned coi : 1;
  unsigned in_coi_from_init : 1;
  unsigned in_coi_from_spec : 1;

  Expr * init_expr;
  Expr * next_expr;
  Expr * def_expr;

  AIG * init_aig;
  AIG * next_aig;
  AIG * def_aig;

  AIG ** shifted_init_aigs;
  AIG ** shifted_def_aigs;
  AIG ** shifted_next_aigs;

  Symbol * chain;
  Symbol * order;
};

/*------------------------------------------------------------------------*/

struct Expr
{
  Tag tag;
  Symbol * symbol;
  Expr * c0;
  Expr * c1;
  AIG * aig;
  Expr * next;
};

/*------------------------------------------------------------------------*/

struct AIG
{
  Symbol * symbol;
  unsigned slice;
  unsigned level;
  AIG * c0;
  AIG * c1;
  int idx;		/* Tseitin index */
  AIG * next;		/* collision chain */
  AIG * cache;		/* cache for operations */
  unsigned id;		/* unique id for hashing/comparing purposes */
  unsigned universal : 1;
  unsigned innermost_existential : 1;
#ifndef NDEBUG
  unsigned mark : 1;
#endif
};

/*------------------------------------------------------------------------*/

struct Scope
{
  AIG **symbols;
  int sym_cnt;
};


/*------------------------------------------------------------------------*/

static const char * input_name;
static int close_input;
static FILE * input;
static Mode mode;
static unsigned fully_relational;
static unsigned arbitrary_scoping;
static unsigned coi_and_state;

static int lineno;
static int saved_char;
static int char_has_been_saved;
static Tag token;

static unsigned count_buffer;
static char * buffer;
static unsigned size_buffer;

static int next_allowed;
static int temporal_operators_allowed;

/*------------------------------------------------------------------------*/

static int close_output;
static FILE * output;
static Format format;
static int verbose;
static int header;

/*------------------------------------------------------------------------*/

static Symbol * first_symbol;
static Symbol * last_symbol;

static unsigned size_symbols;
static unsigned count_symbols;
static Symbol ** symbols;

/*------------------------------------------------------------------------*/

static Expr * first_expr;
static Expr * last_expr;
static unsigned count_exprs;

static unsigned size_expr_stack;
static unsigned count_expr_stack;
static Expr ** expr_stack;

static Expr * trans_expr;
static Expr * last_trans_expr;

static Expr * init_expr;
static Expr * last_init_expr;

static Expr * invar_expr;
static Expr * last_invar_expr;

static Expr * spec_expr;

static int count_states;
static int count_inputs;
static int count_outputs;
static int functional;

static int count_comparators;
static AIG *** accu;

/*------------------------------------------------------------------------*/

static unsigned size_aigs;
static unsigned count_aigs;
static AIG ** aigs;

static unsigned size_cached;
static unsigned count_cached;
static AIG ** cached;

static AIG * trans_aig;
static AIG * init_aig;
static AIG * bad_aig;
static AIG * good_aig;

static unsigned start0;
static unsigned start1;
static unsigned end0;
static unsigned end1;
static unsigned init0;
static unsigned init1;

/*------------------------------------------------------------------------*/

static int bound;
static int window = 2;
static int no_simple_state_constraints;
static int sorting = 0;

/*------------------------------------------------------------------------*/

static unsigned count_shifted;
static unsigned count_mapped;

/*------------------------------------------------------------------------*/

static unsigned primes[] = { 21433, 65537, 332623, 1322963, 200000123 };
static unsigned * eoprimes = primes + sizeof (primes)/sizeof(primes[0]);

/*------------------------------------------------------------------------*/

static Scope *scopes;
static int num_scopes;
static int max_scope;
static unsigned outermost_existential;

/*------------------------------------------------------------------------*/

static void
die (const char * msg, ...)
{
  va_list ap;
  fputs ("*** [smv2qbf] ", stderr);
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

/*------------------------------------------------------------------------*/

static void
perr (const char * msg, ...)
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
msg (int level, const char * msg, ...)
{
  va_list ap;

  if (level > verbose)
    return;

  fprintf (stderr, "[smv2qbf] ");
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

/*------------------------------------------------------------------------*/

static void
enlarge_scopes(void)
{
  int os, i;
  os = num_scopes;
  num_scopes = num_scopes ? 2 * num_scopes : 1;
  scopes = realloc(scopes, num_scopes * sizeof(Scope));

  for (i = os; i < num_scopes; i++)
    {
      (scopes + i)->symbols = 0;
      (scopes + i)->sym_cnt = 0;
    }
  
}

/*------------------------------------------------------------------------*/

static void
enlarge_vars(Scope *s)
{
  s->symbols = realloc(s->symbols, (s->sym_cnt + 5) * sizeof(Symbol *));
}

/*------------------------------------------------------------------------*/

static void
set_scope(AIG *sym, int scope)
{
  Scope *s;
  while (scope > num_scopes - 1)
    enlarge_scopes();

  if (scope > max_scope)
    max_scope =  scope;

  s = scopes + scope;

  if (!(s->sym_cnt % 5))
    enlarge_vars(s);

  s->symbols[s->sym_cnt++] = sym;
  
  return;
}

/*------------------------------------------------------------------------*/

extern AIG *
symbol_aig(Symbol *, unsigned);

extern AIG**
find_aig(Symbol *, unsigned, AIG *, AIG *);


/*------------------------------------------------------------------------*/

static void
set_scope_for_slice(int slice, int scope)
{
  Symbol * p;
  AIG *  aig;

  for (p = first_symbol; p; p = p->order)
    {
      if (!(aig = *find_aig(p, slice, 0, 0)))
	continue;

      /* generated symbols set elsewhere */
      if (!strncmp(p->name, "<symbol", 7))
	continue;

      set_scope(aig, scope);
    }
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
      ch = next_char();
      if (ch == '-')
	{
	  while ((ch = next_char () != '\n' && ch != EOF))
	    ;

	  goto SKIP_WHITE_SPACE;
	}
      else if (ch != '>')
	invalid_token ();

      return IMPLIES;
    }

  if (ch == '!' || ch == '|' || ch == '(' || ch == ')' ||
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

  if (isalpha (ch) || ch == '_')
    {
      while (isalnum (ch = next_char ()) || 
	     ch == '.' || ch == '_' || ch == '-')
	;
      save_char (ch);
      push_buffer (0);

      if (!strcmp (buffer, "AG") || !strcmp (buffer, "G"))
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
      if (!strcmp (buffer, "SPEC") || !strcmp(buffer, "LTLSPEC"))
	return SPEC;
      if (!strcmp (buffer, "TRANS"))
	return TRANS;
      if (!strcmp (buffer, "TRUE"))
	return ONE;
      if (!strcmp (buffer, "VAR"))
	return VAR;
      if (!strcmp (buffer, "FALSE"))
	return ZERO;

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

static void
next_token (void)
{
  token = get_next_token ();
#if 0
  /* Dump all tokens to 'stdout' to debug scanner and parser.
   */
  fprintf (stdout, "%s:%d: ", input_name, lineno);
  switch (token)
    {
      case AG: puts ("AG"); break;
      case ASSIGN: puts ("ASSIGN"); break;
      case BECOMES: puts (":="); break;
      case boolean: puts ("boolean"); break;
      case CASE: puts ("case"); break;
      case DEFINE: puts ("DEFINE"); break;
      case ESAC: puts ("esac"); break;
      case IFF: puts ("<->"); break;
      case IMPLIES: puts ("->"); break;
      case INIT: puts ("INIT"); break;
      case init: puts ("init"); break;
      case INVAR: puts ("INVAR"); break;
      case MODULE: puts ("MODULE"); break;
      case next: puts ("next"); break;
      case SPEC: puts ("SPEC"); break;
      case SYMBOL: puts (buffer); break;
      case TRANS: puts ("TRANS"); break;
      case VAR: puts ("VAR"); break;
      default: printf ("%c\n", token); break;
      case EOF: puts ("<EOF>"); break;
    }
#endif
}

/*------------------------------------------------------------------------*/

static unsigned
hash_str (const char * str)
{
  const unsigned * q;
  unsigned char ch;
  const char * p;
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
hash_symbol (const char * str)
{
  unsigned res = hash_str (str) & (size_symbols - 1);
  assert (res < size_symbols);
  return res;
}

/*------------------------------------------------------------------------*/

static void
enlarge_symbols (void)
{
  Symbol ** old_symbols, * p, * chain;
  unsigned old_size_symbols, h, i;

  old_size_symbols = size_symbols;
  old_symbols = symbols;

  size_symbols = size_symbols ? 2 * size_symbols : 1;
  NEWN(symbols, size_symbols);

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
find_symbol (void)
{
  Symbol ** res, * s;

  for (res = symbols + hash_symbol (buffer);
       (s = *res) && strcmp (s->name, buffer);
       res = &s->chain)
    ;

  return res;
}

/*------------------------------------------------------------------------*/

static Symbol *
new_symbol (void)
{
  Symbol ** p, * res;

  assert (count_buffer > 0);
  assert (!buffer[count_buffer - 1]);

  if (size_symbols <= count_symbols)
    enlarge_symbols ();

  p = find_symbol ();
  res = *p;

  if (!res)
    {
      NEW (res);
      res->name = strdup (buffer);
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
gensym (void)
{
  while (size_buffer < 30)
    enlarge_buffer ();
  sprintf (buffer, "<symbol%u>", count_symbols);
  count_buffer = strlen (buffer) + 1;
  return new_symbol ();
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
  Expr * res;

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
  Expr * res;

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
eat_symbolic_token (Tag expected, const char * str)
{
  if (token != expected)
    perr ("expected '%s'", str);

  next_token ();
}

/*------------------------------------------------------------------------*/

static Symbol *
eat_symbol (void)
{
  Symbol * res;

  if (token != SYMBOL)
    perr ("expected variable");

  res = new_symbol ();
  next_token ();

  return res;
}

/*------------------------------------------------------------------------*/

static void
parse_vars (void)
{
  Symbol * symbol;

  assert (token == VAR);

  next_token ();

  while (token == SYMBOL)
    {
      symbol = new_symbol ();
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

static Expr * parse_expr (void);

/*------------------------------------------------------------------------*/

static Expr *
parse_next (void)
{
  Expr * res;

  if (!next_allowed)
    perr ("'next' not allowed here");

  assert (token == next);
  next_token ();

  eat_token ('(');
  next_allowed = 0;
  res = parse_expr ();
  next_allowed = 1;
  eat_token (')');

  return new_expr (next, res, 0);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_case (void)
{
  Expr * res, * clause, * lhs, * rhs;
  int count = count_expr_stack;

  assert (token == CASE);
  next_token ();

  lhs = 0;

  while (token != ESAC)
    {
      if (token == EOF)
	perr ("'ESAC' missing");

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
  Expr * res = 0;

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
parse_not (void)
{
  int count = 0;
  Expr * res;

  while (token == '!')
    {
      next_token ();
      count = !count;
    }

  res = parse_basic ();
  if (count)
    res = new_expr ('!', res, 0);

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_ag (void)
{
  if (token == AG)
    {
      if (!temporal_operators_allowed)
	perr ("'AG' not allowed here");

      next_token ();
      return new_expr (AG, parse_not (), 0);
    }
  else 
    return parse_not ();
}

/*------------------------------------------------------------------------*/

static Expr *
parse_right_associative (Tag tag, Expr *(*f)(void))
{
  Expr * res = f ();

  while (token == tag)
    {
      next_token ();
      res = new_expr (tag, res, f ());
    }

  return res;
}

/*------------------------------------------------------------------------*/

static Expr *
parse_left_associative (Tag tag, Expr *(*f)(void))
{
  int count = count_expr_stack;
  Expr * res = f ();

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

/*------------------------------------------------------------------------*/

static Expr *
parse_and (void)
{
  return parse_left_associative ('&', parse_ag);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_or (void)
{
  return parse_left_associative ('|', parse_and);
}

/*------------------------------------------------------------------------*/

static Expr *
parse_iff (void)
{
  return parse_left_associative (IFF, parse_or);
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

  if (expr->tag == AG)
    return 1;

  if (contains_temporal_operator (expr->c0))
    return 1;

  return contains_temporal_operator (expr->c1);
}

/*------------------------------------------------------------------------*/

static void
parse_spec (void)
{
  assert (token == SPEC);

  if (spec_expr)
    perr ("multiple specifications");
  next_token ();

  temporal_operators_allowed = 1;
  spec_expr = parse_expr ();
  temporal_operators_allowed = 0;

  if (spec_expr->tag != AG || contains_temporal_operator (spec_expr->c0))
    perr ("can handle only simple 'AG' safety properties");
}

/*------------------------------------------------------------------------*/

static void
parse_assigns (void)
{
  Symbol * symbol;
  Expr * rhs;
  Tag tag;

  assert (token == ASSIGN);
  next_token ();

  while (token == init || token == next)
    {
      tag = token;
      next_token ();
      eat_token ('(');
      symbol = eat_symbol ();
      if (symbol->def_expr)
	perr ("can not assign already defined variable '%s'", symbol->name);
      if (tag == init && symbol->init_expr)
	perr ("multiple 'init' assignments for '%s'", symbol->name);
      if (tag == next && symbol->next_expr)
	perr ("multiple 'init' assignments for '%s'", symbol->name);
      eat_token (')');
      eat_symbolic_token (BECOMES, ":=");
      rhs = parse_expr ();
      eat_token (';');

      if (tag == init)
	symbol->init_expr = rhs;
      else 
	symbol->next_expr = rhs;
    }
}

/*------------------------------------------------------------------------*/

static void
parse_defines (void)
{
  Symbol * symbol;
  Expr * rhs;

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
      rhs = parse_expr ();
      eat_token (';');
      symbol->def_expr = rhs;
    }
}

/*------------------------------------------------------------------------*/

static void
parse_init (void)
{
  Expr * res, ** p;

  assert (token == INIT);
  next_token ();

  res = parse_expr ();

  p = last_init_expr ? &last_init_expr->c1 : &init_expr;
  assert ((*p)->tag == '1');
  last_init_expr = *p = new_expr ('&', res, *p);
}

/*------------------------------------------------------------------------*/

static void
parse_trans (void)
{
  Expr * res, ** p;

  assert (token == TRANS);
  next_token ();

  next_allowed = 1;
  res = parse_expr ();
  next_allowed = 0;

  p = last_trans_expr ? &last_trans_expr->c1 : &trans_expr;
  assert ((*p)->tag == '1');
  last_trans_expr = *p = new_expr ('&', res, *p);
}

/*------------------------------------------------------------------------*/

static void
parse_invar (void)
{
  Expr * res, ** p;

  assert (token == INVAR);
  next_token ();

  res = parse_expr ();

  p = last_invar_expr ? &last_invar_expr->c1 : &invar_expr;
  assert ((*p)->tag == '1');
  last_invar_expr = *p = new_expr ('&', res, *p);
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
  else
    perr ("expected EOF or section start");
}

/*------------------------------------------------------------------------*/

static void
parse_main (void)
{
  if (token != MODULE)
    perr ("expected 'MODULE'");

  next_token();

  if (token != SYMBOL || strcmp (buffer, "main"))
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

/*------------------------------------------------------------------------*/

static void
parse (void)
{
  lineno = 1;
  next_token ();
  init_expr = true_expr ();
  trans_expr = true_expr ();
  invar_expr = true_expr ();
  parse_main ();

  if (!spec_expr)
    perr ("specification missing");
}

/*------------------------------------------------------------------------*/

static void
check_all_variables_are_defined_or_declared (void)
{
  Symbol * p;

  for (p = first_symbol; p; p = p->order)
    {
#ifndef NDEBUG
      if (p->def_expr)
	{
	  assert (!p->declared);
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
  Symbol * p;

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
    perr ("cyclic definition for '%s'", s->name);

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
  Symbol * p;

  for (p = first_symbol; p; p = p->order)
    check_non_cyclic_symbol (p);

  unmark_symbols ();
}

/*------------------------------------------------------------------------*/

static void
add_to_coi (Expr * expr, int from_init, int from_spec)
{
  Symbol * s;

  if (!expr)
    return;

  if (expr->tag == SYMBOL)
    {
      s = expr->symbol;
      if (s->coi)
	return;

      s->coi = 1;

      if (from_init)
	s->in_coi_from_init = 1;

      if (from_spec)
	s->in_coi_from_spec = 1;

      if (s->def_expr)
	add_to_coi (s->def_expr, from_init, from_spec);

      if (s->init_expr)
	add_to_coi (s->init_expr, from_init, from_spec);

      if (s->next_expr)
	add_to_coi (s->next_expr, from_init, from_spec);
    }
  else
    {
      add_to_coi (expr->c0, from_init, from_spec);
      add_to_coi (expr->c1, from_init, from_spec);
    }
}

/*------------------------------------------------------------------------*/

static void
coi (void)
{
  int count_vars, count_vars_in_coi;
  Symbol * p;

  add_to_coi (invar_expr, 0, 0);
  add_to_coi (trans_expr, 0, 0);

  add_to_coi (init_expr, 1, 0);
  add_to_coi (spec_expr, 0, 1);
  
  count_vars = count_vars_in_coi = 0;
  for (p = first_symbol; p; p = p->order)
    {
      if (p->def_expr)
	continue;
#if 0
      p->coi = 1;		/* force 100% coi */
#endif
      count_vars++;
      if (p->coi)
	count_vars_in_coi++;
    }

  msg (1, "%d variables out of %d in COI %.0f%%",
       count_vars_in_coi, count_vars,
       count_vars ? 100.0 * count_vars_in_coi / (double) count_vars : 0);
}

/*------------------------------------------------------------------------*/

static void
current_or_next_state_occurrence (Expr * expr, int next_state)
{
  Symbol * symbol;

  if (!expr)
    return;

  if (expr->tag == SYMBOL)
    {
      symbol = expr->symbol;
      if (next_state)
	{
	  if (!symbol->occurrence_in_next_state_checked)
	    {
	      if (!symbol->occurs_in_next_state)
		symbol->occurs_in_next_state = 1;

	      symbol->occurrence_in_next_state_checked = 1;

	      if (symbol->next_expr)
		current_or_next_state_occurrence (symbol->next_expr, 0);
	      else if (symbol->def_expr)
		current_or_next_state_occurrence (symbol->def_expr, 1);
	    }
	}
      else
	{
	  if (!symbol->occurrence_in_current_state_checked)
	    {
	      if (!symbol->occurs_in_current_state)
		symbol->occurs_in_current_state = 1;

	      symbol->occurrence_in_current_state_checked = 1;

	      if (symbol->def_expr)
		current_or_next_state_occurrence (symbol->def_expr, 0);
	    }
	}
    }
  else if (expr->tag == next)
    {
      assert (!next_state);
      current_or_next_state_occurrence (expr->c0, 1);
    }
  else
    {
      current_or_next_state_occurrence (expr->c0, next_state);
      current_or_next_state_occurrence (expr->c1, next_state);
    }
}

/*------------------------------------------------------------------------*/

static void
add_everything (void)
{
  Symbol *p;

  for (p = first_symbol; p; p = p->order)
    {
      p->coi = 1;
      p->state = 1;
      count_states++;
    }
  return;
}

/*------------------------------------------------------------------------*/

static void
find_states_and_inputs (void)
{
  Symbol * p;

  current_or_next_state_occurrence (invar_expr, 0);
  current_or_next_state_occurrence (invar_expr, 1);
  current_or_next_state_occurrence (trans_expr, 0);

  for (p = first_symbol; p; p = p->order)
    {
      if (!p->coi)
	continue;

      if (p->def_expr)       /* will be handled implicitly */
	continue;

      if (p->next_expr)
	{
	  if (!p->occurs_in_next_state)
	    p->occurs_in_next_state = 1;

	  current_or_next_state_occurrence (p->next_expr, 0);
	}
    }

  for (p = first_symbol; p; p = p->order)
    {
      if (p->def_expr) 
	continue;

      if (!p->coi)
	{
	  msg (2, "%s not in coi", p->name);
	  continue;
	}

      if (p->occurs_in_current_state && p->occurs_in_next_state)
	{
	  p->state = 1;
	  count_states++;
	}
      else if (p->occurs_in_current_state)
	{
	  count_inputs++;
	  p->input = 1;
	}
      else if (p->occurs_in_next_state)
	{
	  count_outputs++;
	  p->output = 1;
	}
      else if (p->in_coi_from_spec)
	{
	  count_outputs++;
	  p->output = 1;
	}
      else
	{
	  assert (p->in_coi_from_init);
	  count_inputs++;
	  p->input = 1;
	}

      if (verbose >= 2)
	{
	  fprintf (stderr, "[smv2qbf] %s", p->name);
	  if (p->state)
	    fputs (" state", stderr);
	  if (p->input)
	    fputs (" input", stderr);
	  if (p->output)
	    fputs (" output", stderr);
	  fputc ('\n', stderr);
	}
    }
}

/*------------------------------------------------------------------------*/

static void
check_functional (void)
{
  if (init_expr->tag == '1' &&
      invar_expr->tag == '1' &&
      trans_expr->tag == '1')
    {
      functional = 1;
    }
  else
    functional = 0;

  if (verbose)
    fprintf (stderr,
	     "[smv2qbf] %s model\n",
	     functional ? "functional" : "relational");
}

/*------------------------------------------------------------------------*/

static void
analyze (void)
{
  check_all_variables_are_defined_or_declared ();
  check_non_cyclic_definitions ();
  check_functional ();
  coi ();
  if (coi_and_state)
    add_everything ();
  else
    find_states_and_inputs ();
}

/*------------------------------------------------------------------------*/

static unsigned
hash_aig (Symbol * symbol, unsigned slice, AIG * c0, AIG * c1)
{
  const unsigned * q;
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
  AIG ** old_aigs, *p, * next;

  old_aigs = aigs;
  old_size_aigs = size_aigs;

  size_aigs = size_aigs ? 2 * size_aigs : 1;
  NEWN(aigs, size_aigs);

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
  AIG ** p, * a;
 
  for (p = aigs + hash_aig (s, l, c0, c1);
       (a = *p) && !eq_aig (a, s, l, c0, c1);
       p = &a->next)
    ;

  return p;
}

/*------------------------------------------------------------------------*/

static AIG *
not_aig (AIG * aig)
{
  long aig_as_long, res_as_long;
  AIG * res;

  aig_as_long = (long) aig;
  res_as_long = -aig_as_long;
  res = (AIG *) res_as_long;

  assert (sign_aig (aig) * sign_aig (res) == -1);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
simplify_aig_one_level (AIG * a, AIG * b)
{
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

#define strip_aig(sign,aig) \
  do { \
    (sign) = sign_aig (aig); \
    if ((sign) < 0) \
      (aig) = not_aig (aig); \
  } while (0)

/*------------------------------------------------------------------------*/

static AIG *
simplify_aig_two_level (AIG * a, AIG * b)
{
  AIG * a0, * a1, * b0, * b1;
  int s, t;

  if (window < 2)
    return 0;

  strip_aig (s, a);
  strip_aig (t, b);

  a0 = (a->symbol) ? a : a->c0;
  a1 = (a->symbol) ? a : a->c1;
  b0 = (b->symbol) ? b : b->c0;
  b1 = (b->symbol) ? b : b->c1;

  if (s > 0 && t > 0)
    {
      /* Idempotence.
       */
      if (a0 == b)
	return a;
      if (a1 == b)
	return a;
      if (b0 == a)
	return b;
      if (b1 == a)
	return b;

      /* Contradiction.
       */
      if (a0 == not_aig (b))
	return FALSE;
      if (a1 == not_aig (b))
	return FALSE;
      if (b0 == not_aig (a))
	return FALSE;
      if (b1 == not_aig (a))
	return FALSE;
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
      /* (!a0 | !a1) & (b0 & b1) */

      /* Simple subsumption.
       */
      if (a0 == not_aig (b))
	return b;
      if (a1 == not_aig (b))
	return b;

      /* More complex subsumption.
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
      /* a0 & a1 & (!b0 | !b1) */

      /* Simple subsumption.
       */
      if (b0 == not_aig (a))
	return a;
      if (b1 == not_aig (a))
	return a;

      /* More complex subsumption.
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

      /* (!a0 | !a1) & (!b0 | !b1) */

      /* Resolution.
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
simplify_aig (AIG * a, AIG * b)
{
  AIG * res;

  if ((res = simplify_aig_one_level (a, b)))
    return res;

  return simplify_aig_two_level (a, b);
}

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

static unsigned
aig2level (AIG * aig)
{
  aig = stripped_aig (aig);

  if (aig == TRUE)
    return 0;

  return aig->level;
}

/*------------------------------------------------------------------------*/

static unsigned
aig2slice (AIG * aig)
{
  aig = stripped_aig (aig);

  if (aig == TRUE)
    return 0;

  return aig->slice;
}

/*------------------------------------------------------------------------*/

static unsigned
max_unsigned (unsigned a, unsigned b)
{
  return a > b ? a : b;
}

/*------------------------------------------------------------------------*/

static AIG *
new_aig (Symbol * symbol, unsigned slice, AIG * c0, AIG * c1)
{
  int try_to_simplify_again;
  AIG ** p, * res;

  assert (!symbol == (c0 && c1));
  assert (!slice || symbol);

  if (count_aigs >= size_aigs)
    enlarge_aigs ();

  if (!symbol)
    {
TRY_TO_SIMPLIFY_AGAIN:
      if ((res = simplify_aig (c0, c1)))
	return res;

      if (window >= 2)
	{
	  if (sign_aig (c0) > 0 && !c0->symbol &&
	      sign_aig (c1) > 0 && !c1->symbol)
	    {
	      /* (a & b) & (a & c) == b & (a & c)
	       */
	      try_to_simplify_again = 1;

	      if (c0->c0 == c1->c0)
		{
		  c0 = c0->c1;
		}
	      else if (c0->c0 == c1->c1)
		{
		  c0 = c0->c1;
		}
	      else if (c0->c1 == c1->c0)
		{
		  c0 = c0->c0;
		}
	      else if (c0->c1 == c1->c1)
		{
		  c0 = c0->c0;
		}
	      else
		try_to_simplify_again = 0;

	      if (try_to_simplify_again)
		goto TRY_TO_SIMPLIFY_AGAIN;
	    }
	}

      if (stripped_aig (c0)->id > stripped_aig (c1)->id)
	swap_aig (c0, c1);
    }

  p = find_aig (symbol, slice, c0, c1);
  res = *p;
  if (!res)
    {
      NEW (res);

      assert (sign_aig (res) > 0);

      if (symbol)
	{
	  res->symbol = symbol;
	  res->slice = slice;
	}
      else
	{
	  assert (!res->symbol);
	  res->slice = max_unsigned (aig2slice (c0), aig2slice (c1));
	  res->level = max_unsigned (aig2level (c0), aig2level (c1)) + 1;
	}

      res->c0 = c0;
      res->c1 = c1;
      res->id = count_aigs++;
      *p = res;
    }

  return res;
}

/*------------------------------------------------------------------------*/

AIG *
symbol_aig (Symbol * symbol, unsigned slice)
{
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

/*------------------------------------------------------------------------*/

static void
cache (AIG * aig)
{
  assert (sign_aig (aig) > 0);

  if (count_cached >= size_cached)
    {
      size_cached = size_cached ? 2 * size_cached : 1;
      cached = realloc (cached, size_cached * sizeof (cached[0]));
    }

  cached[count_cached++] = aig;
}

/*------------------------------------------------------------------------*/

static AIG *
shift_aig_aux (AIG * aig, unsigned delta)
{
  AIG * res, * l, * r;
  int sign;

  strip_aig (sign, aig);

  if (!aig->cache)
    {
      count_shifted++;
      if (!aig->symbol)
	{
	  l = shift_aig_aux (aig->c0, delta);
	  r = shift_aig_aux (aig->c1, delta);
	  res = and_aig (l, r);
	}
      else
	res = symbol_aig (aig->symbol, aig->slice + delta);

      aig->cache = res;
      cache (aig);
    }
  else
    res = aig->cache;

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static void
reset_cache (void)
{
  unsigned i;
  AIG * aig;

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
shift_aig (AIG * aig, unsigned delta)
{
  AIG * res;

  if (delta == 0 || aig == FALSE || aig == TRUE)
    return aig;

  res = shift_aig_aux (aig, delta);
  reset_cache ();

  return res;
}

/*------------------------------------------------------------------------*/

static int
cmp_indices_slices_first (const void * p, const void * q)
{
  AIG * a = *(AIG**)p;
  AIG * b = *(AIG**)q;
  int res;

  if ((res = a->slice - b->slice))
    return res;

  if ((res = a->level - b->level))
    return res;

  return a->id - b->id;
}

/*------------------------------------------------------------------------*/

static int
cmp_indices_inputs_first (const void * p, const void * q)
{
  AIG * a = *(AIG**)p;
  AIG * b = *(AIG**)q;

  if (a->symbol)
    {
      if (!b->symbol)
	return -1;
    }
  else if (b->symbol)
    return 1;

  return a->id - b->id;
}

/*------------------------------------------------------------------------*/

static void
sort_cached_indices (int (*cmp_indices)(const void*,const void*))
{
  unsigned i;
  AIG * aig;

  qsort (cached, count_cached, sizeof (cached[0]), cmp_indices);

  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx);
      aig->idx = i + 1;
    }
}

/*------------------------------------------------------------------------*/

static void
reset_indices (void)
{
  unsigned i;
  AIG * aig;

  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig);
      assert (aig->idx);
      aig->idx = 0;
    }

  count_cached = 0;
}

/*------------------------------------------------------------------------*/

static void
index_aig (AIG * aig)
{
  if (!aig)
    return;

  aig = stripped_aig (aig);
  if (aig == TRUE)
    return;

  assert (aig != FALSE);

  if (aig->idx)
    return;

  index_aig (aig->c0);
  index_aig (aig->c1);

  cache (aig);
  assert (count_cached);
  aig->idx = count_cached;
}

/*------------------------------------------------------------------------*/

static int
aig2idx (AIG * aig)
{
  int sign, res;

  strip_aig (sign, aig);
  res = aig->idx;
  res *= sign;
  assert (res);

  return res;
}

/*------------------------------------------------------------------------*/

static void
print_idx_to_variable_mapping (FILE * file)
{
  Symbol * symbol;
  unsigned i;
  AIG * aig;

  if (!header)
    return;

  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      symbol = aig->symbol;
      if (!symbol)
	continue;

      fprintf (file, "c %d = %s'%d\n", i + 1, symbol->name, aig->slice);
    }
}

/*------------------------------------------------------------------------*/

static void
print_non_constant_aig (AIG * root, FILE * file, int qbf)
{
  int i, ands, max;
  AIG * aig;

  assert (!qbf);		/* can not handle qbf yet */
  assert (root);
  assert (!count_cached);

  index_aig (root);
  sort_cached_indices (cmp_indices_inputs_first);

  print_idx_to_variable_mapping (file);

  ands = 0;
  max = 0;
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];

      if (aig->idx > max)
	max = aig->idx;

      if (!aig->symbol)
	ands++;
    }

  fprintf (file, "p aig %d %d %d\n", max, ands, aig2idx (root));

  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx == i + 1);
      if (aig->symbol)
	continue;

      fprintf (file,
	       "%d %d %d 0\n",
	       aig->idx, aig2idx (aig->c0), aig2idx (aig->c1));
    }
  reset_indices ();
}

/*------------------------------------------------------------------------*/

static void
print_aig (AIG * root, FILE * file, int qbf)
{
  if (root == TRUE)
    fputs ("p aig 2 1 -2\n2 -1 1 0\n", file);
  else if (root == FALSE)
    fputs ("p aig 2 1 2\n2 -1 1 0\n", file);
  else
    print_non_constant_aig (root, file, qbf);
}

/*------------------------------------------------------------------------*/
#ifndef NDEBUG
/*------------------------------------------------------------------------*/

void
paig (AIG * root)
{
  print_aig (root, output, 0);
}

/*------------------------------------------------------------------------*/
#endif
/*------------------------------------------------------------------------*/

static void
check_positive_symbol_aig (AIG * aig)
{
  assert (aig);
  assert (sign_aig (aig) > 0);
  assert (aig->symbol);
}

/*------------------------------------------------------------------------*/

static void
set_universal_symbol_aig (AIG * aig)
{
  check_positive_symbol_aig (aig);
  aig->universal = 1;
}

/*------------------------------------------------------------------------*/

static int
is_universal_symbol_aig (AIG * aig)
{
  check_positive_symbol_aig (aig);
  return aig->universal;
}

/*------------------------------------------------------------------------*/

static void
set_innermost_existential_symbol_aig (AIG * aig)
{
  check_positive_symbol_aig (aig);
  aig->innermost_existential = 1;
}

/*------------------------------------------------------------------------*/

static int
is_innermost_existential_symbol_aig (AIG * aig)
{
  check_positive_symbol_aig (aig);
  return aig->innermost_existential;
}

/*------------------------------------------------------------------------*/

static void
print_non_constant_aig_as_cnf (AIG * root, FILE * file, int qbf)
{
  unsigned i, count_clauses, universal_variables;
  AIG * aig;

  assert (root);
  assert (root != TRUE);
  assert (root != FALSE);
  assert (!count_cached);

  index_aig (root);
  sort_cached_indices (cmp_indices_slices_first);
  print_idx_to_variable_mapping (file);

  count_clauses = 1;
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx == i + 1);
      if (aig->symbol)
	continue;

      count_clauses += 3;
    }

  fprintf (file, "p cnf %d %d\n", count_cached, count_clauses);

  if (qbf)
    {
      universal_variables = 0;

      for (i = 0; i < count_cached; i++)
	{
	  aig = cached[i];
	  if (!aig->symbol)
	    continue;

	  if (!is_universal_symbol_aig (aig))
	    continue;

	  msg (2, "%s'%u universal", aig->symbol->name, aig->slice);
	  universal_variables++;
	}

      msg (1, "found %u universal variables", universal_variables);

      if (universal_variables)	/* otherwise propositional */
	{
	  fputs ("e ", file);
	  for (i = 0; i < count_cached; i++)
	    {
	      aig = cached[i];
	      if (!aig->symbol)
		continue;

	      if (is_innermost_existential_symbol_aig (aig))
		continue;

	      if (is_universal_symbol_aig (aig))
		continue;

	      fprintf (file, "%d ", aig->idx);
	    }
	  fputs ("0\n", file);

	  fputs ("a ", file);
	  for (i = 0; i < count_cached; i++)
	    {
	      aig = cached[i];
	      if (!aig->symbol)
		continue;

	      if (!is_universal_symbol_aig (aig))
		continue;

	      fprintf (file, "%d ", aig->idx);
	    }
	  fputs ("0\n", file);

	  fputs ("e ", file);
	  for (i = 0; i < count_cached; i++)
	    {
	      aig = cached[i];
	      if (aig->symbol && !is_innermost_existential_symbol_aig (aig))
		continue;

	      fprintf (file, "%d ", aig->idx);
	    }
	  fputs ("0\n", file);
	}
    }

  fprintf (file, "%d 0\n", aig2idx (root));
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx == i + 1);
      if (aig->symbol)
	continue;

      fprintf (file,
	       "%d %d 0\n"
	       "%d %d 0\n"
	       "%d %d %d 0\n",
	       -aig->idx, aig2idx (aig->c0),
	       -aig->idx, aig2idx (aig->c1),
	       aig->idx, -aig2idx (aig->c0), -aig2idx (aig->c1));
    }
  reset_indices ();
}

/*------------------------------------------------------------------------*/
static inline int
is_existential(int scope)
{
  if ((scope % 2) & outermost_existential)
    return 0;
  else if(!(scope % 2) & !outermost_existential)
    return 0;
  return 1;
}

/*------------------------------------------------------------------------*/

static void
print_non_constant_aig_as_cnf_arb (AIG * root, FILE * file, int qbf)
{
  unsigned i, j, count_clauses;
  AIG * aig;

  assert (root);
  assert (root != TRUE);
  assert (root != FALSE);
  assert (!count_cached);

  index_aig (root);
  sort_cached_indices (cmp_indices_slices_first);
  print_idx_to_variable_mapping (file);

  count_clauses = 1;
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx == i + 1);
      if (aig->symbol)
	continue;

      count_clauses += 3;
    }

  fprintf (file, "p cnf %d %d\n", count_cached, count_clauses);

  if (qbf)
    {
      Scope *s = scopes;
      for (i = 0; i < max_scope; i++)
	{
	  s = scopes + i;
	  if (!s->sym_cnt)
	    continue;
	  assert(s->sym_cnt);

	  if (is_existential(i))
	    fputs ("e ", file);
	  else
	    fputs ("a ", file);

	  for (j = 0; j < s->sym_cnt; j++)
	    if (s->symbols[j]->idx)
	      fprintf(file, "%d ", s->symbols[j]->idx);

	  fprintf(file, "0\n");
	}

      if (!is_existential(max_scope))
	{
	  s = scopes + max_scope;
	  fputs("a ", file);
	  for (j = 0; j < s->sym_cnt; j++)
	    fprintf(file, "%d ", s->symbols[j]->idx);

	  fprintf(file, "0\n");
	}

      fputs ("e ", file);
      for (i = 0; i < count_cached; i++)
	{
	  aig = cached[i];
	  if (aig->symbol && !is_innermost_existential_symbol_aig (aig))
	    continue;
	  
	  fprintf (file, "%d ", aig->idx);
	}
      fputs ("0\n", file);
    }


  fprintf (file, "%d 0\n", aig2idx (root));
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (aig->idx == i + 1);
      if (aig->symbol)
	continue;

      fprintf (file,
	       "%d %d 0\n"
	       "%d %d 0\n"
	       "%d %d %d 0\n",
	       -aig->idx, aig2idx (aig->c0),
	       -aig->idx, aig2idx (aig->c1),
	       aig->idx, -aig2idx (aig->c0), -aig2idx (aig->c1));
    }
  reset_indices ();
}

/*------------------------------------------------------------------------*/

static void
print_aig_as_cnf (AIG * aig, FILE * file, int qbf)
{
  if (aig == TRUE)
    fputs ("p cnf 0 0\n", file);
  else if (aig == FALSE)
    fputs ("p cnf 1 2\n1 0\n-1 0\n", file);
  else if (!arbitrary_scoping)
    print_non_constant_aig_as_cnf (aig, file, qbf);
  else
    print_non_constant_aig_as_cnf_arb (aig, file, qbf);
}

/*------------------------------------------------------------------------*/

static AIG * build_expr (Expr *, unsigned);

/*------------------------------------------------------------------------*/

static AIG *
build_cases (Expr * expr, unsigned slice)
{
  AIG * c, * t, * e;
  Expr * clause;

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
build_expr (Expr * expr, unsigned slice)
{
  Tag tag = expr->tag;
  AIG * l, * r;

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

  if (tag == IMPLIES)
    return implies_aig (l, r);

  assert (tag == IFF);

  return iff_aig (l, r);
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
  Symbol * p;

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

static void
build (void)
{
  AIG * invar_aig, * next_invar_aig;

  invar_aig = build_expr (invar_expr, 0);

  init_aig = build_expr (init_expr, 0);
  init_aig = and_aig (init_aig, invar_aig);

  trans_aig = build_expr (trans_expr, 0);
  trans_aig = and_aig (invar_aig, trans_aig);
  next_invar_aig = next_aig (invar_aig);
  trans_aig = and_aig (trans_aig, next_invar_aig);

  assert (spec_expr->tag == AG);
  good_aig = build_expr (spec_expr->c0, 0);
  bad_aig = not_aig (good_aig);

  build_assignments ();
}

/*------------------------------------------------------------------------*/

static int
is_valid_position (unsigned i)
{
  return (start0 <= i && i <= end0) || (start1 <= i && i <= end1);
}

/*------------------------------------------------------------------------*/

static void 
indent(int i)
{
  while (i--)
    {
      printf(" ");
    }
}

/*------------------------------------------------------------------------*/

void
pretty_print_aig(AIG * aig)
{
  static int i = 0;
  if (sign_aig(aig) < 0)
    {
      indent(i);
      printf("neg\n");
    }
  aig = stripped_aig(aig);
  if (!aig) return;

  if (aig == TRUE)
    {
      indent(i);
      printf("TRUE\n");
      return;
    }
  
  if (aig == FALSE)
    {
      indent(i);
      printf("FALSE\n");
      return;
    }
  
  if (aig->symbol)
    {
      indent(i);
      printf("symbol %s slice %d\n", aig->symbol->name, aig->slice);
      if (aig->symbol->def_aig) 
	{
	  indent(i);
	  printf("defined by\n");
	  i += 2;
	  pretty_print_aig(aig->symbol->def_aig);
	  i -= 2;
	}
    }

  
  indent(i);
  printf("c0: ");
  if (!aig->c0)
    printf("NULL");
  printf("\n");
  i+=2;
  pretty_print_aig(aig->c0);  
  i-=2;
  indent(i);
  printf("c1:");
  if (!aig->c1)
    printf("NULL");
  printf("\n");
  i+=2;
  pretty_print_aig(aig->c1);
  i-=2;
}

/*------------------------------------------------------------------------*/

static AIG *
map_aig (AIG * aig)
{
  AIG * res, * tmp, * l, * r;
  Symbol * symbol;
  int sign;

  if (aig == TRUE || aig == FALSE)
    return aig;

  strip_aig (sign, aig);
  res = aig->cache;
  if (!res)
    {
#ifndef NDEBUG
      assert (!aig->mark);		/* check for cyclic calls */
      aig->mark = 1;
#endif
      count_mapped++;
      symbol = aig->symbol;
      if (symbol)
	{
	  res = aig;
	  if (symbol->def_expr)
	    {
	      assert (symbol->def_aig);
	      tmp = symbol->shifted_def_aigs[aig->slice];
	      assert (tmp);
	      res = map_aig (tmp);
	    }
	  else if (is_valid_position (aig->slice))
	    {
	      if (aig->slice == init0 || aig->slice == init1)
		{
		  if (symbol->init_expr)
		    {
		      tmp = symbol->shifted_init_aigs[aig->slice];
		      assert (tmp);
		      res = map_aig (tmp);
		    }
		}
	      else if (aig->slice != start0 && aig->slice != start1)
		{
		  if (symbol->next_expr)
		    {
		      assert (symbol->next_aig);
		      tmp = symbol->shifted_next_aigs[aig->slice - 1];
		      assert (tmp);
		      res = map_aig (tmp);
		    }
		}
	    }
	}
      else
	{
	  l = map_aig (aig->c0);
	  r = map_aig (aig->c1);
	  res = and_aig (l, r);
	}
#ifndef NDEBUG
      aig->mark = 0;			/* reset check for cyclic calls */
#endif
      aig->cache = res;
      cache (aig);
    }

  if (sign < 0)
    res = not_aig (res);

  return res;
}

/*------------------------------------------------------------------------*/

static void
unroll_assignments_at_step (unsigned i)
{
  AIG * lhs, * rhs, * res;
  Symbol * p;

  assert (is_valid_position (i));

  for (p = first_symbol; p; p = p->order)
    {
      lhs = symbol_aig (p, i);
      if (lhs->cache)
	continue;

      res = lhs;
      if (p->def_expr)
	{
	  assert (p->def_aig);
	  rhs = p->shifted_def_aigs[i];
	  assert (rhs);
	  res = map_aig (rhs);
	}
      else if (i == init0 || i == init1)
	{
	  if (p->init_expr)
	    {
	      rhs = p->shifted_init_aigs[i];
	      assert (rhs);
	      res = map_aig (rhs);
	    }
	}
      else if (i != start0 && i != start1)
	{
	  if (p->next_expr)
	    {
	      assert (p->next_aig);
	      rhs = p->shifted_next_aigs [i - 1];
	      assert (rhs);
	      res = map_aig (rhs);
	    }
	}

      lhs->cache = res;
      cache (lhs);
    }
}

/*------------------------------------------------------------------------*/

static void
unroll_assignments (void)
{
  unsigned i, min_start, max_end;
  Symbol * p;

  max_end = end0 > end1 ? end0 : end1;
  min_start = start0 < start1 ? start0 : start1;

  for (p = first_symbol; p; p = p->order)
    {
      if (p->def_expr)
	{
	  assert (p->def_aig);
	  NEWN (p->shifted_def_aigs, max_end + 1);

	  for (i = min_start; i <= max_end; i++)
	    p->shifted_def_aigs[i] = shift_aig (p->def_aig, i);
	}

      if (p->init_expr)
	{
	  assert (p->init_aig);
	  assert (start0 <= start1);
	  NEWN (p->shifted_init_aigs, start1 + 1);

	  p->shifted_init_aigs[start0] = shift_aig (p->init_aig, start0);
	  p->shifted_init_aigs[start1] = shift_aig (p->init_aig, start1);
	}

      if (p->next_expr)
	{
	  assert (p->next_aig);
	  NEWN (p->shifted_next_aigs, max_end + 1);

	  for (i = min_start; i < max_end; i++)
	    p->shifted_next_aigs[i] = shift_aig (p->next_aig, i);
	}
    }

  for (i = min_start; i <= max_end; i++)
    unroll_assignments_at_step (i);
}

/*------------------------------------------------------------------------*/

static AIG *
assign (AIG * aig)
{
  AIG * res;

  assert (0 <= start0);
  assert (start0 <= end0);

  assert (0 <= start1);

  assert (init0 == UINT_MAX || init0 == start0);
  assert (init1 == UINT_MAX || init1 == start1);

  unroll_assignments ();
  res = map_aig (aig);
  reset_cache ();

  return res;
}

extern AIG * make_relational ();
extern AIG * eq_states_trans(unsigned, unsigned);
extern AIG * eq_states(unsigned, unsigned);
extern void set_innermost_existential_states(unsigned);
extern void mr();

/*------------------------------------------------------------------------*/

static AIG *
bmc_lin (void)
{
  Symbol *oracle_symbol;
  AIG * path, *choose, *chosen, *oracle, *checker;
  unsigned i;

  mr();
  path = shift_aig(init_aig, 2);
  path = and_aig(path, trans_aig);
  // path = and_aig(path, make_relational());

  oracle_symbol = gensym ();
  chosen = FALSE;
  for (i = 2; i < bound + 2; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      set_universal_symbol_aig (oracle);
      choose = and_aig (not_aig (chosen), oracle);

      path = and_aig(path, 
		     implies_aig(choose, 
				 and_aig(eq_states_trans(0, i), 
					 eq_states_trans(1, i + 1))));

      chosen = or_aig (chosen, choose);
    }

  checker = FALSE;
  for (i = 2; i <= bound + 2; i++)
    checker = or_aig(checker, shift_aig(bad_aig, i));


  path = and_aig(path, checker);
  set_innermost_existential_states(0);
  set_innermost_existential_states(1);
  return path;
}

/*------------------------------------------------------------------------*/

static AIG *
bmc (void)
{
  AIG * path, * checker, * res;
  unsigned i;

  path = init_aig;
  for (i = 0; i < bound; i++)
    path = and_aig (path, shift_aig (trans_aig, i));

  checker = FALSE;
  for (i = 0; i <= bound; i++)
    checker = or_aig (checker, shift_aig (bad_aig, i));

  res = and_aig (path, checker);

  start0 = start1 = 0;
  end0 = end1 = bound;
  init0 = init1 = 0;

  if (!fully_relational)
    res = assign (res);

  return res;
}

/*------------------------------------------------------------------------*/

AIG *
eq_states_trans (unsigned i, unsigned j)
{
  AIG * res, * a, * b, * eq;
  Symbol * p;

  assert (i != j);

  res = TRUE;
  for (p = first_symbol; p; p = p->order)
    {
      if (!p->coi)
	continue;

      if (!p->occurs_in_next_state)
	continue;
      a = symbol_aig (p, i);
      b = symbol_aig (p, j);
      eq = iff_aig (a, b);
      res = and_aig (res, eq);
    }

  return res;
}

/*------------------------------------------------------------------------*/

AIG *
eq_states (unsigned i, unsigned j)
{
  AIG * res, * a, * b, * eq;
  Symbol * p;

  assert (i != j);

  res = TRUE;
  for (p = first_symbol; p; p = p->order)
    {
      if (!p->coi)
	continue;

      if (!p->state)
	continue;

      a = symbol_aig (p, i);
      b = symbol_aig (p, j);
      eq = iff_aig (a, b);
      res = and_aig (res, eq);
    }

  return res;
}

/*------------------------------------------------------------------------*/

static void
set_universal_states (unsigned i)
{
  Symbol * p;
  AIG * s;

  for (p = first_symbol; p; p = p->order)
    {
      s = symbol_aig (p, i);
      set_universal_symbol_aig (s);
      assert (!is_innermost_existential_symbol_aig (s));
    }
}

/*------------------------------------------------------------------------*/

void
set_innermost_existential_states (unsigned i)
{
  Symbol * p;
  AIG *  s;

  for (p = first_symbol; p; p = p->order)
    {
      s = symbol_aig (p, i);
      set_innermost_existential_symbol_aig (s);
      assert (!is_universal_symbol_aig (s));
    }
}

/*------------------------------------------------------------------------*/

static AIG *
accugtrec (int i, int j, int k)
{
  AIG * a, * b, * c, * lt, * gt, * res;

  if (k < 0)
    return FALSE;

  a = accu[i][k];
  b = accu[j][k];
  c = accugtrec (i, j, k - 1);

  gt = and_aig (a, not_aig (b));
  lt = and_aig (not_aig (a), b);
  res = or_aig (gt, and_aig (not_aig (lt), c));

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
accugt (int i, int j)
{
  return accugtrec (i, j, count_states - 1);
}

/*------------------------------------------------------------------------*/

static void
oddevencmpswap (int i, int j, int last)
{
  AIG * cond, * a, * b;
  int k;

  if (j > last)
    return;

  count_comparators++;

  cond = accugt (i, j);
  if (cond == FALSE)
    return;

  for (k = 0; k < count_states; k++)
    {
      a = accu[i][k];
      b = accu[j][k];
      accu[i][k] = ite_aig (cond, b, a);
      accu[j][k] = ite_aig (cond, a, b);
    }

  if (verbose >= 3)
    fprintf (stderr, "[smv2qbf] oddevencmpswap %d %d\n", i, j);
}

/*------------------------------------------------------------------------*/

static void
oddevenmerge (int l, int r, int k, int last)
{
  int c =  r - l;

  if (c <= k)
    return;

  c = (c + k - 1) / k;

  if (c > 2)
    {
      int i;
      oddevenmerge (l, r - k, 2 * k, last);
      oddevenmerge (l + k, r, 2 * k, last);

      for (i = 1; i <= c - 3; i += 2)
	oddevencmpswap (l + i * k, l + (i + 1) * k, last);
    }
  else
    oddevencmpswap (l, l + k, last);

  if (verbose >= 3)
    fprintf (stderr, "[smv2qbf] oddevenmerge %d %d %d\n", l, r, k);
}

/*------------------------------------------------------------------------*/

static void
oddevensort (int l, int r, int last)
{
  int k = r - l;

  if (k == 1)
    return;

  k /= 2;

  oddevensort (l, l + k, last);
  oddevensort (l + k, r, last);
  oddevenmerge (l, r, 1, last);

  if (verbose >= 3)
    fprintf (stderr, "[smv2qbf] oddevensort %d %d\n", l, r);
}

/*------------------------------------------------------------------------*/

static AIG *
simple_states_constraints (int skip_bound)
{
  int i, j, b;
  Symbol * p;
  AIG * res;

  if (bound == 0 || no_simple_state_constraints)
    return TRUE;

  res = TRUE;

  if (sorting)
    {
      NEWN (accu, bound + 1);		/* no skip_bound here => delete */

      for (i = 0; i <= bound - skip_bound; i++)
	{
	  NEWN (accu[i], count_states);
	  j = 0;
	  for (p = first_symbol; p; p = p->order)
	    {
	      if (!p->state)
		continue;

	      assert (j < count_states);
	      accu[i][j++] = symbol_aig (p, i);
	    }
	  assert (j == count_states);
	}

      for (b = 1; b <= bound - skip_bound; b *= 2)
	;

      oddevensort (0, b, bound - skip_bound);

      res = TRUE;
      for (i = 0; i < bound - skip_bound; i++)
	res = and_aig (res, accugt (i + 1, i));

      if (verbose)
	fprintf (stderr, "[smv2qbf] %d comparators\n", count_comparators);
    }
  else
    {
      for (i = 0; i <= bound - 1 - skip_bound; i++)
	for (j = i + 1; j <= bound - skip_bound; j++)
	  res = and_aig (res, not_aig (eq_states (i, j)));
    }

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
induction (void)
{
  AIG * path, * checker, * simple, * res;
  unsigned i;

  path = TRUE;			/* no initial state constraints */
  for (i = 0; i < bound; i++)
    path = and_aig (path, shift_aig (trans_aig, i));

  simple = simple_states_constraints (1);
  res = and_aig (simple, path);

  checker = TRUE;
  for (i = 0; i < bound; i++)
    checker = and_aig (checker, shift_aig (good_aig, i));

  checker = and_aig (checker, shift_aig (bad_aig, bound));

  res = and_aig (res, checker);

  start0 = start1 = 0;
  end0 = end1 = bound;
  init0 = init1 = UINT_MAX;

  if (!fully_relational)
    res = assign (res);

  return res;
}

/*------------------------------------------------------------------------*/

void
mr (void)
{
  Symbol *p;
  AIG *temp;

  for (p = first_symbol; p; p = p->order)
    {
      if (!p->coi)
	continue;

      if (p->init_expr)
	{
	  assert(p->init_aig);
	  temp = symbol_aig(p, 0);
	  init_aig = and_aig(init_aig, 
			     iff_aig(temp, p->init_aig));
	}

      if (p->def_expr)
	{

	  assert(p->def_aig);
	  temp = symbol_aig(p, 0);
	  init_aig = and_aig(init_aig,
			     iff_aig(temp, p->def_aig));
	  trans_aig = and_aig(trans_aig,
			      iff_aig(temp, p->def_aig));
	  good_aig = and_aig(good_aig,
			     iff_aig(temp, p->def_aig));
	  bad_aig = and_aig(bad_aig,
			    iff_aig(temp, p->def_aig));
	}

      if (p->next_expr)
	{
	  assert(p->next_aig);
	  temp = symbol_aig(p, 1);
	  trans_aig = and_aig(trans_aig, iff_aig(temp, p->next_aig));
	}
    }
}

/*------------------------------------------------------------------------*/

/* idea is to substitute all defines with the actual definition
 * to transition relation instead of handling them in map_aig()
 * traverses all symbols and creates symbol aig for slice 0.
 *
 * -- tjussila --
 */
AIG *
make_relational (void)
{
  Symbol *p;
  AIG *aig, *temp;
  int i;

  aig = TRUE;
  for (p = first_symbol; p; p = p->order)
    {
      if (p->init_expr && mode == BMC_QBF_MODE)
	{
	  assert(p->init_aig);
	  temp = symbol_aig(p, 2);
	  aig = and_aig(aig, iff_aig(temp, shift_aig(p->init_aig, 2)));
	}
      else if (p->init_expr && mode == REOCCURRENCE_QBF_MODE)
	{
	  assert(p->init_aig);
	  temp = symbol_aig(p, 3);
	  aig = and_aig(aig, iff_aig(temp, shift_aig(p->init_aig, 3)));
	}
      else if (p->init_expr && mode == DIAMETER_QBF_MODE)
	{
	  assert(p->init_aig);
	  temp = symbol_aig(p, 4);
	  aig = and_aig(aig, iff_aig(temp, shift_aig(p->init_aig, 4)));
	  temp = symbol_aig(p, bound + 5);
	  aig = and_aig(aig, iff_aig(temp, shift_aig(p->init_aig, bound + 5)));
	}

      if (p->def_expr)
	{
	  assert(p->def_aig);
	  if (p->coi && mode == BMC_QBF_MODE)
	    {
	      for (i = 2; i <= bound + 2; i++)
		{
		  temp = symbol_aig(p, i);
		  aig = and_aig(aig, iff_aig(temp, shift_aig(p->def_aig, i)));
		}
	    }
	  
	  if (p->coi)
	    {
	      /* defines for slices zero and one */
	      temp = symbol_aig(p, 0);
	      aig = and_aig(aig, iff_aig(temp, p->def_aig));
	      
	      /* needed for bad_aig among others (shifted to slice 1) */
	      temp = symbol_aig(p, 1);
	      aig = and_aig(aig, iff_aig(temp, shift_aig(p->def_aig, 1)));

	      if (mode == DIAMETER_QBF_MODE)
		{
		  temp = symbol_aig(p, 2);
		  aig = and_aig(aig, iff_aig(temp, shift_aig(p->def_aig, 2)));
		}
	    }
	}
      else if (p->next_expr)
	{
	  assert(p->next_aig);
	  if (p->coi)
	    {
	      temp = symbol_aig(p, 1);
	      aig = and_aig(aig, iff_aig(temp, p->next_aig));

	      if (mode == DIAMETER_QBF_MODE)
		{
		  temp = symbol_aig(p, 3);
		  aig = and_aig(aig, iff_aig(temp, shift_aig(p->next_aig, 2)));
		}
	    }
	}
    }
  
  return aig;
}

/*------------------------------------------------------------------------*/

static AIG *
linearinduction_bin_trans (void)
{
  AIG * oracle, *trans, * simple, *res, *temp;
  Symbol * oracle_symbol;
  unsigned i, j = 0, k = 0;

  if (bound)
    while((bound - 1) >> j) j++;

  NEWN(oracle_symbol, j);
  NEWN(oracle, j);
  for (i = 0; i < j; i++)
    {
      oracle_symbol[i] = *gensym();
      oracle[i] = *symbol_aig(oracle_symbol + i, 3);
      set_universal_symbol_aig(oracle + i);
    }

  /* no initial state constraints */
  // trans = and_aig(trans_aig, make_relational());
  mr();
  trans = and_aig(trans_aig, good_aig);

  simple = TRUE;

  for (i = 3; i < bound + 3; i++)
    {
      temp = TRUE;
      for (k = 0; k < j; k++)
	{
	  if ((i - 3) & (1 << k))
	    temp = and_aig(temp, oracle + k);
	  else 
	    temp = and_aig(temp, not_aig(oracle + k));
	}

#if 0
      trans = and_aig(trans, iff_aig(temp, eq_states_trans(0,i)));
      trans = and_aig(trans, implies_aig(temp, eq_states_trans(1,i + 1)));
#else
      trans = and_aig(trans, 
      		      implies_aig(temp, 
      				  and_aig(eq_states_trans(0, i), 
					  eq_states_trans(1, i + 1))));
#endif
      
      if (i == bound + 2)
	trans = and_aig(trans,
			implies_aig(temp,
				    shift_aig(bad_aig, 1)));

#if 1
      simple = and_aig (simple, iff_aig (temp, eq_states (2, i)));
#endif
    }

  res = and_aig(trans, simple);
      
  set_innermost_existential_states (0);
  set_innermost_existential_states (1);
  set_innermost_existential_states (2);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
linearinduction_trans (void)
{
  AIG * oracle, *trans, * simple, *choose, *chosen, *res;
  Symbol * oracle_symbol;
  unsigned i;

  mr();
  /* no initial state constraints */
  // trans = and_aig(trans_aig, make_relational());
  trans = and_aig(trans_aig, good_aig);

  oracle_symbol = gensym ();
  chosen = FALSE;
  simple = TRUE;

  for (i = 3; i < bound + 3; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      set_universal_symbol_aig (oracle);
      choose = and_aig (not_aig (chosen), oracle);

#if 0
      trans = and_aig(trans, iff_aig(choose, eq_states_trans(0,i)));
      trans = and_aig(trans, implies_aig(choose, eq_states_trans(1,i + 1)));
#else
      trans = and_aig(trans, 
		      implies_aig(choose, 
				  and_aig(eq_states_trans(0, i), 
					  eq_states_trans(1, i + 1))));
#endif
      

      if (i == bound + 2)
	trans = and_aig(trans,
			implies_aig(choose,
				    shift_aig(bad_aig, 1)));

#if 1
      simple = and_aig (simple, iff_aig (choose, eq_states (2, i)));
#endif
      chosen = or_aig (chosen, choose);
    }

  res = and_aig(trans, simple);
      
  set_innermost_existential_states (0);
  set_innermost_existential_states (1);
  set_innermost_existential_states (2);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
linearinduction (void)
{
  AIG * path, * checker, * simple, * res, * chosen, * choose, * oracle;
  Symbol * oracle_symbol;
  unsigned i;

  path = TRUE;			/* no initial state constraints */
  for (i = 1; i <= bound; i++)
    path = and_aig (path, shift_aig (trans_aig, i));

  simple = TRUE;
  if (bound > 0 && !no_simple_state_constraints)
    {
#if 1
      oracle_symbol = gensym ();
      chosen = FALSE;

      for (i = 1; i <= bound; i++)
	{
	  oracle = symbol_aig (oracle_symbol, i);
	  set_universal_symbol_aig (oracle);
	  choose = and_aig (not_aig (chosen), oracle);
	  simple = and_aig (simple, iff_aig (choose, eq_states (0, i)));
	  chosen = or_aig (chosen, choose);
	}
#else
      /* This should give the same results as 'induction' after index
       * transformation.
       */
      int j;
      for (i = 1; i < bound; i++)
	for (j = i + 1; j <= bound; j++)
	  simple = and_aig (simple, not_aig (eq_states (i, j)));
#endif
    }

  res = and_aig (simple, path);

  checker = TRUE;
  for (i = 1; i <= bound; i++)
    checker = and_aig (checker, shift_aig (good_aig, i));

  checker = and_aig (checker, shift_aig (bad_aig, bound + 1));

  res = and_aig (res, checker);

  start0 = start1 = 1;
  end0 = end1 = bound + 1;
  init0 = init1 = UINT_MAX;

  if (!fully_relational)
    res = assign (res);

  set_innermost_existential_states (0);

  return res;
}

/*------------------------------------------------------------------------*/
#ifndef NDEBUG
/*------------------------------------------------------------------------*/

void
pretty_print_scopes (void)
{
  int i, j;
  Scope *s;
  for (i = 0; i < num_scopes; i++)
    {
      s = scopes + i;
      printf("scope %d contains:\n", i);
      for(j = 0; j < s->sym_cnt; j++)
	  printf("\tvar %s, slice %d\n", s->symbols[j]->symbol->name,
		 s->symbols[j]->slice);
    }
  return;
}


/*------------------------------------------------------------------------*/
#endif
/*------------------------------------------------------------------------*/

static AIG *
reoccurrence_lin (void)
{
  AIG * res, * choose, * chosen, * oracle;
  Symbol * oracle_symbol;
  unsigned i;

  num_scopes = 0;
  max_scope = 0;
  outermost_existential = 1;

  res = shift_aig(init_aig, 3);
  res = and_aig(res, trans_aig);
  res = and_aig(res, make_relational());

  oracle_symbol = gensym ();
  chosen = FALSE;

  for (i = 3; i <= bound + 3; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      set_universal_symbol_aig (oracle);
      set_scope(oracle, 1);
      choose = and_aig (not_aig (chosen), oracle);

      if (i < bound + 3)
	res = and_aig(res, 
		      implies_aig(choose, 
				  and_aig(eq_states_trans(0, i), 
					  eq_states_trans(1, i + 1))));
      

      res = and_aig(res, iff_aig(choose, eq_states(2, i)));

      chosen = or_aig(chosen, choose);
      set_scope_for_slice(i, 0);
    }

  set_scope_for_slice(0, 2);
  set_scope_for_slice(1, 2);
  set_scope_for_slice(2, 2);
  set_innermost_existential_states(0);
  set_innermost_existential_states(1);
  set_innermost_existential_states(2);
  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
reoccurrence (void)
{
  AIG * res, * simple;
  unsigned i;

  res = init_aig;
  for (i = 0; i < bound; i++)
    res = and_aig (res, shift_aig (trans_aig, i));

  simple = simple_states_constraints (0);
  res = and_aig (res, simple);

  start0 = start1 = init0 = init1 = 0;
  end0 = end1 = bound;
  res = assign (res);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG *
diameter_lin (void)
{
  AIG * res, * choose, * chosen, * oracle, * path, * path2, * loop;
  Symbol * oracle_symbol;
  unsigned i;

  num_scopes = 0;
  max_scope = 0;
  mr ();
  // outermost_existential = 1;

  if (!bound)
    return init_aig;

  oracle_symbol = gensym ();

  chosen = FALSE;
  path = and_aig(trans_aig, shift_aig(init_aig, 4));

  for (i = 4; i < bound + 4; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      choose = and_aig (not_aig (chosen), oracle);
      set_scope(oracle, 1);

      path = and_aig(path, 
		     implies_aig(choose, 
				 and_aig(eq_states_trans(0, i), 
					 eq_states_trans(1, i + 1))));
      

      chosen = or_aig(chosen, choose);
      set_scope_for_slice(i, 0);
    }
  set_scope_for_slice(bound + 4, 0);

  oracle_symbol = gensym();

  chosen = FALSE;
  /* trans_aig has self loops for initial states */
  path2 = and_aig(shift_aig(trans_aig, 2), shift_aig(init_aig, bound + 5));
  for (i = bound + 5; i < 2*bound + 4; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      set_scope (oracle, 4);
      choose = and_aig (not_aig (chosen), oracle);


      path2 = and_aig(path2, 
		      implies_aig(choose, 
				  and_aig(eq_states_trans(2, i), 
					  eq_states_trans(3, i + 1))));
      

      chosen = or_aig(chosen, choose);
      set_scope_for_slice(i, 3);
    }
  set_scope_for_slice (2 * bound + 4, 3);

  loop = FALSE;
  for (i = bound + 5; i <= 2*bound + 3; i++)
    loop = or_aig(loop, eq_states(i, bound + 4));
  // loop = and_aig(init_aig, shift_aig(init_aig, 1));

  res = implies_aig (path, and_aig (path2, loop));
  set_scope_for_slice(0, 2);
  set_scope_for_slice(1, 2);
  set_scope_for_slice(2, 5);
  set_innermost_existential_states(2);
  set_scope_for_slice(3, 5);
  set_innermost_existential_states(3);
  return res;
}

/*------------------------------------------------------------------------*/
/* !forall s2,s3,s4,i, exists s0, s1 (i = 0 -> I(s2) & T(s2, s3)
 *                                    i = 1 -> T(s3, s4)
 *                                    ...
 *                                    i = j -> T(s(j + 2), s(j + 3))
 *                                    & T(s0, s1))
 *  ->
 *  exists t2, t3, t4 forall j exists t0, t1
 *                             (i = 0 -> I(t2) & T(t2, t3)
 *                              i = 1 -> T(t3, t4)
 *                              i = j - 1 -> T(t(j + 1), t(j + 2))
 *                              & T(t0, t1)) &
 *                             (s(j + 3) = t2 | s(j + 3) = t3 | s(j + 3) = t4)
 */
static AIG *
diameter_lin_ORIGINAL (void)
{
  AIG * res, * choose, * chosen, * oracle, * path, * path2, * loop;
  Symbol * oracle_symbol;
  unsigned i;

  num_scopes = 0;
  max_scope = 0;
  mr ();
  // outermost_existential = 1;

  if (!bound)
    return init_aig;

  oracle_symbol = gensym ();

  chosen = FALSE;
  path = and_aig(trans_aig, shift_aig(init_aig, 4));

  for (i = 4; i < bound + 4; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      choose = and_aig (not_aig (chosen), oracle);
      set_scope(oracle, 1);

      path = and_aig(path, 
		     implies_aig(choose, 
				 and_aig(eq_states_trans(0, i), 
					 eq_states_trans(1, i + 1))));
      

      chosen = or_aig(chosen, choose);
      set_scope_for_slice(i, 0);
    }
  set_scope_for_slice(bound + 4, 0);

  chosen = FALSE;
  path2 = and_aig(shift_aig(trans_aig, 2), shift_aig(init_aig, bound + 5));
  for (i = bound + 5; i < 2*bound + 4; i++)
    {
      oracle = symbol_aig (oracle_symbol, i);
      set_scope (oracle, 4);
      choose = and_aig (not_aig (chosen), oracle);


      path2 = and_aig(path2, 
		      implies_aig(choose, 
				  and_aig(eq_states_trans(2, i), 
					  eq_states_trans(3, i + 1))));
      

      chosen = or_aig(chosen, choose);
      set_scope_for_slice(i, 3);
    }
  set_scope_for_slice (2 * bound + 4, 3);

  loop = FALSE;
  for (i = bound + 5; i <= 2*bound + 3; i++)
    loop = or_aig(loop, eq_states(i, bound + 4));
  // loop = and_aig(init_aig, shift_aig(init_aig, 1));

  res = implies_aig (path, and_aig (path2, loop));
  set_scope_for_slice(0, 2);
  set_scope_for_slice(1, 2);
  set_scope_for_slice(2, 5);
  set_innermost_existential_states(2);
  set_scope_for_slice(3, 5);
  set_innermost_existential_states(3);
  return res;
}

/*------------------------------------------------------------------------*/
/* !forall s0,x0,s1,x1,s2,x2,s3.
 *   I(s0) & T(s0,x0,s1) & T(s1,x2,s2) & T(s2,x2,s3)
 *   ->
 *   exists t0,y0,t1,y1,t2.
 *     I(t0) & T(t0,y0,t1) & T(t1,y1,t2) &
 *     (s3 = t0 | s3 = t1 | s3 = t2)
 */
static AIG *
diameter (void)
{
  AIG * path0, * path1, *loop, * res;
  unsigned i;

  if (!bound)
    return init_aig;

  init0 = 0;
  end0 = init0 + bound;
  path0 = shift_aig (init_aig, init0);
  for (i = init0; i < end0; i++)
    path0 = and_aig (path0, shift_aig (trans_aig, i));

  init1 = end0 + 1;
  end1 = init1 + bound - 1;
  path1 = shift_aig (init_aig, init1);
  for (i = init1; i < end1; i++)
    path1 = and_aig (path1, shift_aig (trans_aig, i));

  loop = FALSE;
  for (i = init1; i <= end1; i++)
    loop = or_aig (loop, eq_states (end0, i));

  res = not_aig (implies_aig (path0, and_aig (path1, loop)));

  start0 = init0;
  start1 = init1;
  res = assign (res);

  for (i = init1; i <= end1; i++)
    set_universal_states (i);

  return res;
}

/*------------------------------------------------------------------------*/

static AIG*
iterat_sq_diam (void)
{
  Symbol * oracle_symbol;
  AIG * trans[2 * bound + 1], * oracle, * t1, * t2, * res;
  unsigned i, ext_scope = 0, start, end;

  mr ();
  /* transition relation shifted 3 slices, innermost scope */
  trans[0] = shift_aig(trans_aig, 3);

  for (i = 1; i <= bound; i++)
    {
      /* ext_scope such that deepest (i = bound) gets scope 0 */
      ext_scope = (bound - i) * 2;

      oracle_symbol = gensym ();
      oracle = symbol_aig(oracle_symbol, 1);
      set_scope (oracle, ext_scope + 1);

      if (i != bound)
	start = 3 * (i + 1);
      else
	start = 0;

      t1 = and_aig (eq_states_trans (3*i, start), 
		    eq_states_trans (3*i + 1, 3*i + 2));
      t1 = implies_aig (oracle, t1);

      t2 = and_aig (eq_states_trans (3*i, 3*i + 2), 
		    eq_states_trans (3*i + 1, start + 1));
      t2 = implies_aig (not_aig (oracle), t2);
      
      trans[i] = and_aig (and_aig (t1, t2), trans[i - 1]);
      
      // printf("setting scope of %d to %d\n", 3*i, ext_scope);
      set_scope_for_slice (3*i, ext_scope + 2);
      set_scope_for_slice (3*i + 1, ext_scope + 2);
      set_scope_for_slice (3*i + 2, ext_scope);
    }

  /* starting point for trans[bound + 1] first slice for trans[bound + 2] */
  start = 3 * (bound + 2);
  end = start + 1;

  /* trans[bound + 1] contains T(s0, s1) shifted to next free slice */
  trans[bound + 1] = or_aig(shift_aig(trans_aig, start), 
			    eq_states(start, end));

  for (i = bound + 2; i <= 2 * bound + 1; i++)
    {
      /* ext_scope such that deepest (i = 2 * bound + 1) gets scope 1 */
      ext_scope = ((2 * bound + 1) - i) * 2 + 1;

      oracle_symbol = gensym ();
      oracle = symbol_aig(oracle_symbol, 2);
      set_scope (oracle, ext_scope + 1);

      if (i != 2 * bound + 1)
	{
	  start = 3 * (i + 1);
	  end = start + 1;
	}
      else
	{
	  start = 0;
	  end = start + 2;
	}

      t1 = and_aig (eq_states_trans (3 * i, start), 
		    eq_states_trans (3 * i + 1, 3 * i + 2));
      t1 = implies_aig (oracle, t1);

      t2 = and_aig (eq_states_trans (3 * i, 3 * i + 2), 
		    eq_states_trans (3 * i + 1, end));
      t2 = implies_aig (not_aig (oracle), t2);
      
      trans[i] = and_aig (and_aig (t1, t2), trans[i - 1]);
      
      // printf("setting scope of %d to %d\n", 3 * i - 3, ext_scope);
      set_scope_for_slice (3 * i, ext_scope + 2);
      set_scope_for_slice (3 * i + 1, ext_scope + 2);
      set_scope_for_slice (3 * i + 2, ext_scope);
    }

  res = and_aig(trans[bound], shift_aig (trans_aig, 1));
  res = implies_aig(res, trans[2 * bound + 1]);

  set_scope_for_slice(0, 0);
  set_scope_for_slice(1, 0);
  set_scope_for_slice(2, 0);

  /* innermost from left side of implication */
  // set_innermost_existential_states(3);
  // set_innermost_existential_states(4);

  /* innermost from right side of implication */
  set_innermost_existential_states(3 * (bound + 2));
  set_innermost_existential_states(3 * (bound + 2) + 1);

  return res;
}

/*------------------------------------------------------------------------*/
/*
 * \exists s0, s1 I(s0) & T^2i (s0,s1) &  B(s1)
 */

static AIG*
iterat_sq (void)
{
  Symbol * oracle_symbol;
  AIG * trans[bound + 1], * oracle, * t1, * t2, * res;
  unsigned i;

  outermost_existential = 1;
  mr ();
  trans[0] = or_aig(shift_aig(trans_aig, 3), eq_states(3, 4));

  res = TRUE;
  for (i = 1; i <= bound; i++)
    {
      unsigned start;
      unsigned sb = (bound - i) * 2;

      oracle_symbol = gensym ();
      oracle = symbol_aig(oracle_symbol, 2);
      set_scope (oracle, sb + 1);

      if (i != bound)
	start = 3 * (i + 1);
      else
	start = 0;


      t1 = and_aig (eq_states_trans (3*i, start), 
		    eq_states_trans (3*i + 1, 3*i + 2));
      t1 = implies_aig (oracle, t1);

      t2 = and_aig (eq_states_trans (3*i, 3*i + 2), 
		    eq_states_trans (3*i + 1, start + 1));
      t2 = implies_aig (not_aig (oracle), t2);
      
      trans[i] = and_aig (and_aig (t1, t2), trans[i - 1]);
      
      set_scope_for_slice (3*i, sb + 2);
      set_scope_for_slice (3*i + 1, sb + 2);
      set_scope_for_slice (3*i + 2, sb);
    }
  res = and_aig (init_aig, trans[bound]);
  res = and_aig (res, shift_aig (bad_aig, 1));
  set_scope_for_slice(0, 0);
  set_scope_for_slice(1, 0);
  set_innermost_existential_states(3);
  set_innermost_existential_states(4);
  return res;
}

/*------------------------------------------------------------------------*/
/* !forall s0,x0,s1,x1,s2,x2,s3.
 *   !B(s0) & T(s0,x0,s1) & 
 *   !B(s1) & T(s1,x2,s2) & 
 *   !B(s2) & T(s2,x2,s3) &
 *   B(s3)
 *   ->
 *   exists t0,y0,t1,y1,t2.
 *     !B(t0) & T(t0,y0,t1) &
 *     !B(t1) & T(t1,y1,t2) &
 *     B(t2) 
 *     &
 *     (s0 = t0 | s0 = t1)
 */
static AIG *
fixpoint (void)
{
  AIG * path0, * path1, * loop, * res, * good_trans;
  unsigned i;

  if (!bound)
    return bad_aig;

  good_trans = and_aig (good_aig, trans_aig);

  start0 = 0;
  end0 = start0 + bound;
  path0 = TRUE;
  for (i = start0; i < end0; i++)
    path0 = and_aig (path0, shift_aig (good_trans, i));

  path0 = and_aig (path0, shift_aig (bad_aig, end0));

  start1 = end0 + 1;
  end1 = start1 + bound - 1;
  path1 = TRUE;
  for (i = start1; i < end1; i++)
    path1 = and_aig (path1, shift_aig (good_trans, i));

  path1 = and_aig (path1, shift_aig (bad_aig, end1));

  loop = FALSE;
  for (i = start1; i < end1; i++)
    loop = or_aig (loop, eq_states (start0, i));

  res = not_aig (implies_aig (path0, and_aig (path1, loop)));

  init0 = init1 = UINT_MAX;
  res = assign (res);

  for (i = start1; i <= end1; i++)
    set_universal_states (i);

  return res;
}

/*------------------------------------------------------------------------*/

static void
release_symbols (void)
{
  Symbol * p, * order;

  for (p = first_symbol; p; p = order)
    {
      order = p->order;
      free (p->shifted_def_aigs);
      free (p->shifted_init_aigs);
      free (p->shifted_next_aigs);
      free (p->name);
      free (p);
    }

  free (symbols);
}

/*------------------------------------------------------------------------*/

static void
release_exprs (void)
{
  Expr * p, * next;

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
  AIG * p, * next;

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

static char
mode2char (Mode m)
{
  switch (m)
    {
      case BMC_MODE:
	return 'b';
      case REOCCURRENCE_MODE:
	return 'r';
      case INDUCTION_MODE:
	return 'i';
      case LINEARINDUCTION_MODE:
	return 'l';
      case DIAMETER_MODE:
	return 'd';
      default:
	assert (mode == FIXPOINT_MODE);
	return 'f';
    }
}

/*------------------------------------------------------------------------*/

static const char *
format2str (Format fmt)
{
  if (fmt == EMPTY_FORMAT)
    return "";

  if (fmt == CNF_FORMAT)
    return "--cnf";

  assert (fmt == AIG_FORMAT);

  return "--aig";
}

/*------------------------------------------------------------------------*/

static void
log_options (void)
{
  if (!verbose)
    return;

  fprintf (stderr,
           "[smv2qbf] smv2qbf -%c %s %u %s\n",
	   mode2char (mode), format2str (format), bound, input_name);

  fprintf (stderr,
           "[smv2qbf] "
	   "$Id: smvtoaig.c,v 1.1 2006-08-30 15:04:14 biere Exp $"
	   "\n");
}

/*------------------------------------------------------------------------*/

static void
release_accu (void)
{
  int i;

  if (!accu)
    return;

  for (i = 0; i <= bound; i++)
    free (accu[i]);

  free (accu);
}

/*------------------------------------------------------------------------*/

static void
release (void)
{
  msg (1, "%u = %u + %u + %u variables (states + inputs + outputs)",
	  count_states + count_inputs + count_outputs,
	  count_states, count_inputs, count_outputs);
  msg (2, "%u symbols, %u expressions", count_symbols, count_exprs);
  msg (2, "%u aigs, %u shifted, %u mapped",
       count_aigs, count_shifted, count_mapped);

  release_symbols ();
  release_exprs ();
  release_accu ();
  release_aigs ();

  free (expr_stack);
  free (buffer);
}

/*------------------------------------------------------------------------*/

#define USAGE \
"usage: smv2qnf [<option> ...]\n" \
"\n" \
"where <option> is one of the following:\n" \
"\n" \
"  -v[<inc>]   increase verbose level by <inc> (default 1)\n" \
"  -h          print command line option summary\n" \
"  -b          standard BMC mode (default, SAT)\n" \
"  -C          BMC mode QBF\n" \
"  -S          BMC with iterative squaring (QBF)\n" \
"  -d          diameter checking mode (QBF)\n" \
"  -D          diameter checking mode, trans.relation shared (QBF)\n" \
"  -SD         diameter checking mode, iter.squaring (QBF)\n" \
"  -f          fixpoint checking mode (QBF)\n" \
"  -r          reoccurrence diameter checking mode (SAT)\n" \
"  -rs         reoccurrence diameter checking mode sorting states (SAT)\n" \
"  -R          reoccurrence diameter checking mode (QBF)\n" \
"  -i          k-induction checking mode (SAT)\n" \
"  -is         k-induction checking mode sorting states (SAT)\n" \
"  -l          linear k-induction checking mode (QBF)\n" \
"  -L          linear k-induction checking mode trans relation shared (QBF)\n" \
"  -B          linear k-induction checking mode binary forall (QBF)\n" \
"  --no-simple no simple state constraints with k-induction\n" \
"  -w<window>  specify AIG simplification window (<window>=1,2 default 2)\n" \
"  -o <output> set output file (default <stdout>)\n" \
"  --header    include back annotation in header as comments\n" \
"  --cnf       print result in DIMACS format (default)\n" \
"  --aig       print result in AIG format\n" \
"  --relational consider model fully relational\n" \
"  --all-state every variable counts\n" \
"  <bound>     set counter example length (<bound>=0,1,2,... default 0)\n" \
"  <input>     flat SMV input file (default <stdin>)\n"

/*------------------------------------------------------------------------*/

static int
parse_non_negative_number (const char * str)
{
  const char * p;
  char ch;
  int res;

  res = 0;
  for (p = str; (ch = *p); p++)
    {
      if (!isdigit (ch))
	return -1;

      res = 10 * res + (ch - '0');
    }

  return res;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char ** argv)
{
  int i, tmp, qbf;
  AIG * res;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stdout);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-v"))
	verbose++;
      else if (argv[i][0] == '-' && argv[i][1] == 'v')
	verbose += atoi (argv[i] + 2);
      else if (!strcmp (argv[i], "-b"))
	mode = BMC_MODE;
      else if (!strcmp (argv[i], "-C"))
	mode = BMC_QBF_MODE;
      else if (!strcmp (argv[i], "-d"))
	mode = DIAMETER_MODE;
      else if (!strcmp (argv[i], "-D"))
	mode = DIAMETER_QBF_MODE;
      else if (!strcmp (argv[i], "-SD"))
	mode = DIAMETER_SQR_QBF_MODE;
      else if (!strcmp (argv[i], "-S"))
	mode = SQR_QBF_MODE;
      else if (!strcmp (argv[i], "-r"))
	mode = REOCCURRENCE_MODE;
      else if (!strcmp (argv[i], "-rs"))
	{
	  mode = REOCCURRENCE_MODE;
	  sorting = 1;
	}
      else if (!strcmp (argv[i], "-R"))
	mode = REOCCURRENCE_QBF_MODE;
      else if (!strcmp (argv[i], "-i"))
	mode = INDUCTION_MODE;
      else if (!strcmp (argv[i], "-is"))
	{
	  mode = INDUCTION_MODE;
	  sorting = 1;
	}
      else if (!strcmp (argv[i], "-l"))
	mode = LINEARINDUCTION_MODE;
      else if (!strcmp (argv[i], "-L"))
	mode = LINEARINDUCTION_TRANS_MODE;
      else if (!strcmp (argv[i], "-B"))
	mode = LINEARINDUCTION_BIN_TRANS_MODE;
      else if (!strcmp (argv[i], "-f"))
	mode = FIXPOINT_MODE;
      else if (!strcmp (argv[i], "--no-simple"))
	no_simple_state_constraints = 1;
      else if (!strcmp (argv[i], "--sort"))
	sorting = 1;
      else if (!strcmp (argv[i], "--relational"))
	fully_relational = 1;
      else if (!strcmp (argv[i], "--all-state"))
	coi_and_state = 1;
      else if (!strcmp (argv[i], "-o"))
	{
	  if (output)
	    die ("multiple output files");
	  else if (!(output = fopen (argv[i], "w")))
	    die ("can not write to '%s'", argv[i]);
	  else
	    close_output = 1;
	}
      else if (!strcmp (argv[i], "--header"))
	header = 1;
      else if (!strcmp (argv[i], "--cnf"))
	format = CNF_FORMAT;
      else if (!strcmp (argv[i], "--aig"))
	format = AIG_FORMAT;
      else if ((tmp = parse_non_negative_number (argv[i])) >= 0)
	{
	  bound = tmp;
	}
      else if (argv[i][0] == '-' && argv[i][1] == 'w')
	{
	  window = atoi (argv[i] + 2);
	  if (window != 1 && window != 2)
	    die ("can only use 1 or 2 as argument to '-w'");
	}
      else if (argv[i][0] == '-')
	die ("unknown command line option '%s' (try '-h')", argv[i]);
      else if (input)
	die ("multiple input files");
      else if (!(input = fopen (argv[i], "r")))
	die ("can not read '%s'", argv[i]);
      else
	{
	  input_name = argv[i];
	  close_input = 1;
	}
    }

  if (!input)
    {
      input = stdin;
      input_name = "<stdin>";
    }

  if (!output)
    output = stdout;

  if (verbose)
    log_options ();

  parse ();
  analyze ();
  build ();

  if (fully_relational)
    mr();

  qbf = 0;
  if (mode == DIAMETER_MODE)
    {
      res = diameter ();
      qbf = 1;
    }
  else if (mode == DIAMETER_QBF_MODE)
    {
      arbitrary_scoping = 1;
      res = diameter_lin ();
      qbf = 1;
    }
  else if (mode == DIAMETER_SQR_QBF_MODE)
    {
      arbitrary_scoping = 1;
      res = iterat_sq_diam ();
      qbf = 1;
    }
  else if (mode == SQR_QBF_MODE)
    {
      arbitrary_scoping = 1;
      res = iterat_sq ();
      qbf = 1;
    }
  else if (mode == REOCCURRENCE_MODE)
    res = reoccurrence ();
  else if (mode == REOCCURRENCE_QBF_MODE)
    {
      res = reoccurrence_lin ();
      qbf = 1;
    }
  else if (mode == INDUCTION_MODE)
    res = induction ();
  else if (mode == LINEARINDUCTION_MODE)
    {
      res = linearinduction ();
      qbf = 1;
    }
  else if (mode == LINEARINDUCTION_TRANS_MODE)
    {
      res = linearinduction_trans ();
      qbf = 1;
    }
  else if (mode == LINEARINDUCTION_BIN_TRANS_MODE)
    {
      res = linearinduction_bin_trans ();
      qbf = 1;
    }
  else if (mode == FIXPOINT_MODE)
    {
      res = fixpoint ();
      qbf = 1;
    }
  else if (mode == BMC_QBF_MODE)
    {
      res = bmc_lin ();
      qbf = 1;
    }
  else
    {
      assert (mode == BMC_MODE);
      res = bmc ();
    }

  if (format == AIG_FORMAT)
    print_aig (res, output, qbf);
  else if (format == CNF_FORMAT)
    print_aig_as_cnf (res, output, qbf);

  release ();

  if (close_output)
    fclose (output);

  if (close_input)
    fclose (input);

  return 0;
}
