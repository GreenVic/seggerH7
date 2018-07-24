#define DCT_SCALING_SUPPORTED

#define JMALLOC  malloc
#define JFREE    free

#define NO_GETENV
#undef  USE_MSDOS_MEMMGR
#undef  USE_MAC_MEMMGR
#define USE_HEAP_MEM
#define MAX_ALLOC_CHUNK  0x10000 

#define HAVE_PROTOTYPES
#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT

#undef CHAR_IS_UNSIGNED

#define HAVE_STDDEF_H
#define HAVE_STDLIB_H

#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN

#ifdef JPEG_INTERNALS
  #undef RIGHT_SHIFT_IS_UNSIGNED
#endif 

#undef PROGRESS_REPORT
