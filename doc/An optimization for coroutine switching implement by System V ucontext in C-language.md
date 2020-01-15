### 序
前段时间，此文的电脑被人霸占做PPT去了。是的没错，霸占。

那些夜晚只能喝着一碗醪糟水并尝试阅读李娟的散文。随着翻页篇章稍厚，再一次陷入对其文字的带着羡慕的欣赏之中。这些与描述内容具天生契合性的文字竟间接地鼓舞着心灵不为小事而偏离善良。这些由一针一线编织在箱底的珍宝，真令人赞叹。

于是想到优化“一种在C语言中用 System V ucontext 实现的协程切换”的事。此种激励或许是此文对这个世界仅存有的了解了，所以再次做了等拿回电脑主权就将这些写下来的决心。

为了尽量突显此协程与其他并发方式[^1]在资源方面的优势，以及提升此协程在各型[^2]计算机上协程的并发上限，此文对该协程机制进行了一些修改和补充。

随之将其上传到 github 的主要原因是想到此文对该程序的优化手段或许已山穷水尽，从而希望互联网中与此文具相同理念的同学出出新招去进一步提升该程序的水平。
ucoroutine_switching github 地址：https://github.com/misskissC/ucoroutine_switching 

下面是此文的一些优化线索。

### 1 将协程运行栈空间大小的设置交给调用者
每个协程控制单元结构体将继承调用者所设置的栈空间大小。
```C
/**
 * coroutine control unit structure */
typedef struct coroutine_control_s {
    /* ... */
    
    /* coroutine stack size(byte) in 
       current coroutine control unit.*/
    int ss;
} cc_s;

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
```

### 2 cs_yield_from() 严格同步子协程
有没有发现——上一次并未完全严格实现 cs_yield_from() 对其子协程的同步，现在来完成吧。
```C
#define BACKYF      (0x10)
/**
 * coroutine control unit structure */
typedef struct coroutine_control_s {
    /* ... */
    /* magic box for logic control */
    char box;
} cc_s;

void * 
cs_yield_from(cc_s *cc, ci_s *self, 
    char *id, void *co, void *arg)
{
    int ret;
    void *t, *rv = NULL;
    ci_s *ci = NULL;

    ci = cs_co(cc, id, co, arg);
    IF_EXPS_THEN_RETURN(!ci, NULL);

#define BSYF(ci) (ci->state && (BSTATE(ci) &= ~BACKYF))
    while (BSYF(ci) && (t = cs_send(ci))) {
        if (BSTATE(ci)) BSTATE(ci) |= BACKYF;
        rv = t; self->rv = ci->rv;
        self->state = SUSPENDING;
        ret = swapcontext(&self->cctx, &BCCTX(self));
        IF_EXPS_THEN_RETURN(ret, NULL);
    }
#undef BSYF

    return rv;
}

static void inline 
_running_ci(ci_s *ci)
{
    if ((BORN <= ci->state) &&
        (SUSPENDING >= ci->state)) {
        cs_send(ci);
    }
    return ;
}
```

### 3 首尾向后向前分别调度协程运行
在协程单元数较大时可提升调度时间效率，该方法适用于服务器机型大并发量。
```C
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
```

### 4 查看协程运行栈空间大小的土方法
之前为每个协程默认分配了 32Kb 栈空间，运行 100000 个协程时，加上为主线程预分配的内存，共需要 6.4Gb 内存。如此对于内存资源少的计算机来说，并发上限不会很高。

通过查看协程汇编程序以确定该协程的运行栈空间大小，此方法有些土但真实有效，如果要追求对内存资源的节约，土土也是值得的。
另外，此文已将 ucontext 所需栈空间大小以宏的方式定义在了 ln_cs.h 中，并且留了裕度。

通过该方式，内存资源大概节约了三分之一，即协程并发上限可提升近3倍。在第6小节将为主线程预分配的空间去除后，并发上限将提升近6倍。
```C
#define BYTES
/* enlarge this value when stack of interfaces 
   in ln_cs.c enlarged. */
#define CS_MARGIN      (1024)BYTES

/* this value got by test */
#define UCSWAP_STACK   ((1024 << 3) + (1024 << 1) + 1024)BYTES
#define CS_INNER_STACK (CS_MARGIN + UCSWAP_STACK)

/* the stack needed by interfaces is about CS_INNER_STACK.
 * the _fo_fn's stack got by its assembly:
 * 
 * _co_fn:
 *     pushq   %rbp
 *     movq    %rsp, %rbp
 *     subq    $48, %rsp
 * ... */
#define _COFN      (48)
#define _CO_MARGIN (0)
#define _CO_STACK  (_COFN + CS_INNER_STACK)
```

