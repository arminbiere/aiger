#include "aiger.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#define GZIP "gzip -c > %s 2>/dev/null"
#define GUNZIP "gunzip -c %s 2>/dev/null"

#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = private->malloc_callback (private->memory_mgr, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define REALLOCN(p,m,n) \
  do { \
    size_t mbytes = (m) * sizeof (*(p)); \
    size_t nbytes = (n) * sizeof (*(p)); \
    size_t minbytes = (mbytes < nbytes) ? mbytes : nbytes; \
    void * res = private->malloc_callback (private->memory_mgr, nbytes); \
    memcpy (res, (p), minbytes); \
    if (nbytes > mbytes) \
      memset (((char*)res) + mbytes, 0, nbytes - mbytes); \
    private->free_callback (private->memory_mgr, (p), mbytes); \
    (p) = res; \
  } while (0)

#define FIT(p,m,n) \
  do { \
    size_t old_size = (m); \
    size_t new_size = (n); \
    if (old_size < new_size) \
      { \
	REALLOCN (p,old_size,new_size); \
	(m) = new_size; \
      } \
  } while (0)

#define ENLARGE(p,s) \
  do { \
    size_t old_size = (s); \
    size_t new_size = old_size ? 2 * old_size : 1; \
    REALLOCN (p,old_size,new_size); \
    (s) = new_size; \
  } while (0)

#define PUSH(p,t,s,l) \
  do { \
    if ((t) == (s)) \
      ENLARGE (p, s); \
    (p)[(t)++] = (l); \
  } while (0)

#define DELETEN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    private->free_callback (private->memory_mgr, (p), bytes); \
    (p) = 0; \
  } while (0)

#define NEW(p) NEWN (p,1)
#define DELETE(p) DELETEN (p,1)

#define IMPORT_private_FROM(p) \
  aiger_private * private = (aiger_private*) (p)

#define EXPORT_public_FROM(p) \
  aiger * public = &(p)->public

typedef struct aiger_private aiger_private;
typedef struct aiger_buffer aiger_buffer;
typedef struct aiger_reader aiger_reader;

struct aiger_private
{
  aiger public;
  unsigned size_literals;
  unsigned size_nodes;
  unsigned size_inputs;
  unsigned size_outputs;
  unsigned size_latches;
  unsigned size_symbols;
  void *memory_mgr;
  aiger_malloc malloc_callback;
  aiger_free free_callback;
  char * error;
};

struct aiger_buffer
{
  char * start;
  char * cursor;
  char * end;
};

struct aiger_reader
{
  void * state;
  aiger_get get;

  int ch;

  unsigned lineno;
  unsigned charno;

  unsigned lineno_at_last_token_start;

  aiger_mode mode;
  unsigned maxidx;
  unsigned inputs;
  unsigned latches;
  unsigned outputs;
  unsigned ands;

  char * buffer;
  unsigned top_buffer;
  unsigned size_buffer;
};

aiger *
aiger_init_mem (void *memory_mgr,
		aiger_malloc external_malloc, aiger_free external_free)
{
  aiger_private *private;
  aiger * res;

  assert (external_malloc);
  assert (external_free);
  private = external_malloc (memory_mgr, sizeof (*private));
  memset (private, 0, sizeof (*private));
  private->memory_mgr = memory_mgr;
  private->malloc_callback = external_malloc;
  private->free_callback = external_free;
  res = &private->public;

  return res;
}

static void *
aiger_default_malloc (void *state, size_t bytes)
{
  return malloc (bytes);
}

static void
aiger_default_free (void *state, void *ptr, size_t bytes)
{
  free (ptr);
}

aiger *
aiger_init (void)
{
  return aiger_init_mem (0, aiger_default_malloc, aiger_default_free);
}

static void
aiger_delete_str (aiger_private * private, char * str)
{
  if (str)
    DELETEN (str, strlen (str) + 1);
}

static char *
aiger_copy_str (aiger_private * private, const char * str)
{
  char * res;

  if (!str || !str[0])
    return 0;

  NEWN (res, strlen (str) + 1);
  strcpy (res, str);

  return res;
}

static void
aiger_delete_symbols (aiger_private * private,
                      aiger_symbol * symbols, unsigned size)
{
  unsigned i;
  for (i = 0; i < size; i++)
    aiger_delete_str (private, symbols[i].str);

  DELETEN (symbols, size);
}

void
aiger_reset (aiger * public)
{
  IMPORT_private_FROM (public);

  DELETEN (public->literals, private->size_literals);
  DELETEN (public->nodes, private->size_nodes);

  aiger_delete_symbols (private, public->inputs, private->size_inputs);
  aiger_delete_symbols (private, public->latches, private->size_latches);
  aiger_delete_symbols (private, public->outputs, private->size_outputs);

  DELETEN (public->next, private->size_latches);

  aiger_delete_str (private, private->error);

  DELETE (private);
}

static void
aiger_import_literal (aiger_private * private, unsigned lit)
{
  EXPORT_public_FROM (private);

  if (!aiger_sign (lit))
    lit = aiger_not (lit);		/* always use larger lit */

  if (lit > public->max_literal)
    public->max_literal = lit;

  while (lit >= private->size_literals)
    ENLARGE (public->literals, private->size_literals);
}

