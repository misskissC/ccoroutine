一种在C语言中用 System V ucontext 实现的协程切换

### 1 python 协程原理
尝试了解上篇文字“一种在 python 中用 asyncio 和协程实现的IO并发”中协程的基本原理。python 协程可基于其生成器实现，其生成器由 yield 和 yield from[^1] 标识。

#### 1.1 yield
在实例化对象时，python 将包含 yield 语句的函数实例化为生成器。在生成器中，每通过 send() 运行到 yield 时返回，再次通过 send() 运行时从 yield 返回处继续运行。
```python
>>> def fun():
...     yield 0
...     yield 1
...
>>> gen = fun()
>>> print(type(gen))
<class 'generator'>
>>> gen.send(None)
0
>>> gen.send(None)
1
```

通过生成器字节码进一步理解生成器执行过程。
```python
>>> import dis
>>> def fun():
...     yield 0
...     yield 1
...
>>> gen = fun()
>>> dis.dis(gen)
  2      0 LOAD_CONST        1 (0)
         2 YIELD_VALUE
         4 POP_TOP

  3      6 LOAD_CONST        2 (1)
         8 YIELD_VALUE
        10 POP_TOP
        12 LOAD_CONST        0 (None)
        14 RETURN_VALUE
>>> gen=fun()
>>> gen.gi_frame.f_lasti
-1
>>> gen.send(None)
0
>>> gen.gi_frame.f_lasti
2
>>> gen.send(None)
1
>>> gen.gi_frame.f_lasti
8
```
python 执行 gen=fun() 语句时，将 gen 实例化为生成器。

python 在堆上为 gen 复制一份函数 fun() 的字节码，同时在堆上为 gen 生成一份维护 fun() 字节码运行的信息，包括记录 gen 运行位置的成员 gi_frame.f_lasti。

每通过 gen.send(None) 执行其(gen)堆上 fun() 的字节码时，即从 gen.gi_frame.f_lasti（-1表未开始或已结束） 位置处执行。

在执行到 yield 语句时返回，并将其堆上 fun() 字节码的当前运行位置更新到 gen.gi_frame.f_lasti 中供下次运行，直到 gen 堆上的函数 fun() 运行结束。

#### 1.2 yield from
yield from 可用于等待一个生成器运行结束。
```python
>>> import dis
>>> def fun():
...     yield 0
...     yield 1
...
>>> def f_fun():
...     gen = fun()
...     yield from gen
...     print('gen done')
...
>>> gen_f = f_fun()
>>> gen_f.gi_frame.f_lasti
-1
>>> dis.dis(gen_f)
  2      0 LOAD_GLOBAL              0 (fun)
         2 CALL_FUNCTION            0
         4 STORE_FAST               0 (gen)

  3      6 LOAD_FAST                0 (gen)
         8 GET_YIELD_FROM_ITER
        10 LOAD_CONST               0 (None)
        12 YIELD_FROM
        14 POP_TOP

  4     16 LOAD_GLOBAL              1 (print)
        18 LOAD_CONST               1 ('gen done')
        20 CALL_FUNCTION            1
        22 POP_TOP
        24 LOAD_CONST               0 (None)
        26 RETURN_VALUE
>>>
>>> gen_f.send(None)
0
>>> gen_f.gi_frame.f_lasti
10
>>>
>>> gen_f.send(None)
1
>>> gen_f.gi_frame.f_lasti
10
>>> gen_f.send(None)
gen done
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
StopIteration
```
f_fun() 中的 yield from 让 gen_f 被 python 实例化为生成器。

gen_f 与 yield 标识生成的生成器 gen 不同的是——在 gen_f 所等待生成器 gen 运行结束前， gen_f 每次都从 yield from 语句处返回。

直到 gen 运行结束，gen_f yield from 后续语句才会被执行。基于 yield 和 yield from 机制可实现协程并发。

#### 1.3 基于 yield && yield form 机制写个简单的协程并发
了解 yield 和 yield from 后，用他们实现一个简单的协程并发例子吧。
```python
#!/usr/bin/python3
# -*- coding: utf-8 -*-

''' ln_crtn_eg.py '''
import sys

_nr = 0

def fn_decorator(fn):
    def _w(max):
        global _nr
        for i in range(max):
            _nr += 1
            yield f'increase in {fn.__name__}: {_nr}'
    return _w

def f_fn_decorator(fn_x):
    def _w(max):
        gen = fn_x(max)
        yield from gen
        print(f'increase in {fn_x.__name__} done')
    return _w
    
@fn_decorator
def fn_m(max):
    pass
    
@fn_decorator
def fn_n(max):
    pass

@f_fn_decorator
def f_fn_m(max):
    return fn_m(max)
    
@f_fn_decorator
def f_fn_n(max):
    return fn_n(max)
    
if __name__ == '__main__':
    max = 3
    gen = [f_fn_m(max), f_fn_n(max)]
    for i in range(max + 1):
        try: print(gen[0].send(None))
        except StopIteration: pass
        
        try: print(gen[1].send(None))
        except StopIteration: pass
```

