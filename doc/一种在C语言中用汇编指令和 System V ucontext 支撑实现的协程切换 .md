### 1 实现内容
此文在看了 python `yield` 和 `yield from` 机制后，觉得**这种轻量级编程技术在小并发场景下优雅可爱，在大并发场景下较进程或线程而言能突破资源瓶颈**，实在令人忍不住而想在C语言中实现一个。

然后经过一些学习后，此文就在 Linux 上用C语言实现了一个。目前具体包括
```C
[1] co_yield() —— 类似 python 的 yield，用于协程切换；
[2] co_send()  —— 类似 python 生成器中的 send()，用于开始或恢复协程的执行；
[3] co_yield_from() —— 类似 python 的 yield from，用于同步基于 co_yield() 切换的协程；
[4] co_loop() —— 略似于 python 的 asyncio.loop()，用于协程并发调度。

e.g.
/**
 ** brief: suspending current coroutine related 
    with ci then switch to specific coroutine 
    when co_yield() called. 
 ** param: ci, bears control-information for 
               corresponding coroutine.
 ** return: zero returned if succeeded, 
    otherwise errno. 
 ** sidelight: co_yield() just like the 'yield' 
    in python. */
extern int 
co_yield(ci_s *ci);
```
彩蛋：在此文最后一节进行 python `yield` 和 `yield from` 原理粗探吧，先进入主题。

### 2 基石——协程上下文及切换
上一节所涉及功能的共同基石是`协程上下文及切换`，此文用两种方式来支撑。

#### 2.1 System V ucontext
此文先不从头开始，先看看有没有描述协程上下文及切换的现成 C 库，于是找到 System V ucontext。通过阅读 ucontext 手册，用 “ucontext 所描述的协程上下文及相关一族切换函数”实现一个在 C 环境下的类似于 `yield` 和 `yield from` 机制的协程切换应该不成问题。

##### (1) 理解 ucontext
根据手册注释下 ucontext 数据数据结构体和相关函数吧。
```C
#include <ucontext.h>

描述协程上下文的结构体类型 ucontext_t 至少包含以下成员。
```C
typedef struct ucontext_t {
    /* uc_link，由 makecontext() 
       创建协程运行结束后，程序
       切换到 uc_link 所指协程上
       下文处运行，uc_link 为 NULL 
       时则整个线程退出。*/
    struct ucontext_t *uc_link;
    
    /* 用于记录在当前协程中所需屏蔽的信号 */
    sigset_t          uc_sigmask;
    
    /* 协程栈空间 */
    stack_t           uc_stack;
    
    /* 协程上下文，主要用于存储协程运行涉及的寄存器状态 */
    mcontext_t        uc_mcontext;
    ...
} ucontext_t;

/**
 ** 功能：将程序当前协程级上下文保存
    到 ucp 指向的类型为 ucontext_t 的
    结构体中。
    
 ** 返回值：执行成功返回0；执行
    失败时将错误码保存在 errno 
    变量中并返回-1。*/
int getcontext(ucontext_t *ucp);

/**
 ** 功能：用 func 地址处的协程上下文修改
    由 getcontext() 在 ucp 所指结构体中
    创建的协程上下文。makecontext() 支持
    向 func 地址（函数）传递 argc 个 int 
    类型参数。
    
 ** 注：在调用 makecontext() 之前，必须为 
    ucp->uc_stack 分配内存用作协程运行栈，
    并为 ucp->uc_link 指定 ucp 对应协程运行
    结束后 将切换运行协程的协程上下文。*/
void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);


/**
 ** 功能：将当前协程上下文保存在 oucp 所指
    结构体中，并跳转执行 ucp 所指协程上下文处。
    
 ** 返回值：swapcontext() 执行成功时暂不返回
    （后续由 oucp 成功切换回来时，该函数会返回0）；
    执行失败时将错误码保存在 errno 变量中随后返回-1。
    errno 为 ENOMEM时 表明所设置栈内存已不足。*/
