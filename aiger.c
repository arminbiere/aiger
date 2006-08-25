#include "aiger.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct aiger_private aiger_private;
typedef struct aiger_buffer aiger_buffer;

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
  char * buffer;
  char * cursor;
  char * end_of_buffer;
};

aiger *
aiger_init_mem (void *memory_mgr,
		aiger_malloc external_malloc, aiger_free external_free)
{
  aiger_private *res;
  assert (external_malloc);
  assert (external_free);
  res = external_malloc (memory_mgr, sizeof (*res));
  memset (res, 0, sizeof (*res));
  res->memory_mgr = memory_mgr;
  res->malloc_callback = external_malloc;
  res->free_callback = external_free;
  return &res->public;
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

static void
aiger_delete_str (aiger_private * private, char * str)
{
  DELETEN (str, strlen (str) + 1);
}

static char *
aiger_copy_str (aiger_private * private, const char * str)
{
  char * res;
  NEWN (res, strlen (str) + 1);
  return strcpy (res, str);
}

void
aiger_reset (aiger * public)
{
  IMPORT_private_FROM (public);
  unsigned i;

  for (i = 0; i < public->num_symbols; i++)
    aiger_delete_str (private, public->symbols[i].str);

  DELETEN (public->symbols, private->size_symbols);

  DELETEN (public->literals, private->size_literals);
  DELETEN (public->nodes, private->size_nodes);

  DELETEN (public->inputs, private->size_inputs);
  DELETEN (public->next, private->size_latches);
  DELETEN (public->latches, private->size_latches);
  DELETEN (public->outputs, private->size_outputs);

  if (private->error)
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
aiger_add_input (aiger * public, unsigned lit)
{
  IMPORT_private_FROM (public);

  assert (lit);
  assert (!aiger_sign (lit));

  aiger_import_literal (private, lit);

  assert (!public->literals[lit].latch);
  assert (!public->literals[lit].node);

  PUSH (public->inputs, public->num_inputs, private->size_inputs, lit);
  public->literals[lit].input = 1;
}

void
aiger_add_output (aiger * public, unsigned lit)
{
  IMPORT_private_FROM (public);
  aiger_import_literal (private, lit);
  PUSH (public->outputs, public->num_outputs, private->size_outputs, lit);
}

void
aiger_add_symbol (aiger * public, unsigned lit, const char * str)
{
  IMPORT_private_FROM (public);
  aiger_symbol * symbol;
  aiger_import_literal (private, lit);
  if (public->num_symbols == private->size_symbols)
    ENLARGE (public->symbols, private->size_symbols);
  symbol = public->symbols + public->num_symbols++;
  symbol->lit = lit;
  symbol->str = aiger_copy_str (private, str);
}

void
aiger_add_latch (aiger * public, unsigned lit, unsigned next)
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

  public->latches[public->num_latches] = lit;
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

static void
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
}

static void
aiger_error_uu (
  aiger_private * private, const char * s, unsigned a, unsigned b)
{
  unsigned tmp_len, error_len;
  char * tmp;
  assert (!private->error);
  tmp_len = strlen (s) + 100;
  NEWN (tmp, tmp_len);
  sprintf (tmp, s, a, b);
  error_len = strlen (tmp) + 1;
  NEWN (private->error, error_len);
  memcpy (private->error, tmp, error_len);
  DELETEN (tmp, tmp_len);
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
      latch = public->latches[i];
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
    aiger_error_uu (private, "argument %u of and node %u undefined", 
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
      output = public->outputs[i];
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
  unsigned char i, j, * stack, size_stack, top_stack, tmp;
  EXPORT_public_FROM (private);
  aiger_literal * literal;
  aiger_node * node;

  if (private->error)
    return;

  stack = 0;
  size_stack = top_stack = 0;

  for (i = 2; i <= public->max_literal; i += 2)
    {
      literal = public->literals + i;

      node = literal->node;
      if (!node)
	continue;

      if (literal->mark)
	continue;

      PUSH (stack, top_stack, size_stack, i);
      while (!private->error && top_stack)
	{
	  j = stack[top_stack - 1];
	  assert (!aiger_sign (j));
	  if (j)
	    {
	      literal = public->literals + j;
	      if (literal->mark)
		{
		  if (literal->onstack)
		    aiger_error_u (private, 
			            "cyclic definition for and gate %u", j);
		  top_stack--;
		  continue;
		}

	      node = literal->node;
	      if (!node)
		{
		  top_stack--;
		  continue;
		}

	      /* Prefix code.
	       */
	      literal->mark = 1;
	      literal->onstack = 1;
	      PUSH (stack, top_stack, size_stack, 0);

	      tmp = aiger_strip (node->rhs0);
	      if (tmp >= 2)
		PUSH (stack, top_stack, size_stack, tmp);

	      tmp = aiger_strip (node->rhs1);
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
  if (buffer->cursor == buffer->end_of_buffer)
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
    if (public->inputs[i] != 2 * (i + 1))
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
      aiger_put_u (state, put, public->num_inputs) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_nodes) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_latches) == EOF ||
      put (' ', state) == EOF ||
      aiger_put_u (state, put, public->num_outputs) == EOF ||
      put ('\n', state) == EOF)
    return 0;

  if (public->num_inputs)
    {
      if (aiger_normalized_inputs (public))
	{
	  if (aiger_put_s (state, put, "c inputs ") == EOF ||
	      aiger_put_u (state, put, public->num_inputs) == EOF ||
	      aiger_put_s (state, put, " from ") == EOF ||
	      aiger_put_u (state, put, 2) == EOF ||
	      aiger_put_s (state, put, " to ") == EOF ||
	      aiger_put_u (state, put, 2 * public->num_inputs) == EOF ||
	      aiger_put_s (state, put, "\n0\n") == EOF)
	    return 0;
	}
      else
	{
	  if (aiger_put_s (state, put, "c inputs ") == EOF ||
	      aiger_put_u (state, put, public->num_inputs) == EOF ||
	      put ('\n', state) == EOF)
	    return 0;

	  for (i = 0; i < public->num_inputs; i++)
	    if (aiger_put_u (state, put, public->inputs[i]) == EOF ||
		put ('\n', state) == EOF)
	    return 0;
	}
    }

  if (public->num_latches)
    {
      if (aiger_put_s (state, put, "c latches ") == EOF ||
	  aiger_put_u (state, put, public->num_latches) == EOF ||
	  put ('\n', state) == EOF)
	return 0;

      for (i = 0; i < public->num_latches; i++)
	if (aiger_put_u (state, put, public->latches[i]) == EOF ||
	    put (' ', state) == EOF ||
	    aiger_put_u (state, put, public->next[i]) == EOF ||
	    put ('\n', state) == EOF)
	return 0;
    }

  if (public->num_outputs)
    {
      if (aiger_put_s (state, put, "c outputs ") == EOF ||
	  aiger_put_u (state, put, public->num_outputs) == EOF ||
	  put ('\n', state) == EOF)
	return 0;

      for (i = 0; i < public->num_outputs; i++)
	if (aiger_put_u (state, put, public->outputs[i]) == EOF ||
	    put ('\n', state) == EOF)
	return 0;
    }

  if (public->num_nodes)
    {
      if (aiger_put_s (state, put, "c ands ") == EOF ||
	  aiger_put_u (state, put, public->num_nodes) == EOF ||
	  put ('\n', state) == EOF)
	return 0;
    }

  return 1;
}

static int
aiger_write_symbols (aiger * public, void * state, aiger_put put)
{
  aiger_symbol * symbol;
  unsigned i;

  if (!public->num_symbols)
    return 1;

  if (aiger_put_s (state, put, "c symbols ") == EOF ||
      aiger_put_u (state, put, public->num_symbols) == EOF ||
      put ('\n', state) == EOF)
    return 0;

  for (i = 0; i < public->num_symbols; i++)
    {
      symbol = public->symbols + i;
      if (aiger_put_u (state, put, symbol->lit) == EOF ||
	  put (' ', state) == EOF ||
	  aiger_put_s (state, put, symbol->str) == EOF ||
	  put ('\n', state) == EOF)
	return 0;
    }

  return 1;
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
  
  if (!aiger_write_symbols (public, state, put))
    return 0;

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
      tmp = public->inputs[i];
      if (tmp > max)
	max = tmp;
    }

  for (i = 0; i < public->num_latches; i++)
    {
      tmp = public->latches[i];
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
  unsigned res, old, * top, child;
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

	      child = aiger_strip (node->rhs1);
	      literal = public->literals + child;
	      if (child >= 2 && !literal->input && !literal->onstack)
		{
		  assert (top < stack + 2 * public->num_nodes);
		  *top++ = child;
		}

	      child = aiger_strip (node->rhs0);
	      literal = public->literals + child;
	      if (child >= 2 && !literal->input && !literal->onstack)
		{
		  assert (top < stack + 2 * public->num_nodes);
		  *top++ = child;
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
      tmp = public->inputs[i];
      assert (!aiger_sign (tmp));
      if (tmp > res)
	res = tmp;
    }

  for (i = 0; i < public->num_latches; i++)
    {
      tmp = public->latches[i];
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
  aiger_symbol * symbol;
  aiger_node * node;
  char * str;

  size_code = public->max_literal + 1;
  NEWN (code, size_code);
  NEWN (stack, 2 * public->num_nodes);

  code[1] = 1;			/* not used actually */

  new = aiger_max_input_or_latch (public) + 2;

  for (i = 0; i < public->num_inputs; i++)
    {
      old = public->inputs[i];
      public->inputs[i] = aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_latches; i++)
    {
      old = public->latches[i];
      public->latches[i] = aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_latches; i++)
    {
      old = public->next[i];
      public->next[i] = aiger_reencode_lit (public, old, &new, code, stack);
    }

  for (i = 0; i < public->num_outputs; i++)
    {
      old = public->outputs[i];
      public->outputs[i] = aiger_reencode_lit (public, old, &new, code, stack);
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
    public->literals[i].node = 0;

  for (i = 0; i < public->num_nodes; i++)
    {
      node = public->nodes + i;
      public->literals[node->lhs].node = node;
      public->literals[aiger_not (node->lhs)].node = node;
    }

  j = 0;
  for (i = 0; i < public->num_symbols; i++)
    {
      symbol = public->symbols + i;
      new = code[symbol->lit];
      if (new)
	{
	  str = symbol->str;
	  symbol = public->symbols + j++;
	  symbol->lit = new;
	  symbol->str = str;
	}
      else
	aiger_delete_str (private, symbol->str);
    }
  public->num_symbols = j;

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
                     aiger_write_mode mode, void * state, aiger_put put)
{
  assert (mode == aiger_binary_write_mode || mode == aiger_ascii_write_mode);
  if (mode == aiger_ascii_write_mode)
    return aiger_write_ascii (public, state, put);
  else
    return aiger_write_binary (public, state, put);
}

int
aiger_write_to_file (aiger * public, aiger_write_mode mode, FILE * file)
{
  return aiger_write_generic (public,
                              mode, file, (aiger_put) aiger_default_put);
}

int
aiger_write_to_string (aiger * public, 
                       aiger_write_mode mode, char * str, size_t len)
{
  aiger_buffer buffer;
  int res;

  buffer.buffer = str;
  buffer.cursor = str;
  buffer.end_of_buffer = str + len;
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

#define GZIP "gzip -c > %s 2>/dev/null"

int
aiger_open_and_write_to_file (aiger * public, const char * file_name)
{
  IMPORT_private_FROM (public);
  aiger_write_mode mode;
  int res, pclose_file;
  char * cmd, size_cmd;
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
    mode = aiger_binary_write_mode;
  else
    mode = aiger_ascii_write_mode;

  res = aiger_write_to_file (public, mode, file);

  if (pclose_file)
    pclose (file);
  else
    fclose (file);

  return res;
}
