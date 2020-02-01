A coroutine switching implement by System V ucontext in C-language
----

The coroutine switching implement by System V ucontext in C-language called `ucoroutine-switching` this time. <br>

The `cs_yield()`, `cs_yield_from()`, `cs_send()`, `cs_loop()` in `ucoroutine-switching` are just like the `yield`, `yield from`, `generator.send()`, `asyncio.loop()` in python respectively. 

I have had wrote two blogs to flatter `ucoroutine-switching`, but it‘s core routines always less than 500 lines, this is the reason i still love it.<br>

The potential bottleneck of `ucoroutine-switching` besets me at the same time, so goddess hopes more knowledgeable guys just like you can **continue to improve it**.<br>

### catalogs
```C
.
├── doc
│   ├── A coroutine switching implement ... in C-language.md
│   └── An optimization for ... in C-language.md
├── experiences
│   ├── make_e
│   ├── loop_e
│   │   ├── loop_e.c
│   │   ├── Makefile
│   ├── yield_e
│   │   ├── Makefile
│   │   └── yield_e.c
│   └── yield_from_e
│       ├── Makefile
│       └── yield_from_e.c
├── include
│   ├── ln_comm.h
│   └── ln_cs.h
├── readme.md
└── src
    └── ln_cs.c
```
`src`, core routines for `ucoroutine-switching`. <br>
`experiences`, namely examples for `ucoroutine-switching`, `yield`, `send`, `yield from`, `loop` experiences included. <br>
`doc`, chinese documents or notes for `ucoroutine-switching`. <br>

### running experience
running 20000000 simple coroutines such as `_co_yield_from_fn` && `_co_fn` in `loop_e` to experience `ucoroutine-switching`.
10000000 coroutines(_co_fn) switching, other 10000000 coroutines(_co_yield_from_fn) used to sync the former 10000000 coroutines to terminate respectively.
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

start 'top' on another terminal to see the little memory consumption.
```C
top - 16:41:24 up 3 days,  8:02,  3 users,  load average: 0.49, 0.24, 0.15
Tasks:   1 total,   1 running,   0 sleeping,   0 stopped,   0 zombie
%Cpu(s):  3.6 us,  4.5 sy,  0.0 ni, 91.5 id,  0.1 wa,  0.0 hi,  0.3 si,  0.0 st

  PID USER    PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
18036  lxr    20   0    4348    352    276 R  28.2  0.0   0:12.37  loop_e
```
the memory consumption will increase more when the corotines' number and running time growth.