例子运行体验。
```python
> python ln_crtn_eg.py
increase in fn_m: 1
increase in fn_n: 2
increase in fn_m: 3
increase in fn_n: 4
increase in fn_m: 5
increase in fn_n: 6
increase in f_fn_m done
increase in f_fn_n done
```

在 ln_crtn_eg.py 中可看到：协程在编程语言语句层面切换，变量的共享不用加锁；协程并发可发生在单线程中，适用于速度不如CPU快的异步IO并发场景[^2]。

**这种轻量级编程技术在小并发场景下优雅可爱，在大并发场景下较进程和线程而言能突破资源瓶颈**。实在令人忍不住想在C语言中实现一个。

### 2 开始实现标题内容
此文先不从头开始，先看看有没有描述协程上下文的现成库，最好是C库。还真有—— System V[^4] 的 ucontext。

#### 2.1 先读 ucontext 手册，看其能完成编程目标否
```C
#include <ucontext.h>

int getcontext(ucontext_t *ucp);
int setcontext(const ucontext_t *ucp);

void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);
int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
```

描述协程上下文的结构体类型 ucontext_t 至少包含以下成员。
```C
typedef struct ucontext_t {
    struct ucontext_t *uc_link;
    sigset_t          uc_sigmask;
    stack_t           uc_stack;
    mcontext_t        uc_mcontext;
    ...
} ucontext_t;
```
uc_link，由 makecontext() 创建的协程上下文对应协程运行结束后，程序切换到 uc_link 所指协程上下文处运行（为NULL时则整个线程退出）。

uc_sigmask，用于记录 在当前协程上下文中 所需屏蔽的信号。
uc_stack，指向当前协程运行所需内存空间，以栈的方式使用。
uc_mcontext，真正描述协程上下文的结构体类型，主要用于保存当前CPU各寄存器的状态值。

**int getcontext(ucontext_t *ucp);**
getcontext() 将程序当前协程级上下文[^3]保存到 ucp 指向的类型为 ucontext_t 的结构体中。

getcontext() 执行成功返回0；执行失败时将错误码保存在 errno 变量中并返回-1。

**int setcontext(const ucontext_t *ucp);**
setcontext() 跳转 ucp 所指协程上下文处执行。
setcontext() 执行成功不返回；执行失败时将错误码保存在 errno 变量中并返回-1。

若 setcontext() 中 ucp 所指协程上下文由 getcontext() 创建，当 ucp 对应协程终止后将继续执行该协程后续程序。若 setcontext() 中 ucp 所指协程上下文由 makecontext() 创建，当 ucp 对应协程终止时会切换到 ucp->uc_link 所指协程上下文处运行；若 uc_link 为NULL，则当前线程退出。

**void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);**
makecontext() 用 func 地址处的协程上下文修改由 getcontext() 在 ucp 所指结构体中创建的协程上下文。

在调用 makecontext() 之前，必须为 ucp->uc_stack 分配内存用作协程运行栈，并为 ucp->uc_link 指定 ucp 对应协程运行结束后 将切换运行协程的协程上下文。makecontext() 支持向 func 地址（函数）传递 argc 个 int 类型参数。通过调用 setcontext() 或 swapcontext() 即可跳转执行 func，且在 func 处可获取指定的 argc 个 int 类型实参。

**int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);**
swapcontext() 将当前协程上下文保存在 oucp 所指结构体中，并跳转执行 ucp 所指协程上下文处。

swapcontext() 执行成功时暂不返回（后续由 oucp 成功切换回来时，该函数会返回0）；执行失败时将错误码保存在 errno 变量中随后返回-1。errno 为 ENOMEM时 表明所设置栈内存已不足。

通过阅读 ucontext 手册，用 ucontext_t 及相关一族函数实现一个在C环境下的类似于 yield && yield from 机制的协程切换应该不成问题。