void
aiger_add_input (aiger * public, unsigned lit, const char * str)
{
  IMPORT_private_FROM (public);
  aiger_symbol symbol;

  assert (lit);
  assert (!aiger_sign (lit));

  aiger_import_literal (private, lit);

  assert (!public->literals[lit].input);
  assert (!public->literals[lit].latch);
  assert (!public->literals[lit].node);

  symbol.lit = lit;
  symbol.str = aiger_copy_str (private, str);

  PUSH (public->inputs, public->num_inputs, private->size_inputs, symbol);
  public->literals[lit].input = 1;
}

void
aiger_add_output (aiger * public, unsigned lit, const char * str)
{
  IMPORT_private_FROM (public);
  aiger_symbol symbol;
  aiger_import_literal (private, lit);
  symbol.lit = lit;
  symbol.str = aiger_copy_str (private, str);
  PUSH (public->outputs, public->num_outputs, private->size_outputs, symbol);
}

void
aiger_add_latch (aiger * public, 
                 unsigned lit, unsigned next, const char * str)
{
  IMPORT_private_FROM (public);
  unsigned size_latches;

  assert (lit);
  assert (!aiger_sign (lit));

  aiger_import_literal (private, lit);
  aiger_import_literal (private, next);

  assert (!public->literals[lit].input);
  assert (!public->literals[lit].latch);
  assert (!public->literals[lit].node);

  size_latches = private->size_latches;
  if (public->num_latches == size_latches)
    {
      ENLARGE (public->latches, private->size_latches);
      ENLARGE (public->next, size_latches);

      assert (size_latches == private->size_latches);
    }

  public->latches[public->num_latches].lit = lit;
  public->latches[public->num_latches].str = aiger_copy_str (private, str);
  public->next[public->num_latches] = next;
  public->num_latches++;

  public->literals[lit].latch = 1;
}

void
aiger_add_and (aiger * public, unsigned lhs, unsigned rhs0, unsigned rhs1)
{
  IMPORT_private_FROM (public);
  aiger_node *node, *old_nodes;
  aiger_literal * literal;
  unsigned idx, i;
  long delta;

  assert (lhs > 1);
  assert (!aiger_sign (lhs));

  aiger_import_literal (private, lhs);

  assert (!public->literals[lhs].node);
  assert (!public->literals[aiger_not (lhs)].node);
  assert (!public->literals[lhs].input);
  assert (!public->literals[lhs].latch);

  aiger_import_literal (private, rhs0);
  aiger_import_literal (private, rhs1);

  idx = aiger_lit2idx (lhs);

  if (public->num_nodes == private->size_nodes)
    {
      old_nodes = public->nodes;
      ENLARGE (public->nodes, private->size_nodes);
      delta = ((char*)public->nodes) - (char*)old_nodes;

      for (i = 2; i <= public->max_literal; i++)
	{
	  literal = public->literals + i;
	  if (literal->node)
	    literal->node = (aiger_node *)(delta + (char*) literal->node);
	}
    }

  node = public->nodes + public->num_nodes++;

  node->lhs = lhs;
  node->rhs0 = rhs0;
  node->rhs1 = rhs1;

  assert (!node->client_data);

  public->literals[lhs].node = node;
  public->literals[aiger_not (lhs)].node = node;
}

static const char *
aiger_error_s (aiger_private * private, const char * s, const char * a)
{
  unsigned tmp_len, error_len;
  char * tmp;
  assert (!private->error);
  tmp_len = strlen (s) + strlen (a) + 1;
  NEWN (tmp, tmp_len);
  sprintf (tmp, s, a);
  error_len = strlen (tmp) + 1;
  NEWN (private->error, error_len);
  memcpy (private->error, tmp, error_len);
  DELETEN (tmp, tmp_len);
  return private->error;
}

static const char *
aiger_error_u (aiger_private * private, const char * s, unsigned u)
{
  unsigned tmp_len, error_len;
  char * tmp;
  assert (!private->error);
  tmp_len = strlen (s) + sizeof (u) * 4 + 1;
  NEWN (tmp, tmp_len);
  sprintf (tmp, s, u);
  error_len = strlen (tmp) + 1;
  NEWN (private->error, error_len);
  memcpy (private->error, tmp, error_len);
  DELETEN (tmp, tmp_len);
  return private->error;
}

static const char *
aiger_error_uu (
  aiger_private * private, const char * s, unsigned a, unsigned b)
{
  unsigned tmp_len, error_len;
  char * tmp;
  assert (!private->error);
  tmp_len = strlen (s) + sizeof (a) * 4 + sizeof (b) * 4 + 1;
  NEWN (tmp, tmp_len);
  sprintf (tmp, s, a, b);
  error_len = strlen (tmp) + 1;
  NEWN (private->error, error_len);
  memcpy (private->error, tmp, error_len);
  DELETEN (tmp, tmp_len);
  return private->error;
}

static const char *
aiger_error_usu (
  aiger_private * private, 
  const char * s, unsigned a, const char * t, unsigned b)
{
  unsigned tmp_len, error_len;
  char * tmp;
  assert (!private->error);
  tmp_len = strlen (s) + strlen (t) + sizeof (a) * 4 + sizeof (b) * 4 + 1;
  NEWN (tmp, tmp_len);
  sprintf (tmp, s, a, t, b);
  error_len = strlen (tmp) + 1;
  NEWN (private->error, error_len);
  memcpy (private->error, tmp, error_len);
  DELETEN (tmp, tmp_len);
  return private->error;
}