### 5 创建协程时运行已有协程
边创建协程边运行已有协程的好处不仅可以提升并发现象，最主要的作用是可以节约内存——复用协程切换次数较少的协程数据结构体。诸如运行体验 loop_e.c 中的协程_co_fn，cs_loop()可处理的协程量无上限。
```C
/* coroutine logic control */
#define YFCO    (0x01)

/* define RUNNING_WHEN_CREATING in 
   Makefile when call cs_loop().
   RUNNING_WHEN_REATING is good for
   saving memory. */
#if RUNNING_WHEN_CREATING
#define LOOP_AWHILE
#endif

static void inline 
_loop_awhile(cc_s *cc, ci_s *ci)
{
    register int conr;
    register cc_s *_cc = NULL;

    IF_EXPS_THEN_RETURN((cc->box & YFCO) && 
        (BSTATE(ci) |= BACKYF), VOIDV);

    (void)cs_send(ci);
    for (_cc = cc; _cc; _cc = _cc->next) {
        if ((conr = CONR(_cc)))
            _cc_cos_scheduler(_cc);
    }
    return ;
}

ci_s *
cs_co(cc_s *cc, 
    char *id, void *co, void *arg)
{
    ci_s *ci  = NULL;
    
    IF_EXPS_THEN_RETURN(!cc || !co, NULL);
    
    ci = _get_cc_idle_ci(cc);
    IF_EXPS_THEN_RETURN(!ci, NULL);
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
```

### 6 在服务器或嵌入式设备上运行
若拥有内存资源特别富有的计算机，那可为并发的协程预分配内存资源以省去频繁的内存分配和释放开销。若所拥有的计算机资源较贫乏，则边运行边分配和释放内存资源。
```C
/* define CO_STACK_PRE in Makefile 
   when running ln_cs.c on server
   which owns rich memory; then 
   those memories would be freed by 
   cc_s-unit.*/
#if MEMORY_ALLOC_PRE
#define CO_STACK_PRE
#endif

ci_s *
cs_co(cc_s *cc, 
    char *id, void *co, void *arg)
{
    ci_s *ci  = NULL;
    
    IF_EXPS_THEN_RETURN(!cc || !co, NULL);

    ci = _get_cc_idle_ci(cc);
    IF_EXPS_THEN_RETURN(!ci, NULL);
#ifndef CO_STACK_PRE
    ci->stack = (char *)calloc(cc->ss, 1);
    IF_EXPS_THEN_RETURN(!ci->stack, NULL);
#endif
    /* ... */
}

static inline ci_s * 
_get_unit_ci(int nr, int ss)
{
    ci_s *ci = NULL;

    ci = (ci_s *)calloc(sizeof(ci_s) * nr, 1);
    IF_EXPS_THEN_RETURN(!ci, NULL);

#ifdef CO_STACK_PRE
    int i;
    char *stack = NULL;
    for (i = 0; i < nr; ++i) {
        stack = (char *)calloc(ss, 1);
        IF_EXPS_THEN_RETURN(!stack && _put_unit_ci(ci, i), NULL);
        ci[i].stack = stack;
    }
#endif
    
    return ci;
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
```

### 7 简单运行体验一下吧
#### (1) yield
```C
/**
 * yield_e.c,
 * for experiencing yeild mechanism
 *
 * lxr, 2020.01 */

#include "ln_comm.h"
#include "ln_cs.h"
#include <string.h>

/* the stack needed by interfaces is about CS_INNER_STACK.
 * the _fo_fn's stack got by its assembly:
 * 
 * _co_fn:
 *     pushq   %rbp
 *     movq    %rsp, %rbp
 *     subq    $48, %rsp
 * ... */
#define _COFN      (48)
#define _CO_MARGIN (0)
#define _CO_STACK  (_COFN + CS_INNER_STACK)
#define CMMB_UNIT  (_CO_STACK + _CO_MARGIN)
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
        fprintf(stderr, "%d\n", i);
        IF_EXPS_THEN_RETURN(cs_yield(ci), VOIDV);
    }

    return ;
}

static void 
_co_fn_sends(cc_s *cc)
{
    ci_s *ci = NULL;
    cofn_arg_s arg = {3};

    ci = cs_co(cc, "_co_fn", _co_fn, &arg);
    IF_EXPS_THEN_TIPS_AND_RETURN(!ci, VOIDV, 
        "Failed to create co for _co_fn\n");

    cs_send(ci);
    cs_send(ci);
    cs_send(ci);
    cs_send(ci);
    cs_send(ci);
    
    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_fn_sends(cc);
    cs_deinit(cc);

    return 0;
}
```

