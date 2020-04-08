/**
 * ln_co.c,
 ** coroutine switching, sync, 
    concurrency mechanism implement.
  
 ** lxr, 2020.01 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "ln_co.h"
#include "ln_comm.h"
#include "ln_context.h"

/* define CO_STACK_PRE in Makefile 
   when running ln_cs.c on server
   which owns rich memory; then 
   those memories would be freed by 
   cc_s-unit.*/
#if MEMORY_ALLOC_PRE
#define CO_STACK_PRE
#endif

/* define RUNNING_WHEN_CREATING in 
   Makefile when call co_loop().
   RUNNING_WHEN_REATING is good for
   saving memory. */
#if RUNNING_WHEN_CREATING
#define LOOP_AWHILE
#endif

#define NONE        ( -1)
/* coroutine states */
#define PREGNANT    (0x00)
#define BORN        (0x01)
#define RUNNABLE    (0x02)
#define SUSPENDING  (0x04)
#define BACKCI      (0x08)
#define BACKYF      (0x10)

/* coroutine logic control */
#define YFCO    (0x01)

/* just give up cpu sovereignty */
#define NAP (99)
#define giveup_sov(us) ({usleep(us);true;})

/**
 * used to record coroutine informations. */
typedef struct coroutine_info_s {
    void *co;  /* coroutine subroutine */
    void *arg; /* coroutine arguments */
    char *id;  /* coroutine id/name */
    int state; /* coroutine states */

    /* coroutine stack,
       current coroutine context. */
    char *stack;
    cctx_s cctx;

    /* the memory bears 
       coroutine's return-value */
    crv_s rv; 

    /* switch to the coroutine 
       corresponded by "back" when 
       current coroutine switching 
       or terminated. */
    ci_s *back;

    /* the cc_s current ci_s belongs to */
    cc_s *cc;
} ci_s;

/**
 * coroutine control unit structure */
typedef struct coroutine_control_s {
    ci_s *ci; /* point to the (ci_s) array */
    /* the unit/total, unused ci_s numbers. */
    int nr, unused;
    
    /* coroutine stack size(byte) in 
       current coroutine control unit.*/
    int ss;

    /* magic box for logic control */
    char box;

    /* next coroutine control unit */
    cc_s *next;
} cc_s;

/* abbreviations */
#define CONR(cc)    (cc->nr - cc->unused)
#define BCCTX(ci)   (ci->back->cctx)
#define UNUSED(ci)  (ci->cc->unused)
#define BSTATE(ci)  (ci->back->state)
#define BUNUSED(ci) (ci->back->cc->unused)
#define _SET_CC_MBR(mbr, v) cc->mbr = v
#define _SET_CI_MBR(mbr, v) ci->mbr = v

static bool 
_put_unit_ci(ci_s *ci, int nr)
{
    int i;
    for (i = 0; i < nr; ++i)
        free(ci[i].stack);
    free(ci);
    
    return true;
}

static bool inline 
_put_cc(cc_s *cc)
{
    (void)_put_unit_ci(cc->ci, cc->nr);
    free(cc);
    
    return true;
}

static inline ci_s * 
_get_unit_ci(int nr, int ss)
{
    ci_s *ci = NULL;

    ci = (ci_s *)calloc(sizeof(ci_s) * nr, 1);
    IF_EXPS_THEN_RETURN(!ci, NULL);

#ifdef CO_STACK_PRE
    int i; char *stack = NULL;
    for (i = 0; i < nr; ++i) {
        stack = (char *)calloc(ss, 1);
        IF_EXPS_THEN_RETURN(!stack && _put_unit_ci(ci, i), NULL);
        ci[i].stack = stack;
    }
#endif
    
    return ci;
}

static cc_s * 
_get_cc(int nr, int ss)
{
    ci_s *ci = NULL;
    cc_s *cc = NULL;

    cc = (cc_s *)calloc(sizeof(cc_s), 1);
    IF_EXPS_THEN_RETURN(!cc, NULL);

    ci = _get_unit_ci(nr, ss);
    IF_EXPS_THEN_RETURN(!ci && ln_free(cc), NULL);
    _SET_CC_MBR(ss, ss);
    _SET_CC_MBR(ci, ci);
    _SET_CC_MBR(nr, nr);
    _SET_CC_MBR(unused, nr);

    return cc;
}

