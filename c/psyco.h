 /***************************************************************/
/***                 Global Psyco definitions                  ***/
 /***************************************************************/

#ifndef _PSYCO_H
#define _PSYCO_H


#include <Python.h>
#include <structmember.h>   /* for offsetof() */


/*****************************************************************/
 /***   Various customizable parameters (use your compilers'    ***/
  /***   option to override them, e.g. -DXXX=value in gcc)       ***/

 /* set to 0 to disable all debugging checks and output */
#ifndef PSYCO_DEBUG
# define PSYCO_DEBUG   0
#endif


 /* define to 1 for extra assert()'s */
#ifndef ALL_CHECKS
# define ALL_CHECKS    (PSYCO_DEBUG ? 1 : 0)
#endif

 /* level of debugging outputs: 0 = none, 1 = a few, 2 = more,
    3 = detailled, 4 = full execution trace */
#ifndef VERBOSE_LEVEL
# define VERBOSE_LEVEL   (PSYCO_DEBUG ? 0 : 0)
#endif

 /* dump information about profiling and statistics */
#ifndef VERBOSE_STATS
# define VERBOSE_STATS   (VERBOSE_LEVEL>=2)
#endif

 /* define for *heavy* memory checking: 0 = off, 1 = reasonably heavy,
                                        2 = unreasonably heavy */
#ifndef HEAVY_MEM_CHECK
# define HEAVY_MEM_CHECK   (PSYCO_DEBUG ? 0 : 0)
#endif
#ifdef MS_WIN32
# undef HEAVY_MEM_CHECK
# define HEAVY_MEM_CHECK   0  /* not supported on Windows */
#endif

 /* define to write produced blocks of code into a file; see 'xam.py'
       0 = off, 1 = only manually (from a debugger or with _psyco.dumpcodebuf()),
       2 = only when returning from Psyco,
       3 = every time a new code block is built */
#ifndef CODE_DUMP
# define CODE_DUMP         (PSYCO_DEBUG ? 1 : 0)
#endif

#if CODE_DUMP && !defined(CODE_DUMP_FILE)
# define CODE_DUMP_FILE    "psyco.dump"
#endif

 /* define to inline the most common functions in the produced code
    (should be enabled unless you want to trade code size for speed) */
#ifndef INLINE_COMMON_FUNCTIONS
# define INLINE_COMMON_FUNCTIONS     1
#endif

#if CODE_DUMP && defined(HAVE_DLFCN_H)
 /* define to locate shared symbols and write them in CODE_DUMP_FILE
    requires the GNU extension dladdr() in <dlfcn.h>
    Not really useful, only finds non-static symbols. */
/*# include <dlfcn.h>
  # define CODE_DUMP_SYMBOLS*/
#endif

#define DEFAULT_RECURSION    10   /* default value for the 'rec' argument */


/*****************************************************************/

/* Size of buffer to allocate when emitting code.
   Can be as large as you like (most OSes will not actually allocate
   RAM pages before they are actually used). We no longer perform
   any realloc() on this; a single allocated code region is reused
   for all code buffers until it is exhausted. There are BUFFER_MARGIN
   unused bytes at the end, so BIG_BUFFER_SIZE has better be large to
   minimize this effect.
   Linux note: I've seen in my version of glibc's malloc() that it
   uses mmap for sizes >= 128k, and that it will refuse the use mmap
   more than 1024 times, which means that if you allocate blocks of
   128k you cannot allocate more than 128M in total.
   Note that Psyco will usually allocate and fill two buffers in
   parallel, by the way vcompiler.c works. However, it occasionally
   needs more; codemanager.c can handle any number of parallely-growing
   buffers. There is a safeguard in vcompiler.c to put an upper bound
   on this number (currently should not exceed 4).
   In debugging mode, we use a small size to stress the buffer-
   continuation coding routines. */
#ifndef BIG_BUFFER_SIZE
# define BIG_BUFFER_SIZE  (PSYCO_DEBUG ? 2*BUFFER_MARGIN : 0x100000)
#endif

/* A safety margin for occasional overflows: we might write a few
   instructions too much before we realize we wrote past 'codelimit'.
   XXX carefully check that it is impossible to overflow by more
   We need more than 128 bytes because of the way conditional jumps
   are emitted; see pycompiler.c.
   The END_CODE macro triggers a silent buffer change if space is
   getting very low -- less than GUARANTEED_MINIMUM */
#ifndef BUFFER_MARGIN
# define BUFFER_MARGIN    1024
#endif

/* When emitting code, all called functions can assume that they
   have at least this amount of room to write their code. If they
   might need more, they have to allocate new buffers and write a
   jump to these from the original code (jumps can be done in less
   than GUARANTEED_MINIMUM bytes). */
#ifndef GUARANTEED_MINIMUM
# define GUARANTEED_MINIMUM    64
#endif


#ifndef ALL_STATIC
# define ALL_STATIC  0   /* make all functions static; set to 1 by hack.c */
#endif

