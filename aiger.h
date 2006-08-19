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

#define aiger_undefined 0
#define aiger_false -1
#define aiger_true 1

typedef void * (*aiger_malloc)(void * state, size_t);
typedef void (*aiger_free)(void * state, void * ptr, size_t);

aiger * aiger_init (void);
aiger * aiger_init_mem (void * mem_mgr, aiger_malloc, aiger_free);

void aiger_reset (aiger *);

aiger_node * aiger_new_node (aiger *, int idx, aiger_type);

void aiger_add_attribute (aiger *, int lit, const char *);
void aiger_add_symbol (aiger *, int lit, const char *);

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
  int lhs;
  int rhs[2];
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
  int max_idx;
  aiger_literal ** literals;		/* [-max_idx..max_idx] */

  int num_inputs;
  int * inputs;

  int num_next_state_functions;
  int * next_state_functions;

  int num_outputs;
  int * outputs;
};

#endif
