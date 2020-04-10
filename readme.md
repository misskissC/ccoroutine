A coroutine switching implement by ucontext && assembly in C-language
----

The coroutine switching implement in C-language called `ccoroutine` this time. it is powerful and lightweight.

The `cs_yield()`, `cs_yield_from()`, `cs_send()`, `cs_loop()` in `ccoroutine` are just like the `yield`, `yield from`, `generator.send()`, `asyncio.loop()` in python respectively. 

I have had wrote two blogs to flatter `ccoroutine`, but it‘s core routines always less than 500 lines, this is the reason i still love it.

The potential bottleneck of `ccoroutine` besets me at the same time, so goddess hopes more knowledgeable guys just like you can **continue to improve it**.

### catalogs
```C
.
├── doc
│   └── 一种在C语言中用汇编指令和 System V ucontext 支撑实现的协程切换.md
├── experiences
│   ├── loop_e
│   │   ├── loop_e.c
│   │   └── SConstruct
│   ├── yield_e
│   │   ├── SConstruct
│   │   └── yield_e.c
│   └── yield_from_e
│       ├── SConstruct
│       └── yield_from_e.c
├── include
│   ├── ln_co.h
│   └── ln_comm.h
├── readme.md
└── src
    ├── context
    │   ├── assembly
    │   │   ├── ln_asm.c
    │   │   └── SConscript
    │   ├── ln_context.h
    │   ├── SConscript
    │   └── ucontext
    │       ├── ln_uc.c
    │       └── SConscript
    ├── ln_co.c
    └── SConscript
```
`src`, core logic for `ccoroutine`.

`context`, coroutine switching supporter.

`experiences`, namely examples for `ccoroutine`, `yield`, `send`, `yield from`, `loop` experiences included.

`doc`, chinese documents or notes for `ccoroutine`.

the `SConscript` in `src/` would build out ccoroutine library. use `lncc = SConscript('../../src/SConscript')` to get the library path in `SConstruct` of experiences.

### running experience
if you owns one running 20000000 simple coroutines such as `_co_yield_from_fn` && `_co_fn` in `loop_e` on a server-computer to experience `ccoroutine`.
10000000 coroutines(_co_fn) switching, other 10000000 coroutines(_co_yield_from_fn) used to sync the former 10000000 coroutines to terminate respectively.

running `scons -Q` to build the target program.
```C
[a@b loop_e]$ scons -Q
...
[a@b loop_e]$
[a@b loop_e]$ ./loop_e 2>o.txt
[a@b loop_e]$ vi o.txt
       1 '_co_yield_from_fn' sync '_co_fn' terminated. '_co_fn' return-value: 012
...
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
the memory consumption will increase more when the corotines' number and running time growth, of course.