static int
aiger_literal_defined (aiger_private * private, unsigned lit)
{
  EXPORT_public_FROM (private);
  aiger_literal * literal;

  assert (lit <= public->max_literal);
  lit = aiger_strip (lit);
  literal = public->literals + lit;

  return literal->node || literal->input || literal->latch;
}

static void
aiger_check_next_defined (aiger_private * private)
{
  EXPORT_public_FROM (private);
  unsigned i, next, latch;

  if (private->error)
    return;

  for (i = 0; !private->error && i < public->num_latches; i++)
    {
      latch = public->latches[i].lit;
      assert (!aiger_sign (latch));
      assert (latch <= public->max_literal);
      assert (public->literals[latch].latch);
      next = public->next[i];
      if (!aiger_literal_defined (private, next))
	aiger_error_uu (private, 
	                 "next state function %u of latch %u undefined",
			 next, latch);
    }
}

static void
aiger_check_right_hand_side_defined (
  aiger_private * private, aiger_node * node, unsigned rhs)
{
  if (!node || private->error)
    return;

  if (!aiger_literal_defined (private, rhs))
    aiger_error_uu (private, "literal %u in and node %u undefined", 
		     rhs, node->lhs);
}

static void
aiger_check_right_hand_sides_defined (aiger_private * private)
{
  EXPORT_public_FROM (private);
  aiger_literal * literal;
  aiger_node * node;
  unsigned i;

  if (private->error)
    return;

  if (public->literals)
    {
      for (i = 0; !private->error && i <= public->max_literal; i += 2)
	{
	  literal = public->literals + i;

	  node = literal->node;
	  if (!node)
	    continue;

	  aiger_check_right_hand_side_defined (private, node, node->rhs0);
	  aiger_check_right_hand_side_defined (private, node, node->rhs1);
	}
    }
}

static void
aiger_check_outputs_defined (aiger_private * private)
{
  EXPORT_public_FROM (private);
  unsigned i, output;

  if (private->error)
    return;

  for (i = 0; !private->error && i < public->num_outputs; i++)
    {
      output = public->outputs[i].lit;
      output = aiger_strip (output);
      if (output <= 1)
	continue;

      assert (output <= public->max_literal);
      if (!aiger_literal_defined (private, output))
	aiger_error_u (private, "output %u undefined", output);
    }
}

static void
aiger_check_for_cycles (aiger_private * private)
{
  unsigned i, j, * stack, size_stack, top_stack, tmp;
  EXPORT_public_FROM (private);
  aiger_literal * literal;

  if (private->error)
    return;

  stack = 0;
  size_stack = top_stack = 0;

  for (i = 2; i <= public->max_literal; i += 2)
    {
      literal = public->literals + i;

      if (!literal->node || literal->mark)
	continue;

      PUSH (stack, top_stack, size_stack, i);
      while (!private->error && top_stack)
	{
	  j = stack[top_stack - 1];
	  assert (!aiger_sign (j));
	  if (j)
	    {
	      literal = public->literals + j;
	      if (literal->mark && literal->onstack)
		aiger_error_u (private, "cyclic definition for and gate %u", j);

	      if (!literal->node || literal->mark)
		{
		  top_stack--;
		  continue;
		}

	      /* Prefix code.
	       */
	      literal->mark = 1;
	      literal->onstack = 1;
	      PUSH (stack, top_stack, size_stack, 0);

	      tmp = aiger_strip (literal->node->rhs0);
	      if (tmp >= 2)
		PUSH (stack, top_stack, size_stack, tmp);

	      tmp = aiger_strip (literal->node->rhs1);
	      if (tmp >= 2)
		PUSH (stack, top_stack, size_stack, tmp);
	    }
	  else	
	    {
	      /* All descendends traversed.  This is the postfix code.
	       */
	      assert (top_stack >= 2);
	      top_stack -= 2;
	      j = stack[top_stack];
	      assert (j >= 2);
	      literal = public->literals + j;
	      assert (literal->mark);
	      assert (literal->onstack);
	      literal->onstack = 0;
	    }
	}
    }

  DELETEN (stack, size_stack);
}

const char *
aiger_check (aiger * public)
{
  IMPORT_private_FROM (public);

  if (private->error)
    DELETEN (private->error, strlen (private->error) + 1);

  aiger_check_next_defined (private);
  aiger_check_outputs_defined (private);
  aiger_check_right_hand_sides_defined (private);
  aiger_check_for_cycles (private);

  return private->error;
}

static int
aiger_default_get (FILE * file)
{
  return getc (file);
}

static int
aiger_default_put (char ch, FILE * file)
{
  return putc ((unsigned char)ch, file);
}

static int
aiger_string_put (char ch, aiger_buffer * buffer)
{
  unsigned char res;
  if (buffer->cursor == buffer->end)
    return EOF;
  *buffer->cursor++ = ch;
  res = ch;
  return ch;
}

static int
aiger_put_s (void * state, aiger_put put, const char * str)
{
  const char * p;
  char ch;

  for (p = str; (ch = *p); p++)
    if (put (ch, state) == EOF)
      return EOF;

  return p - str;		/* 'fputs' semantics, >= 0 is OK */
}

static int
aiger_put_u (void * state, aiger_put put, unsigned u)
{
  char buffer [sizeof (u) * 4];
  sprintf (buffer, "%u", u);
  return aiger_put_s (state, put, buffer);
}