int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
```

##### (2) 协程初次运行
由于 ucontext 已经包含了对协程上下文的描述及切换，所以只需在协程初次运行时进行一些初始化，包括对 ucontext 数据结构体和协程参数的初始设置。
```C
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
    ret = swapcontext(bctx, ctx);
    IF_EXPS_THEN_RETURN(ret, errno);

    return ret;
}
```
在初始化 ucontext 数据结构体和协程参数后，即调用 swapcontext 将当前的协程上下文保存在 ci 的成员中，然后跳转执行协程函数，待协程函数调用 co_yield() 时协程函数将挂起而返回到调用 swapcontext 处，从而让主线程继续运行。

ci_s 结构体 和 co_yield() 后续再介绍。

#### 2.2 汇编指令
##### (1) 了解协程上下文涉及内容
在C语言中，一个协程可以用一个函数来表示，那么协程上下文即一个函数执行所涉及的上下文——与栈、指令指针关联的寄存器，以及函数调用约定涉及的寄存器。

此文不知晓更标准的称呼，就以“gcc convention on __i386”和“gcc convention on __amd64”来分别代表 Linux 上32位C程序和64位C程序中函数的调用约定吧。
**[1] gcc convention on __i386**
```C
ebp, ebx, esi, edi - 由被调用函数保证其值不变；

默认用栈传递参数，如
void fn(int a, int b);

在父函数中调用 fn(1, 2); 时传参方式为
push 2 --> b
push 1 --> a
即从右往左将实参依次入栈。

可以指定前3个参数以寄存器的方式传递，以
extern void __attribute__ ((__noinline__, __regparm__(2)))
fn(int a, int b) 
声明fn时，形参a和b的值将分别由eax和edx两个寄存器传递。
```

**[2] gcc convention on __amd64**
```C
rbp, rbx, r12, r13, r14, r15 - 由被调用函数保证其值不变；

前6个参数默认用寄存器传递，后续参数用栈传递，如
void fn(uint32_t a, uint32_t b, 
        uint32_t c, uint32_t e, 
        uint32_t f, uint32_t g, 
        uint32_t h);
      
在父函数中调用fn(1, 2, 3, 4, 5, 6, 7)时
edi=1  --> a, esi=2 --> b, edx=3 --> c, 
ecx=4  --> e, r8d=5 --> f, r9d=6 --> g,
push 7 --> h
```

##### (2) 协程切换和协程传参

**[1] 协程切换**
由于C编译器会往C函数中添加维护栈帧的指令，所以协程切换函数必须由汇编来完成。若不想涉及汇编程序的编译和链接，可以在C程序中使用 __asm__ 关键字告知编译器将汇编指令嵌在C程序中。来吧，尝试按照协程上下文涉及信息编写该协程切换函数。
```C
__asm__(
    "\t.globl co_switch_asm\n"
    "co_switch_asm:\n"
    
    /* according to the call 
       convention:
       
       ebp, ebx, esi, edi need
       called-function to backup
       on i386;
       
       rbp, rbx, r12, r13, r14, r15
       need called-function to backup
       on amd64. */
    #if __i386
        "pushl %ebp\n\t"
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"

        /* backup current stack-top
           to first(left-most) argument,
           then assign co-stack-top to esp.
           
           see: declaration of 
           co_switch_asm 
           in ln_context.h */
        "movl %esp, (%eax)\n\t"
        "movl (%edx), %esp\n\t"
        
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "popl %ebp\n\t"
         
         /* switching. 
            the target-address in stack 
            by call or co_start_asm. */
        "popl  %ecx\n\t"
        "jmpl *%ecx\n\t"

    #elif __amd64
        "pushq %rbp\n\t"
        "pushq %rbx\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"

    /* I want to save _CORET here
       by instructions just like
       'pushq _CORET'(etc.)to accept 
       coroutine return. unfortunately 
       fail.

       so coroutines which use CCTX_ASM
       to support coroutine-switching 
       must use co_end() to terminate 
       itself before return.

       the same situation on __i386, 
       please do a favor to accept the
       'return' statement of coroutine
       if you owns the same faith. */

        /* same meaning as i386.
           see _co_arg_medium for 
           the argument-passing 
           convention. */
        "movq %rsp, (%rdi)\n\t"
        "movq (%rsi), %rsp\n\t"

        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbx\n\t"
        "popq %rbp\n\t"
        
        /* switching. 
           same meaning as i386. */
        "popq  %rcx\n\t"
        "jmpq *%rcx\n"
        "_CORET:"

    #else
        #error "coroutine-context unsupported"
               " on current architecture"
    #endif
);
```

用汇编指令实现的 co_switch_asm 可以用于协程切换，但无法完成协程参数的传递以及接受协程中 return 语句。此处先解决第一个问题，第二个问题后续再解决吧。

**[2] 协程传参**
经此文思前想后，可用一个中间函数来传递协程参数。桶 co_switch_asm，这个中间函数也只能用汇编指令实现。
```C
void 
_co_arg_medium(void);

