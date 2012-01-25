/***************************************************************************
Copyright (c) 2012, Fabio Somenzi, University of Colorado.
Copyright (c) 2006-2011, Armin Biere, Johannes Kepler University.
Copyright (c) 2006, Marc Herbstritt, University of Freiburg.
Copyright (c) 1995-2004, Regents of the University of Colorado.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

Neither the name of the authors, nor the names of contributors, nor the name
of the copyright holders, including the name of the Johannes Kepler
University, the name of the University of Freiburg, or the name of the
University of Colorado may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/

/*------------------------------------------------------------------------*/
/* The BLIF parsing routines are borrowed from CUDD and are slightly      */
/* adjusted to AIGER. The files are maintained in directory cudd_util.    */
/* Some CUDD routines that were initially coupled with the CUDD BDD       */
/* manager are adapted to AIGER, see below.                               */
/*------------------------------------------------------------------------*/

#include "aiger.h"

/*------------------------------------------------------------------------*/
/* The following code is taken from CUDD 2.4.1. It is mainly a cut&paste  */
/* of util.h (and corresponding .c-files), st.[h|c], and bnet.[h|c].      */
/*------------------------------------------------------------------------*/

/*--CUDD::util::begin-----------------------------------------------------*/

#define NIL(type)		((type *) 0)

#define ALLOC(type, num)	\
    ((type *) malloc((long) sizeof(type) * (long) (num)))

#define REALLOC(type, obj, num)	\
    ((type *) realloc((char *) (obj), (long) sizeof(type) * (long) (num)))

#define FREE(obj)		\
    ((obj) ? (free((char *) (obj)), (obj) = 0) : 0)

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ABS(a)			((a) < 0 ? -(a) : (a))
#define MAX(a,b)		((a) > (b) ? (a) : (b))
#define MIN(a,b)		((a) < (b) ? (a) : (b))

/*--CUDD::util::end-------------------------------------------------------*/

/*--CUDD::st::begin-------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* Constant declarations                                                     */
/*---------------------------------------------------------------------------*/

#define ST_DEFAULT_MAX_DENSITY 5
#define ST_DEFAULT_INIT_TABLE_SIZE 11
#define ST_DEFAULT_GROW_FACTOR 2.0
#define ST_DEFAULT_REORDER_FLAG 0
#define ST_OUT_OF_MEM -10000

/*---------------------------------------------------------------------------*/
/* Type declarations                                                         */
/*---------------------------------------------------------------------------*/

typedef struct st_table_entry st_table_entry;
struct st_table_entry
{
  char *key;
  char *record;
  st_table_entry *next;
};

typedef struct st_table st_table;
struct st_table
{
  int (*compare) (const char *, const char *);
  int (*hash) (char *, int);
  int num_bins;
  int num_entries;
  int max_density;
  int reorder_flag;
  double grow_factor;
  st_table_entry **bins;
};

typedef struct st_generator st_generator;
struct st_generator
{
  st_table *table;
  st_table_entry *entry;
  int index;
};

enum st_retval
{ ST_CONTINUE, ST_STOP, ST_DELETE };

typedef enum st_retval (*ST_PFSR) (char *, char *, char *);

typedef int (*ST_PFICPCP) (const char *, const char *);	/* type for comparison function */

typedef int (*ST_PFICPI) (char *, int);	/* type for hash function */

/**AutomaticStart*************************************************************/

/*---------------------------------------------------------------------------*/
/* Function prototypes                                                       */
/*---------------------------------------------------------------------------*/

extern st_table *st_init_table_with_params (ST_PFICPCP, ST_PFICPI, int, int,
					    double, int);
extern st_table *st_init_table (ST_PFICPCP, ST_PFICPI);
extern void st_free_table (st_table *);
extern int st_lookup (st_table *, void *, void *);
extern int st_lookup_int (st_table *, void *, int *);
extern int st_insert (st_table *, void *, void *);
extern int st_add_direct (st_table *, void *, void *);
extern int st_find_or_add (st_table *, void *, void *);
extern int st_find (st_table *, void *, void *);
extern st_table *st_copy (st_table *);
extern int st_delete (st_table *, void *, void *);
extern int st_delete_int (st_table *, void *, int *);
extern int st_foreach (st_table *, ST_PFSR, char *);
extern int st_strhash (char *, int);
extern int st_numhash (char *, int);
extern int st_ptrhash (char *, int);
extern int st_numcmp (const char *, const char *);
extern int st_ptrcmp (const char *, const char *);
extern st_generator *st_init_gen (st_table *);
extern int st_gen (st_generator *, void *, void *);
extern int st_gen_int (st_generator *, void *, int *);
extern void st_free_gen (st_generator *);

/**AutomaticEnd***************************************************************/

/**CFile***********************************************************************

  FileName    [st.c]

  PackageName [st]

  Synopsis    [Symbol table package.]

  Description [The st library provides functions to create, maintain,
  and query symbol tables.]

  SeeAlso     []

  Author      []

  Copyright   []

******************************************************************************/

#define ST_NUMCMP(x,y) ((x) != (y))

#define ST_NUMHASH(x,size) (ABS((long)x)%(size))

#define st_shift ((sizeof (void*) == 8) ? 3 : 2)

#define ST_PTRHASH(x,size) ((unsigned int)((unsigned long)(x)>>st_shift)%size)

#define EQUAL(func, x, y) \
    ((((func) == st_numcmp) || ((func) == st_ptrcmp)) ?\
      (ST_NUMCMP((x),(y)) == 0) : ((*func)((x), (y)) == 0))

#define do_hash(key, table)\
    ((int)((table->hash == st_ptrhash) ? ST_PTRHASH((char *)(key),(table)->num_bins) :\
     (table->hash == st_numhash) ? ST_NUMHASH((char *)(key), (table)->num_bins) :\
     (*table->hash)((char *)(key), (table)->num_bins)))

#define PTR_NOT_EQUAL(table, ptr, user_key)\
(ptr != NIL(st_table_entry) && !EQUAL(table->compare, (char *)user_key, (ptr)->key))

#define FIND_ENTRY(table, hash_val, key, ptr, last) \
    (last) = &(table)->bins[hash_val];\
    (ptr) = *(last);\
    while (PTR_NOT_EQUAL((table), (ptr), (key))) {\
	(last) = &(ptr)->next; (ptr) = *(last);\
    }\
    if ((ptr) != NIL(st_table_entry) && (table)->reorder_flag) {\
	*(last) = (ptr)->next;\
	(ptr)->next = (table)->bins[hash_val];\
	(table)->bins[hash_val] = (ptr);\
    }

/**AutomaticStart*************************************************************/

/*---------------------------------------------------------------------------*/
/* Static function prototypes                                                */
/*---------------------------------------------------------------------------*/

static int rehash (st_table *);

/**AutomaticEnd***************************************************************/


/*---------------------------------------------------------------------------*/
/* Definition of exported functions                                          */
/*---------------------------------------------------------------------------*/

/**Function********************************************************************

  Synopsis    [Create and initialize a table.]

  Description [Create and initialize a table with the comparison function
  compare_fn and hash function hash_fn. compare_fn is
  <pre>
	int compare_fn(const char *key1, const char *key2)
  </pre>
  It returns <,=,> 0 depending on whether key1 <,=,> key2 by some measure.<p>
  hash_fn is
  <pre>
	int hash_fn(char *key, int modulus)
  </pre>
  It returns a integer between 0 and modulus-1 such that if
  compare_fn(key1,key2) == 0 then hash_fn(key1) == hash_fn(key2).<p>
  There are five predefined hash and comparison functions in st.
  For keys as numbers:
  <pre>
	 st_numhash(key, modulus) { return (unsigned int) key % modulus; }
  </pre>
  <pre>
	 st_numcmp(x,y) { return (int) x - (int) y; }
  </pre>
  For keys as pointers:
  <pre>
	 st_ptrhash(key, modulus) { return ((unsigned int) key/4) % modulus }
  </pre>
  <pre>
	 st_ptrcmp(x,y) { return (int) x - (int) y; }
  </pre>
  For keys as strings:
  <pre>
         st_strhash(x,y) - a reasonable hashing function for strings
  </pre>
  <pre>
	 strcmp(x,y) - the standard library function
  </pre>
  It is recommended to use these particular functions if they fit your 
  needs, since st will recognize certain of them and run more quickly
  because of it.]

  SideEffects [None]

  SeeAlso     [st_init_table_with_params st_free_table]

******************************************************************************/
st_table *
st_init_table (ST_PFICPCP compare, ST_PFICPI hash)
{
  return st_init_table_with_params (compare, hash, ST_DEFAULT_INIT_TABLE_SIZE,
				    ST_DEFAULT_MAX_DENSITY,
				    ST_DEFAULT_GROW_FACTOR,
				    ST_DEFAULT_REORDER_FLAG);

}				/* st_init_table */


/**Function********************************************************************

  Synopsis    [Create a table with given parameters.]

  Description [The full blown table initializer.  compare and hash are
  the same as in st_init_table. density is the largest the average
  number of entries per hash bin there should be before the table is
  grown.  grow_factor is the factor the table is grown by when it
  becomes too full. size is the initial number of bins to be allocated
  for the hash table.  If reorder_flag is non-zero, then every time an
  entry is found, it is moved to the top of the chain.<p>
  st_init_table(compare, hash) is equivelent to
  <pre>
  st_init_table_with_params(compare, hash, ST_DEFAULT_INIT_TABLE_SIZE,
			    ST_DEFAULT_MAX_DENSITY,
			    ST_DEFAULT_GROW_FACTOR,
			    ST_DEFAULT_REORDER_FLAG);
  </pre>
  ]

  SideEffects [None]

  SeeAlso     [st_init_table st_free_table]

******************************************************************************/
st_table *
st_init_table_with_params (ST_PFICPCP compare,
			   ST_PFICPI hash,
			   int size,
			   int density, double grow_factor, int reorder_flag)
{
  int i;
  st_table *newt;

  newt = ALLOC (st_table, 1);
  if (newt == NIL (st_table))
    {
      return NIL (st_table);
    }
  newt->compare = compare;
  newt->hash = hash;
  newt->num_entries = 0;
  newt->max_density = density;
  newt->grow_factor = grow_factor;
  newt->reorder_flag = reorder_flag;
  if (size <= 0)
    {
      size = 1;
    }
  newt->num_bins = size;
  newt->bins = ALLOC (st_table_entry *, size);
  if (newt->bins == NIL (st_table_entry *))
    {
      FREE (newt);
      return NIL (st_table);
    }
  for (i = 0; i < size; i++)
    {
      newt->bins[i] = 0;
    }
  return newt;

}				/* st_init_table_with_params */


/**Function********************************************************************

  Synopsis    [Free a table.]

  Description [Any internal storage associated with table is freed.
  It is the user's responsibility to free any storage associated
  with the pointers he placed in the table (by perhaps using
  st_foreach).]

  SideEffects [None]

  SeeAlso     [st_init_table st_init_table_with_params]

******************************************************************************/
void
st_free_table (st_table * table)
{
  st_table_entry *ptr, *next;
  int i;

  for (i = 0; i < table->num_bins; i++)
    {
      ptr = table->bins[i];
      while (ptr != NIL (st_table_entry))
	{
	  next = ptr->next;
	  FREE (ptr);
	  ptr = next;
	}
    }
  FREE (table->bins);
  FREE (table);

}				/* st_free_table */


/**Function********************************************************************

  Synopsis    [Lookup up `key' in `table'.]

  Description [Lookup up `key' in `table'. If an entry is found, 1 is
  returned and if `value' is not nil, the variable it points to is set
  to the associated value.  If an entry is not found, 0 is returned
  and the variable pointed by value is unchanged.]

  SideEffects [None]

  SeeAlso     [st_lookup_int]

******************************************************************************/
int
st_lookup (st_table * table, void *key, void *value)
{
  int hash_val;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      return 0;
    }
  else
    {
      if (value != NIL (void))
	{
	  *(char **) value = ptr->record;
	}
      return 1;
    }

}				/* st_lookup */


/**Function********************************************************************

  Synopsis    [Lookup up `key' in `table'.]

  Description [Lookup up `key' in `table'.  If an entry is found, 1 is
  returned and if `value' is not nil, the variable it points to is
  set to the associated integer value.  If an entry is not found, 0 is
  return and the variable pointed by `value' is unchanged.]

  SideEffects [None]

  SeeAlso     [st_lookup]

******************************************************************************/
int
st_lookup_int (st_table * table, void *key, int *value)
{
  int hash_val;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      return 0;
    }
  else
    {
      if (value != NIL (int))
	{
	  *value = (int) (long) ptr->record;
	}
      return 1;
    }

}				/* st_lookup_int */