static int
aiger_normalized_inputs (aiger * public)
{
  unsigned i;

  for (i = 0; i < public->num_inputs; i++)
    if (public->inputs[i].lit != 2 * (i + 1))
      return 0;

  return 1;
}

static int
aiger_normalized_latches (aiger * public)
{
  unsigned i;

  for (i = 0; i < public->num_inputs; i++)
    if (public->inputs[i].lit != 2 * (i + 1) + 2 * public->num_inputs)
      return 0;

  return 1;
}

static int
aiger_write_header (aiger * public, 
                    const char * format_string, void * state, aiger_put put)
{
  unsigned i;

  if (aiger_put_s (state, put, "p ") == EOF ||
      aiger_put_s (state, put, format_string) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, aiger_lit2idx(public->max_literal)) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_inputs) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_latches) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_outputs) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_nodes) == EOF ||
      put ('\n', state) == EOF)
    return 0;

  if (public->num_inputs)
    {
      if (public->num_inputs > 1 && aiger_normalized_inputs (public))
	{
	  if (aiger_put_s (state, put, "0\n") == EOF)
	    return 0;
	}
      else
	{
	  for (i = 0; i < public->num_inputs; i++)
	    if (aiger_put_u (state, put, public->inputs[i].lit) == EOF ||
		put ('\n', state) == EOF)
	    return 0;
	}
    }

  if (public->num_latches)
    {
      if (public->num_latches > 1 && aiger_normalized_latches (public))
	{
	  if (aiger_put_s (state, put, "0\n") == EOF)
	    return 0;
	}
      else
	{
	  for (i = 0; i < public->num_latches; i++)
	    if (aiger_put_u (state, put, public->latches[i].lit) == EOF ||
		put (' ', state) == EOF ||
		aiger_put_u (state, put, public->next[i]) == EOF ||
		put ('\n', state) == EOF)
	    return 0;
	}
    }

  if (public->num_outputs)
    {
      for (i = 0; i < public->num_outputs; i++)
	if (aiger_put_u (state, put, public->outputs[i].lit) == EOF ||
	    put ('\n', state) == EOF)
	return 0;
    }

  return 1;
}

static int
aiger_have_at_least_one_symbol_aux (aiger * public,
                                    aiger_symbol * symbols, unsigned size)
{
  unsigned i;

  for (i = 0; i < size; i++)
    if (symbols[i].str)
      return 1;

  return 0;
}

static int
aiger_have_at_least_one_symbol (aiger * public)
{
  if (aiger_have_at_least_one_symbol_aux (public,
	                                  public->inputs, public->num_inputs))
    return 1;

  if (aiger_have_at_least_one_symbol_aux (public,
	                                  public->outputs, public->num_outputs))
    return 1;

  if (aiger_have_at_least_one_symbol_aux (public,
	                                  public->latches, public->num_latches))
    return 1;

  return 0;
}

static int
aiger_write_symbols_aux (aiger * public,
                         void * state, aiger_put put,
                         const char * type,
			 aiger_symbol * symbols, unsigned size)
{
  unsigned i;

  for (i = 0; i < size; i++)
    {
      if (!symbols[i].str)
	continue;

      assert (symbols[i].str[0]);

      if (aiger_put_s (state, put, type) == EOF ||
	  put (' ', state) == EOF ||
	  aiger_put_u (state, put, i) == EOF ||
	  put (' ', state) == EOF ||
          aiger_put_u (state, put, symbols[i].lit) == EOF ||
	  put (' ', state) == EOF ||
	  aiger_put_s (state, put, symbols[i].str) == EOF ||
          put ('\n', state) == EOF)
	return 0;
    }

  return 1;
}

static int
aiger_write_symbols (aiger * public, void * state, aiger_put put)
{
  if (!aiger_write_symbols_aux (public, state, put,
				"i", public->inputs, public->num_inputs))
    return 0;

  if (!aiger_write_symbols_aux (public, state, put,
	                        "l", public->latches, public->num_latches))
    return 0;

  if (!aiger_write_symbols_aux (public, state, put,
	                        "o", public->outputs, public->num_outputs))
    return 0;

  return 1;
}

int
aiger_write_symbols_to_file (aiger * public, FILE * file)
{
  return aiger_write_symbols (public, file, (aiger_put) aiger_default_put);
}

static int
aiger_write_ascii (aiger * public, void * state, aiger_put put)
{
  aiger_node * node;
  unsigned i;

  assert (!aiger_check (public));

  if (!aiger_write_header (public, "aig", state, put))
    return 0;

  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;
      if (aiger_put_u (state, put, node->lhs) == EOF ||
          put (' ', state) == EOF ||
          aiger_put_u (state, put, node->rhs0) == EOF ||
          put (' ', state) == EOF ||
          aiger_put_u (state, put, node->rhs1) == EOF ||
          put ('\n', state) == EOF)
	return 0;
    }
  
  if (aiger_have_at_least_one_symbol (public))
    {
      if (!aiger_write_symbols (public, state, put))
	return 0;
    }

  return 1;
}

#ifndef NDEBUG
static int
aiger_is_reencoded (aiger * public)
{
  unsigned i, tmp, max;
  aiger_node * node;

  max = 0;
  for (i = 0; i < public->num_inputs; i++)
    {
      tmp = public->inputs[i].lit;
      if (tmp > max)
	max = tmp;
    }

  for (i = 0; i < public->num_latches; i++)
    {
      tmp = public->latches[i].lit;
      if (tmp > max)
	max = tmp;
    }

  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;

      if (node->lhs <= max)
	return 0;

      if (node->lhs < node->rhs0)
	return 0;

      if (node->rhs0 < node->rhs1)
	return 0;
    }

  return 1;
}
#endif