/** 
 * because of the instructions on 
 * stack-frame would be automatically 
 * added by C-compiler in C-functions, 
 * those routines can't be 
 * inline-assembly in C-function.
 *
 * @reality: the routines on __i386 
 *  not tested by me. */
__asm__  (
    "\t.globl _co_arg_medium\n"
    "_co_arg_medium:\n"
    /* the arguments co_fn, arg, ci 
       prepared by co_start_asm */
    #if   __i386
        "popl %eax\n\t" // get co_fn
        "popl %ecx\n\t" // get arg
        "popl %edx\n\t" // get ci

    /* call convention on arguments 
       of gcc on __i386 e.g.

       void fn(int a, int b);

       fn(1, 2);
       passing argument by stack 
       in caller:
       push 2 --> b
       push 1 --> a */
        "movl $0, %esi\n\t"
        "pushl %esi\n\t"
        "pushl %ecx\n\t"
        "pushl %esi\n\t"
        "pushl %edx\n\t"
    #elif __amd64
        "popq %rax\n\t" // get co_fn
        "popq %rsi\n\t" // get arg
        "popq %rdi\n\t" // get ci

    /* call convention on arguments 
       of gcc on __amd64 e.g.
       void fn(uint32_t a, uint32_t b, 
                uint32_t c, uint32_t e, 
                uint32_t f, uint32_t g, 
                uint32_t h);
      
      passing arguments by registers 
      and stack in caller:
      edi --> a, esi --> b, edx --> c, 
      ecx --> e, r8d --> f, r9d --> g,
      push real h --> formal h */
        "movl %esi, %edx\n\t"
        "movq %rsi, %rcx\n\t"
        "shrq $32,  %rcx\n\t"
        
        
        "movq %rdi, %rsi\n\t"
        "shrq $32,  %rsi\n\t"
        
        "jmpq *%rax\n\t"
    #endif
);
```
当调用协程初次运行时，用 _co_arg_medium 函数来向协程传递参数，此处参数传递兼容了 ucontext 的传参方式。_co_arg_medium 通过栈来向协程传递参数，所以调用协程初次运行的函数就需要提前将参数安置在栈中。
```C
typedef struct asm_cctx_s {
    void **sp;
} cctx_s;

int 
co_start_asm(ci_s *ci)
{
    cctx_s *ctx = NULL;
    int ss = co_ssize(ci);
    char *stack = co_stack(ci);
    
    IF_EXPS_THEN_RETURN(!ci || !stack || !ss, 
        CODE_BADPARAM);

    ctx = co_cctx(ci);
    ctx->sp = (void **)(stack + ss);

    /* ctx->sp points to (void *),
       so arithmetic unit of 
       ctx->sp is sizeof(void *). 

       initial co-stack as follow: 
       ------+---------+------+----+----+----+
         ... |co_medium| co_fn| arg| ci |NULL|
       ------+---------+------+----+----+----+
       ^     ^                               ^
       |     |                               |
       stack sp                        stack+ss */
    *--ctx->sp = NULL;
    *--ctx->sp = ci;
    *--ctx->sp = co_arg(ci);
    *--ctx->sp = co_cofn(ci);
    *--ctx->sp = _co_arg_medium;

    /* Reserved for subroutines to 
       backup registers: ebp, ebx, 
       esi, edi on i386; rbp, rbx, 
       r12, r13, r14, r15 on amd64.*/
    ctx->sp -= CS_RESERVE_NR;
    
    (void)co_switch_asm(co_bcctx(ci), ctx);
    return CODE_NONE;
}
```

#### 2.3 统一 asm 和 ucontext 对外接口
在用汇编指令和 ucontext 支撑协程切换时故意做兼容的目标就是为了对外提供统一的接口，再通过预定义宏和一些少许包装就可以实现这个目标。
```C
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