在继续开展剩余工作之前，先写个简单例子跟 ucontext 打个照面。
```C
#include <stdio.h>
#include <unistd.h>
#include <ucontext.h>

#ifdef __GNUC__
#define IF_EXPS_TRUE_THEN_RETURN(exps, retv) \
({ \
    if (exps) return retv; \
})

#else
#define IF_EXPS_TRUE_THEN_RETURN(exps, retv) \
do { \
    if (exps) return retv; \
} while(0)

#endif

int main(void)
{
#define _SYSCALL_ERR (-1)

    int retv;
    ucontext_t cctx;

    retv = getcontext(&cctx);
    IF_EXPS_TRUE_THEN_RETURN(_SYSCALL_ERR == retv, retv);

    fprintf(stderr, "coroutine context comes from %s\n",
        retv++ ? "main()" : "ucontext");

    usleep(500 * 1000);
    (void)setcontext(&cctx);

    return 0;
#undef _SYSCALL_ERR
}
```

例子运行体验。
```C
$ gcc ln_crtn_ctx.c -o cctx
$ ./cctx
coroutine context comes from ucontext
coroutine context comes from ucontext
coroutine context comes from ucontext
...
```

#### 2.2 初定管理协程切换的数据结构体
```C
#include <ucontext.h>

typedef struct coroutine_info_s    ci_s;
typedef struct coroutine_control_s cc_s;

/* the type of coroutine function */
typedef void (*cfn_f)(cc_s *cc, void *arg);

/* type flag for memory-managing */
typedef enum _mm_e {
    MM_STATIC,
    MM_HEAP_UFREE,

    MM_MAX
} mm_e;

typedef struct coroutine_return_value_s {
    void *buf; /* point to the return-value buffer */
    int  len; /* return-value buffer length */
    char flag; /* see mm_e */
} crv_s;

typedef struct coroutine_info_s {
    cfn_f cfn;  /* coroutine function */
    void *arg;  /* coroutine function arguments */
    char *cname;/* coroutine function name/id */
    int state;  /* coroutine current states */
    
    char *stack; /* memory for coroutine stack */
    int   size;  /* stack size */
    ucontext_t cctx; /* current coroutine ucontext */

    /* on the memory bears coroutine's return-value */
    crv_s rv; 

    /* switch to the coroutine corresponded by "back" 
       when current coroutine switching or terminated. */
    ci_s *back;

    /* the cc which current ci belongs to */
    cc_s *cc;
} ci_s;

typedef struct coroutine_control_s {
    ci_s *ci;     /* point to the (ci_s) arrary */
    int running;  /* the index of running coroutine */
    int nr, left; /* coroutines total/left number ci point to */
    cc_s *next;   /* next coroutine control unit */
} cc_s;
```
这么多！看来是真要打算实现呐。

#### 2.3 将数据结构体的作用转换为C代码
此篇文字打算按照以下方式利用管理协程切换的数据结构体。
```C
  | <--- ci unit ---> |  | <--- ci unit ---> |
  +------+-----+------+  +------+-----+------+
  | ci_s | ... | ci_s |  | ci_s | ... | ci_s | ...
  +------+-----+------+  +------+-----+------+
  ^                      ^
+-|--+-----+------+    +-|--+-----+------+
| ci | ... | next |    | ci | ... | next |  ...
+----+-----+------+    +----+-----+------+
cc                     cc
```
以单元量(unit)分配和释放数据结构体的原因是为了减少在协程切换过程中频繁通过系统调用分配内存，以提升协程切换效率。
相关接口如下。
```C
static bool inline 
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

static ci_s * 
_get_unit_ci(int nr, int cmmb)
{
    int i;
    ci_s *ci = NULL;
    char *stack = NULL;

    ci = (ci_s *)calloc(sizeof(ci_s) * nr, 1);
    IF_EXPS_TRUE_THEN_RETURN(!ci, NULL);

#define _STACK_MIN (1024 * 8)
    cmmb = (cmmb > _STACK_MIN) ? cmmb : _STACK_MIN;
#undef _STACK_MIN
    for (i = 0; i < nr; ++i) {
        stack = (char *)calloc(cmmb, 1);
        IF_EXPS_TRUE_THEN_RETURN(!stack && _put_unit_ci(ci, i), NULL);
        ci[i].stack = stack;
        ci[i].size  = cmmb;
    }
    
    return ci;
}

static cc_s * 
_get_cc(int nr, int cmmb)
{
    ci_s *ci = NULL;
    cc_s *cc = NULL;

    cc = (cc_s *)calloc(sizeof(cc_s), 1);
    IF_EXPS_TRUE_THEN_RETURN(!cc, NULL);

    ci = _get_unit_ci(nr, cmmb);
    IF_EXPS_TRUE_THEN_RETURN(!ci && ln_free(cc), NULL);
#define _SET_CC_MBR(mbr, v) cc->mbr = v
    _SET_CC_MBR(ci, ci);
    _SET_CC_MBR(running, NONE);
    _SET_CC_MBR(nr, nr);
    _SET_CC_MBR(left, nr);
#undef _SET_CC_MBR

    return cc;
}

cc_s * 
cs_init(int cnr, int cmmb)
{
    return _get_cc(cnr, cmmb);
}

void 
cs_deinit(cc_s *cc)
{
    cc_s *t, *_cc;

    IF_EXPS_TRUE_THEN_RETURN(!cc, VOIDV);
    for (_cc = t = cc; _cc; _cc = t) {
        t = t->next;
        _put_cc(_cc);
    }

    return ;
}
```

