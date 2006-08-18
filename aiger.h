#ifndef aiger_h_INCLUDED
#define aiger_h_INCLUDED

#include <stdio.h>

typedef struct aiger aiger;
typedef struct aiger_node aiger_node;
typedef struct aiger_attribute aiger_attribute;
typedef struct aiger_symbol aiger_symbol;

enum aiger_type
{
  aiger_input = 1,
  aiger_output = 2,
  aiger_latch = 4,

  aiger_input_output = aiger_input | aiger_output,
  aiger_output_latch = aiger_output | aiger_latch,
};

typedef enum aiger_type aiger_type;

typedef void * (*aiger_malloc)(void * state, size_t);
typedef void * (*aiger_free)(void * state, void * ptr);

aiger * aiger_init (aiger_malloc,aiger_free);
void aiger_reset (aiger *);

void aiger_insert (aiger *, int pos_idx, aiger_type, void * external);

void aiger_attribute (aiger *, int pos_idx, const char *);
void aiger_symbol (aiger *, int pos_idx, const char *);

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
  aiger_attribute * attributes;
  aiger_attribute * symbols;
};

struct aiger 
{
  int max_idx;
  aiger_node * table;

  int num_inputs;
  int * inputs;

  int num_next_state_functions;
  int * next_state_functions;

  int num_outputs;
  int * outputs;
};

#endif