```

### 3 co_send() 和 co_yield() 的实现
支撑协程切换的基石已经编写好了，现在开始实现具体的协程切换机制吧。先实现最基本的co_send() 和 co_yield()。
#### 3.1 数据结构体
要实现协程切换的目标机制，需要什么样的结构体呢？这是一个演变的过程吧，此文几经调整和成员追加才得到了以下最终的数据结构体。
```C
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
```
我们先将这个数据结构体定义在.c源文件中，不让其他文件访问其成员。

#### 3.2 co_send()
co_send() 函数根据协程当前状态运行协程，分以下几种情况。
[1] 若当前协程为诞生(BORN)状态，则调用 _co_start() 初次调用协程运行，_co_start()的本质是第二节中实现的初次调用协程函数，主要是为了给协程传参并跳转协程处运行。协程调用 co_yield() 后将返回到 co_send() 调用 _co_start() 处继续运行。

[2] 若当前协程为挂起(SUSPENDING)状态，则调用 _co_switch() 切换到协程中恢复协程的运行，_co_switch() 的本质是第2节实现的协程切换函数。协程调用 co_yield() 后将返回到 co_send() 调用 _co_switch() 处继续运行。

[3] 若当前协程为可运行(RUNNABLE)状态，说明协程未调用 co_yield() 而自然返回，此时调用 _co_end() 结束协程运行。_co_end() 主要负责置位管理协程状态数据结构体成员的状态以标识协程已完成运行。
```C
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
```

#### 3.3 co_yield()
co_yield() 主要用于协程切换，即从一个协程切换到另一个协程，并置协程管理的相关状态为，表明协程已挂起。
```C
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
```

另外，由于汇编指令描述的协程切换还不支持协程的 return 语句，所以此文用 co_end() 函数来明确结束协程已完成运行，此函数需在协程 return 语句之前运行或完全代替 return 语句。

co_end() 所做的事情无非是置位协程管理的状态为可运行状态并随之完成协程切换以契合 co_send() 去调用 _co_end()而结束协程的运行。
```C
void 
co_end(ci_s *ci)
{
    IF_EXPS_THEN_RETURN(!ci, VOIDV);
    ci->state |= RUNNABLE;
    (void)co_switch_to(&ci->cctx, &BCCTX(ci));
}
```

当然，也可以修改汇编版的协程切换函数以支持协程的 return 语句，但此文还没有学会支持该机制的语法。

### 4 co_yield_from() 的实现
如何实现 co_yield_from() 去同步基于 co_yield() 的协程呢？这个机制也是花了此文不少时间去思考，不过随着思考的积累突然想明白后，对应的编码就不难啦—— co_yield_from() 同步协程返回后直接返回到 co_yield_from() 的父协程中即可。
```C
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
```
此处 co_yield_from() 除了同步了基于 co_yield() 的协程外，还支持了边创建协程边运行的机制。从代码上看，该机制的支持仅有跟 YFCO，BACKYF 宏关联的几句代码，但他确实花了此文不少时间。

在真正实现该功能之前，此文差不多已经尝试了10多种皆未成功的逻辑控制方法。程序中有注释为证：
```C
#ifdef LOOP_AWHILE
    /* what a milestone 
       for running coroutine when creating new one!
       I have had try other dozens before this logic control. */
    _loop_awhile(cc, ci);
    giveup_sov(NAP);
#endif
```

### 5 协程并发调度的实现
此文按照以“单元”为单位管理多个协程，cc 管理一个单元，各单元上有 unit 个管理协程运行的数据结构体。各单元以链表的形式发生逻辑关联。
```C
  | <--- ci unit ---> |  | <--- ci unit ---> |
  +------+-----+------+  +------+-----+------+
  | ci_s | ... | ci_s |  | ci_s | ... | ci_s | ...
  +------+-----+------+  +------+-----+------+
  ^                      ^