static int inline 
_get_cis_idle_ci(ci_s *ci, int nr)
{
    int h, t;
    int f = NONE;

#define _ST(i) ci[i].state
    for (h = 0, t = nr - 1; h <= t; ++h, --t) {
        IF_EXPS_THEN_BREAK(!_ST(h) && ((f = h) + 1));
        IF_EXPS_THEN_BREAK(!_ST(t) && (f = t));
    }
#undef _ST

    return f;
}

static ci_s * 
_get_cc_idle_ci(cc_s *cc)
{
    int f = NONE;
    cc_s *p   = NULL;
    cc_s *_cc = NULL;

    for (_cc = cc; _cc; _cc = _cc->next) {
        p = _cc;
        IF_EXPS_THEN_CONTINUE(!_cc->unused);
        f  = _get_cis_idle_ci(_cc->ci, _cc->nr);
        IF_EXPS_THEN_BREAK(NONE != f);
    }
    if (!_cc) {
        p->next = _get_cc(cc->nr, cc->ss);
        IF_EXPS_THEN_RETURN(!p->next, NULL);
        _cc = p->next;
        f = 0;
    }
    _cc->unused--;
    _cc->ci[f].cc = _cc;
    
    return (_cc->ci + f);
}

static int inline 
_co_start(ci_s *ci)
{
    return co_start(ci);
}

static int inline 
_co_switch(ci_s *ci)
{
    int ret;
    ci->state = RUNNABLE;
    ret = co_switch_to(&BCCTX(ci), &ci->cctx);
    IF_EXPS_THEN_RETURN(ret, errno);

    return ret;
}

static void inline 
_co_end(ci_s *ci)
{
    _SET_CI_MBR(co,     NULL);
    _SET_CI_MBR(arg,    NULL);
    _SET_CI_MBR(state, PREGNANT);
#ifndef CO_STACK_PRE
    free(ci->stack);
    _SET_CI_MBR(stack, NULL);
#endif
    UNUSED(ci)++;
    
    if (ci->back && (BSTATE(ci) == BACKCI)) {
        BSTATE(ci) = PREGNANT;
        BUNUSED(ci)++;
    }
    
    return ;
}

static void inline 
_running_ci(ci_s *ci)
{
    if ((BORN <= ci->state) &&
        (SUSPENDING >= ci->state)) {
        co_send(ci);
    }
    return ;
}

static void 
_cc_cos_scheduler(cc_s *cc)
{
    register int conr;
    register ci_s *s, *e;
    register cc_s *_cc = cc;

    s = _cc->ci;
    e = s + _cc->nr - 1;
    while ((conr = CONR(_cc)) && (s <= e)) {
        _running_ci(s); _running_ci(e);
        ++s; --e;
    }
    
    return ;
}

static void inline 
_loop_awhile(cc_s *cc, ci_s *ci)
{
    register int conr;
    register cc_s *_cc = NULL;

    IF_EXPS_THEN_RETURN((cc->box & YFCO) && 
        (BSTATE(ci) |= BACKYF), VOIDV);

    (void)co_send(ci);
    for (_cc = cc; _cc; _cc = _cc->next) {
        if ((conr = CONR(_cc)))
            _cc_cos_scheduler(_cc);
    }
    
    return ;
}

cc_s * 
co_init(int cnr, int ss)
{
    return _get_cc(cnr << 1, ss);
}

void 
co_deinit(cc_s *cc)
{
    cc_s *t, *_cc;
    
    IF_EXPS_THEN_RETURN(!cc, VOIDV);
    for (_cc = t = cc; _cc; _cc = t) {
        t = t->next;
        _put_cc(_cc);
    }

    return ;
}

ci_s *
co_co(cc_s *cc, char *id, void *co, void *arg)
{
    ci_s *ci  = NULL;
    
    IF_EXPS_THEN_RETURN(!cc || !co, NULL);

    ci = _get_cc_idle_ci(cc);
    IF_EXPS_THEN_RETURN(!ci, NULL);
#ifndef CO_STACK_PRE
    ci->stack = (char *)calloc(cc->ss, 1);
    IF_EXPS_THEN_RETURN(!ci->stack, NULL);
#endif
    _SET_CI_MBR(co, co);
    _SET_CI_MBR(id, id);
    _SET_CI_MBR(arg, arg);
    _SET_CI_MBR(state, BORN);
    
    ci->back = _get_cc_idle_ci(cc);
    IF_EXPS_THEN_RETURN(!ci->back && !(ci->state = PREGNANT), NULL);
    BSTATE(ci) = BACKCI;

#ifdef LOOP_AWHILE
    /* what a milestone 
       for running coroutine when creating new one!
       I have had try other dozens before this logic control. */
    _loop_awhile(cc, ci);
    giveup_sov(NAP);
#endif
    
    return ci;
}