为协程分配数据结构体的相关函数如下。
```C
static int inline 
_get_cis_idle_ci(ci_s *ci, int nr)
{
    int f = NONE;
    int h, t = nr - 1;

#define _ST(i) ci[i].state
    for (h = 0; h <= t; ++h, --t) {
        IF_EXPS_TRUE_THEN_BREAK(!_ST(h) && ((f = h) + 1));
        IF_EXPS_TRUE_THEN_BREAK(!_ST(t) && (f = t));
    }
#undef _ST

    return f;
}

static ci_s * 
_get_cc_idle_ci(cc_s *cc)
{
    int f = NONE;
    cc_s *p = NULL;
    cc_s *_cc = NULL;

    for (_cc = cc; _cc; _cc = _cc->next){
        p = _cc;
        IF_EXPS_TRUE_THEN_CONTINUE(!_cc->left);
        f  = _get_cis_idle_ci(_cc->ci, _cc->nr);
        IF_EXPS_TRUE_THEN_BREAK(NONE != f);
    }

    if (!_cc) {
        p->next = _get_cc(CI_UNIT, STACK_UNIT);
        IF_EXPS_TRUE_THEN_RETURN(!p->next, NULL);
        _cc = p->next;
        f = 0;
    }
    _cc->left--;
    _cc->ci[f].cc = _cc;
    
    return (_cc->ci + f);
}

ci_s *
cs_co(cc_s *cc, 
    char *cname, void *cfn, void *arg)
{
    ci_s *ci  = NULL;
    IF_EXPS_TRUE_THEN_RETURN(!cc || !cfn, NULL);

    ci = _get_cc_idle_ci(cc);
    IF_EXPS_TRUE_THEN_RETURN(!ci, NULL);
#define _SET_CI_MBR(mbr, v) ci->mbr = v
    _SET_CI_MBR(cfn, (cfn_f)cfn);
    _SET_CI_MBR(arg, arg);
    _SET_CI_MBR(cname, cname);
    _SET_CI_MBR(state, BORN);
#undef _SET_CI_MBR

    ci->back = _get_cc_idle_ci(cc);
    IF_EXPS_TRUE_THEN_RETURN(!ci->back && !(ci->state = PREGNANT), NULL);
    ci->back->state = BACKCI;

    return ci;
}
```

