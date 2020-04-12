/**
 * yield_from_e.c,
 * for experiencing yeild from mechanism
 *
 * lxr, 2020.01 */

#include "ln_co.h"
#include "ln_comm.h"
#include <string.h>

/* the stack needed by interfaces is about CS_INNER_STACK.
 * the _fo_fn's stack got by its assembly:
 * 
 * _co_yield_from_fn:
 *     pushq   %rbp
 *     movq    %rsp, %rbp
 *     subq    $48, %rsp
 * ... */
#define _COFROMFN  (48)
#define _CO_STACK  (_COFROMFN + CS_INNER_STACK)
#define CMMB_UNIT  (_CO_STACK)
#define CNR_UNIT   (2)

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
    crv_s *rv = co_rv(ci);
    
    memset(rv, 0x00, sizeof(crv_s));
    rv->buf = calloc(sizeof(int) * nr, 1);
    IF_EXPS_THEN_RETURN(!rv->buf, VOIDV);
    rv->flag = MM_HEAP_UFREE;
    for (i = 0; i < nr; ++i) {
        ((int *)rv->buf)[i] = i;
        rv->len++;
        co_yield(ci);
    }

    co_end(ci);
    return ;
}

static void 
_co_yield_from_fn(uint32_t ci_l32, uint32_t ci_h32, 
        uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    cofn_arg_s *ar;

    ci = _HL32TOUPTR(ci_l32,  ci_h32);
    ar = _HL32TOUPTR(arg_l32, arg_h32);
    IF_EXPS_THEN_TIPS_AND_RETURN(!ci || !ar, 
        VOIDV, "bad parameter in %s\n", __func__);

    crv_s *rv = NULL;
    rv = co_yield_from(co_cc(ci), ci, "_co_fn", _co_fn, ar);

    int i;
    IF_EXPS_THEN_TIPS(true, "'%s' sync '_co_fn' terminated."
        " '_co_fn' return: ", __func__);
    for (i = 0; i < rv->len; ++i)
        IF_EXPS_THEN_TIPS(true, "%d", ((int *)(rv->buf))[i]);
   IF_EXPS_THEN_TIPS(true, "\n");
    if (MM_HEAP_UFREE == rv->flag) {
        free(rv->buf);
	}
    
	co_end(ci);
    return ;
}


static void 
_co_yield_from_sends(cc_s *cc)
{
    ci_s *ci;
    cofn_arg_s arg = {9};
    
    ci = co_co(cc, "_co_yield_from", _co_yield_from_fn, &arg);
    IF_EXPS_THEN_RETURN(!ci, VOIDV);

    crv_s *rv;
    while ((rv = co_send(ci))) {
        IF_EXPS_THEN_TIPS(true, "%d\n", ((int *)rv->buf)[rv->len - 1]);
    }
    IF_EXPS_THEN_TIPS(true, "\n");
    
    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = co_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_yield_from_sends(cc);
    co_deinit(cc);

    return 0;
}