static void
aiger_new_code (unsigned lit, unsigned * new, unsigned * code)
{
  unsigned tmp = aiger_strip (lit);
  unsigned res;

  assert (!code[tmp]);
  res = *new;
  code[tmp] = res;
  code[tmp + 1] = res + 1;
  *new += 2;
}

static unsigned
aiger_reencode_lit (aiger * public, unsigned lit,
                    unsigned * new, unsigned * code, unsigned * stack)
{
  unsigned res, old, * top, child0, child1, tmp;
  aiger_literal * literal;
  aiger_node * node;

  if (lit < 2)
    return lit;

  res = code[lit];
  if (res)
    return res;

  literal = public->literals + lit;
  if (literal->node)
    {
      top = stack;
      *top++ = aiger_strip (lit);
      while (top > stack)
	{
	  old = *--top;
	  if (old)
	    {
	      if (code[old])
		continue;

	      literal = public->literals + old;
	      if (literal->onstack)
		continue;

	      literal->onstack = 1;

	      *top++ = old;
	      *top++ = 0;

	      node = literal->node;
	      assert (node);

	      child0 = aiger_strip (node->rhs0);
	      child1 = aiger_strip (node->rhs1);

	      if (child0 < child1)
	        {
		  tmp = child0;
		  child0 = child1;
		  child1 = tmp;
		}

	      assert (child1 < child0);	/* traverse smaller child first */

	      if (child0 >= 2)
		{
		  literal = public->literals + child0;
		  if (!literal->input && !literal->latch && !literal->onstack)
		    {
		      assert (top < stack + 2 * public->num_nodes);
		      *top++ = child0;
		    }
		}

	      if (child1 >= 2)
		{
		  literal = public->literals + child1;
		  if (!literal->input && !literal->latch && !literal->onstack)
		    {
		      assert (top < stack + 2 * public->num_nodes);
		      *top++ = child1;
		    }
		}
	    }
	  else
	    {
	      assert (top > stack);
	      old = *--top;
	      assert (!code[old]);
	      assert (public->literals[old].onstack);
	      public->literals[old].onstack = 0;
	      aiger_new_code (old, new, code);
	    }
	}
    }
  else 
    {
      assert (literal->input || literal->latch);
      assert (lit < *new);

      code[lit] = lit;
      code[aiger_not(lit)] = aiger_not(lit);
    }

  assert (code[lit]);

  return code[lit];
}

static int
cmp_lhs (const void * a, const void * b)
{
  const aiger_node * c = a;
  const aiger_node * d = b;
  return ((int)c->lhs) - (int)d->lhs;
}

static unsigned
aiger_max_input_or_latch (aiger * public)
{
  unsigned i, tmp, res;

  res = 0;

  for (i = 0; i < public->num_inputs; i++)
    {
      tmp = public->inputs[i].lit;
      assert (!aiger_sign (tmp));
      if (tmp > res)
	res = tmp;
    }

  for (i = 0; i < public->num_latches; i++)
    {
      tmp = public->latches[i].lit;
      assert (!aiger_sign (tmp));
      if (tmp > res)
	res = tmp;
    }

  return res;
}

void
aiger_reencode (aiger * public)
{
  unsigned * code, i, j, size_code, old, new, * stack, lhs, rhs0, rhs1, tmp;
  IMPORT_private_FROM (public);
  aiger_node * node;

  size_code = public->max_literal + 1;
  if (size_code < 2)
    size_code = 2;

  NEWN (code, size_code);
  NEWN (stack, 2 * public->num_nodes);

  code[1] = 1;			/* not used actually */

  new = aiger_max_input_or_latch (public) + 2;

  for (i = 0; i < public->num_inputs; i++)
    {
      old = public->inputs[i].lit;
      public->inputs[i].lit =
	aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_latches; i++)
    {
      old = public->latches[i].lit;
      public->latches[i].lit =
	aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_latches; i++)
    {
      old = public->next[i];
      public->next[i] = aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_outputs; i++)
    {
      old = public->outputs[i].lit;
      public->outputs[i].lit =
	aiger_reencode_lit (public, old, &new, code, stack);
    }

  DELETEN (stack, 2 * public->num_nodes);

  j = 0;
  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;
      lhs = code[node->lhs];
      if (!lhs)
	continue;

      rhs0 = code[node->rhs0];
      rhs1 = code[node->rhs1];

      assert (rhs0);
      assert (rhs1);

      node = public->nodes + j++;

      if (rhs0 < rhs1)
	{
	  tmp = rhs1;
	  rhs1 = rhs0;
	  rhs0 = tmp;
	}

      assert (lhs > rhs0);
      assert (rhs0 > rhs1);

      node->lhs = lhs;
      node->rhs0 = rhs0;
      node->rhs1 = rhs1;

      node->client_data = 0;
    }
  public->num_nodes = j;

  qsort (public->nodes, j, sizeof (*node), cmp_lhs);

  if (public->max_literal + 1 < new)
    aiger_import_literal (private, new - 1);

  for (i = 2; i <= public->max_literal; i++)
    {
      public->literals[i].node = 0;
      public->literals[i].client_bit = 0;
    }

  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;
      public->literals[node->lhs].node = node;
      public->literals[aiger_not (node->lhs)].node = node;
    }

  assert (aiger_is_reencoded (public));
  assert (!aiger_check (public));

  DELETEN (code, size_code);
}

