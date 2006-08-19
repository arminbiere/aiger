#ifndef aiger_h_INCLUDED
#define aiger_h_INCLUDED

#include <stdio.h>

typedef struct aiger aiger;
typedef struct aiger_node aiger_node;
typedef struct aiger_attribute aiger_attribute;
typedef struct aiger_symbol aiger_symbol;
typedef struct aiger_literal aiger_literal;

enum aiger_type
{
  aiger_input = 1,
  aiger_output = 2,
  aiger_latch = 4,

  aiger_input_output = aiger_input | aiger_output,
  aiger_output_latch = aiger_output | aiger_latch,
};

typedef enum aiger_type aiger_type;

#define aiger_false 0
#define aiger_true 1

#define aiger_not(l) \
  ((l)^1)

#define aiger_int2lit(i) \
  (((i) < 0) ? (2 * - (i)) + 1 : 2 * (i))

typedef void * (*aiger_malloc)(void * state, size_t);
typedef void (*aiger_free)(void * state, void * ptr, size_t);

aiger * aiger_init (void);
aiger * aiger_init_mem (void * mem_mgr, aiger_malloc, aiger_free);

void aiger_reset (aiger *);

aiger_node * aiger_new_node (aiger *, unsigned idx, aiger_type);

void aiger_add_attribute (aiger *, unsigned lit, const char *);
void aiger_add_symbol (aiger *, unsigned lit, const char *);

struct aiger_attribute
{
  char * str;
  aiger_attribute * next;
};

struct aiger_symbol
{
  char * str;
  aiger_symbol * next;
};

struct aiger_node
{
  unsigned lhs;
  unsigned rhs[2];
  void * client_data;
};

struct aiger_literal
{
  aiger_node * node;			/* shared with dual literal */
  aiger_attribute * attributes;		/* list of attributes */
  aiger_symbol * symbols;		/* list of symbols */
};

struct aiger 
{
  unsigned max_idx;
  aiger_literal ** literals;		/* [-max_idx..max_idx] */

  unsigned num_inputs;
  unsigned * inputs;

  unsigned num_next_state_functions;
  unsigned * next_state_functions;

  unsigned num_outputs;
  unsigned * outputs;
};

#endif
