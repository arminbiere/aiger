/***************************************************************************
Copyright (c) 2006-2018, Armin Biere, Johannes Kepler University.

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

/*------------------------------------------------------------------------*/
/* This file contains the API of the 'SimpAIG' library, which is a simple
 * implementation of an AIG data structure.  The code of the library 
 * consists of 'simpaig.c' and 'simpaig.h' and is independent of the 'AIGER'
 * library.
 */
#ifndef simpaig_h_INCLUDED
#define simpaig_h_INCLUDED

#include <stdlib.h>		/* for 'size_t' */

typedef struct simpaigmgr simpaigmgr;
typedef struct simpaig simpaig;

typedef void *(*simpaig_malloc) (void *mem_mgr, size_t);
typedef void (*simpaig_free) (void *mem_mgr, void *ptr, size_t);

simpaigmgr *simpaig_init (void);
simpaigmgr *simpaig_init_mem (void *mem_mgr, simpaig_malloc, simpaig_free);
void simpaig_reset (simpaigmgr *);

int simpaig_isfalse (const simpaig *);
int simpaig_istrue (const simpaig *);
int simpaig_signed (const simpaig *);
void *simpaig_isvar (const simpaig *);
int simpaig_slice (const simpaig *);
int simpaig_isand (const simpaig *);

/* The following functions do not give  back a new reference.  The reference
 * is shared with the argument.
 */
simpaig *simpaig_strip (simpaig *);
simpaig *simpaig_not (simpaig *);
simpaig *simpaig_child (simpaig *, int child);

/* The functions below this point will all return a new reference, if they
 * return a 'simpaig *'.  The user should delete the returned aig with
 * 'simpaig_dec', if memory is scarce.
 */
simpaig *simpaig_false (simpaigmgr *);
simpaig *simpaig_true (simpaigmgr *);

/* A variable in 'SimpAIG' consists of an arbitrary external pointer and a
 * time offset the time 'slice'.  This is useful for time frame expansion.
 * If only combinational problems are modelled, then slice should be set 0.
 */
simpaig *simpaig_var (simpaigmgr *, void *var, int slice);

simpaig *simpaig_and (simpaigmgr *, simpaig * a, simpaig * b);
simpaig *simpaig_or (simpaigmgr *, simpaig * a, simpaig * b);
simpaig *simpaig_implies (simpaigmgr *, simpaig * a, simpaig * b);
simpaig *simpaig_xor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig *simpaig_xnor (simpaigmgr *, simpaig * a, simpaig * b);
simpaig *simpaig_ite (simpaigmgr *, simpaig * c, simpaig * t, simpaig * e);

/* Increment and decrement reference counts.
 */
simpaig *simpaig_inc (simpaigmgr *, simpaig *);
void simpaig_dec (simpaigmgr *, simpaig *);

/* With 'simpaig_substitute' a set of variables ('lhs') is replaced
 * recursively by AIGs ('rhs').  The mapping of left hand sides to right
 * hand sides is given by multiple calls to 'simpaig_assign' before calling
 * 'simpaig_substitute'.  The substitution is performed recursively in such
 * a way that all left hand sides are eliminated.   Therefore the
 * assignments have to be acyclic.  When  'simpaig_substitute' returns the
 * assignment is reset.
 */
void simpaig_assign (simpaigmgr *, simpaig * lhs, simpaig * rhs);
simpaig *simpaig_substitute (simpaigmgr *, simpaig *);

/* Replace (in place) all the AIGs in the array 'a' of size 'n' recursively
 * and in parallel.  The reference counts of the given AIGs are decremented
 * and thos of the resulting ones after substitution are incremented.
 */
void simpaig_substitute_parallel (simpaigmgr *, simpaig ** a, unsigned n);

/* Shift the time by 'delta' which in essence replaces every variable by a
 * time shifted copy.
 */
simpaig *simpaig_shift (simpaigmgr *, simpaig *, int delta);

/* This function will recursively assign tseitin indices to all nodes and
 * variables of the AIG.  The return value is the maximum tseitin index
 * allocated, starting with 0 for the FALSE node.  Tseitin indices are
 * shared across multiple calls to 'simpaig_assign_indices' as long
 * 'simpaig_reset_indices' is not called.  This also means that references
 * to this nodes which are indexed have to be maintained.  So it is always
 * a good idea to reset the indices after they are not used anymore.
 */
void simpaig_assign_indices (simpaigmgr *, simpaig *);
void simpaig_reset_indices (simpaigmgr *);

/* Return the 'unsigned' tseitin index of an unsigned AIG and the maximal
 * valid tseitin index respectively.
 */
unsigned simpaig_index (simpaig *);
unsigned simpaig_max_index (simpaigmgr *);

/* There are two ways to obtain signed tseitin indices.  In the first
 * version we use signed numbers.  A negative number denotes a negated node.
 * This is is how literals are defined in the DIMACS format.  Note that the
 * absolute value of the result is the unsigned tseitin index returned by
 * 'simpaig_index' plus one.  All indices are different from zero.
 * Otherwise it would be problematic to distinguish FALSE from TRUE.  FALSE
 * has '1' as int index and TRUE '-1'.
 */
int simpaig_int_index (simpaig *);

/* The second type of signed tseitin indices uses the least significant to
 * store the sign as in the AIGER format.  FALSE has '0' as unsigned index
 * and TRUE '1'.
 */
unsigned simpaig_unsigned_index (simpaig *);

/* The number of nodes still alive.
 */
unsigned simpaig_current_nodes (simpaigmgr *);

#endif