static int
aiger_write_delta (aiger * public, void * state, aiger_put put, unsigned delta)
{
  unsigned tmp = delta;
  unsigned char ch;

  while (tmp & ~0x7f)
    {
      ch = tmp & 0x7f;
      ch |= 0x80;

      if (put (ch, state) == EOF)
	return 0;

      tmp >>= 7;
    }

  ch = tmp;
  return put (ch, state) != EOF;
}

static int
aiger_write_binary (aiger * public, void * state, aiger_put put)
{
  aiger_node * node;
  unsigned lhs, i;

  assert (!aiger_check (public));
  aiger_reencode (public);

  if (!aiger_write_header (public, "big", state, put))
    return 0;

  lhs = aiger_max_input_or_latch (public) + 2;

  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;

      assert (lhs == node->lhs);
      assert (lhs > node->rhs0);
      assert (node->rhs0 > node->rhs1);

      aiger_write_delta (public, state, put, lhs - node->rhs0);
      aiger_write_delta (public, state, put, node->rhs0 - node->rhs1);

      lhs += 2;
    }

  if (public->num_nodes)
    {
      if (put ('\n', state) == EOF)
	return 0;
    }

  if (!aiger_write_symbols (public, state, put))
    return 0;

  return 1;
}

int
aiger_write_generic (aiger * public,
                     aiger_mode mode, void * state, aiger_put put)
{
  assert (mode == aiger_binary_mode || mode == aiger_ascii_mode);
  if (mode == aiger_ascii_mode)
    return aiger_write_ascii (public, state, put);
  else
    return aiger_write_binary (public, state, put);
}

int
aiger_write_to_file (aiger * public, aiger_mode mode, FILE * file)
{
  return aiger_write_generic (public,
                              mode, file, (aiger_put) aiger_default_put);
}

int
aiger_write_to_string (aiger * public, 
                       aiger_mode mode, char * str, size_t len)
{
  aiger_buffer buffer;
  int res;

  buffer.start = str;
  buffer.cursor = str;
  buffer.end = str + len;
  res = aiger_write_generic (public, 
                             mode, &buffer, (aiger_put) aiger_string_put);

  if (!res)
    return 0;

  if (aiger_string_put (0, &buffer) == EOF)
    return 0;

  return 1;
}

static int
aiger_has_suffix (const char * str, const char * suffix)
{
  if (strlen (str) < strlen (suffix))
    return 0;

  return !strcmp (str + strlen (str) - strlen (suffix), suffix);
}

int
aiger_open_and_write_to_file (aiger * public, const char * file_name)
{
  IMPORT_private_FROM (public);
  int res, pclose_file;
  char * cmd, size_cmd;
  aiger_mode mode;
  FILE * file;

  assert (file_name);

  if (aiger_has_suffix (file_name, ".gz"))
    {
      size_cmd = strlen (file_name) + strlen (GZIP);
      NEWN (cmd, size_cmd);
      sprintf (cmd, GZIP, file_name);
      file = popen (cmd, "w");
      DELETEN (cmd, size_cmd);
      pclose_file = 1;
    }
  else
    {
      file = fopen (file_name, "w");
      pclose_file = 0;
    }

  if (!file)
    return 0;

  if (aiger_has_suffix (file_name, ".big") ||
      aiger_has_suffix (file_name, ".big.gz"))
    mode = aiger_binary_mode;
  else
    mode = aiger_ascii_mode;

  res = aiger_write_to_file (public, mode, file);

  if (pclose_file)
    pclose (file);
  else
    fclose (file);

  return res;
}

static int
aiger_next_ch (aiger_reader * reader)
{
  int res;

  res = reader->get (reader->state);

  if (isspace (reader->ch) && !isspace (res))
    reader->lineno_at_last_token_start = reader->lineno;

  reader->ch = res;

  if (res == '\n')
    reader->lineno++;

  if (res != EOF)
    reader->charno++;

  return res;
}

/* Read a number assuming that the current character has already been
 * checked to be a digit, e.g. the start of the number to be read.
 */
static unsigned
aiger_read_number (aiger_reader * reader)
{
  unsigned res;

  assert (isdigit (reader->ch));
  res = reader->ch - '0';

  while (isdigit (aiger_next_ch (reader)))
    res = 10 * res + (reader->ch - '0');

  return res;
}

/* Expect and read an unsigned number followed by at least one white space
 * character.  The white space should either the space character or a new
 * line as specified by the 'followed_by' parameter.  If a number can not be
 * found or there is no white space after the number, an apropriate error
 * message is returned.
 */
static const char *
aiger_read_literal (aiger_private * private,
		    aiger_reader * reader, 
		    unsigned * res_ptr,
		    char followed_by)
{
  unsigned res;

  assert (followed_by == ' ' || followed_by == '\n');

  if (!isdigit (reader->ch))
    return aiger_error_u (private,
	                  "line %u: expected literal", reader->lineno);

  res = aiger_read_number (reader);

  if (followed_by == ' ')
    {
      if (reader->ch != ' ')
	return aiger_error_uu (private,
			      "line %u: expected space after literal %u",
			      reader->lineno_at_last_token_start, res);
    }
  else
    {
      if (reader->ch != '\n')
	return aiger_error_uu (private,
			       "line %u: expected new line after literal %u",
			       reader->lineno_at_last_token_start, res);
    }
  
  aiger_next_ch (reader);	/* skip white space */

  *res_ptr = res;

  return 0;
}

