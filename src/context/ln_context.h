/**
 * ln_context.h,
 * to get the context of coroutine 
 * according to the macro predefined.
 *
 * lxr, 2020.02 */

#ifndef _LN_CONTEXT_H_
#define _LN_CONTEXT_H_

#if defined(CCTX_ASM)
typedef struct asm_cctx_s {
    void **sp;
} cctx_s;

#if __i386
#define CS_RESERVE_NR (4)
/* curr and next passed value by eax adn edx respectively */
extern void __attribute__ ((__noinline__, __regparm__(2))) 
co_switch_asm(cctx_s *curr, cctx_s *next);


#elif __amd64
#define CS_RESERVE_NR (6)
extern void 
co_switch_asm(cctx_s *curr, cctx_s *next);
#endif

#include "ln_co.h"
extern int 
co_start_asm(ci_s *ci);
#define co_switch_to(curr, next) ({co_switch_asm(curr, next); 0;})
#define co_start(ci) co_start_asm(ci);

#elif defined(CCTX_UC)
#include <ucontext.h>
typedef ucontext_t cctx_s;
#define co_switch_to(curr, next) swapcontext(curr, next)

#include "ln_co.h"
extern int 
co_start_uc(ci_s *ci);
#define co_start(ci) co_start_uc(ci)

#endif

#endif
