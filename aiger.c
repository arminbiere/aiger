#include "aiger.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct aiger_internal aiger_internal;

struct aiger_internal
{
  aiger public_interface;
  int size_literals;
  int size_inputs;
  int size_next_state_functions;
  int size_outputs;
  void * memory_mgr;
  aiger_malloc malloc_callback;
  aiger_free free_callback;
};

aiger *
aiger_init_mem (
  void * memory_mgr,
  aiger_malloc external_malloc,
  aiger_free external_free)
{
  aiger_internal * res;
  assert (external_malloc);
  assert (external_free);
  res = external_malloc (memory_mgr, sizeof (*res));
  memset (res, 0, sizeof (*res));
  res->memory_mgr = memory_mgr;
  res->malloc_callback = external_malloc;
  res->free_callback = external_free;
  return &res->public_interface;
}

static void *
aiger_default_malloc (void * state, size_t bytes)
{
  return malloc (bytes);
}

static void
aiger_default_free (void * state, void * ptr, size_t bytes)
{
  free (ptr);
}

aiger *
aiger_init (void)
{
  return aiger_init_mem (0, aiger_default_malloc, aiger_default_free);
}

#define NEW(p) \
  do { \
    size_t bytes = sizeof (*(p)); \
    (p) = mgr->malloc_callback (mgr->memory_mgr, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define DELETE(p) \
  do { \
    size_t bytes = sizeof (*(p)); \
    mgr->free_callback (mgr->memory_mgr, (p), bytes); \
  } while (0)

#define NEWN(p,n) \
  do { \
    size_t bytes = (n) * sizeof (*(p)); \
    (p) = mgr->malloc_callback (mgr->memory_mgr, bytes); \
    memset ((p), 0, bytes); \
  } while (0)

#define IMPORT(p) \
  aiger_internal * mgr = (aiger_internal*) public_interface

void
aiger_reset (aiger * public_interface)
{
  IMPORT (public_interface);
  DELETE (mgr);
}