/**Function********************************************************************

  Synopsis    [Insert value in table under the key 'key'.]

  Description [Insert value in table under the key 'key'.  Returns 1
  if there was an entry already under the key; 0 if there was no entry
  under the key and insertion was successful; ST_OUT_OF_MEM otherwise.
  In either of the first two cases the new value is added.]

  SideEffects [None]

  SeeAlso     []

******************************************************************************/
int
st_insert (st_table * table, void *key, void *value)
{
  int hash_val;
  st_table_entry *newt;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      if (table->num_entries / table->num_bins >= table->max_density)
	{
	  if (rehash (table) == ST_OUT_OF_MEM)
	    {
	      return ST_OUT_OF_MEM;
	    }
	  hash_val = do_hash (key, table);
	}
      newt = ALLOC (st_table_entry, 1);
      if (newt == NIL (st_table_entry))
	{
	  return ST_OUT_OF_MEM;
	}
      newt->key = (char *) key;
      newt->record = (char *) value;
      newt->next = table->bins[hash_val];
      table->bins[hash_val] = newt;
      table->num_entries++;
      return 0;
    }
  else
    {
      ptr->record = (char *) value;
      return 1;
    }

}				/* st_insert */


/**Function********************************************************************

  Synopsis    [Place 'value' in 'table' under the key 'key'.]

  Description [Place 'value' in 'table' under the key 'key'.  This is
  done without checking if 'key' is in 'table' already.  This should
  only be used if you are sure there is not already an entry for
  'key', since it is undefined which entry you would later get from
  st_lookup or st_find_or_add. Returns 1 if successful; ST_OUT_OF_MEM
  otherwise.]

  SideEffects [None]

  SeeAlso     []

******************************************************************************/
int
st_add_direct (st_table * table, void *key, void *value)
{
  int hash_val;
  st_table_entry *newt;

  hash_val = do_hash (key, table);
  if (table->num_entries / table->num_bins >= table->max_density)
    {
      if (rehash (table) == ST_OUT_OF_MEM)
	{
	  return ST_OUT_OF_MEM;
	}
    }
  hash_val = do_hash (key, table);
  newt = ALLOC (st_table_entry, 1);
  if (newt == NIL (st_table_entry))
    {
      return ST_OUT_OF_MEM;
    }
  newt->key = (char *) key;
  newt->record = (char *) value;
  newt->next = table->bins[hash_val];
  table->bins[hash_val] = newt;
  table->num_entries++;
  return 1;

}				/* st_add_direct */


/**Function********************************************************************

  Synopsis    [Lookup `key' in `table'.]

  Description [Lookup `key' in `table'.  If not found, create an
  entry.  In either case set slot to point to the field in the entry
  where the value is stored.  The value associated with `key' may then
  be changed by accessing directly through slot.  Returns 1 if an
  entry already existed, 0 if it did not exist and creation was
  successful; ST_OUT_OF_MEM otherwise.  As an example:
  <pre>
      char **slot;
  </pre>
  <pre>
      char *key;
  </pre>
  <pre>
      char *value = (char *) item_ptr <-- ptr to a malloc'd structure
  </pre>
  <pre>
      if (st_find_or_add(table, key, &slot) == 1) {
  </pre>
  <pre>
	 FREE(*slot); <-- free the old value of the record
  </pre>
  <pre>
      }
  </pre>
  <pre>
      *slot = value;  <-- attach the new value to the record
  </pre>
  This replaces the equivelent code:
  <pre>
      if (st_lookup(table, key, &ovalue) == 1) {
  </pre>
  <pre>
         FREE(ovalue);
  </pre>
  <pre>
      }
  </pre>
  <pre>
      st_insert(table, key, value);
  </pre>
  ]

  SideEffects [None]

  SeeAlso     [st_find]

******************************************************************************/
int
st_find_or_add (st_table * table, void *key, void *slot)
{
  int hash_val;
  st_table_entry *newt, *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      if (table->num_entries / table->num_bins >= table->max_density)
	{
	  if (rehash (table) == ST_OUT_OF_MEM)
	    {
	      return ST_OUT_OF_MEM;
	    }
	  hash_val = do_hash (key, table);
	}
      newt = ALLOC (st_table_entry, 1);
      if (newt == NIL (st_table_entry))
	{
	  return ST_OUT_OF_MEM;
	}
      newt->key = (char *) key;
      newt->record = (char *) 0;
      newt->next = table->bins[hash_val];
      table->bins[hash_val] = newt;
      table->num_entries++;
      if (slot != NIL (void))
	 *(char ***) slot = &newt->record;
      return 0;
    }
  else
    {
      if (slot != NIL (void))
	 *(char ***) slot = &ptr->record;
      return 1;
    }

}				/* st_find_or_add */


/**Function********************************************************************

  Synopsis    [Lookup `key' in `table'.]

  Description [Like st_find_or_add, but does not create an entry if
  one is not found.]

  SideEffects [None]

  SeeAlso     [st_find_or_add]

******************************************************************************/
int
st_find (st_table * table, void *key, void *slot)
{
  int hash_val;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      return 0;
    }
  else
    {
      if (slot != NIL (void))
	{
	  *(char ***) slot = &ptr->record;
	}
      return 1;
    }

}				/* st_find */


/**Function********************************************************************

  Synopsis    [Return a copy of old_table and all its members.]

  Description [Return a copy of old_table and all its members.
  (st_table *) 0 is returned if there was insufficient memory to do
  the copy.]

  SideEffects [None]

  SeeAlso     []

******************************************************************************/
st_table *
st_copy (st_table * old_table)
{
  st_table *new_table;
  st_table_entry *ptr, *newptr, *next, *newt;
  int i, j, num_bins = old_table->num_bins;

  new_table = ALLOC (st_table, 1);
  if (new_table == NIL (st_table))
    {
      return NIL (st_table);
    }

  *new_table = *old_table;
  new_table->bins = ALLOC (st_table_entry *, num_bins);
  if (new_table->bins == NIL (st_table_entry *))
    {
      FREE (new_table);
      return NIL (st_table);
    }
  for (i = 0; i < num_bins; i++)
    {
      new_table->bins[i] = NIL (st_table_entry);
      ptr = old_table->bins[i];
      while (ptr != NIL (st_table_entry))
	{
	  newt = ALLOC (st_table_entry, 1);
	  if (newt == NIL (st_table_entry))
	    {
	      for (j = 0; j <= i; j++)
		{
		  newptr = new_table->bins[j];
		  while (newptr != NIL (st_table_entry))
		    {
		      next = newptr->next;
		      FREE (newptr);
		      newptr = next;
		    }
		}
	      FREE (new_table->bins);
	      FREE (new_table);
	      return NIL (st_table);
	    }
	  *newt = *ptr;
	  newt->next = new_table->bins[i];
	  new_table->bins[i] = newt;
	  ptr = ptr->next;
	}
    }
  return new_table;

}				/* st_copy */


/**Function********************************************************************

  Synopsis    [Delete the entry with the key pointed to by `keyp'.]

  Description [Delete the entry with the key pointed to by `keyp'.  If
  the entry is found, 1 is returned, the variable pointed by `keyp' is
  set to the actual key and the variable pointed by `value' is set to
  the corresponding entry.  (This allows the freeing of the associated
  storage.)  If the entry is not found, then 0 is returned and nothing
  is changed.]

  SideEffects [None]

  SeeAlso     [st_delete_int]

******************************************************************************/
int
st_delete (st_table * table, void *keyp, void *value)
{
  int hash_val;
  char *key = *(char **) keyp;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      return 0;
    }

  *last = ptr->next;
  if (value != NIL (void))
     *(char **) value = ptr->record;
  *(char **) keyp = ptr->key;
  FREE (ptr);
  table->num_entries--;
  return 1;

}				/* st_delete */


/**Function********************************************************************

  Synopsis    [Delete the entry with the key pointed to by `keyp'.]

  Description [Delete the entry with the key pointed to by `keyp'.
  `value' must be a pointer to an integer.  If the entry is found, 1
  is returned, the variable pointed by `keyp' is set to the actual key
  and the variable pointed by `value' is set to the corresponding
  entry.  (This allows the freeing of the associated storage.) If the
  entry is not found, then 0 is returned and nothing is changed.]

  SideEffects [None]

  SeeAlso     [st_delete]

******************************************************************************/
int
st_delete_int (st_table * table, void *keyp, int *value)
{
  int hash_val;
  char *key = *(char **) keyp;
  st_table_entry *ptr, **last;

  hash_val = do_hash (key, table);

  FIND_ENTRY (table, hash_val, key, ptr, last);

  if (ptr == NIL (st_table_entry))
    {
      return 0;
    }

  *last = ptr->next;
  if (value != NIL (int))
     *value = (int) (long) ptr->record;
  *(char **) keyp = ptr->key;
  FREE (ptr);
  table->num_entries--;
  return 1;

}				/* st_delete_int */


/**Function********************************************************************

  Synopsis    [Iterates over the elements of a table.]

  Description [For each (key, value) record in `table', st_foreach
  call func with the arguments
  <pre>
	  (*func)(key, value, arg)
  </pre>
  If func returns ST_CONTINUE, st_foreach continues processing
  entries.  If func returns ST_STOP, st_foreach stops processing and
  returns immediately. If func returns ST_DELETE, then the entry is
  deleted from the symbol table and st_foreach continues.  In the case
  of ST_DELETE, it is func's responsibility to free the key and value,
  if necessary.<p>

  The routine returns 1 if all items in the table were generated and 0
  if the generation sequence was aborted using ST_STOP.  The order in
  which the records are visited will be seemingly random.]

  SideEffects [None]

  SeeAlso     [st_foreach_item st_foreach_item_int]

******************************************************************************/
int
st_foreach (st_table * table, ST_PFSR func, char *arg)
{
  st_table_entry *ptr, **last;
  enum st_retval retval;
  int i;

  for (i = 0; i < table->num_bins; i++)
    {
      last = &table->bins[i];
      ptr = *last;
      while (ptr != NIL (st_table_entry))
	{
	  retval = (*func) (ptr->key, ptr->record, arg);
	  switch (retval)
	    {
	    case ST_CONTINUE:
	      last = &ptr->next;
	      ptr = *last;
	      break;
	    case ST_STOP:
	      return 0;
	    case ST_DELETE:
	      *last = ptr->next;
	      table->num_entries--;	/* cstevens@ic */
	      FREE (ptr);
	      ptr = *last;
	    }
	}
    }
  return 1;

}				/* st_foreach */


int
st_strhash (char *string, int modulus)
{
  int val = 0;
  int c;

  while ((c = *string++) != '\0')
    {
      val = val * 997 + c;
    }

  return ((val < 0) ? -val : val) % modulus;

}				/* st_strhash */

int
st_numhash (char *x, int size)
{
  return ST_NUMHASH (x, size);
}

int
st_ptrhash (char *x, int size)
{
  return ST_PTRHASH (x, size);
}

int
st_numcmp (const char *x, const char *y)
{
  return ST_NUMCMP (x, y);
}

int
st_ptrcmp (const char *x, const char *y)
{
  return ST_NUMCMP (x, y);
}


/**Function********************************************************************

  Synopsis    [Initializes a generator.]

  Description [Returns a generator handle which when used with
  st_gen() will progressively return each (key, value) record in
  `table'.]

  SideEffects [None]

  SeeAlso     [st_free_gen]

******************************************************************************/
st_generator *
st_init_gen (st_table * table)
{
  st_generator *gen;

  gen = ALLOC (st_generator, 1);
  if (gen == NIL (st_generator))
    {
      return NIL (st_generator);
    }
  gen->table = table;
  gen->entry = NIL (st_table_entry);
  gen->index = 0;
  return gen;

}				/* st_init_gen */