static const char *
aiger_already_defined (aiger * public, aiger_reader * reader, unsigned lit)
{
  IMPORT_private_FROM (public);

  assert (lit);
  assert (!aiger_sign (lit));

  if (public->max_literal < lit)
    return 0;

  if (public->literals[lit].input)
    return aiger_error_uu (private,
			   "line %u: literal %u already defined as input",
			   reader->lineno_at_last_token_start, lit);

  if (public->literals[lit].latch)
    return aiger_error_uu (private,
			   "line %u: literal %u already defined as latch",
			   reader->lineno_at_last_token_start, lit);

  if (public->literals[lit].node)
    return aiger_error_uu (private,
			   "line %u: literal %u already defined as and node",
			   reader->lineno_at_last_token_start, lit);
  return 0;
}

static const char *
aiger_read_header (aiger * public, aiger_reader * reader)
{
  IMPORT_private_FROM (public);
  unsigned i, lit, next;
  const char * error;

  assert (reader->ch == 'p');
  if (aiger_next_ch (reader) != ' ')
INVALID_HEADER:
    return aiger_error_u (private, "line %u: invalid header", reader->lineno);

  aiger_next_ch (reader);
  if (reader->ch != 'b' && reader->ch != 'a')
    goto INVALID_HEADER;

  reader->mode = (reader->ch == 'b') ? aiger_binary_mode : aiger_ascii_mode;

  if (aiger_next_ch (reader) != 'i' || 
      aiger_next_ch (reader) != 'g' ||
      aiger_next_ch (reader) != ' ')
    goto INVALID_HEADER;

  aiger_next_ch (reader);

  if (aiger_read_literal (private, reader, &reader->maxidx, ' ') ||
      aiger_read_literal (private, reader, &reader->inputs, ' ') ||
      aiger_read_literal (private, reader, &reader->latches, ' ') ||
      aiger_read_literal (private, reader, &reader->outputs, ' ') ||
      aiger_read_literal (private, reader, &reader->ands, '\n'))
    {
      assert (private->error);
      return private->error;
    }

  public->max_literal = 2 * reader->maxidx + 1;
  FIT (public->literals, private->size_literals, public->max_literal + 1);
  FIT (public->inputs, private->size_inputs, reader->inputs);
  FIT (public->outputs, private->size_outputs, reader->outputs);

  if (private->size_latches < reader->latches)
    {
      REALLOCN (public->latches, private->size_latches, reader->latches);
      REALLOCN (public->next, private->size_latches, reader->latches);
      private->size_latches = reader->latches;
    }

  FIT (public->nodes, private->size_nodes, reader->ands);

  for (i = 0; i < reader->inputs; i++)
    {
      error = aiger_read_literal (private, reader, &lit, '\n');
      if (error)
	return error;

      if (!lit || aiger_sign (lit) || lit > public->max_literal)
	return aiger_error_uu (private,
			      "line %u: literal %u is not a valid input",
			      reader->lineno_at_last_token_start, lit);

      error = aiger_already_defined (public, reader, lit);
      if (error)
	return error;

      aiger_add_input (public, lit, 0);
    }

  for (i = 0; i < reader->latches; i++)
    {
      error = aiger_read_literal (private, reader, &lit, ' ');
      if (error)
	return error;

      if (!lit || aiger_sign (lit) || lit > public->max_literal)
	return aiger_error_uu (private,
			      "line %u: literal %u is not a valid latch",
			      reader->lineno_at_last_token_start, lit);

      error = aiger_already_defined (public, reader, lit);
      if (error)
	return error;

      error = aiger_read_literal (private, reader, &next, '\n');
      if (error)
	return error;

      if (next > public->max_literal)
	return aiger_error_uu (private,
			      "line %u: literal %u is not a valid literal",
			      reader->lineno_at_last_token_start, next);

      aiger_add_latch (public, lit, next, 0);
    }

  for (i = 0; i < reader->outputs; i++)
    {
      error = aiger_read_literal (private, reader, &lit, '\n');
      if (error)
	return error;

      if (lit > public->max_literal)
	return aiger_error_uu (private,
			      "line %u: literal %u is not a valid output",
			      reader->lineno_at_last_token_start, lit);

      aiger_add_output (public, lit, 0);
    }

  return 0;
}

static const char *
aiger_read_ascii (aiger * public, aiger_reader * reader)
{
  IMPORT_private_FROM (public);
  unsigned i, lhs, rhs0, rhs1;
  const char * error;

  for (i = 0; i < reader->ands; i++)
    {
      error = aiger_read_literal (private, reader, &lhs, ' ');
      if (error)
	return error;

      if (!lhs || aiger_sign (lhs) || lhs > public->max_literal)
	return aiger_error_uu (private,
	                       "line %u: "
			       "literal %u is not a valid LHS of AND node",
			       reader->lineno_at_last_token_start, lhs);

      error = aiger_already_defined (public, reader, lhs);
      if (error)
	return error;

      error = aiger_read_literal (private, reader, &rhs0, ' ');
      if (error)
	return error;

      if (rhs0 > public->max_literal)
	return aiger_error_uu (private,
	                       "line %u: literal %u is not a valid literal",
			       reader->lineno_at_last_token_start, rhs0);

      error = aiger_read_literal (private, reader, &rhs1, '\n');
      if (error)
	return error;

      if (rhs1 > public->max_literal)
	return aiger_error_uu (private,
	                       "line %u: literal %u is not a valid literal",
			       reader->lineno_at_last_token_start, rhs1);

      aiger_add_and (public, lhs, rhs0, rhs1);
    }
  
  return 0;
}