#### 2.4 转换协程切换逻辑为C代码
##### (1) 实现 yield
yield 相关函数是后续 yield from 和 loop switching 的基础，相关代码如下。
```C
static int 
_co_start(ci_s *ci)
{
    int ret;
    char *_arg  = NULL;
    ucontext_t *ctx = NULL;

    ctx = &ci->cctx;
    ret = getcontext(ctx);
    IF_EXPS_TRUE_THEN_RETURN(ret, errno);

    ci->state    = RUNNABLE;
    ctx->uc_link = &ci->back->cctx;
    ctx->uc_stack.ss_sp   = ci->stack;
    ctx->uc_stack.ss_size = ci->size;
    /**
     * there's not matter if ci->cfn's type is not
     * func_t, as long as ci->cfn wouldn't achieve 
     * more parameters than makecontext() passed. */
    _arg = ci->arg;
    typedef void (*func_t)(void);
    makecontext(ctx, (func_t)ci->cfn, 4, 
        (uint32_t)((uintptr_t)ci), 
        (uint32_t)((uintptr_t)ci >> 32), 
        (uint32_t)((uintptr_t)_arg), 
        (uint32_t)((uintptr_t)_arg >> 32) );
    ret = swapcontext(&ci->back->cctx, ctx);
    IF_EXPS_TRUE_THEN_RETURN(ret, errno);
    
    return ret;
}

static int 
_co_switch(ci_s *ci)
{
    int ret;

    ci->state = RUNNABLE;
    ret = swapcontext(&ci->back->cctx, &ci->cctx);
    IF_EXPS_TRUE_THEN_RETURN(ret, errno);

    return ret;
}

static void 
_co_end(ci_s *ci)
{
    cc_s *cc = NULL;
    
#define _SET_CI_MBR(mbr, v) ci->mbr = v
    _SET_CI_MBR(cfn,   NULL);
    _SET_CI_MBR(arg,   NULL);
    _SET_CI_MBR(state, PREGNANT);
#undef _SET_CI_MBR
    cc = ci->cc;
    cc->left++;
    cc->running = NONE;
    
    if (ci->back && (ci->back->state == BACKCI)) {
        ci->back->state = PREGNANT;
        ci->back->cc->left++;
    }

    return ;
}

void * 
cs_send(ci_s *ci)
{
    int state;
    void *out = NULL;
    int   ret = CODE_NONE;

    IF_EXPS_TRUE_THEN_RETURN(!ci, NULL);

    state = ci->state;
    char *cname = ci->cname ? ci->cname : "unnamed_co";
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(
        (BORN > state) || (SUSPENDING < state), 
        NULL, "%s not running now\n", cname  );

    if (BORN == state) {
        ret = _co_start(ci);
    } else if (SUSPENDING == state) {
        ret = _co_switch(ci);
    }
    
    if (RUNNABLE == ci->state) {
        _co_end(ci);
    }

    out = (PREGNANT != ci->state) ? &ci->rv : out;
    IF_EXPS_TRUE_THEN_TIPS(ret, "co switch error: %d\n", ret);
    
    return out;
}

int 
cs_yield(ci_s *ci)
{
    int ret;
    cc_s *cc = NULL;

    IF_EXPS_TRUE_THEN_RETURN(!ci, CODE_BADPARAM);

    cc = ci->cc;
    cc->running = NONE;
    ci->state   = SUSPENDING;
    ret = swapcontext(&ci->cctx, &ci->back->cctx);
    IF_EXPS_TRUE_THEN_RETURN(ret, errno); 

    return ret;
}
```

yield 相关机制的函数就实现完啦，真是质的一步！先写点测试代码运行体验下，测试代码可以写得随意一点了吧。
```C
#define CNR_UNIT  (32)
#define CMMB_UNIT (1024 * 32U)

typedef struct _c_args {
    /* anything else */
    void *in;
} c_arg_s;

/**
 * there's not matter if _co_fn_eg's type 
 * is not cfn_f, because makecontext() 
 * will pass corresponding parameters for 
 * _co_fn_eg. */
static void 
_co_fn_eg(uint32_t ci_l32, uint32_t ci_h32, 
        uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    void *arg;
    int i, *in;

#define _HL32TOUPTR(h32, l32) \
    ((void *)(l32 | ((uintptr_t)h32 << 32)))
    ci  = _HL32TOUPTR(ci_h32,  ci_l32);
    arg = _HL32TOUPTR(arg_h32, arg_l32);
#undef _HL32TOUPTR

    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!ci || !arg, 
        VOIDV, "bad parameter in %s\n", __func__);
    /* brackets in hand then the world in hand */
    in  = (int *)(((c_arg_s *)arg)->in);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!in, VOIDV,
        "bad parameter in %s\n", __func__);

    int *buf = calloc(sizeof(int) * in[0], 1);
    IF_EXPS_TRUE_THEN_RETURN(!buf, VOIDV);
    ci->rv.buf   = buf;
    ci->rv.bflag = MM_HEAP_UFREE;
    for (i = 0; i < in[0]; ++i) {
        buf[i] = i + in[0];
        ci->rv.blen = i + 1;
        cs_yield(ci);
    }
    
    return ;
}

static void
_co_switching_yield_eg(cc_s *cc)
{
    int in = 3;
    ci_s *ci = NULL;
    crv_s *rv = NULL;
    c_arg_s arg = {&in};

#define _CNAME "co_fn_single_eg"
    ci = cs_co(cc, _CNAME, _co_fn_eg, &arg);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!ci, VOIDV, "Failed to create co\n");

crv_s *tmp = NULL;
#define _CO_SEND() \
({ \
    rv = cs_send(ci); \
    IF_EXPS_TRUE_THEN_TIPS(rv && (tmp = rv), "%d\n", ((int *)rv->buf)[rv->blen - 1]); \
})
    _CO_SEND();
    _CO_SEND();
    _CO_SEND();
    _CO_SEND();
    _CO_SEND();
    
#undef _CO_SEND
#undef _CNAME

    if (MM_HEAP_UFREE == tmp->bflag)
        free(tmp->buf);

    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_switching_yield_eg(cc);
    cs_deinit(cc);

    return 0;
}
```