进入 yield_e 目录运行 make 后运行可执行文件。
```C
0
1
2
_co_fn not running now
```

#### (2) yield from
```C
/**
 * yield_from_e.c,
 * for experiencing yeild from mechanism
 *
 * lxr, 2020.01 */

#include "ln_comm.h"
#include "ln_cs.h"
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
#define _CO_MARGIN (0)
#define _CO_STACK  (_COFROMFN + CS_INNER_STACK)
#define CMMB_UNIT  (_CO_STACK + _CO_MARGIN)
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
        cs_yield(ci);
    }

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
    rv = cs_yield_from(co_cc(ci), ci, "_co_fn", _co_fn, ar);

    int i;
    fprintf(stderr, "'%s' sync '_co_fn' terminated. '_co_fn' return-value: ", __func__);
    for (i = 0; i < rv->len; ++i)
        fprintf(stderr, "%d", ((int *)(rv->buf))[i]);
    fprintf(stderr, "\n");
    if (MM_HEAP_UFREE == rv->flag)
        free(rv->buf);
        
    return ;
}


static void 
_co_yield_from_sends(cc_s *cc)
{
    ci_s *ci;
    cofn_arg_s arg = {9};
    
    ci = cs_co(cc, "_co_yield_from", _co_yield_from_fn, &arg);
    IF_EXPS_THEN_RETURN(!ci, VOIDV);

    crv_s *rv;
    while ((rv = cs_send(ci))) {
        fprintf(stderr, "%d\n", ((int *)rv->buf)[rv->len - 1]);
    }
    fprintf(stderr, "\n");
    
    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_yield_from_sends(cc);
    cs_deinit(cc);

    return 0;
}
```

进入 yield_from_e 目录运行 make 后运行可执行文件。
```C
0
1
2
3
4
5
6
7
8
'_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012345678
```

#### (3) loop
```C
/**
 * loop_e.c,
 * for experiencing cs_loop() mechanism
 *
 * lxr, 2020.01 */

#include "ln_comm.h"
#include "ln_cs.h"
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
#define _CO_MARGIN (0)
#define _CO_STACK  (_COFROMFN + CS_INNER_STACK)
#define CMMB_UNIT  (_CO_STACK + _CO_MARGIN)
#define CNR_UNIT   (6 << 1)

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
        IF_EXPS_THEN_RETURN(cs_yield(ci), VOIDV);
    }

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
    rv = cs_yield_from(co_cc(ci), ci, "_co_fn", _co_fn, ar);

    int i;
    fprintf(stderr, "'%s' sync '_co_fn' terminated."
        " '%s' return-value: ", __func__, co_id(ci));
    for (i = 0; i < rv->len; ++i)
        fprintf(stderr, "%d", ((int *)(rv->buf))[i]);
    fprintf(stderr, "\n");
    if (MM_HEAP_UFREE == rv->flag)
        free(rv->buf);
        
    return ;
}

static void 
_co_loop_all(cc_s *cc, int conr)
{
    int i;
    cofn_arg_s arg = {3};

    for (i = 0; i < conr; ++i)
        IF_EXPS_THEN_BREAK(!cs_co(cc, "_co_fn", _co_yield_from_fn, &arg));
    IF_EXPS_THEN_TIPS(i < conr, "%d coroutines run failed\n", conr - i);
    
    (void)cs_loop(cc);

    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_loop_all(cc, 1e+7);
    cs_deinit(cc);

    return 0;
}
```

进入 loop_e 目录运行 make 后运行。
```C
       1 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
       2 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
       3 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
...
 9999998 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
 9999999 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
10000000 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
```

在 loop_e.c 运行过程中查看运行 20000000 个简单协程（_co_yield_from_fn && _co_fn）时的资源消耗情况。
```C
PID USER      PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
 3357 lixianru  20   0    4348    352    276 R  29.2  0.0   0:19.75 co_loop_experience
```
相比最初的程序，4中的土方法可绝对的节约内存。

边创建边运行机制在协程切换次数有限（如_co_fn） 情况下可完美节约内存资源，是一种策略型的内存节约方式。除节约内存的可能性外，该方法还可提升协程并发感。虽然该方法不能绝对达到节约内存的目的，但其实现比较困难——在此版本之前此文想了近10种控制逻辑，皆以失败告终。


[^1]：如线程。
[^2]：如服务器，个人笔记本，嵌入式开发板等。