static const char *
aiger_read_binary (aiger * public, aiger_reader * reader)
{
  return 0;
}

static const char *
aiger_read_symbols (aiger * public, aiger_reader * reader)
{
  IMPORT_private_FROM (public);
  const char * error, * type;
  unsigned lit, pos, num;
  aiger_symbol * symbol;

  assert (!reader->buffer);

  for (;;)
    {
      if (reader->ch == EOF)
	return 0;

      if (reader->ch != 'i' && reader->ch != 'l' && reader->ch != 'o') 
INVALID_SYMBOL_TABLE_ENTRY:
	return aiger_error_u (private,
	                      "line %u: invalid symbol table entry",
			      reader->lineno);

      if (reader->ch == 'i')
	{
	  type = "input";
	  num = public->num_inputs;
	  symbol = public->inputs;
	}
      else if (reader->ch == 'l')
	{
	  type = "latch";
	  num = public->num_latches;
	  symbol = public->latches;
	}
      else
	{
	  assert (reader->ch == 'o');
	  type = "output";
	  num = public->num_outputs;
	  symbol = public->outputs;
	}

      if (aiger_next_ch (reader) != ' ')
	goto INVALID_SYMBOL_TABLE_ENTRY;

      aiger_next_ch (reader);
      error = aiger_read_literal (private, reader, &pos, ' ');
      if (error)
	return error;

      if (pos >= num)
	return aiger_error_usu (private,
		    "line %u: %s symbol table entry position %u too large",
		    reader->lineno_at_last_token_start, type, pos);

      symbol += pos;

      error = aiger_read_literal (private, reader, &lit, ' ');
      if (error)
	return error;

      if (symbol->lit != lit)
	return aiger_error_usu (private,
		    "line %u: %s symbol table entry literal %u does not match",
		    reader->lineno_at_last_token_start, type, lit);

      if (symbol->str)
	return aiger_error_usu (private,
		    "line %u: %s %u has multiple symbols",
		    reader->lineno_at_last_token_start, type, lit);

      while (reader->ch != '\n' && reader->ch != EOF)
	{
	  PUSH (reader->buffer, 
	        reader->top_buffer, reader->size_buffer, (char) reader->ch);
	  aiger_next_ch (reader);
	}

      if (reader->ch == EOF)
	return aiger_error_u (private,
	                      "line %u: new line missing", reader->lineno);

      assert (reader->ch == '\n');
      aiger_next_ch (reader);

      PUSH (reader->buffer, reader->top_buffer, reader->size_buffer, 0);
      symbol->str = aiger_copy_str (private, reader->buffer);
      reader->top_buffer = 0;
    }
}

const char * 
aiger_read_generic (aiger * public, void * state, aiger_get get)
{
  IMPORT_private_FROM (public);
  aiger_reader reader;
  const char * error;

  reader.lineno = 1;
  reader.charno = 1;
  reader.state = state;
  reader.get = get;
  reader.ch = ' ';
  reader.buffer = 0;
  reader.top_buffer = 0;
  reader.size_buffer = 0;
SCAN:
  aiger_next_ch (&reader);
  if (reader.ch == 'c')
    {
      while (aiger_next_ch (&reader) != '\n' && reader.ch != EOF)
	;

      if (reader.ch == EOF)
HEADER_MISSING:
	return aiger_error_u (private, 
	                      "line %u: header missing", reader.lineno);
      goto SCAN;
    }

  if (reader.ch == EOF)
    goto HEADER_MISSING;

  if (reader.ch != 'p')
    return aiger_error_u (private, "line %u: expected header", reader.lineno);

  error = aiger_read_header (public, &reader);
  if (error)
    return error;

  if (reader.mode == aiger_binary_mode)
    error = aiger_read_binary (public, &reader);
  else
    error = aiger_read_ascii (public, &reader);

  if (error)
    return error;

  error = aiger_read_symbols (public, &reader);
  DELETEN (reader.buffer, reader.size_buffer);
  if (error)
    return error;

  assert (reader.ch == EOF);

  return aiger_check (public);
}

const char *
aiger_read_from_file (aiger * public, FILE * file)
{
  return aiger_read_generic (public, file, (aiger_get) aiger_default_get);
}

const char *
aiger_open_and_read_from_file (aiger * public, const char * file_name)
{
  IMPORT_private_FROM (public);
  char * cmd, size_cmd;
  const char * res;
  int pclose_file;
  FILE * file;

  if (aiger_has_suffix (file_name, ".gz"))
    {
      size_cmd = strlen (file_name) + strlen (GUNZIP);
      NEWN (cmd, size_cmd);
      sprintf (cmd, GUNZIP, file_name);
      file = popen (cmd, "r");
      DELETEN (cmd, size_cmd);
      pclose_file = 1;
    }
  else
    {
      file = fopen (file_name, "r");
      pclose_file = 0;
    }

  if (!file)
    return aiger_error_s (private, "can not read '%s'", file_name);

  res = aiger_read_from_file (public, file);

  if (pclose_file)
    pclose (file);
  else
    fclose (file);

  return res;
}