在 linux 下编译运行。
```C
$ make 
$ ./cswitching
3
4
5
co_fn_single_eg not running now
```
这个执行机制算得上与跟 python 的 yeild 相似吧。顺便解释一点，_co_fn_eg 只会返回三个数，但需要执行第 4 次 cs_send() 去终止协程，其实当 cs_send() 返回 NULL 时就代表协程执行完毕了。此文打算不再迷恋刚刚所实现的 yield，因为要趁热打铁，赶紧实现 yield from。

##### (2) 实现 yield from
如何实现 yield from？如果想明白了只需增加如下函数——由此可以看出此篇文字的计算机基础还算过关吧^_^。
```C
void * 
cs_yield_from(cc_s *cc, ci_s *self, 
        char *cname, void *cfn, void *arg)
{
    void *rv = NULL;
    ci_s *ci = NULL;
    
    ci = cs_co(cc, cname, cfn, arg);
    IF_EXPS_TRUE_THEN_RETURN(!ci, NULL);

    int ret;
    void *tmp = NULL;
    while ((tmp = cs_send(ci))) {
        rv = tmp;
        self->rv = ci->rv;
        self->state = SUSPENDING;
        ret = swapcontext(&self->cctx, &self->back->cctx);
        IF_EXPS_TRUE_THEN_RETURN(ret, NULL);
    }
    
    return rv;
}
```

编写测试代码运行体验。
```C
static void 
_co_yield_from_fn_eg(uint32_t ci_l32, uint32_t ci_h32, 
        uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    void *arg;
    int i, *in;

#define _HL32TOUPTR(h32, l32) \
    ((void *)(l32 | ((uintptr_t)h32 << 32)))
    ci  = _HL32TOUPTR(ci_h32,  ci_l32);
    arg = _HL32TOUPTR(arg_h32, arg_l32);
#undef _HL32TOUPTR
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!ci || !arg, 
        VOIDV, "bad parameter in %s\n", __func__);

    /* brackets in hand then the world in hand */
    in  = (int *)(((c_arg_s *)arg)->in);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!in, VOIDV,
        "bad parameter in %s\n", __func__);

    int mbr = 3;
    c_arg_s _arg = { &mbr };
    crv_s *rv = NULL;
    rv = cs_yield_from(ci->cc, ci, "co_in_yield_from", _co_fn_eg, &_arg);

    IF_EXPS_TRUE_THEN_TIPS(true, "====yield from done====\nthe coroutine return value is: \n");
    for (i = 0; i < rv->blen; ++i)
        fprintf(stderr, "%d", ((int *)(rv->buf))[i]);
    fprintf(stderr, "\n");

    if (MM_HEAP_UFREE == rv->bflag)
        free(rv->buf);
        
    return ;
}

static void 
_co_swtching_yield_from_eg(cc_s *cc)
{
    int in = 3;
    ci_s *ci = NULL;
    crv_s *rv = NULL;
    c_arg_s arg = {&in};

#define _CNAME "co_fn_in_yield_from_eg"
    ci = cs_co(cc, _CNAME, _co_yield_from_fn_eg, &arg);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!ci, VOIDV, "Failed to create co\n");

    int i;
    for (i = 0; ; ++i) {
        rv = cs_send(ci);
        IF_EXPS_TRUE_THEN_TIPS(rv, "%d\n", ((int *)rv->buf)[rv->blen - 1]);
        IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!rv, VOIDV, _CNAME" ended\n\n");
    }
#undef _CNAME
    
    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_swtching_yield_from_eg(cc);
    cs_deinit(cc);

    return 0;
}
```

在linux下编译运行。
```C
$ make
$ ./cswitching
3
4
5
====yield from done====
the coroutine return value is:
345
co_fn_in_yield_from_eg ended
```
可以看出，cs_yield_from() 之后的语句在 cs_yield_from() 所等待协程运行完毕后才得以运行，这对于等待异步IO完全返回很便利。

实现 cs_yield() 和 cs_yield_from() 之后，再实现下协程并发吧，就像 asyncio.loop() 那样。

##### (3) 实现 loop switching 
实现 loop switching 要想清楚的一个关键问题是
“由 cs_yield_from() 创建的协程数据结构体，包含 cs_yield_from() 语句的协程结构体，以及其他协程的协程结构体 是否可以在程序任意处调用 cs_send() 运行协程？”

