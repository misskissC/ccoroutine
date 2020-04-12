/**
 * yield_e.c,
 * for experiencing yeild mechanism
 *
 * lxr, 2020.01 */

#include "ln_co.h"
#include "ln_comm.h"
#include <string.h>

/* the stack needed by interfaces is about CS_INNER_STACK.
 * the _fo_fn's stack got by its assembly:
 * 
 * _co_fn:
 *     pushq   %rbp
 *     movq    %rsp, %rbp
 *     subq    $48, %rsp
 * ... */
#define _COFN      ((48))
#define _CO_STACK  (_COFN + CS_INNER_STACK)
#define CMMB_UNIT  (_CO_STACK)
#define CNR_UNIT   (1)

#define _HL32TOUPTR(l32, h32) \
    ((void *)(l32 | ((uintptr_t)h32 << 32)))

/* example for coroutine-arguments */
typedef struct _co_arg_s {
    int nr;
} cofn_arg_s;

/* coroutine type is co_t, see ln_cs.h. */
static void 
_co_fn(uint32_t ci_l32, uint32_t ci_h32, 
    uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    cofn_arg_s *ar;

    ci = _HL32TOUPTR(ci_l32,  ci_h32);
    ar = _HL32TOUPTR(arg_l32, arg_h32);
    IF_EXPS_THEN_TIPS_AND_RETURN(!ci || !ar, 
        VOIDV, "bad parameter in %s\n", __func__);

    int i, nr = ar->nr;
    for (i = 0; i < nr; ++i) {
        ln_fprintf(10, "%d\n", i);
        IF_EXPS_THEN_RETURN(co_yield(ci), VOIDV);
    }

    co_end(ci);
    return ;
}

static void 
_co_fn_sends(cc_s *cc)
{
    ci_s *ci = NULL;
    cofn_arg_s arg = {3};

    ci = co_co(cc, "_co_fn", _co_fn, &arg);
    IF_EXPS_THEN_TIPS_AND_RETURN(!ci, VOIDV, 
        "Failed to create co for _co_fn\n");

    co_send(ci);
    co_send(ci);
    co_send(ci);
    co_send(ci);
    co_send(ci);
    
    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = co_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_fn_sends(cc);
    co_deinit(cc);

    return 0;
}
