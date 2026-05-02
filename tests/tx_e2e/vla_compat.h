// VLA compatibility header for MSVC
// MSVC doesn't support VLAs in C99 style, so we use alloca() instead

#ifndef _VLA_COMPAT_H_
#define _VLA_COMPAT_H_

#include <stdlib.h>

#ifdef _MSC_VER
    #include <malloc.h>
    #define VLA_ALLOC(type, name, size) \
        type* name = (type*)_alloca(sizeof(type) * (size))
#else
    #define VLA_ALLOC(type, name, size) \
        type name[size]
#endif

#endif