此篇文字认真分析过，得到的答案是**可以**（不在yield form中作严格同步的话）——此篇文字计算机基础知识不错吧^_^。另外，此篇文字还分析到 cs_yield_from() 函数需要改动一点点。具体如下。 
```C
void * 
cs_yield_from(cc_s *cc, ci_s *self, 
        char *cname, void *cfn, void *arg)
{
    void *rv = NULL;
    ci_s *ci = NULL;
    
    ci = cs_co(cc, cname, cfn, arg);
    IF_EXPS_TRUE_THEN_RETURN(!ci, NULL);

    int ret;
    void *tmp = NULL;
    /* while ((tmp = cs_send(ci))) {*/
    while (ci->state && (tmp = cs_send(ci))) {
        rv = tmp;
        self->rv = ci->rv;
        self->state = SUSPENDING;
        ret = swapcontext(&self->cctx, &self->back->cctx);
        IF_EXPS_TRUE_THEN_RETURN(ret, NULL);
    }
    
    return rv;
}

static void 
_running_cc_cos(cc_s *cc)
{
    ci_s *ci, *ci_end;
    int conr = cc->nr - cc->left;

    IF_EXPS_TRUE_THEN_RETURN(!conr, VOIDV);
    ci_end = cc->ci + cc->nr;
    for (ci = cc->ci; (ci < ci_end) && conr; ++ci) {
        conr = cc->nr - cc->left;
        if (BACKCI == ci->state) {
            conr--;
        } else if ((BORN <= ci->state) && (SUSPENDING >= ci->state)){
            conr--;
            cc->running = ci - cc->ci;
            cs_send(ci);
        }
    }

    return ;
}

int 
cs_loop(cc_s *cc)
{
    bool has_co;
    cc_s *p, *_cc = NULL;

    IF_EXPS_TRUE_THEN_RETURN(!cc, CODE_BADPARAM);
_loop:
    has_co = false;
    for (_cc = cc; _cc; _cc = _cc->next) {
        if (_cc->nr - _cc->left) {
            has_co = true;
            _running_cc_cos(_cc);
        } else if (_cc != cc) {
            for (p = cc; p->next != _cc; p = p->next) 
                VOIDV;
            p->next = _cc->next;
            _put_cc(_cc);
        }
    }
IF_EXPS_TRUE_THEN_GOTO_LABEL(has_co, _loop);

    return CODE_NONE;
}
```
需要改动的地方就这么多，万能的 cs_send()，真是了不起。另外，cs_loop() 在轮询需运行的协程外，当检测到一个 ci_s 单元上没有管理协程时，就将其内存资源释放掉，避免本程序占着系统内存不放而影响其他程序运行。

运行体验留到下一节吧，此篇文字打算在个人笔记本中的虚拟机中运行10000个协程，前5000个为包含 cs_yield_from() 的协程，后5000个为一般协程。

#### 2.5 协程运行调度 cs_loop() 运行体验
```C
/**
 * there's not matter if _co_fn_eg's type 
 * is not cfn_f, because makecontext() 
 * will pass corresponding parameters for 
 * _co_fn_eg. */
static void 
_co_fn_eg(uint32_t ci_l32, uint32_t ci_h32, 
        uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    void *arg;
    int i, *in;
    
    /* ... */
    
    for (i = 0; i < in[0]; ++i) {
        buf[i] = i + in[0];
        ci->rv.blen = i + 1;
        cs_yield(ci);
    }
#if 1
    if (!strncmp("co_fn", ci->cname, 5)) {
        fprintf(stderr, "yield %s coroutine return value is:", ci->cname);
        for (i = 0; i < ci->rv.blen; ++i)
            fprintf(stderr, "%d", ((int *)(ci->rv.buf))[i]);
        fprintf(stderr, "\n");
        free(ci->rv.buf);
        ci->rv.bflag = MM_HEAP_UFREE - 1;
    }
#endif
    return ;
}

static void 
_co_yield_from_fn_eg(uint32_t ci_l32, uint32_t ci_h32, 
        uint32_t arg_l32, uint32_t arg_h32)
{
    ci_s *ci;
    void *arg;
    int i, *in;
    
    /* ... */

    int mbr = 3;
    c_arg_s _arg = { &mbr };
    crv_s *rv = NULL;
    rv = cs_yield_from(ci->cc, ci, "co_in_yield_from", _co_fn_eg, &_arg);

    IF_EXPS_TRUE_THEN_TIPS(true, "yield from coroutine return value is: ");
    for (i = 0; i < rv->blen; ++i)
        fprintf(stderr, "%d", ((int *)(rv->buf))[i]);
    fprintf(stderr, "\n");

    if (MM_HEAP_UFREE == rv->bflag)
        free(rv->buf);
        
    return ;
}

static void 
_co_switching_all_eg(cc_s *cc, int conr)
{
    int i;
    int in = 3;
    c_arg_s arg = {&in};

#define _CN_MAX (30)
    for (i = 0; i < conr / 2; ++i) {
        IF_EXPS_TRUE_THEN_BREAK(!cs_co(cc, "", _co_yield_from_fn_eg, &arg));
    }
    for (; i < conr; ++i) {
        IF_EXPS_TRUE_THEN_BREAK(!cs_co(cc, "co_fn_x", _co_fn_eg, &arg));
    }
#undef _CN_MAX
    IF_EXPS_TRUE_THEN_TIPS(i < conr, "no memory for left %d coroutines now\n", conr - i);
    (void)cs_loop(cc);

    return ;
}

int callconvention 
main(void)
{
    cc_s *cc = NULL;

    cc  = cs_init(CNR_UNIT, CMMB_UNIT);
    IF_EXPS_TRUE_THEN_TIPS_AND_RETURN(!cc, CODE_NOMEM,
        "no enough memory on this machine now\n");
    _co_switching_all_eg(cc, 10000);
    cs_deinit(cc);

    return 0;
}
```