/**Function********************************************************************

  Synopsis [returns the next (key, value) pair in the generation
  sequence. ]

  Description [Given a generator returned by st_init_gen(), this
  routine returns the next (key, value) pair in the generation
  sequence.  The pointer `value_p' can be zero which means no value
  will be returned.  When there are no more items in the generation
  sequence, the routine returns 0.

  While using a generation sequence, deleting any (key, value) pair
  other than the one just generated may cause a fatal error when
  st_gen() is called later in the sequence and is therefore not
  recommended.]

  SideEffects [None]

  SeeAlso     [st_gen_int]

******************************************************************************/
int
st_gen (st_generator * gen, void *key_p, void *value_p)
{
  int i;

  if (gen->entry == NIL (st_table_entry))
    {
      /* try to find next entry */
      for (i = gen->index; i < gen->table->num_bins; i++)
	{
	  if (gen->table->bins[i] != NIL (st_table_entry))
	    {
	      gen->index = i + 1;
	      gen->entry = gen->table->bins[i];
	      break;
	    }
	}
      if (gen->entry == NIL (st_table_entry))
	{
	  return 0;		/* that's all folks ! */
	}
    }
  *(char **) key_p = gen->entry->key;
  if (value_p != NIL (void))
    {
      *(char **) value_p = gen->entry->record;
    }
  gen->entry = gen->entry->next;
  return 1;

}				/* st_gen */


/**Function********************************************************************

  Synopsis    [Returns the next (key, value) pair in the generation
  sequence.]

  Description [Given a generator returned by st_init_gen(), this
  routine returns the next (key, value) pair in the generation
  sequence.  `value_p' must be a pointer to an integer.  The pointer
  `value_p' can be zero which means no value will be returned.  When
  there are no more items in the generation sequence, the routine
  returns 0.]

  SideEffects [None]

  SeeAlso     [st_gen]

******************************************************************************/
int
st_gen_int (st_generator * gen, void *key_p, int *value_p)
{
  int i;

  if (gen->entry == NIL (st_table_entry))
    {
      /* try to find next entry */
      for (i = gen->index; i < gen->table->num_bins; i++)
	{
	  if (gen->table->bins[i] != NIL (st_table_entry))
	    {
	      gen->index = i + 1;
	      gen->entry = gen->table->bins[i];
	      break;
	    }
	}
      if (gen->entry == NIL (st_table_entry))
	{
	  return 0;		/* that's all folks ! */
	}
    }
  *(char **) key_p = gen->entry->key;
  if (value_p != NIL (int))
    {
      *value_p = (int) (long) gen->entry->record;
    }
  gen->entry = gen->entry->next;
  return 1;

}				/* st_gen_int */


/**Function********************************************************************

  Synopsis    [Reclaims the resources associated with `gen'.]

  Description [After generating all items in a generation sequence,
  this routine must be called to reclaim the resources associated with
  `gen'.]

  SideEffects [None]

  SeeAlso     [st_init_gen]

******************************************************************************/
void
st_free_gen (st_generator * gen)
{
  FREE (gen);

}				/* st_free_gen */


/*---------------------------------------------------------------------------*/
/* Definition of static functions                                            */
/*---------------------------------------------------------------------------*/

static int
rehash (st_table * table)
{
  st_table_entry *ptr, *next, **old_bins;
  int i, old_num_bins, hash_val, old_num_entries;

  /* save old values */
  old_bins = table->bins;
  old_num_bins = table->num_bins;
  old_num_entries = table->num_entries;

  /* rehash */
  table->num_bins = (int) (table->grow_factor * old_num_bins);
  if (table->num_bins % 2 == 0)
    {
      table->num_bins += 1;
    }
  table->num_entries = 0;
  table->bins = ALLOC (st_table_entry *, table->num_bins);
  if (table->bins == NIL (st_table_entry *))
    {
      table->bins = old_bins;
      table->num_bins = old_num_bins;
      table->num_entries = old_num_entries;
      return ST_OUT_OF_MEM;
    }
  /* initialize */
  for (i = 0; i < table->num_bins; i++)
    {
      table->bins[i] = 0;
    }

  /* copy data over */
  for (i = 0; i < old_num_bins; i++)
    {
      ptr = old_bins[i];
      while (ptr != NIL (st_table_entry))
	{
	  next = ptr->next;
	  hash_val = do_hash (ptr->key, table);
	  ptr->next = table->bins[hash_val];
	  table->bins[hash_val] = ptr;
	  table->num_entries++;
	  ptr = next;
	}
    }
  FREE (old_bins);

  return 1;

}				/* rehash */

/*--CUDD::st::end---------------------------------------------------------*/

/*--CUDD::bnet::begin-----------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* Constant declarations                                                     */
/*---------------------------------------------------------------------------*/

/* Different types of nodes. (Used in the "BnetNode" type.) */
#define BNET_CONSTANT_NODE 0
#define BNET_INPUT_NODE 1
#define BNET_PRESENT_STATE_NODE 2
#define BNET_INTERNAL_NODE 3
#define BNET_OUTPUT_NODE 4
#define BNET_NEXT_STATE_NODE 5

/* Type of DD of a node. */
#define BNET_LOCAL_DD 0
#define BNET_GLOBAL_DD 1

/*---------------------------------------------------------------------------*/
/* Type declarations                                                         */
/*---------------------------------------------------------------------------*/

/* The following types implement a very simple data structure for a boolean
** network. The intent is to be able to read a minimal subset of the blif
** format in a data structure from which it's easy to build DDs for the
** circuit.
*/

/* Type to store a line of the truth table of a node. The entire truth table
** implemented as a linked list of objects of this type.
*/
typedef struct BnetTabline
{
  char *values;			/* string of 1, 0, and - */
  struct BnetTabline *next;	/* pointer to next table line */
} BnetTabline;

/* Node of the boolean network. There is one node in the network for each
** primary input and for each .names directive. This structure
** has a field to point to the DD of the node function. The function may
** be either in terms of primary inputs, or it may be in terms of the local
** inputs. The latter implies that each node has a variable index
** associated to it at some point in time. The field "var" stores that
** variable index, and "active" says if the association is currently valid.
** (It is indeed possible for an index to be associated to different nodes
** at different times.)
*/
typedef struct BnetNode
{
  char *name;			/* name of the output signal */
  int type;			/* input, internal, constant, ... */
  int ninp;			/* number of inputs to the node */
  int nfo;			/* number of fanout nodes for this node */
  char **inputs;		/* input names */
  BnetTabline *f;		/* truth table for this node */
  int polarity;			/* f is the onset (0) or the offset (1) */
  int active;			/* node has variable associated to it (1) or not (0) */
  int var;			/* DD variable index associated to this node */
  void *aig;			/* AIG for the function of this node */
  int exdc_flag;		/* whether an exdc node or not */
  struct BnetNode *exdc;	/* pointer to exdc of dd node */
  int count;			/* auxiliary field for DD dropping */
  int level;			/* maximum distance from the inputs */
  int visited;			/* flag for search */
  struct BnetNode *next;	/* pointer to implement the linked list of nodes */
} BnetNode;

/* Very simple boolean network data structure. */
typedef struct BnetNetwork
{
  char *name;			/* network name: from the .model directive */
  int npis;			/* number of primary inputs */
  int ninputs;			/* number of inputs */
  char **inputs;		/* primary input names: from the .inputs directive */
  int npos;			/* number of primary outputs */
  int noutputs;			/* number of outputs */
  char **outputs;		/* primary output names: from the .outputs directive */
  int nlatches;			/* number of latches */
  char ***latches;		/* next state names: from the .latch directives */
  BnetNode *nodes;		/* linked list of the nodes */
  st_table *hash;		/* symbol table to access nodes by name */
  char *slope;			/* wire_load_slope */
} BnetNetwork;

/*--CUDD-stuff::end----------------------------------------------------------*/

#define CUDD_TRUE 1
#define CUDD_FALSE 0

/**AutomaticStart*************************************************************/

/*---------------------------------------------------------------------------*/
/* Function prototypes                                                       */
/*---------------------------------------------------------------------------*/

extern BnetNetwork *Bnet_ReadNetwork (FILE * fp, int pr);
extern void Bnet_PrintNetwork (BnetNetwork * net);
extern void Bnet_FreeNetwork (BnetNetwork * net);

/**AutomaticEnd***************************************************************/

#define MAXLENGTH 131072

static char BuffLine[MAXLENGTH];
static char *CurPos;

static char *readString (FILE * fp);
static char **readList (FILE * fp, int *n);
static void printList (char **list, int n);

static int bnetSetLevel (BnetNetwork * net);
static int bnetLevelDFS (BnetNetwork * net, BnetNode * node);

/**AutomaticEnd***************************************************************/

/*---------------------------------------------------------------------------*/
/* Definition of exported functions                                          */
/*---------------------------------------------------------------------------*/


