/**
 * ln_co.h,
 * declarations for ln_co.c 
 * or some else type-definitions.
 *
 * lxr, 2020.01 */

#ifndef _LN_CO_H_
#define _LN_CO_H_

#include <stdint.h>

#define BYTES
/* enlarge this value when stack of interfaces 
   in ln_co.c enlarged. */
#define CS_MARGIN  (1024)BYTES
#define CCTX_STACK (((1024 << 3) + (1024 << 1) + 1024))
#define CS_INNER_STACK (CS_MARGIN + CCTX_STACK)

typedef struct coroutine_info_s    ci_s;
typedef struct coroutine_control_s cc_s;

/** 
 * the type of coroutine subroutine. */
typedef void (*co_t)(uint32_t ci_h32, uint32_t ci_l32,
    uint32_t arg_h32, uint32_t arg_l32);

/* for memory-managing */
typedef enum _mm_e {
    MM_STATIC,
    MM_HEAP_UFREE, /* freed by user */

    MM_MAX
} mm_e;

/**
 * used to return return-value by coroutine */
typedef struct coroutine_return_value_s {
    void *buf; /* point to the return-value buffer */
    int  len;  /* buffer length */
    char flag; /* see mm_e */
} crv_s;


/**
 ** brief: instantiate and init 
    coroutine-control-structure.
 ** param: cnr, coroutine number in unit;
           ss,  stack size of coroutine.
 ** return: the address of coroutine-control-
    structure if succeeded, otherwise NULL. */
extern cc_s * 
co_init(int cnr, int ss);

/**
 ** reclaim the coroutine-control-structure 
    pointed by cc. */
extern void 
co_deinit(cc_s *cc);

/**
 ** brief: create coroutine-control-information 
    for coroutine.
 ** param: 
        cc,  coroutine-control-structure;
        id,  coroutine id/name;
        co,  coroutine address;
        arg, coroutine argument.
 ** return: the address where bears control 
    information for coroutine. */
extern ci_s *
co_co(cc_s *cc, char *id, void *co, void *arg);

/** 
 ** brief: the coroutine related with ci would be 
    scheduled once when co_send() called once.
 ** param: ci, bears control-information for 
               corresponding coroutine. 
 ** return: coroutine-return-value address returned; 
    NULL returned if coroutine terminated. 
 ** sidelight: co_send() just like the send() 
    method of generator in python. */
void * 
co_send(ci_s *ci);

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

/**
 ** brief: create and sync specific sub-coroutine 
    when co_yield_from() called in current coroutine. 
 ** param: 
        cc,   coroutine-control-structure address;
        self, current coroutine control-information;
        id,   sub-coroutine id;
        co,   sub-coroutine address;
        arg,  sub-coroutine argument;
 ** return: return-value address of sub-coroutine 
    at every turn.
 ** sidelight: co_yield_from() just like the 
    'yield from' in python. */
extern void * 
co_yield_from(cc_s *cc, ci_s *self, 
        char *id, void *co, void *arg);

/**
 ** brief: coroutines scheduler. 
 ** param: cc, coroutine-control-structure address.
 ** return: CODE_NONE returned if succeeded, 
    otherwise CODE_BADPARAM.
 ** sidelight: co_loop() is just a bit like the 
    'asyncio.loop()' in python. */
extern int 
co_loop(cc_s *cc);

/* terminate the coroutine related with ci
   when no return-machine in current 
   coroutine context, such as the CCTX_ASM 
   now, namely CCTX_UC not needed. */
extern void 
co_end(ci_s *ci);


/* get the address of 
   return-value-structure for 
   the coroutine related with ci. */
extern crv_s * 
co_rv(ci_s *ci);

/* get the address of 
   coroutine-control-structure for 
   the coroutine related with ci. */
extern cc_s * 
co_cc(ci_s *ci);

/* get id for the 
   coroutine related with ci. */
extern char *
co_id(ci_s *ci);

/* get the stack address for the 
   coroutine related with ci. */
extern void * 
co_stack(ci_s *ci);

/* get the stack size for the 
   coroutine related with ci. */
extern int 
co_ssize(ci_s *ci);

/* get the routines for the 
   coroutine related with ci.*/
extern void * 
co_cofn(ci_s *ci);

/* get the argument for the 
   coroutine related with ci. */
extern void * 
co_arg(ci_s *ci);

/* get context for the 
   coroutine related with ci. */
extern void *
co_cctx(ci_s *ci);

/* get the back context for the
   coroutine related with ci. */
extern void *
co_bcctx(ci_s *ci);

#endif
