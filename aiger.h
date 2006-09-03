#ifndef aiger_h_INCLUDED
#define aiger_h_INCLUDED

#include <stdio.h>

/*------------------------------------------------------------------------*/

#define aiger_version "0.1"

/*------------------------------------------------------------------------*/

typedef struct aiger aiger;
typedef struct aiger_and aiger_and;
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

/*------------------------------------------------------------------------*/
/* Each literal is associated to a variable having an unsigned index.  The
 * variable index is obtained by deviding the literal index by two, which is
 * the same as removing the sign bit.
 */
#define aiger_var2lit(i) \
  (((unsigned)(i)) << 1)

#define aiger_lit2var(l) \
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
  aiger_ascii_mode = 1,
  aiger_binary_mode = 2,
  aiger_compact_mode = 4,
  aiger_stripped_mode = 8,
};

typedef enum aiger_mode aiger_mode;

/*------------------------------------------------------------------------*/

struct aiger_and
{
  unsigned lhs;			/* as literal [2..2*max_idx], even */
  unsigned rhs0;		/* as literal [0..2*max_idx+1] */
  unsigned rhs1;		/* as literal [0..2*max_idx+1] */

  /* This field can be used by the client to build an AIG or for other
   * purposes. It is initialized by zero and is supposed to be under user
   * control.  There is no internal usage in the library.  After an AND is
   * created it can be written and is not changed until the library is reset
   * or reencoded.  Note that reencode is called during writing an AIG in
   * binary or compact format and thus client data is reset to zero.
   */
  void *client_data;
};

/*------------------------------------------------------------------------*/

struct aiger_symbol
{
  unsigned lit;
  unsigned next;		/* only valid for latches */
  char * name;
};

/*------------------------------------------------------------------------*/

struct aiger
{
  /* p [abc]ig m i l o a
   */
  unsigned maxvar;
  unsigned num_inputs;
  unsigned num_latches;
  unsigned num_outputs;
  unsigned num_ands;

  aiger_symbol *inputs;		/* [0..num_inputs[ */
  aiger_symbol *latches;	/* [0..num_latches[ */
  aiger_symbol *outputs;	/* [0..num_outputs[ */
  aiger_and * ands;		/* [0..num_ands[ */

  char ** comments;		/* zero terminated */
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
/* Treat the literal as input, output and latch respectively.  The literal
 * of latches and inputs can not be signed nor a constant (< 2).  You can
 * not register latches or inputs multiple times.  An input can not be a
 * latch.  The last argument is the symbolic name if non zero.
 */
void aiger_add_input (aiger *, unsigned lit, const char *);
void aiger_add_latch (aiger *, unsigned lit, unsigned next, const char *);
void aiger_add_output (aiger *, unsigned lit, const char *);

/*------------------------------------------------------------------------*/
/* Register an unsigned AND with AIGER.  The arguments are signed literals
 * as discussed above, e.g. the least significant bit stores the sign and
 * the remaining bit the (real) index.  The 'lhs' has to be unsigned (even).
 * It identifies the AND and can only registered once.  After registration
 * the AND can be accessed through 'ands[aiger_lit2idx (lhs)]'.
 */
void aiger_add_and (aiger *, unsigned lhs, unsigned rhs0, unsigned rhs1);

/*------------------------------------------------------------------------*/
/* Add a line of comments.  The comment may not contain a new line character.
 */
void aiger_add_comment (aiger *, const char * comment_line);

/*------------------------------------------------------------------------*/
/* This checks the consistency for debugging and testing purposes.
 */
const char * aiger_check (aiger *);

/*------------------------------------------------------------------------*/
/* These are the writer functions for AIGER.  They return zero on failure.
 * The assumptions on 'aiger_put' are the same as with 'fputc'.  Note, that
 * writing in binary mode triggers 'aig_reencode' and thus destroys the
 * and structure including client data.
 */
int aiger_write_to_file (aiger *, aiger_mode, FILE *);
int aiger_write_to_string (aiger *, aiger_mode, char *str, size_t len);
int aiger_write_generic (aiger *, aiger_mode, void *state, aiger_put);

/*------------------------------------------------------------------------*/
/* The following function allows to write to a file.  The write mode is
 * determined from the suffix in the file name.  The mode use is binary for
 * a '.big' suffix, compact mode for a '.cig' suffix and ASCII mode
 * otherwise.  In addition as further suffix '.gz' can be added which
 * requests the file to written by piping it through 'gzip'.  This feature
 * assumes that the 'gzip' program is in your path and can be executed
 * through 'popen'.
 */
int aiger_open_and_write_to_file (aiger *, const char * file_name);

/*------------------------------------------------------------------------*/
/* The binary format reencodes AND indices, since it requires the indices
 * to respect the child/parent relation, e.g. child indices will always be
 * smaller than their parent indices.   This function can directly be called
 * by the client.  As a side effect ANDs that are not in any cone of a next
 * state function nor in the cone of any output function are discarded.
 * The new indices of ANDs start immediately after the largest input and
 * latch index.  The data structures are updated accordingly including
 * 'max_literal'. The client data within ANDs is reset to zero.  If the
 * second argument is non zero the input indices and latch indices are moved
 * to a contiguous block which starts at index 2.
 */
void aiger_reencode (aiger *, int compact_inputs_and_latches);

/*------------------------------------------------------------------------*/
/* Read an AIG from a FILE a string or through a generic interface.  These
 * functions return a non zero error message if an error occurred and
 * otherwise 0.  The paramater 'aiger_get' has the same return values as
 * 'getc', e.g. it returns 'EOF' when done.  After an error occurred the
 * library becomes invalid.  Only 'aiger_reset' or 'aiger_error' can be
 * used.  The latter returns the previously returned error message.
 */
const char *aiger_read_from_file (aiger *, FILE *);
const char *aiger_read_from_string (aiger *, const char *str);
const char *aiger_read_generic (aiger *, void *state, aiger_get);

/*------------------------------------------------------------------------*/
/* Returns '0' if the library is in an invalid state.  After this function
 * returns a non zero error message, only 'aiger_reset' can be called
 * (beside 'aiger_error').
 */
const char * aiger_error (aiger *);

/*------------------------------------------------------------------------*/
/* Same semantics as with 'aiger_open_and_write_to_file'.
 */
const char * aiger_open_and_read_from_file (aiger *, const char *);

/*------------------------------------------------------------------------*/
/* Write symbol table or the comments to a file.  Result is zero on failure.
 */
int aiger_write_symbols_to_file (aiger *, FILE * file);
int aiger_write_comments_to_file (aiger *, FILE * file);

/*------------------------------------------------------------------------*/
/* Remove symbols and comments.  The result is the number of symbols
 * and comments removed.
 */
unsigned aiger_strip_symbols_and_comments (aiger *);

/*------------------------------------------------------------------------*/
/* If 'lit' is an input or a latch with a name, the symbolic name is
 * returned.   Note, that literals can be used for multiple outputs.
 * Therefore there is no way to associate a name with a literal itself.
 * Names for outputs are stored in the 'outputs' symbols.
 */
const char * aiger_get_symbol (aiger *, unsigned lit);

/*------------------------------------------------------------------------*/
/* Check whether the given unsigned, e.g. even, literal was defined as
 * 'input', 'latch' or 'and'.
 */
int aiger_is_input (aiger *, unsigned lit);
int aiger_is_latch (aiger *, unsigned lit);
int aiger_is_and (aiger *, unsigned lit);

#endif