/**Function********************************************************************

  Synopsis    [Reads boolean network from blif file.]

  Description [Reads a boolean network from a blif file. A very restricted
  subset of blif is supported. Specifically:
  <ul>
  <li> The only directives recognized are:
    <ul>
    <li> .model
    <li> .inputs
    <li> .outputs
    <li> .latch
    <li> .names
    <li> .exdc
    <li> .wire_load_slope
    <li> .end
    </ul>
  <li> Latches must have an initial values and no other parameters
       specified.
  <li> Lines must not exceed MAXLENGTH-1 characters, and individual names must
       not exceed 1023 characters.
  </ul>
  Caveat emptor: There may be other limitations as well. One should
  check the syntax of the blif file with some other tool before relying
  on this parser. Bnet_ReadNetwork returns a pointer to the network if
  successful; NULL otherwise.
  ]

  SideEffects [None]

  SeeAlso     [Bnet_PrintNetwork Bnet_FreeNetwork]

******************************************************************************/
BnetNetwork *
Bnet_ReadNetwork (FILE * fp /* pointer to the blif file */ ,
		  int pr /* verbosity level */ )
{
  char *savestring;
  char **list;
  int i, j, n;
  BnetNetwork *net;
  BnetNode *newnode;
  BnetNode *lastnode = NULL;
  BnetTabline *newline;
  BnetTabline *lastline;
  char ***latches = NULL;
  int maxlatches = 0;
  int exdc = 0;
  BnetNode *node;
  int count;

  /* Allocate network object and initialize symbol table. */
  net = ALLOC (BnetNetwork, 1);
  if (net == NULL)
    goto failure;
  memset ((char *) net, 0, sizeof (BnetNetwork));
  net->hash = st_init_table (strcmp, st_strhash);
  if (net->hash == NULL)
    goto failure;

  savestring = readString (fp);
  if (savestring == NULL)
    goto failure;
  net->nlatches = 0;
  while (strcmp (savestring, ".model") == 0 ||
	 strcmp (savestring, ".inputs") == 0 ||
	 strcmp (savestring, ".outputs") == 0 ||
	 strcmp (savestring, ".latch") == 0 ||
	 strcmp (savestring, ".wire_load_slope") == 0 ||
	 strcmp (savestring, ".exdc") == 0 ||
	 strcmp (savestring, ".names") == 0
	 || strcmp (savestring, ".end") == 0)
    {
      if (strcmp (savestring, ".model") == 0)
	{
	  /* Read .model directive. */
	  FREE (savestring);
	  /* Read network name. */
	  savestring = readString (fp);
	  if (savestring == NULL)
	    goto failure;
	  net->name = savestring;
	}
      else if (strcmp (savestring, ".inputs") == 0)
	{
	  /* Read .inputs directive. */
	  FREE (savestring);
	  /* Read input names. */
	  list = readList (fp, &n);
	  if (list == NULL)
	    goto failure;
	  if (pr > 2)
	    printList (list, n);
#if 0
	  /* Expect at least one input. */
	  if (n < 1)
	    {
	      (void) fprintf (stdout, "Empty input list.\n");
	      goto failure;
	    }
#endif
	  if (exdc)
	    {
	      for (i = 0; i < n; i++)
		FREE (list[i]);
	      FREE (list);
	      savestring = readString (fp);
	      if (savestring == NULL)
		goto failure;
	      continue;
	    }
	  if (net->ninputs)
	    {
	      net->inputs = REALLOC (char *, net->inputs,
				     (net->ninputs + n) * sizeof (char *));
	      for (i = 0; i < n; i++)
		net->inputs[net->ninputs + i] = list[i];
	    }
	  else
	    net->inputs = list;
	  /* Create a node for each primary input. */
	  for (i = 0; i < n; i++)
	    {
	      newnode = ALLOC (BnetNode, 1);
	      memset ((char *) newnode, 0, sizeof (BnetNode));
	      if (newnode == NULL)
		goto failure;
	      newnode->name = list[i];
	      newnode->inputs = NULL;
	      newnode->type = BNET_INPUT_NODE;
	      newnode->active = CUDD_FALSE;
	      newnode->nfo = 0;
	      newnode->ninp = 0;
	      newnode->f = NULL;
	      newnode->polarity = 0;
	      /* init AIG */
	      newnode->aig = (void *) (NULL);
	      newnode->next = NULL;
	      if (lastnode == NULL)
		{
		  net->nodes = newnode;
		}
	      else
		{
		  lastnode->next = newnode;
		}
	      lastnode = newnode;
	    }
	  net->npis += n;
	  net->ninputs += n;
	}
      else if (strcmp (savestring, ".outputs") == 0)
	{
	  /* Read .outputs directive. We do not create nodes for the primary
	   ** outputs, because the nodes will be created when the same names
	   ** appear as outputs of some gates.
	   */
	  FREE (savestring);
	  /* Read output names. */
	  list = readList (fp, &n);
	  if (list == NULL)
	    goto failure;
	  if (pr > 2)
	    printList (list, n);
	  if (n < 1)
	    {
	      (void) fprintf (stdout, "Empty .outputs list.\n");
	      goto failure;
	    }
	  if (exdc)
	    {
	      for (i = 0; i < n; i++)
		FREE (list[i]);
	      FREE (list);
	      savestring = readString (fp);
	      if (savestring == NULL)
		goto failure;
	      continue;
	    }
	  if (net->noutputs)
	    {
	      net->outputs = REALLOC (char *, net->outputs,
				      (net->noutputs + n) * sizeof (char *));
	      for (i = 0; i < n; i++)
		net->outputs[net->noutputs + i] = list[i];
	    }
	  else
	    {
	      net->outputs = list;
	    }
	  net->npos += n;
	  net->noutputs += n;
	}
      else if (strcmp (savestring, ".wire_load_slope") == 0)
	{
	  FREE (savestring);
	  savestring = readString (fp);
	  net->slope = savestring;
	}
      else if (strcmp (savestring, ".latch") == 0)
	{
	  FREE (savestring);
	  newnode = ALLOC (BnetNode, 1);
	  if (newnode == NULL)
	    goto failure;
	  memset ((char *) newnode, 0, sizeof (BnetNode));
	  newnode->type = BNET_PRESENT_STATE_NODE;
	  list = readList (fp, &n);
	  if (list == NULL)
	    goto failure;
	  if (pr > 2)
	    printList (list, n);
	  /* Expect three names. */
	  if (n != 3)
	    {
	      (void) fprintf (stdout,
			      ".latch not followed by three tokens.\n");
	      goto failure;
	    }

	  if (strcmp (list[2], "0") &&
              strcmp (list[2], "1") &&
              strcmp (list[2], "2"))
	    {
	      (void) fprintf (stdout,
	                      "can not handle '%s' initialized .latch.\n", 
			      list[2]);
	      goto failure;
	    }

	  newnode->name = list[1];
	  newnode->inputs = NULL;
	  newnode->ninp = 0;
	  newnode->f = NULL;
	  newnode->active = CUDD_FALSE;
	  newnode->nfo = 0;
	  newnode->polarity = 0;
	  /* init AIG */
	  newnode->aig = (void *) (NULL);
	  newnode->next = NULL;
	  if (lastnode == NULL)
	    {
	      net->nodes = newnode;
	    }
	  else
	    {
	      lastnode->next = newnode;
	    }
	  lastnode = newnode;
	  /* Add next state variable to list. */
	  if (maxlatches == 0)
	    {
	      maxlatches = 20;
	      latches = ALLOC (char **, maxlatches);
	    }
	  else if (maxlatches <= net->nlatches)
	    {
	      maxlatches += 20;
	      latches = REALLOC (char **, latches, maxlatches);
	    }
	  latches[net->nlatches] = list;
	  net->nlatches++;
	  savestring = readString (fp);
	  if (savestring == NULL)
	    goto failure;
	}
      else if (strcmp (savestring, ".names") == 0)
	{
	  FREE (savestring);
	  newnode = ALLOC (BnetNode, 1);
	  memset ((char *) newnode, 0, sizeof (BnetNode));
	  if (newnode == NULL)
	    goto failure;
	  list = readList (fp, &n);
	  if (list == NULL)
	    goto failure;
	  if (pr > 2)
	    printList (list, n);
	  /* Expect at least one name (the node output). */
	  if (n < 1)
	    {
	      (void) fprintf (stdout, "Missing output name.\n");
	      goto failure;
	    }
	  newnode->name = list[n - 1];
	  newnode->inputs = list;
	  newnode->ninp = n - 1;
	  newnode->active = CUDD_FALSE;
	  newnode->nfo = 0;
	  newnode->polarity = 0;
	  if (newnode->ninp > 0)
	    {
	      newnode->type = BNET_INTERNAL_NODE;
	      for (i = 0; i < net->noutputs; i++)
		{
		  if (strcmp (net->outputs[i], newnode->name) == 0)
		    {
		      newnode->type = BNET_OUTPUT_NODE;
		      break;
		    }
		}
	    }
	  else
	    {
	      newnode->type = BNET_CONSTANT_NODE;
	    }
	  /* init AIG */
	  newnode->aig = (void *) (NULL);
	  newnode->next = NULL;
	  if (lastnode == NULL)
	    {
	      net->nodes = newnode;
	    }
	  else
	    {
	      lastnode->next = newnode;
	    }
	  lastnode = newnode;
	  /* Read node function. */
	  newnode->f = NULL;
	  if (exdc)
	    {
	      newnode->exdc_flag = 1;
	      node = net->nodes;
	      while (node)
		{
		  if (node->type == BNET_OUTPUT_NODE &&
		      strcmp (node->name, newnode->name) == 0)
		    {
		      node->exdc = newnode;
		      break;
		    }
		  node = node->next;
		}
	    }
	  savestring = readString (fp);
	  if (savestring == NULL)
	    goto failure;
	  lastline = NULL;
	  while (savestring[0] != '.')
	    {
	      /* Reading a table line. */
	      newline = ALLOC (BnetTabline, 1);
	      if (newline == NULL)
		goto failure;
	      newline->next = NULL;
	      if (lastline == NULL)
		{
		  newnode->f = newline;
		}
	      else
		{
		  lastline->next = newline;
		}
	      lastline = newline;
	      if (newnode->type == BNET_INTERNAL_NODE ||
		  newnode->type == BNET_OUTPUT_NODE)
		{
		  newline->values = savestring;
		  /* Read output 1 or 0. */
		  savestring = readString (fp);
		  if (savestring == NULL)
		    goto failure;
		}
	      else
		{
		  newline->values = NULL;
		}
	      if (savestring[0] == '0')
		newnode->polarity = 1;
	      FREE (savestring);
	      savestring = readString (fp);
	      if (savestring == NULL)
		goto failure;
	    }
	}
      else if (strcmp (savestring, ".exdc") == 0)
	{
	  FREE (savestring);
	  exdc = 1;
	}
      else if (strcmp (savestring, ".end") == 0)
	{
	  FREE (savestring);
	  break;
	}
      if ((!savestring) || savestring[0] != '.')
	savestring = readString (fp);
      if (savestring == NULL)
	goto failure;
    }

  /* Put nodes in symbol table. */
  newnode = net->nodes;
  while (newnode != NULL)
    {
      int retval = st_insert (net->hash, newnode->name, (char *) newnode);
      if (retval == ST_OUT_OF_MEM)
	{
	  goto failure;
	}
      else if (retval == 1)
	{
	  printf ("Error: Multiple drivers for node %s\n", newnode->name);
	  goto failure;
	}
      else
	{
	  if (pr > 2)
	    printf ("Inserted %s\n", newnode->name);
	}
      newnode = newnode->next;
    }

  if (latches)
    {
      net->latches = latches;

      count = 0;
      net->outputs = REALLOC (char *, net->outputs,
			      (net->noutputs +
			       net->nlatches) * sizeof (char *));
      for (i = 0; i < net->nlatches; i++)
	{
	  for (j = 0; j < net->noutputs; j++)
	    {
	      if (strcmp (latches[i][0], net->outputs[j]) == 0)
		break;
	    }
	  if (j < net->noutputs)
	    continue;
	  savestring = ALLOC (char, strlen (latches[i][0]) + 1);
	  strcpy (savestring, latches[i][0]);
	  net->outputs[net->noutputs + count] = savestring;
	  count++;
	  if (st_lookup (net->hash, savestring, &node))
	    {
	      if (node->type == BNET_INTERNAL_NODE)
		{
		  node->type = BNET_OUTPUT_NODE;
		}
	    }
	}
      net->noutputs += count;

      net->inputs = REALLOC (char *, net->inputs,
			     (net->ninputs +
			      net->nlatches) * sizeof (char *));
      for (i = 0; i < net->nlatches; i++)
	{
	  savestring = ALLOC (char, strlen (latches[i][1]) + 1);
	  strcpy (savestring, latches[i][1]);
	  net->inputs[net->ninputs + i] = savestring;
	}
      net->ninputs += net->nlatches;
    }

  /* Compute fanout counts. For each node in the linked list, fetch
   ** all its fanins using the symbol table, and increment the fanout of
   ** each fanin.
   */
  newnode = net->nodes;
  while (newnode != NULL)
    {
      BnetNode *auxnd;
      for (i = 0; i < newnode->ninp; i++)
	{
	  if (!st_lookup (net->hash, newnode->inputs[i], &auxnd))
	    {
	      (void) fprintf (stdout, "%s not driven\n", newnode->inputs[i]);
	      goto failure;
	    }
	  auxnd->nfo++;
	}
      newnode = newnode->next;
    }

  if (!bnetSetLevel (net))
    goto failure;

  return (net);

failure:
  /* Here we should clean up the mess. */
  (void) fprintf (stdout, "Error in reading network from file.\n");
  return (NULL);

}				/* end of Bnet_ReadNetwork */


/**Function********************************************************************

  Synopsis    [Prints a boolean network created by readNetwork.]

  Description [Prints to the standard output a boolean network created
  by Bnet_ReadNetwork. Uses the blif format; this way, one can verify the
  equivalence of the input and the output with, say, sis.]

  SideEffects [None]

  SeeAlso     [Bnet_ReadNetwork]

******************************************************************************/
void
Bnet_PrintNetwork (BnetNetwork * net /* boolean network */ )
{
  BnetNode *nd;
  BnetTabline *tl;
  int i;

  if (net == NULL)
    return;

  (void) fprintf (stdout, ".model %s\n", net->name);
  (void) fprintf (stdout, ".inputs");
  printList (net->inputs, net->npis);
  (void) fprintf (stdout, ".outputs");
  printList (net->outputs, net->npos);
  for (i = 0; i < net->nlatches; i++)
    {
      (void) fprintf (stdout, ".latch");
      printList (net->latches[i], 3);
    }
  nd = net->nodes;
  while (nd != NULL)
    {
      if (nd->type != BNET_INPUT_NODE && nd->type != BNET_PRESENT_STATE_NODE)
	{
	  (void) fprintf (stdout, ".names");
	  for (i = 0; i < nd->ninp; i++)
	    {
	      (void) fprintf (stdout, " %s", nd->inputs[i]);
	    }
	  (void) fprintf (stdout, " %s\n", nd->name);
	  tl = nd->f;
	  while (tl != NULL)
	    {
	      if (tl->values != NULL)
		{
		  (void) fprintf (stdout, "%s %d\n", tl->values,
				  1 - nd->polarity);
		}
	      else
		{
		  (void) fprintf (stdout, "%d\n", 1 - nd->polarity);
		}
	      tl = tl->next;
	    }
	}
      nd = nd->next;
    }
  (void) fprintf (stdout, ".end\n");

}				/* end of Bnet_PrintNetwork */


void
Bnet_FreeNetwork (BnetNetwork * net)
{
  BnetNode *node, *nextnode;
  BnetTabline *line, *nextline;
  int i;

  FREE (net->name);
  /* The input name strings are already pointed by the input nodes.
   ** Here we only need to free the latch names and the array that
   ** points to them.
   */
  for (i = 0; i < net->nlatches; i++)
    {
      FREE (net->inputs[net->npis + i]);
    }
  FREE (net->inputs);
  /* Free the output name strings and then the array pointing to them.  */
  for (i = 0; i < net->noutputs; i++)
    {
      FREE (net->outputs[i]);
    }
  FREE (net->outputs);

  for (i = 0; i < net->nlatches; i++)
    {
      FREE (net->latches[i][0]);
      FREE (net->latches[i][1]);
      FREE (net->latches[i][2]);
      FREE (net->latches[i]);
    }
  if (net->nlatches)
    FREE (net->latches);
  node = net->nodes;
  while (node != NULL)
    {
      nextnode = node->next;
      if (node->type != BNET_PRESENT_STATE_NODE)
	FREE (node->name);
      for (i = 0; i < node->ninp; i++)
	{
	  FREE (node->inputs[i]);
	}
      if (node->inputs != NULL)
	{
	  FREE (node->inputs);
	}
      /* Free the function table. */
      line = node->f;
      while (line != NULL)
	{
	  nextline = line->next;
	  FREE (line->values);
	  FREE (line);
	  line = nextline;
	}
      FREE (node);
      node = nextnode;
    }
  st_free_table (net->hash);
  if (net->slope != NULL)
    FREE (net->slope);
  FREE (net);

}				/* end of Bnet_FreeNetwork */

