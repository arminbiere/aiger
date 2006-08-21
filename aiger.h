#ifndef aiger_h_INCLUDED
#define aiger_h_INCLUDED

#include <stdio.h>

typedef struct aiger aiger;
typedef struct aiger_node aiger_node;
typedef struct aiger_string aiger_string;
typedef struct aiger_literal aiger_literal;

/*------------------------------------------------------------------------*/
/* AIG references are represented as unsigned integers and are called
 * literals.  The least significant bit is the sign.  The index of a literal
 * can be obtained by dividing the literal by two.  Only positive indices
 * are allowed, which leaves 0 for the boolean constant FALSE.
 */
#define aiger_false 0
#define aiger_true 1

#define aiger_sign(l) \
  (((unsigned)(l))&1)

#define aiger_not(l) \
  (((unsigned)(l))^1)

#define aiger_idx2lit(i) \
  (((unsigned)(i)) << 1)

#define aiger_lit2idx(l) \
  (((unsigned)(l)) >> 1)

/*------------------------------------------------------------------------*/
/* Wrapper for client memory management.  The 'free' wrapper will get as
 * last argument the size of the memory as it was allocated.
 */
typedef void *(*aiger_malloc) (void *mem_mgr, size_t);

typedef void (*aiger_free) (void *mem_mgr, void *ptr, size_t);

/*------------------------------------------------------------------------*/

struct aiger_string
{
  char *str;
  aiger_string *next;
};

struct aiger_node
{
  unsigned lhs;			/* as literal [2..2*max_idx], even */
  unsigned rhs0;		/* as literal [0..2*max_idx+1] */
  unsigned rhs1;		/* as literal [0..2*max_idx+1] */
  void *client_data;		/* no internal use */
};

struct aiger_literal
{
  aiger_string *attributes;	/* list of attributes */
  aiger_string *symbols;	/* list of symbols */
};

struct aiger
{
  unsigned max_idx;
  aiger_literal **literals;	/* [0..2*max_idx + 1] */
  aiger_node **nodes;		/* [0..max_idx] */

  unsigned num_inputs;
  unsigned *inputs;		/* [0..num_inputs[ */

  unsigned num_latches;
  unsigned *latches;		/* [0..num_latches[ */
  unsigned *next;		/* [0..num_latches[ */

  unsigned num_outputs;
  unsigned *outputs;		/* [0..num_outputs[ */
};

/*------------------------------------------------------------------------*/
/* You need to initialize the library first.
 */
aiger *aiger_init (void);

/*------------------------------------------------------------------------*/
/* Same as previous initialization function except that a memory manager
 * from the client is used for memory allocation.
 */
aiger *aiger_init_mem (void *mem_mgr, aiger_malloc, aiger_free);

/*------------------------------------------------------------------------*/

void aiger_reset (aiger *);

/*------------------------------------------------------------------------*/
/* Register and unsigned AIG node with AIGER.  The arguments are signed
 * literals as discussed above, e.g. the least significant bit stores the
 * sign and the remaining bit the (real) index.  The 'lhs' has to be
 * unsigned (even).  It identifies the node and can only registered once.
 * After registration the node can be accessed through 
 * 'nodes[aiger_lit2idx (lhs)]'.
 */
void aiger_and (aiger *, unsigned lhs, unsigned rhs0, unsigned rhs1);

/*------------------------------------------------------------------------*/
/* Treat the literal as input, output and latch respectively.  The literal
 * of latches and inputs can not be signed.  You can not register latches
 * multiple times.  An input can not be a latch.
 */
void aiger_input (aiger *, unsigned lit);
void aiger_output (aiger *, unsigned lit);
void aiger_latch (aiger *, unsigned lit, unsigned next);

/*------------------------------------------------------------------------*/
/* Add a string as symbolic name or attribute to a literal.  You can only
 * associate multiple attributes with a literal.  A literal can have at most
 * one symbol.
 */
void aiger_add_attribute (aiger *, unsigned lit, const char *);
void aiger_add_symbol (aiger *, unsigned lit, const char *);

/*------------------------------------------------------------------------*/
/* Read an AIG from a FILE a string or through a generic interface.  These
 * functions return a non zero error message if an error occurred and
 * otherwise 0.  The function 'get' has the same return values as 'getc',
 * e.g. it returns 'EOF' when done.
 */
const char *aiger_read_from_file (aiger *, FILE *);
const char *aiger_read_from_string (aiger *, const char *str);
const char *aiger_read_generic (aiger *, void *state, int (*get) (void *));

/*------------------------------------------------------------------------*/
/* These are the writer functions for AIGER.  They return zero on failure.
 * The same applies to 'put'.
 */
int aiger_write_to_file (aiger *, FILE *);
int aiger_write_to_string (aiger *, char *str, size_t bytes);
int aiger_write_generic (aiger *, void *state, int (*put) (void *, char));

#endif
