/**
 * ln_uc.c,
 * routines for wrapping ucontext.
 *
 * lxr, 2020.02 */

#include "ln_context.h"
#include "ln_comm.h"
#include "ln_co.h"
#include <errno.h>

int 
co_start_uc(ci_s *ci)
{
    int ret;
    void   *arg  = NULL;
    cctx_s *ctx  = NULL;
    cctx_s *bctx = NULL;
    
    ctx = co_cctx(ci);
    ret = getcontext(ctx);
    IF_EXPS_THEN_RETURN(ret, errno);

    bctx = co_bcctx(ci);
    ctx->uc_link = bctx;
    ctx->uc_stack.ss_sp   = co_stack(ci);
    ctx->uc_stack.ss_size = co_ssize(ci);
    arg = co_arg(ci);
    /**
     * there's not matter if ci->co's type 
     * is not func_t, as long as ci->co 
     * wouldn't achieve more parameters than 
     * makecontext() passed. */
    typedef void (*func_t)(void);
    makecontext(ctx, (func_t)co_cofn(ci), 4, 
        (uint32_t)((uintptr_t)ci), 
        (uint32_t)((uintptr_t)ci >> 32), 
        (uint32_t)((uintptr_t)arg), 
        (uint32_t)((uintptr_t)arg >> 32) );
    ret = co_switch_to(bctx, ctx);
    IF_EXPS_THEN_RETURN(ret, errno);

    return ret;
}