编译运行。
```C
$ make
$ ./cswitching 2> data.dat

vi data.dat
    1 yield from coroutine return value is: 345
    2 yield from coroutine return value is: 345
    3 yield from coroutine return value is: 345
...
 4999 yield from coroutine return value is: 345
 5000 yield from coroutine return value is: 345
 5001 yield co_fn_x coroutine return value is:345
...
 9998 yield co_fn_x coroutine return value is:345
 9999 yield co_fn_x coroutine return value is:345
10000 yield co_fn_x coroutine return value is:345
```
10000是此篇文字为运行体验随便想到的一个数字，并非是 cs_loop() 在本虚拟机中的上限。

此篇文字之前有提到，协程可突破系统资源瓶颈而支持更大的并发量。的确是的呢，不信的话可以在好一点的电脑上尝试开启10000个线程来代替以上协程并发方式哦。

### 3 分析下此篇文子的协程切换实现
终于写到最后了，客观分析下此篇文字实现的协程并发机制的缺点。
**[1]** 在并发量较大时，此协程并发程序会高比例占用CPU。此篇文字觉得系统中有一两个占用CPU高并不是一件值得大惊小怪的事，这样的程序顶多是运行完操作系统分配给他的运行时间片而已，仅此而已——如果不是每个程序的作者都这么想的话。当系统中这样的程序增多时，由于进程持续运行时间为整个时间片再加上切换开销，会使得系统并发现象下降。
**[2]** 在 cs_loop() 中并没有像 _get_cis_idle_ci() 一样从数组两端开始分别向中间搜索。如果想进一步提升协程并发效率，可以像 _get_cis_idle_ci() 一样减少循环次数以提升 cs_loop() 的运行效率[^5]。
**[3]** 虽然此篇文字在编写这些代码时已经比较注意代码量的简化了，但此篇文字坚信，在保持阅读性的前提下这些代码量仍可进一步简化。
**[4]** 虽然此篇文字也一直在注意避免数据结构体成员的冗余，可写到后来发现 cc_s 中的 running 成员并没有太大用处，若去掉似乎又差点什么，所以最终保持了他的冗余。
**[5]** 最后一点，说说本协程机制的硬伤之处——协程运行栈空间。
此篇文字默认分配32Kb内存充当各协程的运行栈空间，最小不能小于8Kb。对于所需运行栈空间较小的协程，会造成内存空间的浪费；对于所需运行栈超过32Kb的协程，运行过程中可能会出现非法访问内存错误而导致整个程序退出。对于后者，可以增加检测栈空间是否快要溢出的机制以尽量避免栈空间使用溢出。但此文还没有想出能绝对检测协程运行栈溢出的机制。如果协程运行栈发生溢出情况后，目前只有增大默认运行栈试验出保险的运行栈大小，但对于其他所需运行栈较小的协程来说，这无疑是一种浪费。所以，协程运行栈是本协程机制程序的一个硬伤——没有能够检测出协程所需运行栈大小的机制。

[^1]：python 3.5 引入了 await，await 功能同 yield from，但需和 async 搭配使用。
[^2]：比线程占更少资源，资源层面的并发量上限大于线程。
[^3]：协程级上下文，即此文所称谓的“协程上下文”；根据协程上下文切换的子程序即为协程。
[^4]：Unix众多版本中的一支。
[^5]：循环次数少可减少CPU分支预测错误——导致预取指队列清空破坏流水线的风险。