int 
co_loop(cc_s *cc)
{
    bool has_co;
    cc_s *p, *_cc = NULL;

    IF_EXPS_THEN_RETURN(!cc, CODE_BADPARAM);
_loop:
    has_co = false;
    for (_cc = cc; _cc; _cc = _cc->next) {
        if (CONR(_cc)) {
            has_co = true;
            _cc_cos_scheduler(_cc);
        } else if (_cc != cc) {
            p->next = _cc->next;
            _put_cc(_cc);
            _cc = p;
        }
        p = _cc;
    }
IF_EXPS_THEN_GOTO_LABEL(has_co && giveup_sov(NAP), _loop);

    return CODE_NONE;
}


void * 
co_send(ci_s *ci)
{
    int state;
    void *out = NULL;
    int   ret = CODE_NONE;

    IF_EXPS_THEN_RETURN(!ci, NULL);
    /* I just heard that CPU likes to predict backward jump.
       yield from mechanism should be more commonly used in 
       practical project, i guess. */
    IF_EXPS_THEN_GOTO_LABEL((BSTATE(ci) & BACKYF), _end);

    state = ci->state;
    char *id = ci->id ? ci->id : "unnamed_co";
    IF_EXPS_THEN_TIPS_AND_RETURN(
        (BORN > state) || (SUSPENDING < state), 
        NULL, "%s not running now\n", id  );

    if (BORN == state) {
        ret = _co_start(ci);
    } else if (SUSPENDING == state) {
        ret = _co_switch(ci);
    }

    if (RUNNABLE == ci->state) {
        _co_end(ci);
    }

    out = (PREGNANT != ci->state) ? &ci->rv : out;
    IF_EXPS_THEN_TIPS(ret, "co switch error: %d\n", ret);

_end:
    return out;
}

int 
co_yield(ci_s *ci)
{
    int ret;
    IF_EXPS_THEN_RETURN(!ci, CODE_BADPARAM);
    ci->state = SUSPENDING;
    ret = co_switch_to(&ci->cctx, &BCCTX(ci));
    IF_EXPS_THEN_RETURN(ret, errno); 

    return ret;
}

void * 
co_yield_from(cc_s *cc, ci_s *self, 
    char *id, void *co, void *arg)
{
    int ret;
    ci_s *ci = NULL;
    void *t, *rv = NULL;
    
    cc->box |= YFCO;
    ci = co_co(cc, id, co, arg);
    cc->box &= ~YFCO;
    IF_EXPS_THEN_RETURN(!ci, NULL);

#define BSYF(ci) (ci->state && (BSTATE(ci) &= ~BACKYF))
    while (BSYF(ci) && (t = co_send(ci))) {
        if (BSTATE(ci)) BSTATE(ci) |= BACKYF;
        rv = t; self->rv = ci->rv;
        self->state = SUSPENDING;
        ret = co_switch_to(&self->cctx, &BCCTX(self));
        IF_EXPS_THEN_RETURN(ret, NULL);
    }
#undef BSYF

    return rv;
}

void 
co_end(ci_s *ci)
{
    IF_EXPS_THEN_RETURN(!ci, VOIDV);
    ci->state |= RUNNABLE;
    (void)co_switch_to(&ci->cctx, &BCCTX(ci));
}

crv_s * 
co_rv(ci_s *ci) 
{
    return ci ? &ci->rv : NULL;
}

cc_s * 
co_cc(ci_s *ci)
{
    return ci ? ci->cc : NULL;
}

char *
co_id(ci_s *ci)
{
    return ci ? ci->id : "";
}

void *
co_cctx(ci_s *ci)
{
    return ci ? &ci->cctx : NULL;
}

void *
co_bcctx(ci_s *ci)
{
    return ci ? &ci->back->cctx : NULL;
}

void * 
co_stack(ci_s *ci)
{
    return ci ? ci->stack : NULL;
}

int 
co_ssize(ci_s *ci)
{
    return ci ? ci->cc->ss : 0;
}

void * 
co_cofn(ci_s *ci)
{
    return ci ? ci->co : NULL;
}

void * 
co_arg(ci_s *ci)
{
    return ci ? ci->arg : NULL;
}
