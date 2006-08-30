#ifndef aiger_h_INCLUDED
#define aiger_h_INCLUDED

#include <stdio.h>

typedef struct aiger aiger;
typedef struct aiger_node aiger_node;
typedef struct aiger_literal aiger_literal;
typedef struct aiger_symbol aiger_symbol;

/*------------------------------------------------------------------------*/
/* AIG references are represented as unsigned integers and are called
 * literals.  The least significant bit is the sign.  The index of a literal
 * can be obtained by dividing the literal by two.  Only positive indices
 * are allowed, which leaves 0 for the boolean constant FALSE.  The boolean
 * constant TRUE is encoded as the unsigned number 1 accordingly.
 */
#define aiger_false 0
#define aiger_true 1

#define aiger_sign(l) \
  (((unsigned)(l))&1)

#define aiger_strip(l) \
  (((unsigned)(l))&~1)

#define aiger_not(l) \
  (((unsigned)(l))^1)

#define aiger_idx2lit(i) \
  (((unsigned)(i)) << 1)

#define aiger_lit2idx(l) \
  (((unsigned)(l)) >> 1)

/*------------------------------------------------------------------------*/
/* Callback functions for client memory management.  The 'free' wrapper will
 * get as last argument the size of the memory as it was allocated.
 */
typedef void *(*aiger_malloc) (void *mem_mgr, size_t);
typedef void (*aiger_free) (void *mem_mgr, void *ptr, size_t);

/*------------------------------------------------------------------------*/
/* Callback function for client character stream reading.  It returns an
 * ASCII character or EOF.  Thus is has the same semantics as the standard
 * library 'getc'.   See 'aiger_read_generic' for more details.
 */
typedef int (*aiger_get)(void * client_state);

/*------------------------------------------------------------------------*/
/* Callback function for client character stream writing.  The return value
 * is EOF iff writing failed and otherwise the character 'ch' casted to an
 * unsigned char.  It has therefore the same semantics as 'fputc' and 'putc'
 * from the standard library.
 */
typedef int (*aiger_put)(char ch, void * client_state);

/*------------------------------------------------------------------------*/

enum aiger_mode
{
  aiger_ascii_mode = 0,
  aiger_binary_mode = 1,
};

typedef enum aiger_mode aiger_mode;

/*------------------------------------------------------------------------*/

struct aiger_node
{
  unsigned lhs;			/* as literal [2..2*max_idx], even */
  unsigned rhs0;		/* as literal [0..2*max_idx+1] */
  unsigned rhs1;		/* as literal [0..2*max_idx+1] */

  /* This field can be used by the client to build an AIG.  It is
   * initialized by zero and is supposed to be under user control.  There is
   * no internal usage in the library.  After a node is created it can be
   * written and is not changed until the library is reset or reencoded.
   * Note that reencode is called when writing the AIG in binary format and
   * thus client data is reset to zero.
   */
  void *client_data;
};

/*------------------------------------------------------------------------*/

struct aiger_literal
{
  unsigned input : 1;		/* this literal is an input */
  unsigned latch : 1;		/* this literal is used as latch */
  unsigned client_bit : 1;	/* client bit semantics as client data */

  unsigned mark : 1;		/* internal usage only */
  unsigned onstack : 1;		/* internal usage only */

  aiger_node * node;		/* shared with negated literal */
};

/*------------------------------------------------------------------------*/

struct aiger_symbol
{
  unsigned lit;
  char * str;
};

/*------------------------------------------------------------------------*/

struct aiger
{
  unsigned max_literal;
  aiger_literal * literals;	/* [0..max_literal] */

  unsigned num_nodes;
  aiger_node * nodes;		/* [0..num_nodes[ */

  unsigned num_inputs;
  aiger_symbol *inputs;		/* [0..num_inputs[ */

  unsigned num_latches;
  aiger_symbol *latches;	/* [0..num_latches[ */
  unsigned *next;		/* [0..num_latches[ */

  unsigned num_outputs;
  aiger_symbol *outputs;	/* [0..num_outputs[ */
};

/*------------------------------------------------------------------------*/
/* You need to initialize the library first.  This generic initialization
 * functions uses standard 'malloc' and 'free' from the standard library for
 * memory management.
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
void aiger_add_and (aiger *, unsigned lhs, unsigned rhs0, unsigned rhs1);

/*------------------------------------------------------------------------*/
/* Treat the literal as input, output and latch respectively.  The literal
 * of latches and inputs can not be signed nor a constant (<= 2).  You can
 * not register latches or inputs multiple times.  An input can not be a
 * latch.  The last argument is the symbolic name if non zero.
 */
void aiger_add_input (aiger *, unsigned lit, const char *);
void aiger_add_output (aiger *, unsigned lit, const char *);
void aiger_add_latch (aiger *, unsigned lit, unsigned next, const char *);

/*------------------------------------------------------------------------*/
/* This checks the consistency for debugging and testing purposes.
 */
const char * aiger_check (aiger *);

/*------------------------------------------------------------------------*/
/* These are the writer functions for AIGER.  They return zero on failure.
 * The assumptions on 'aiger_put' are the same as with 'fputc'.  Note, that
 * writing in binary mode triggers 'aig_reencode' and thus destroys the
 * node structure including client data.
 */
int aiger_write_to_file (aiger *, aiger_mode, FILE *);
int aiger_write_to_string (aiger *, aiger_mode, char *str, size_t len);
int aiger_write_generic (aiger *, aiger_mode, void *state, aiger_put);

/*------------------------------------------------------------------------*/
/* The following function allows to write to a file.  The write mode is
 * determined from the suffix in the file name.  If it is '.big' binary mode
 * is used, otherwise ASCII mode.  In addition the suffix '.gz' can be
 * added which requests the file to written by piping it through 'gzip'.
 * This feature assumes that the 'gzip' program is in your path and can be
 * executed through 'popen'.
 */
int aiger_open_and_write_to_file (aiger *, const char * file_name);

/*------------------------------------------------------------------------*/
/* The binary format reencodes node indices, since it requires the indices
 * to respect the child/parent relation, e.g. child indices will always be
 * smaller than their parent indices.   This function can directly be called
 * by the client.  As a side effect nodes that are not in any cone of a next
 * state function nor in the cone of any output function are discarded.
 * The new indices of nodes start immediately after the largest input and
 * latch index.  The data structures are updated accordingly including
 * 'max_literal'. The client data in nodes is reset to zero.
 */
void aiger_reencode (aiger *);

/*------------------------------------------------------------------------*/
/* Read an AIG from a FILE a string or through a generic interface.  These
 * functions return a non zero error message if an error occurred and
 * otherwise 0.  The paramater 'aiger_get' has the same return values as
 * 'getc', e.g. it returns 'EOF' when done.
 */
const char *aiger_read_from_file (aiger *, FILE *);
const char *aiger_read_from_string (aiger *, const char *str);
const char *aiger_read_generic (aiger *, void *state, aiger_get);

/*------------------------------------------------------------------------*/
/* Same semantics as with 'aiger_open_and_write_to_file'.
 */
const char * aiger_open_and_read_from_file (aiger *, const char *);

/*------------------------------------------------------------------------*/
/* Write symbol table to file.
 */
int aiger_write_symbols_to_file (aiger *, FILE * file);

#endif