/**Function********************************************************************

  Synopsis    [Reads a string from a file.]

  Description [Reads a string from a file. The string can be MAXLENGTH-1
  characters at most. readString allocates memory to hold the string and
  returns a pointer to the result if successful. It returns NULL
  otherwise.]

  SideEffects [None]

  SeeAlso     [readList]

******************************************************************************/
static char *
readString (FILE *
	    fp /* pointer to the file from which the string is read */ )
{
  char *savestring;
  int length;

  while (!CurPos)
    {
      if (!fgets (BuffLine, MAXLENGTH, fp))
	return (NULL);
      BuffLine[strlen (BuffLine) - 1] = '\0';
      CurPos = strtok (BuffLine, " \t");
      if (CurPos && CurPos[0] == '#')
	CurPos = (char *) NULL;
    }
  length = strlen (CurPos);
  savestring = ALLOC (char, length + 1);
  if (savestring == NULL)
    return (NULL);
  strcpy (savestring, CurPos);
  CurPos = strtok (NULL, " \t");
  return (savestring);

}				/* end of readString */


/**Function********************************************************************

  Synopsis    [Reads a list of strings from a file.]

  Description [Reads a list of strings from a line of a file.
  The strings are sequences of characters separated by spaces or tabs.
  The total length of the list, white space included, must not exceed
  MAXLENGTH-1 characters.  readList allocates memory for the strings and
  creates an array of pointers to the individual lists. Only two pieces
  of memory are allocated by readList: One to hold all the strings,
  and one to hold the pointers to them. Therefore, when freeing the
  memory allocated by readList, only the pointer to the list of
  pointers, and the pointer to the beginning of the first string should
  be freed. readList returns the pointer to the list of pointers if
  successful; NULL otherwise.]

  SideEffects [n is set to the number of strings in the list.]

  SeeAlso     [readString printList]

******************************************************************************/
static char **
readList (FILE * fp /* pointer to the file from which the list is read */ ,
	  int *n /* on return, number of strings in the list */ )
{
  char *savestring;
  int length;
  char *stack[8192];
  char **list;
  int i, count = 0;

  while (CurPos)
    {
      if (strcmp (CurPos, "\\") == 0)
	{
	  CurPos = (char *) NULL;
	  while (!CurPos)
	    {
	      if (!fgets (BuffLine, MAXLENGTH, fp))
		return (NULL);
	      BuffLine[strlen (BuffLine) - 1] = '\0';
	      CurPos = strtok (BuffLine, " \t");
	    }
	}
      length = strlen (CurPos);
      savestring = ALLOC (char, length + 1);
      if (savestring == NULL)
	return (NULL);
      strcpy (savestring, CurPos);
      stack[count] = savestring;
      count++;
      CurPos = strtok (NULL, " \t");
    }
  list = ALLOC (char *, count);
  for (i = 0; i < count; i++)
    list[i] = stack[i];
  *n = count;
  return (list);

}				/* end of readList */

/**Function********************************************************************

  Synopsis    [Prints a list of strings to the standard output.]

  Description [Prints a list of strings to the standard output. The list
  is in the format created by readList.]

  SideEffects [None]

  SeeAlso     [readList Bnet_PrintNetwork]

******************************************************************************/
static void
printList (char **list /* list of pointers to strings */ ,
	   int n /* length of the list */ )
{
  int i;

  for (i = 0; i < n; i++)
    {
      (void) fprintf (stdout, " %s", list[i]);
    }
  (void) fprintf (stdout, "\n");

}				/* end of printList */

/**Function********************************************************************

  Synopsis    [Sets the level of each node.]

  Description [Sets the level of each node. Returns 1 if successful; 0
  otherwise.]

  SideEffects [Changes the level and visited fields of the nodes it
  visits.]

  SeeAlso     [bnetLevelDFS]

******************************************************************************/
static int
bnetSetLevel (BnetNetwork * net)
{
  BnetNode *node;

  /* Recursively visit nodes. This is pretty inefficient, because we
   ** visit all nodes in this loop, and most of them in the recursive
   ** calls to bnetLevelDFS. However, this approach guarantees that
   ** all nodes will be reached ven if there are dangling outputs. */
  node = net->nodes;
  while (node != NULL)
    {
      if (!bnetLevelDFS (net, node))
	return (0);
      node = node->next;
    }

  /* Clear visited flags. */
  node = net->nodes;
  while (node != NULL)
    {
      node->visited = 0;
      node = node->next;
    }
  return (1);

}				/* end of bnetSetLevel */


/**Function********************************************************************

  Synopsis    [Does a DFS from a node setting the level field.]

  Description [Does a DFS from a node setting the level field. Returns
  1 if successful; 0 otherwise.]

  SideEffects [Changes the level and visited fields of the nodes it
  visits.]

  SeeAlso     [bnetSetLevel]

******************************************************************************/
static int
bnetLevelDFS (BnetNetwork * net, BnetNode * node)
{
  int i;
  BnetNode *auxnd;

  if (node->visited == 1)
    {
      return (1);
    }

  node->visited = 1;

  /* Graphical sources have level 0.  This is the final value if the
   ** node has no fan-ins. Otherwise the successive loop will
   ** increase the level. */
  node->level = 0;
  for (i = 0; i < node->ninp; i++)
    {
      if (!st_lookup (net->hash, node->inputs[i], &auxnd))
	{
	  return (0);
	}
      if (!bnetLevelDFS (net, auxnd))
	{
	  return (0);
	}
      if (auxnd->level >= node->level)
	node->level = 1 + auxnd->level;
    }
  return (1);

}				/* end of bnetLevelDFS */

/*--CUDD::bnet::end-------------------------------------------------------*/

/*--AIGER_stuff::begin----------------------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>

/*--AIGER::defines--------------------------------------------------------*/
#define USAGE \
  "=[bliftoaig] usage: bliftoaig [-h][-v][-s][-a][-O(1|2|3|4)][src [dst]]\n"
#define AIG_TRUE ((AIG*)(long)1)
#define AIG_FALSE ((AIG*)(long)-1)
#define NEW(p) \
  do { \
    size_t bytes = sizeof (*(p)); \
    (p) = malloc (bytes); \
    memset ((p), 0, bytes); \
  } while (0)
#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = malloc (bytes); \
    memset ((p), 0, bytes); \
  } while (0)
#define strip_aig(sign,aig) \
  do { \
    (sign) = sign_aig (aig); \
    if ((sign) < 0) \
      (aig) = not_aig (aig); \
  } while (0)
#define swap_aig(a,b) \
  do { \
    AIG * tmp_swap_aig = (a); \
    (a) = (b); \
    (b) = tmp_swap_aig; \
  } while(0)

/*--AIGER::typedefs-------------------------------------------------------*/
typedef struct AIG AIG;

/*--AIGER::prototypes-----------------------------------------------------*/
static void die (const char *, ...);
static void msg (int, const char *, ...);
static void cache (AIG *, AIG *);
static unsigned aig_idx (AIG *);
static int add_ands (void);
static int tseitin_network (BnetNetwork *);
static int tseitin_aig (AIG *);
static int dump_network (BnetNetwork *);
static void release_aig_chain (AIG *);
static void release_aigs (void);
static void release (void);
static unsigned hash_aig (int, AIG *, AIG *);
static void enlarge_aigs (void);
static int sign_aig (AIG *);
static int eq_aig (AIG *, int, AIG *, AIG *);
AIG **find_aig (int, AIG *, AIG *);
static AIG *stripped_aig (AIG *);
static AIG *simplify_aig_one_level (AIG *, AIG *);
static AIG *simplify_aig_two_level (AIG * a, AIG * b);
static AIG *new_aig (int, AIG *, AIG *);
static AIG *not_aig (AIG *);
static AIG *not_cond_aig (AIG *, int);
static AIG *and_aig (AIG *, AIG *);
static AIG *or_aig (AIG *, AIG *);
static AIG *xor_aig (AIG *, AIG *);
static AIG *ite_aig (AIG *, AIG *, AIG *);
static FILE *open_file (char *, const char *);
static int Bnet_BuildNodeAIG (BnetNode *, st_table *);
static int Bnet_BuildAIGs (BnetNetwork *);
static int Bnet_BuildExorAIG (BnetNode *, st_table *);
static int Bnet_BuildMuxAIG (BnetNode *, st_table *);

/*--AIGER::structs---------------------------------------------------------*/
struct AIG
{
  AIG *c0;
  AIG *c1;
  char input_flag;		/* 1 = input node, 0 = not a primary input */
  char latch_flag;		/* 1 = latch node, 0 = not a latch */
  char *name;			/* Name */
  unsigned idx;			/* Tseitin index */
  AIG *next;			/* collision chain */
  AIG *cache;			/* cache for shifting and elaboration */
  unsigned id;			/* unique id for hashing/comparing purposes */
};

/*--AIGER::globals---------------------------------------------------------*/
static aiger *aiger_mgr;
static unsigned int tseitin_idx_running = 2;
static const char *blif_file;
static int close_input;
static FILE *input;
static int verbose;
static int ascii;
static const char *output_name;
static int strip_symbols;

static unsigned size_aigs;
static unsigned count_aigs;
static AIG **aigs;

static unsigned size_cached;
static unsigned count_cached;
static AIG **cached;

static unsigned inputs;
static unsigned latches;
static unsigned ands;

static int optimize = 4;

static unsigned primes[] = { 21433, 65537, 332623, 1322963, 200000123 };

#ifndef NDEBUG
static unsigned *eoprimes = primes + sizeof (primes) / sizeof (primes[0]);
#endif

/*--AIGER::function_implementations---------------------------------------*/

