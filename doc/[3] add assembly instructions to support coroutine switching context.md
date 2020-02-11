添加汇编指令支撑协程切换上下文
----

### 1 操作栈的数据结构体
```java
typedef struct asm_cctx_s {
    void **sp;
} cctx_s;
```
这个数据结构体来自[https://github.com/ramonza/libcoro](https://github.com/ramonza/libcoro)。除 cctx_s 的可扩展性外，此文也非常喜欢其使用二级指针 sp 操作栈的方式，所以将其延用。

### 2 协程上下文切换
协程切换涉及相应寄存器的备份、跳转地址的安排等内容，凡是需要严格安排栈内容的地方需要借助汇编指令来完成——C编译器会自动往栈中添加一些栈维护指令。

结合相关代码来看看吧。
```java
/** 
 * because of the instructions on 
 * stack-frame would be automatically 
 * added by C-compiler in C-functions, 
 * those routines can't be 
 * inline-assembly in C-function.
 *
 * @reality: the routines on __i386 
 *  not tested by me. */
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
            by caller or co_start_asm. */
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
实现中的相关约束都在注释里啦。

### 3 给协程传参
ucontext 通过 makecontext 为协程传参，用汇编指令支撑协程切换上下文时传参是个难点。经盘桓此文采取通过一段汇编子程序为协程传参。
```java
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

### 4 启动协程
当初次运行协程时，需初始协程栈，该函数可以使用C语言完成。
```java
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
    
    (void)co_switch_to(co_bcctx(ci), ctx);
    return CODE_NONE;
}
```

### 5 支撑协程切换
以上汇编指令实现的协程切换上下文可嵌于[一种在C语言中用 System V ucontext 所实现协程切换的优化](https://blog.csdn.net/misskissC/article/details/103981540)中，以支撑其中的
[1] yield —— 类似 python 的 yield，用于协程切换；
[2] send —— 类似 python 中生成器中的 send()，用于切换到协程；
[3] yield from —— 类似 python 的 yield from，用于同步基于 yield 的协程；
[4] loop scheduler —— 略似于python 的 asyncio.loop()，用于各协程的并发调度。
机制，作为与 ucontext 同级别的一种选择。
```java
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
```

此文将该汇编指令所支撑的协程切换环境嵌入到[之前](https://blog.csdn.net/misskissC/article/details/103981540)的工程中后，备份于
github 地址：[https://github.com/misskissC/ccoroutine](https://github.com/misskissC/ccoroutine)
gitee 地址：[https://gitee.com/misskissC/ccoroutine](https://gitee.com/misskissC/ccoroutine)
有心情的同学可一起提升该程序哇。