#if ALL_STATIC
# define EXTERNVAR   staticforward
# define EXTERNFN    static
# define DEFINEVAR   statichere
# define DEFINEFN    static
# define INITIALIZATIONFN  inline
#else
# define EXTERNVAR
# define EXTERNFN
# define DEFINEVAR
# define DEFINEFN
# define INITIALIZATIONFN  DEFINEFN
#endif

#if ALL_CHECKS
# define MALLOC_CHECK_    2  /* GCC malloc() checks */
# undef NDEBUG
# include <assert.h>
# if CODE_DUMP
#  define extra_assert(x) ((x) ? (void)0 : (psyco_dump_code_buffers(),       \
                                  assert(x), assert(!"volatile assertion")))
# else
#  define extra_assert(x) assert(x)
# endif
#else
# define extra_assert(x)  (void)0  /* nothing */
#endif

#if VERBOSE_LEVEL
# define debug_printf(level, args)     do { \
        if (VERBOSE_LEVEL >= (level)) {     \
          psyco_debug_printf args;          \
        }                                   \
        if (psyco_logger && (level) == 1) { \
          psyco_flog args;                  \
        }                                   \
      } while (0)
EXTERNFN void psyco_debug_printf(char* msg, ...);
#else
# define debug_printf(level, args)     do { \
        if (psyco_logger && (level) == 1) { \
          psyco_flog args;                  \
        }                                   \
      } while (0)
#endif
EXTERNVAR PyObject* psyco_logger;
EXTERNFN void psyco_flog(char* msg, ...);

#if VERBOSE_LEVEL >= 4
# define TRACE_EXECUTION(msg)         do {                                    \
  BEGIN_CODE  EMIT_TRACE(msg, psyco_trace_execution);  END_CODE } while (0)
# define TRACE_EXECUTION_NOERR(msg)   do {                                    \
  BEGIN_CODE  EMIT_TRACE(msg, psyco_trace_execution_noerr);  END_CODE } while (0)
EXTERNFN void psyco_trace_execution(char* msg, void* code_position);
EXTERNFN void psyco_trace_execution_noerr(char* msg, void* code_position);
#else
# define TRACE_EXECUTION(msg)         do { } while (0) /* nothing */
# define TRACE_EXECUTION_NOERR(msg)   do { } while (0) /* nothing */
#endif


#if INLINE_COMMON_FUNCTIONS
# define inline      __inline static
#else
# define inline      static
#endif

#if HEAVY_MEM_CHECK
# include "linuxmemchk.h"
# if HEAVY_MEM_CHECK > 1
#  define PSYCO_NO_LINKED_LISTS
# endif
#endif


#ifndef bool
typedef int bool;
#endif
#ifndef false
# define false   0
#endif
#ifndef true
# define true    1
#endif

#ifndef PyObject_TypeCheck
# define PyObject_TypeCheck(o,t)   ((o)->ob_type == (t))
#endif


typedef unsigned char code_t;

typedef struct vinfo_s vinfo_t;             /* defined in compiler.h */
typedef struct vinfo_array_s vinfo_array_t; /* defined in compiler.h */
typedef struct PsycoObject_s PsycoObject;   /* defined in compiler.h */
typedef struct FrozenPsycoObject_s FrozenPsycoObject; /* def in dispatcher.h */
typedef struct CodeBufferObject_s CodeBufferObject;  /* def in codemanager.h */
typedef struct global_entries_s global_entries_t;  /* def in dispatcher.h */
typedef struct mergepoint_s mergepoint_t;   /* defined in mergepoint.h */
typedef struct stack_frame_info_s stack_frame_info_t; /* def in pycompiler.h */

EXTERNVAR PyObject* PyExc_PsycoError;
EXTERNVAR long psyco_memory_usage;   /* approximative */


/* moved here from vcompiler.h because needed by numerous header files */
typedef bool (*compute_fn_t)(PsycoObject* po, vinfo_t* vi, bool force);
typedef struct {
  compute_fn_t compute_fn;
} source_virtual_t;


#if CODE_DUMP
EXTERNFN void psyco_dump_code_buffers(void);
#endif
#if CODE_DUMP >= 3
# define dump_code_buffers()    psyco_dump_code_buffers()
#else
# define dump_code_buffers()    do { } while (0) /* nothing */
#endif

/* to display code object names */
#define PyCodeObject_NAME(co)   (co->co_name ? PyString_AS_STRING(co->co_name)  \
                                 : "<anonymous code object>")


/* defined in pycompiler.c */
#define GLOBAL_ENTRY_POINT	psyco_pycompiler_mainloop
EXTERNFN code_t* psyco_pycompiler_mainloop(PsycoObject* po);


/* XXX no handling of out-of-memory conditions. We have to define precisely
   what should occur in various cases, like when we run out of memory in the
   middle of writing code, when the beginning is already executing. When
   should we report the exception? */
#define OUT_OF_MEMORY()      Py_FatalError("psyco: out of memory")

/* Thread-specific state */
EXTERNFN PyObject* psyco_thread_dict(void);

/* defined in dispatcher.c */
EXTERNFN void PsycoObject_EmergencyCodeRoom(PsycoObject* po);


#endif /* _PSYCO_H */
