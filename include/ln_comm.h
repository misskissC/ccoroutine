/**
 * ln_comm.h,
 * common routines contained.
 *
 * lxr, 2020.01 */

#ifndef _LN_COMM_H_
#define _LN_COMM_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define callconvention
#define VOIDV (void)0

typedef enum comm_status_code {
    CODE_NONE = 0,
    CODE_BADPARAM,
    CODE_NOMEM,
    CODE_END,
    
    CODE_MAX
} comm_code_e;

/** 
 * high frequency used */
#ifdef __GNUC__
#define IF_EXPS_THEN_RETURN(exps, retv) \
({ \
    if (exps) return retv; \
})

#else
#define IF_EXPS_THEN_RETURN(exps, retv) \
do { \
    if (exps) return retv; \
} while (0)
#endif

#ifdef __GNUC__
#define IF_EXPS_THEN_BREAK(exps) \
({ \
    if (exps) break; \
})
#endif

#ifdef __GNUC__
#define IF_EXPS_THEN_CONTINUE(exps) \
({ \
    if (exps) continue; \
})
#endif

/**
 * low frequency used */
#ifdef __GNUC__
#define IF_EXPS_THEN_GOTO_LABEL(exps, label) \
({ \
    if (exps) goto label; \
})
#endif

#ifdef __GNUC__
#define IF_EXPS_THEN_TIPS(exps, format, ...) \
({ \
    if (exps) { \
        fprintf(stderr, format, ##__VA_ARGS__); \
    } \
})
#endif

#ifdef __GNUC__
#define IF_EXPS_THEN_TIPS_AND_RETURN(exps, retv, format, ...) \
({ \
    if (exps) { \
        fprintf(stderr, format, ##__VA_ARGS__); \
        return retv; \
    } \
})
#endif

static bool inline 
ln_free(void *ptr)
{
    free(ptr);
    return true;
}

#endif