/*
 \param msg a message string
 \param ... further messages
 \return void
 \brief Dumps message to STDERR and terminates program with error value.
*/
static void
die (const char *msg, ...)
{
  va_list ap;
  fputs ("=[bliftoaig] ", stderr);
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

/*
 \param level message is only dumped when level <= verbose level
 \param msg message string
 \param ... further messages
 \return void
 \brief Dumps message(s) according to verbose level to STDERR. 
*/
static void
msg (int level, const char *msg, ...)
{
  va_list ap;

  if (level > verbose)
    return;

  fprintf (stderr, "=[bliftoaig] ");
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

/*
 \param aig origin AIG node
 \param res result AIG node to be cached
 \return void
 \brief Caches an AIG node that is related to some reference AIG node.
*/
static void
cache (AIG * aig, AIG * res)
{
  assert (sign_aig (aig) > 0);
  aig->cache = res;
  if (count_cached >= size_cached)
    {
      size_cached = size_cached ? 2 * size_cached : 1;
      cached = realloc (cached, size_cached * sizeof (cached[0]));
    }

  cached[count_cached++] = aig;
}

/*
 \param aig an AIG node
 \return the (Tseitin) index of the node
 \brief Returns the (Tseitin) index assigned to this node and takes 
        the sign of the AIG into account.
*/
static unsigned
aig_idx (AIG * aig)
{
  unsigned res;
  int sign;

  assert (aig);

  if (aig == AIG_TRUE)
    return 1;

  if (aig == AIG_FALSE)
    return 0;

  strip_aig (sign, aig);
  res = aig->idx;
  assert (res > 1);
  assert (!(res & 1));
  if (sign < 0)
    res++;

  return res;
}

/*
 \param void
 \return >0 if all is OK, 0 otherwise
 \brief Adds all cached AIG nodes to the AIG manager.
*/
static int
add_ands (void)
{
  unsigned i, j;
  AIG *aig;

  j = 2 * (inputs + latches + 1);
  for (i = 0; i < count_cached; i++)
    {
      aig = cached[i];
      assert (sign_aig (aig) > 0);
      assert (aig_idx (aig) == j);
      aiger_add_and (aiger_mgr,
		     aig_idx (aig), aig_idx (aig->c0), aig_idx (aig->c1));
      j += 2;
    }

  return 1;
}

/*
 \param aig an AIG node
 \return void
 \brief Frees memory of AIGs following the chain starting at AIG aig.
*/
static void
release_aig_chain (AIG * aig)
{
  AIG *p, *next;

  for (p = aig; p; p = next)
    {
      next = p->next;
      if (p->name)
	{
	  free (p->name);
	}
      free (p);
    }
}

/*
 \param void
 \return void
 \brief Frees memory of all AIGs (top-level routine).
*/
static void
release_aigs (void)
{
  unsigned i;

  for (i = 0; i < size_aigs; i++)
    release_aig_chain (aigs[i]);

  free (aigs);
  free (cached);
}

/*
 \param void
 \return void
 \brief Frees memory of AIGs (top-top-level routine).
*/
static void
release (void)
{
  msg (2, "%u aigs", count_aigs);
  release_aigs ();
}

/*
 \param idx (Tseitin) index
 \param c0 left input AIG
 \param c1 right input AIG
 \return a hash key
 \brief Computes a hash key for an AIG node with index idx, 
        or with inputs c0 and c1, respectively. 
*/
static unsigned
hash_aig (int idx, AIG * c0, AIG * c1)
{
  const unsigned *q;
  unsigned long tmp;
  unsigned res;

  q = primes;

  tmp = (unsigned long) idx;
  res = *q++ * tmp;

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

/*
 \param void
 \return void
 \brief Re-allocates memory for the hash storing AIGs, 
        and re-hashed existing AIG nodes.
*/
static void
enlarge_aigs (void)
{
  unsigned old_size_aigs, i, h;
  AIG **old_aigs, *p, *next;

  old_aigs = aigs;
  old_size_aigs = size_aigs;

  size_aigs = size_aigs ? 2 * size_aigs : 1;
  NEWN (aigs, size_aigs);

  for (i = 0; i < old_size_aigs; i++)
    for (p = old_aigs[i]; p; p = next)
      {
	next = p->next;
	h = hash_aig (p->idx, p->c0, p->c1);
	p->next = aigs[h];
	aigs[h] = p;
      }

  free (old_aigs);
}

/*
 \param aig an AIG node
 \return sign
 \brief Returns the sign of an AIG node.
*/
static int
sign_aig (AIG * aig)
{
  long aig_as_long = (long) aig;
  int res = aig_as_long < 0 ? -1 : 1;
  return res;
}

/*
 \param aig an AIG node
 \param idx a (Tseitin) index
 \param c0 left input AIG 
 \param c1 right input AIG
 \return Equivalence status of aig compared to the remaining attributes (idx,c0,c1).
*/
static int
eq_aig (AIG * aig, int idx, AIG * c0, AIG * c1)
{
  assert (sign_aig (aig) > 0);

  return (aig->idx == idx && aig->c0 == c0 && aig->c1 == c1);
}

/*
 \param idx a (Tseitin) index
 \param c0 left input AIG
 \param c1 right input AIG
 \return The collision chain for which the first AIG is equal to (idx,c0,c1).
 \brief Performs lookup whether an AIG node (idx,c0,c1) exists.
*/
AIG **
find_aig (int idx, AIG * c0, AIG * c1)
{
  AIG **p, *a;

  for (p = aigs + hash_aig (idx, c0, c1);
       (a = *p) && !eq_aig (a, idx, c0, c1); p = &a->next)
    ;

  return p;
}

/*
 \param aig a AIG node
 \return a sign-stripped AIG node
 \brief Strips the sign of an AIG node.
*/
static AIG *
stripped_aig (AIG * aig)
{
  return (sign_aig (aig) < 0 ? not_aig (aig) : aig);
}

/*------------------------------------------------------------------------*/
#ifndef NDEBUG
/*------------------------------------------------------------------------*/
/*
 \param aig
 \return void
 \brief Dumps an AIG to STDOUT.
*/
static void
print_aig (AIG * aig)
{
  int sign;

  if (aig == AIG_TRUE)
    fputc ('1', stdout);
  else if (aig == AIG_FALSE)
    fputc ('0', stdout);
  else
    {
      strip_aig (sign, aig);

      if (sign < 0)
	fputc ('!', stdout);

      if (sign < 0)
	fputc ('(', stdout);

      print_aig (aig->c0);
      fputc ('&', stdout);
      print_aig (aig->c1);

      if (sign < 0)
	fputc (')', stdout);
    }
}

/*
 \param aig an AIG node
 \return void
 \brief Dumps an AIG and adds a newline.
*/
void
printnl_aig (AIG * aig)
{
  print_aig (aig);
  fputc ('\n', stdout);
}

/*------------------------------------------------------------------------*/
#endif
/*------------------------------------------------------------------------*/

/*
 \param a an AIG node
 \param b an AIG node
 \return a simplified AIG for (a*b) iff simplifications are applicable
 \brief Checks simple simplfications for computing (a*b).
*/
static AIG *
simplify_aig_one_level (AIG * a, AIG * b)
{
  assert (optimize >= 1);

  if (a == AIG_FALSE || b == AIG_FALSE)
    return AIG_FALSE;

  if (b == AIG_TRUE || a == b)
    return a;

  if (a == AIG_TRUE)
    return b;

  if (a == not_aig (b))
    return AIG_FALSE;

  return 0;
}

/*
 \param a an AIG node
 \param b an AIG node
 \return a simplified AIG for (a*b) iff simplifications are applicable
 \brief Checks more elaborate simplfications using a two-level 
        look-ahead for computing (a*b).
*/
static AIG *
simplify_aig_two_level (AIG * a, AIG * b)
{
  AIG *a0, *a1, *b0, *b1, *signed_a, *signed_b;
  int s, t;

  assert (optimize >= 2);

  signed_a = a;
  signed_b = b;

  strip_aig (s, a);
  strip_aig (t, b);

  a0 = (a->input_flag || a->latch_flag) ? a : a->c0;
  a1 = (a->input_flag || a->latch_flag) ? a : a->c1;
  b0 = (b->input_flag || b->latch_flag) ? b : b->c0;
  b1 = (b->input_flag || b->latch_flag) ? b : b->c1;

  if (s > 0)
    {
      /* Assymetric Contradiction.
       *
       * (a0 & a1) & signed_b
       */
      if (a0 == not_aig (signed_b))
	return AIG_FALSE;
      if (a1 == not_aig (signed_b))
	return AIG_FALSE;

      /* Assymetric Idempotence.
       *
       * (a0 & a1) & signed_b
       */
      if (a0 == signed_b)
	return signed_a;
      if (a1 == signed_b)
	return signed_a;
    }
  else
    {
      /* Assymetric Subsumption.
       *
       * (!a0 | !a1) & signed_b
       */
      if (a0 == not_aig (signed_b))
	return signed_b;
      if (a1 == not_aig (signed_b))
	return signed_b;
    }

  if (t > 0)
    {
      /* Assymetric Contradiction.
       *
       * signed_a & (b0 & b1)
       */
      if (b0 == not_aig (signed_a))
	return AIG_FALSE;
      if (b1 == not_aig (signed_a))
	return AIG_FALSE;

      /* Assymetric Idempotence.
       *
       * signed_a & (b0 & b1)
       */
      if (b0 == signed_a)
	return signed_b;
      if (b1 == signed_a)
	return signed_b;
    }
  else
    {
      /* Assymetric Subsumption.
       *
       * signed_a & (!b0 | !b1)
       */
      if (b0 == not_aig (signed_a))
	return signed_a;
      if (b1 == not_aig (signed_a))
	return signed_a;
    }

  if (s > 0 && t > 0)
    {
      /* Symmetric Contradiction.
       *
       * (a0 & a1) & (b0 & b1)
       */
      if (a0 == not_aig (b0))
	return AIG_FALSE;
      if (a0 == not_aig (b1))
	return AIG_FALSE;
      if (a1 == not_aig (b0))
	return AIG_FALSE;
      if (a1 == not_aig (b1))
	return AIG_FALSE;
    }
  else if (s < 0 && t > 0)
    {
      /* Symmetric Subsumption.
       *
       * (!a0 | !a1) & (b0 & b1)
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
      /* Symmetric Subsumption.
       *
       * a0 & a1 & (!b0 | !b1)
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

      /* Resolution.
       *
       * (!a0 | !a1) & (!b0 | !b1)
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

/*
 \param idx a (Tseitin) index
 \param a an AIG node
 \param b an AIG node
 \return a new AIG node
 \brief Creates a new AIG node, either (idx,0,0) or (-,c0,c1).
        Performs several simplifications if applicable.
*/
static AIG *
new_aig (int idx, AIG * a, AIG * b)
{
  AIG **p, *res;

  if (count_aigs >= size_aigs)
    enlarge_aigs ();

  if (!idx)
    {
    TRY_TO_SIMPLIFY_AGAIN:
      assert (optimize >= 1);
      if ((res = simplify_aig_one_level (a, b)))
	return res;

      if (optimize >= 2 && (res = simplify_aig_two_level (a, b)))
	return res;

      if (optimize >= 3)
	{
	  AIG *not_a = not_aig (a);
	  AIG *not_b = not_aig (b);

	  if (sign_aig (a) < 0 && !(not_a->input_flag || not_a->latch_flag))
	    {
	      /* Assymetric Substitution
	       *
	       * (!a0 | !a1) & b
	       */
	      if (not_a->c0 == b)
		{
		  a = not_aig (not_a->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_a->c1 == b)
		{
		  a = not_aig (not_a->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (b) < 0 && !(not_b->input_flag || not_b->latch_flag))
	    {
	      /* Assymetric Substitution
	       *
	       * a & (!b0 | !b1)
	       */
	      if (not_b->c0 == a)
		{
		  b = not_aig (not_b->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_b->c1 == a)
		{
		  b = not_aig (not_b->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (a) > 0 && !(a->input_flag || a->latch_flag) &&
	      sign_aig (b) < 0 && !(not_b->input_flag || not_b->latch_flag))
	    {
	      /* Symmetric Substitution.
	       *
	       * (a0 & a1) & (!b0 | !b1)
	       */
	      if (not_b->c0 == a->c0 || not_b->c0 == a->c1)
		{
		  b = not_aig (not_b->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_b->c1 == a->c0 || not_b->c1 == a->c1)
		{
		  b = not_aig (not_b->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }

	  if (sign_aig (a) < 0 && !(not_a->input_flag || not_a->latch_flag) &&
	      sign_aig (b) > 0 && !(b->input_flag || b->latch_flag))
	    {
	      /* Symmetric Substitution.
	       *
	       * (!a0 | !a1) & (b0 & b1)
	       */
	      if (not_a->c0 == b->c0 || not_a->c0 == b->c1)
		{
		  a = not_aig (not_a->c1);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (not_a->c1 == b->c0 || not_a->c1 == b->c1)
		{
		  a = not_aig (not_a->c0);
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }
	}

      if (optimize >= 4)
	{
	  if (sign_aig (a) > 0 && !(a->input_flag || a->latch_flag) &&
	      sign_aig (b) > 0 && !(b->input_flag || b->latch_flag))
	    {
	      /* Symmetric Idempotence.
	       *
	       * (a0 & a1) & (b0 & b1)
	       */
	      if (a->c0 == b->c0)
		{
		  a = a->c1;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c0 == b->c1)
		{
		  a = a->c1;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c1 == b->c0)
		{
		  a = a->c0;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}

	      if (a->c1 == b->c1)
		{
		  a = a->c0;
		  goto TRY_TO_SIMPLIFY_AGAIN;
		}
	    }
	}

      if (stripped_aig (a)->id > stripped_aig (b)->id)
	swap_aig (a, b);
    }

  p = find_aig (idx, a, b);
  res = *p;
  if (!res)
    {
      NEW (res);

      assert (sign_aig (res) > 0);

      /* actually, this is the constructor for struct AIG */
      res->c0 = a;
      res->c1 = b;
      res->input_flag = 0;
      res->latch_flag = 0;
      res->name = (char *) (NULL);
      res->idx = idx;
      res->id = count_aigs++;
      *p = res;
    }

  return res;
}

/*
 \param aig an AIG node
 \return !aig 
 \brief Computes an AIG for the complement of the function represented by aig.
*/
static AIG *
not_aig (AIG * aig)
{
  long aig_as_long, res_as_long;
  AIG *res;

  aig_as_long = (long) aig;
  res_as_long = -aig_as_long;
  res = (AIG *) res_as_long;

  assert (sign_aig (aig) * sign_aig (res) == -1);

  return res;
}

/*
 \param a an AIG node
 \param c a conditional boolean value
 \return (c?(!a):a)
 \brief Computes an AIG for the complement of the function 
        represented by a when c=1, otherwise returns a.
*/
static AIG *
not_cond_aig (AIG * a, int c)
{
  return (c ? not_aig (a) : a);
}

/*
 \param a an AIG node
 \param b an AIG node
 \return (a*b)
 \brief Computes an AIG for the conjunction of the functions 
        represented by a and b.
*/
static AIG *
and_aig (AIG * a, AIG * b)
{
  return new_aig (0, a, b);
}

/*
 \param a an AIG node
 \param b an AIG node
 \return (a+b)
 \brief Computes an AIG for the disjunction of the functions 
        represented by a and b.
*/
static AIG *
or_aig (AIG * a, AIG * b)
{
  return not_aig (and_aig (not_aig (a), not_aig (b)));
}

/*
 \param a an AIG node
 \param b an AIG node
 \return (a^b)
 \brief Computes an AIG for the exclusive-sum of the functions 
        represented by a and b.
*/
static AIG *
xor_aig (AIG * a, AIG * b)
{
  return or_aig (and_aig (not_aig (a), b), and_aig (not_aig (b), a));
}

/*
 \param a an AIG node
 \param b an AIG node
 \return (a->b)
 \brief Computes an AIG for the implication of the functions 
        represented by a and b.
*/
static AIG *
implies_aig (AIG * a, AIG * b)
{
  return not_aig (and_aig (a, not_aig (b)));
}

/*
 \param c an AIG node (condition)
 \param a an AIG node (then)
 \param b an AIG node (else)
 \return (c?t:e)
 \brief Computes an AIG for the if-then-else operator of the functions 
        represented by c, t, and e.
*/
static AIG *
ite_aig (AIG * c, AIG * t, AIG * e)
{
  return and_aig (implies_aig (c, t), implies_aig (not_aig (c), e));
}

/*
 \param net network for which Tseitin variables for the AIGs are to be generated
 \return >1 if all is OK, 0 otherwise
 \brief Assigns Tseitin indices to all AIGs corresponding to some network node.
*/
static int
tseitin_network (BnetNetwork * net)
{
  ands = 0;

  int result;
  int i;
  BnetNode *node;

  /* assign Tseitin indices to next-state functions. */
  for (i = 0; i < net->nlatches; i++)
    {
      if (!st_lookup (net->hash, net->latches[i][0], &node))
	{
	  continue;
	}
      if (verbose > 2)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] assigning Tseitin indices for AIG of next-state-function of latch #%d (%s)\n",
			  i, (node->name ? node->name : "[not available]"));
	}
      result = tseitin_aig (node->aig);
      if (result == 0)
	return (0);
      if (verbose > 2)
	{
	  (void) fprintf (stderr, "=[bliftoaig] ... done (%s)\n", node->name);
	}
    }

  /* assign Tseitin indices to output functions */
  for (i = 0; i < net->npos; i++)
    {
      if (!st_lookup (net->hash, net->outputs[i], &node))
	{
	  continue;
	}
      if (verbose > 2)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] assigning Tseitin indices for AIG of output #%d (%s)\n",
			  i, (node->name ? node->name : "[not available]"));
	}
      result = tseitin_aig (node->aig);
      if (result == 0)
	return (0);
      if (verbose > 2)
	{
	  (void) fprintf (stderr, "=[bliftoaig] ... done (%s)\n", node->name);
	}
    }

  msg (1, "%u ands", ands);

  if (verbose > 1)
    {
      (void) fprintf (stderr,
		      "=[bliftoaig] latest assigned Tseitin index: %10d\n",
		      (tseitin_idx_running - 2));
      (void) fprintf (stderr,
		      "=[bliftoaig] number of inputs:              %10d\n",
		      inputs);
      (void) fprintf (stderr,
		      "=[bliftoaig] number of latches:             %10d\n",
		      latches);
      (void) fprintf (stderr,
		      "=[bliftoaig] number of AND-nodes:           %10d\n",
		      ands);
      fflush (stderr);
    }

  assert (inputs + latches + ands == (tseitin_idx_running - 2) / 2);
  return 1;
}

/*
 \param aig an AIG node
 \return >0 if all is OK, 0 otherwise
 \brief Assigns recursively Tseitin indices to AIG nodes.
*/
static int
tseitin_aig (AIG * aig)
{
  int sign;

  if (aig == AIG_TRUE || aig == AIG_FALSE)
    return 1;

  strip_aig (sign, aig);

  if (aig->input_flag || aig->latch_flag)
    {
      assert (aig->idx);
      return 1;
    }

  if (aig->idx)
    return 1;

  tseitin_aig (aig->c0);
  tseitin_aig (aig->c1);

  aig->idx = tseitin_idx_running;
  tseitin_idx_running += 2;

  if (verbose > 3)
    {
      (void) fprintf (stderr,
		      "=[bliftoaig] AIG node %p is assigned Tseitin index #%5d.\n",
		      aig, aig->idx);
    }

  ands++;
  cache (aig, aig);

  return 1;
}

/*
 \param net network for which its AIGs are dumped to file
 \return >0 if all is OK, 0 otherwise
 \brief Dump all AIGER data to the proposed file/stdout.
*/
static int
dump_network (BnetNetwork * net)
{
  int i;
  BnetNode *node, *node2;

  aiger_mgr = aiger_init ();

  /* add inputs */
  for (i = 0; i < net->npis; i++)
    {
      if (!st_lookup (net->hash, net->inputs[i], &node))
	{
	  continue;
	}
      if (verbose > 1)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] dumping primary input #%d (%s) with Tseitin index %d\n",
			  i, (node->name ? node->name : "[not available]"),
			  aig_idx ((AIG *) (node->aig)));
	  fflush (stdout);
	}

      aiger_add_input (aiger_mgr, aig_idx ((AIG *) (node->aig)),
		       strip_symbols ? 0 : node->name);
    }

  /* add latches */
  for (i = 0; i < net->nlatches; i++)
    {
      unsigned reset;
      if (!st_lookup (net->hash, net->latches[i][1], &node))
	{
	  continue;
	}
      if (!st_lookup (net->hash, net->latches[i][0], &node2))
	{
	  die ("Lookup for next-state function failed.\n");
	}

      aiger_add_latch (aiger_mgr, aig_idx ((AIG *) (node->aig)),
		       aig_idx ((AIG *) (node2->aig)),
		       strip_symbols ? 0 : node->name);
      if (net->latches[i][2][0] == '0')
        reset = 0;
      else if (net->latches[i][2][0] == '1')
        reset = 1;
      else
        reset = aig_idx ((AIG *) (node->aig));
      aiger_add_reset (aiger_mgr, aig_idx ((AIG *) (node->aig)),
                       reset);
    }

  /* add AND-nodes */
  if (!add_ands ())
    {
      die ("A failure occured when dumping AND-nodes.\n");
    }

  /* add outputs */
  for (i = 0; i < net->npos; i++)
    {
      if (!st_lookup (net->hash, net->outputs[i], &node))
	{
	  continue;
	}
      if (verbose > 1)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] dumping primary output #%d (%s)\n",
			  i, (node->name ? node->name : "[not available]"));
	}

      aiger_add_output (aiger_mgr, aig_idx ((AIG *) (node->aig)),
			strip_symbols ? 0 : node->name);
    }

  if (!strip_symbols)
    {
      aiger_add_comment (aiger_mgr, "bliftoaig");
      aiger_add_comment (aiger_mgr, aiger_version ());
      aiger_add_comment (aiger_mgr, blif_file);
    }

  if (output_name)
    {
      if (!aiger_open_and_write_to_file (aiger_mgr, output_name))
	die ("Failed to write to %s", output_name);
    }
  else
    {
      aiger_mode mode = ascii ? aiger_ascii_mode : aiger_binary_mode;
      if (!aiger_write_to_file (aiger_mgr, mode, stdout))
	die ("Failed to write to <stdout>");
    }

  aiger_reset (aiger_mgr);

  return 1;
}

/*--AIGER_stuff::end------------------------------------------------------*/

/*--CUDD_STUFF::begin-----------------------------------------------------*/

/* 
 \param filename filename
 \param mode read modus
 \return a file pointer
 \brief Opens a file, or fails with an error message and exits.
        Allows '-' as a synonym for standard input.
 \sa Borrowed from CUDD::nanotrav
*/
static FILE *
open_file (char *filename, const char *mode)
{
  FILE *fp;

  if (strcmp (filename, "-") == 0)
    {
      return mode[0] == 'r' ? stdin : stdout;
    }
  else if ((fp = fopen (filename, mode)) == NULL)
    {
      perror (filename);
      exit (1);
    }
  return (fp);

}				/* end of open_file */

/* 
 \param net network for which AIGs are to be built
 \return >0 if all is OK, 0 otherwise
 \brief Builds AIGs for a network outputs and next state.

 Builds AIGs for a network outputs and next state functions. 
 The method is really brain-dead, but it is very simple.
 Returns 1 in case of success; 0 otherwise.
 
 The aig fields of the network nodes are modified. 

 \sa Borrowed and adapted from CUDD::nanotrav.
*/
static int
Bnet_BuildAIGs (BnetNetwork * net)
{
  int result;
  int i;
  BnetNode *node;

  /* Make sure all inputs have a AIG */
  for (i = 0; i < net->npis; i++)
    {
      if (!st_lookup (net->hash, net->inputs[i], &node))
	{
	  return (0);
	}
      result = Bnet_BuildNodeAIG (node, net->hash);
      if (result == 0)
	return (0);
    }

  /* Make sure all latches have a AIG */
  for (i = 0; i < net->nlatches; i++)
    {
      if (!st_lookup (net->hash, net->latches[i][1], &node))
	{
	  return (0);
	}
      result = Bnet_BuildNodeAIG (node, net->hash);
      if (result == 0)
	return (0);
    }

  /* Build AIGs for the next-state-functions of the circuit */
  for (i = 0; i < net->nlatches; i++)
    {
      if (!st_lookup (net->hash, net->latches[i][0], &node))
	{
	  continue;
	}
      if (verbose > 0)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] building AIG for next-state-function of latch #%d (%s)\n",
			  i, (node->name ? node->name : "[not available]"));
	}
      result = Bnet_BuildNodeAIG (node, net->hash);
      if (result == 0)
	return (0);
      if (verbose > 0)
	{
	  (void) fprintf (stderr, "=[bliftoaig] ... done (%s)\n", node->name);
	}
    }

  /* Build AIGs for the outputs of the circuit */
  for (i = 0; i < net->npos; i++)
    {
      if (!st_lookup (net->hash, net->outputs[i], &node))
	{
	  continue;
	}
      if (verbose > 0)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] building AIG for output #%d (%s)\n",
			  i, (node->name ? node->name : "[not available]"));
	}
      result = Bnet_BuildNodeAIG (node, net->hash);
      if (result == 0)
	return (0);
      if (verbose > 0)
	{
	  (void) fprintf (stderr, "=[bliftoaig] ... done (%s)\n", node->name);
	}
    }

  return (1);

}				/* end of Bnet_BuildAIGs */