+-|--+-----+------+    +-|--+-----+------+
| ci | ... | next |--->| ci | ... | next |  ...
+----+-----+------+    +----+-----+------+
cc                     cc
```

协程并发运行除了边创造协程边运行的策略外，在调度环节对各个调度单元采取首尾间歇调度的方式调度各个协程运行，以提高时间效率。
```C
| <--- ci unit ---> |
+------+------+-----+------+------+
| ci_s | ci_s | ... | ci_s | ci_s |
+------+------+-----+------+------+
^      ^            ^      ^
|      |            |      |
1      3            4      2
-------------> <-------------------
```

对应的代码如下
```C
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
_running_ci(ci_s *ci)
{
    if ((BORN <= ci->state) &&
        (SUSPENDING >= ci->state)) {
        co_send(ci);
    }
    return ;
}
```
协程并发调度运行的本质就是开始实现的 co_send()。


### 7 一些提升
对于协程并发量较大时，程序中包含了一些可选的、有利于并发现象或内存资源节省的可选机制。

**[1] 边创建边运行**
边创建边运行协程除了可提升并发现象外，对于切换次数有限的协程来说，边创建边运行机制能节约内存，当切换次数有限的协程运行完毕后，后续协程可复用其内存资源。该机制特别适合切换次数较少的协程，可造就并发量无上限的可能（如 experiences 下的 _co_fn 的并发量就是无上限）。

**[2] 内存预分配**
在内存资源丰富的计算机上，可为所有协程预分配内存资源以免去内存申请和释放的开销。在内存资源较少的计算机上，可边进行协程调度边进行内存的申请和释放，以保证内存资源复用。

**[3] 协程运行栈**
经过一些测试，协程内部所需运行栈在11KiB（留1KiB裕度后得到CS_INNER_STACK）左右。基于这个数值，用户协程可根据其协程函数所需栈空间（查看协程函数对应的汇编函数）进行扩展。即同时运行 1000 个协程约需 12MB内存资源。

### 8 python yield，yield from 原理粗探
最后，来看看激起此文用C语言所实现目标的大概原理吧。

#### 8.1 yield
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

#### 8.2 yield from
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

### 9 源码备份
时间精力有限，此篇文字未能透露源码的所有面貌，先将源码备份在迎来合适时机时再扩展吧。
github 备份：[https://github.com/misskissC/ccoroutine](https://github.com/misskissC/ccoroutine)
gitee  备份：[https://gitee.com/misskissC/ccoroutine](https://gitee.com/misskissC/ccoroutine)

### 10 运行体验
运行 2e+7 个简单的协程体验一下吧。1e+7 个_co_yield_from_fn 和 1e+7 个_co_fn。
```C
[a@b loop_e]$ make
gcc -Wall -g -I../../include -DRUNNING_WHEN_CREATING=1 -DMEMORY_ALLOC_PRE=0 \
    -o loop_e \
    ../../src/ln_cs.c ./loop_e.c
[a@b loop_e]$
[a@b loop_e]$ ./loop_e 2>o.txt
[a@b loop_e]$ vi o.txt
       1 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
       2 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
       3 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
...
 9999998 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
 9999999 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
10000000 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
```

看看其内存资源消耗情况。
```C
top - 16:41:24 up 3 days,  8:02,  3 users,  load average: 0.49, 0.24, 0.15
Tasks:   1 total,   1 running,   0 sleeping,   0 stopped,   0 zombie
%Cpu(s):  3.6 us,  4.5 sy,  0.0 ni, 91.5 id,  0.1 wa,  0.0 hi,  0.3 si,  0.0 st

  PID USER    PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
18036  lxr    20   0    4348    352    276 R  28.2  0.0   0:12.37  loop_e
```
在协程运行次数十分有限的情况下，基本不会消耗内存资源。当协程并发量充足且协程运行次数上升时，内存资源消耗会相应变多。
