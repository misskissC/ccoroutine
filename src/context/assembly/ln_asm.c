/**
 * ln_asm.c,
 * coroutine context, switching implement 
 * by assembly instructions.
 *
 * lxr, 2020.02 */

#include "ln_context.h"
#include "ln_comm.h"
#include <stdlib.h>

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