/* 
 \param nd node of the boolean network
 \param hash symbol table of the boolean network
 \return 1 in case of success; 0 otherwise
 \brief Builds the AIG for the function of a node.

 Builds the AIG for the function of a node and stores a
 pointer to it in the aig field of the node itself.
 Bnet_BuildNodeAIG returns 1 in case of success; 0 otherwise.

 Sets the aig field of the node.

 \sa CUDD::Bnet_BuildNodeBDD
*/
static int
Bnet_BuildNodeAIG (BnetNode * nd, st_table * hash)
{
  AIG *func;
  AIG *tmp;
  AIG *prod, *var;
  BnetNode *auxnd;
  BnetTabline *line;
  int i;

  /* AIG already built?! */
  if (nd->aig != (void *) (NULL))
    {
      if (verbose > 4)
	{
	  (void) fprintf (stderr,
			  "=[bliftoaig] AIG for module %10s already computed (%p)\n",
			  nd->name, nd->aig);
	  fflush (stderr);
	}
      return (1);
    }

  if (verbose > 2)
    {
      (void) fprintf (stderr, "=[bliftoaig] computing AIG for module %s\n",
		      nd->name);
      fflush (stderr);
    }

  if (nd->type == BNET_CONSTANT_NODE)
    {
      if (nd->f == NULL)
	{			/* constant 0 */
	  func = AIG_FALSE;
	}
      else
	{			/* either constant depending on the polarity */
	  func = AIG_TRUE;
	}
    }
  else if (nd->type == BNET_INPUT_NODE || nd->type == BNET_PRESENT_STATE_NODE)
    {
      if (nd->active == CUDD_TRUE)
	{			/* a variable is already associated: use it */
	  func = (AIG *) (nd->aig);
	  if (func == NULL)
	    goto failure;
	}
      else
	{			/* no variable associated: get a new one */
	  nd->var = tseitin_idx_running;
	  tseitin_idx_running += 2;
	  func = new_aig (nd->var, (AIG *) (NULL), (AIG *) (NULL));
	  if (func == NULL)
	    goto failure;
	  nd->active = CUDD_TRUE;
	  func->name = malloc (strlen (nd->name) + 1);
	  strcpy (func->name, nd->name);
	  if (nd->type == BNET_INPUT_NODE)
	    {
	      if (verbose > 1)
		{
		  (void) fprintf (stderr,
				  "=[bliftoaig] assigned Tseitin index %d (%d) to input %s (current Tseitin idx: %d)\n",
				  nd->var, aig_idx ((AIG *) (func)), nd->name,
				  tseitin_idx_running);
		  fflush (stdout);
		}
	      func->input_flag = 1;
	      inputs++;
	    }
	  if (nd->type == BNET_PRESENT_STATE_NODE)
	    {
	      if (verbose > 1)
		{
		  (void) fprintf (stderr,
				  "=[bliftoaig] assigned Tseitin index %d to latch %s\n",
				  nd->var, nd->name);
		  fflush (stdout);
		}
	      func->latch_flag = 1;
	      latches++;
	    }
	}
    }
  else if (Bnet_BuildExorAIG (nd, hash))
    {
      func = (AIG *) (nd->aig);
    }
  else if (Bnet_BuildMuxAIG (nd, hash))
    {
      func = (AIG *) (nd->aig);
    }
  else
    {
      /* type == BNET_INTERNAL_NODE or BNET_OUTPUT_NODE */
      /* Initialize the sum to logical 0. */
      func = AIG_FALSE;

      /* Build a term for each line of the table and add it to the
       ** accumulator (func).
       */
      line = nd->f;
      while (line != NULL)
	{
	  if (verbose > 4)
	    {
	      (void) fprintf (stderr,
			      "=[bliftoaig] (module %10s) line = %s\n",
			      nd->name, line->values);
	      fflush (stderr);
	    }

	  /* Initialize the product to logical 1. */
	  prod = AIG_TRUE;
	  /* Scan the table line. */
	  for (i = 0; i < nd->ninp; i++)
	    {
	      if (line->values[i] == '-')
		continue;
	      if (!st_lookup (hash, nd->inputs[i], &auxnd))
		{
		  goto failure;
		}
	      if (auxnd->aig == (void *) (NULL))
		{
		  if (!Bnet_BuildNodeAIG (auxnd, hash))
		    {
		      goto failure;
		    }
		}
	      if (line->values[i] == '1')
		{
		  var = (AIG *) (auxnd->aig);
		}
	      else
		{
		  /* line->values[i] == '0' */
		  var = not_aig ((AIG *) (auxnd->aig));
		}
	      tmp = and_aig (prod, var);
	      if (tmp == NULL)
		goto failure;
	      prod = tmp;
	    }
	  tmp = or_aig (func, prod);
	  if (tmp == NULL)
	    goto failure;
	  func = tmp;
	  line = line->next;
	}
    }
  if (nd->polarity == 1)
    {
      nd->aig = not_aig (func);
    }
  else
    {
      nd->aig = func;
    }

  return (1);

failure:
  /* Here we should clean up the mess. */
  return (0);

}				/* end of Bnet_BuildNodeAIG */

