/* Stub util/bmem.h for standalone unit tests. */
#pragma once

#include <stdlib.h>
#include <string.h>

#define bzalloc(n)  calloc(1, (n))
#define bfree(p)    free(p)