/*
 \param nd node of the boolean network
 \param hash symbol table of the boolean network
 \return 1 if the AIG has been built; 0 otherwise
 \brief Builds AIG for a XOR function.

 Checks whether a function is a XOR with 2 or 3 inputs. If so,
 it builds the AIG. Returns 1 if the AIG has been built; 0 otherwise.

 \sa CUDD::Bnet_BuildExorAIG
*/
static int
Bnet_BuildExorAIG (BnetNode * nd, st_table * hash)
{
  int check[8];
  int i;
  int nlines;
  BnetTabline *line;
  AIG *func, *var, *tmp;
  BnetNode *auxnd;

  if (nd->ninp < 2 || nd->ninp > 3)
    return (0);

  nlines = 1 << (nd->ninp - 1);
  for (i = 0; i < 8; i++)
    check[i] = 0;
  line = nd->f;
  while (line != NULL)
    {
      int num = 0;
      int count = 0;
      nlines--;
      for (i = 0; i < nd->ninp; i++)
	{
	  num <<= 1;
	  if (line->values[i] == '-')
	    {
	      return (0);
	    }
	  else if (line->values[i] == '1')
	    {
	      count++;
	      num++;
	    }
	}
      if ((count & 1) == 0)
	return (0);
      if (check[num])
	return (0);
      line = line->next;
    }
  if (nlines != 0)
    return (0);

  if (verbose > 3)
    {
      (void) fprintf (stderr, "=[bliftoaig] (module %10s) is XOR\n",
		      nd->name);
      fflush (stderr);
    }

  /* Initialize the exclusive sum to logical 0. */
  func = AIG_FALSE;

  /* Scan the inputs. */
  for (i = 0; i < nd->ninp; i++)
    {
      if (!st_lookup (hash, nd->inputs[i], &auxnd))
	{
	  goto failure;
	}
      if (auxnd->aig == (void *) (NULL))
	{
	  if (!Bnet_BuildNodeAIG (auxnd, hash))
	    {
	      goto failure;
	    }
	}
      var = (AIG *) (auxnd->aig);
      tmp = xor_aig (func, var);
      if (tmp == NULL)
	goto failure;
      func = tmp;
      nd->aig = func;
    }

  return (1);

failure:
  return (0);

}				/* end of Bnet_BuildExorAIG */

/*
 \param nd node of the boolean network
 \param hash symbol table of the boolean network
 \return 1 if the AIG has been built; 0 otherwise
 \brief Builds AIG for a multiplexer.

 Checks whether a function is a 2-to-1 multiplexer. If so,
 it builds the AIG. Returns 1 if the AIG has been built; 0 otherwise.
 
 \sa CUDD::Bnet_BuildMuxBDD
*/
static int
Bnet_BuildMuxAIG (BnetNode * nd, st_table * hash)
{
  BnetTabline *line;
  char *values[2];
  int mux[2];
  int phase[2];
  int j;
  int nlines = 0;
  int controlC = -1;
  int controlR = -1;
  AIG *func, *f, *g, *h;
  BnetNode *auxnd;

  phase[0] = phase[1] = mux[0] = mux[1] = 0;

  if (nd->ninp != 3)
    return (0);

  for (line = nd->f; line != NULL; line = line->next)
    {
      int dc = 0;
      if (nlines > 1)
	return (0);
      values[nlines] = line->values;
      for (j = 0; j < 3; j++)
	{
	  if (values[nlines][j] == '-')
	    {
	      if (dc)
		return (0);
	      dc = 1;
	    }
	}
      if (!dc)
	return (0);
      nlines++;
    }

  /* At this point we know we have:
   **   3 inputs
   **   2 lines
   **   1 dash in each line
   ** If the two dashes are not in the same column, then there is
   ** exactly one column without dashes: the control column.
   */
  for (j = 0; j < 3; j++)
    {
      if (values[0][j] == '-' && values[1][j] == '-')
	return (0);
      if (values[0][j] != '-' && values[1][j] != '-')
	{
	  if (values[0][j] == values[1][j])
	    return (0);
	  controlC = j;
	  controlR = values[0][j] == '0';
	}
    }
  assert (controlC != -1 && controlR != -1);

  if (verbose > 3)
    {
      (void) fprintf (stderr, "=[bliftoaig] (module %10s) is MUX\n",
		      nd->name);
      fflush (stderr);
    }

  /* At this point we know that there is indeed no column with two
   ** dashes. The control column has been identified, and we know that
   ** its two elements are different. 
   */
  for (j = 0; j < 3; j++)
    {
      if (j == controlC)
	continue;
      if (values[controlR][j] == '1')
	{
	  mux[0] = j;
	  phase[0] = 0;
	}
      else if (values[controlR][j] == '0')
	{
	  mux[0] = j;
	  phase[0] = 1;
	}
      else if (values[1 - controlR][j] == '1')
	{
	  mux[1] = j;
	  phase[1] = 0;
	}
      else if (values[1 - controlR][j] == '0')
	{
	  mux[1] = j;
	  phase[1] = 1;
	}
    }

  /* Get the inputs. */
  if (!st_lookup (hash, nd->inputs[controlC], &auxnd))
    {
      goto failure;
    }
  if (auxnd->aig == (void *) (NULL))
    {
      if (!Bnet_BuildNodeAIG (auxnd, hash))
	{
	  goto failure;
	}
    }
  f = auxnd->aig;
  if (!st_lookup (hash, nd->inputs[mux[0]], &auxnd))
    {
      goto failure;
    }
  if (auxnd->aig == (void *) (NULL))
    {
      if (!Bnet_BuildNodeAIG (auxnd, hash))
	{
	  goto failure;
	}
    }
  g = auxnd->aig;
  g = not_cond_aig (g, phase[0]);
  if (!st_lookup (hash, nd->inputs[mux[1]], &auxnd))
    {
      goto failure;
    }
  if (auxnd->aig == (void *) (NULL))
    {
      if (!Bnet_BuildNodeAIG (auxnd, hash))
	{
	  goto failure;
	}
    }
  h = auxnd->aig;
  h = not_cond_aig (h, phase[1]);
  func = ite_aig (f, g, h);
  if (func == NULL)
    goto failure;
  nd->aig = func;

  return (1);

failure:
  return (0);

}				/* end of Bnet_BuildMuxAIG */

/*--CUDD_STUFF::end-------------------------------------------------------*/

/*--bliftoaig::main-------------------------------------------------------*/

/*
 \param argc number of command line arguments
 \param argv command line arguments
 \return >0 is all is OK, 0 otherwise
 \brief Main routine for bliftoaig, checking command line options, 
        and calling aiger-routines.
*/
int
main (int argc, char **argv)
{
  int i;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, stdout);
	  exit (0);
	}
      else if (!strcmp (argv[i], "-v"))
	verbose++;
      else if (!strcmp (argv[i], "-s"))
	strip_symbols = 1;
      else if (!strcmp (argv[i], "-a"))
	ascii = 1;
      else if (argv[i][0] == '-' && argv[i][1] == 'O')
	{
	  optimize = atoi (argv[i] + 2);
	  if (optimize != 1 && optimize != 2 && optimize != 3
	      && optimize != 4)
	    die ("Can only use 1, 2, 3 or 4 as argument to '-O'");
	}
      else if (argv[i][0] == '-')
	die ("Unknown command line option '%s' (try '-h')", argv[i]);
      else if (output_name)
	die ("Too many files");
      else if (input)
	output_name = argv[i];
      else if (!(input = open_file (argv[i], "r")))
	die ("Can not read '%s'", argv[i]);
      else
	{
	  blif_file = argv[i];
	  close_input = 1;
	}
    }

  if (ascii && output_name)
    die ("Illegal: '-a' in combination with 'dst'");

  if (!ascii && !output_name && isatty (1))
    ascii = 1;

  if (!input)
    {
      input = stdin;
      blif_file = "<stdin>";
    }

  /* taken and adapted from CUDD::nanotrav */
  BnetNetwork *net = NULL;	/* BLIF network */
  net = Bnet_ReadNetwork (input, verbose);
  if (close_input)
    {
      (void) fclose (input);
    }
  if (net == NULL)
    {
      (void) fprintf (stderr, "=[bliftoaig] Syntax error in %s.\n",
		      blif_file);
      exit (2);
    }
  /* ... and optionally echo it to the standard output. */
  if (verbose > 2)
    {
      Bnet_PrintNetwork (net);
    }
  /* end of code from CUDD::nanotrav */

  /* build AIGs */
  if (!Bnet_BuildAIGs (net))
    {
      die ("Something's wrong with the BLIF-file. Cannot build AIGs.");
    }

  /* assign Tseitin variables to AIG nodes */
  if (!tseitin_network (net))
    {
      die
	("Something's went wrong during Tseitin variable generation. Cannot dump AIG.");
    }

  /* dump AIGs to AIGER-file */
  if (!dump_network (net))
    {
      die ("Something's went wrong during dumping network AIGs to a file.");
    }

  /* release AIGs */
  release ();

  /* release network */
  Bnet_FreeNetwork (net);

  return 0;
}
