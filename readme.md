A coroutine switching implement by assembly && ucontext in C-language
----

the coroutine switching implement in C-language called `ccoroutine` this time.

`ccoroutine` is powerful and lightweight, it spports much more concurrency upper limit than thread or process, and it‘s major routines always less than 800 lines. 

the potential bottleneck of `ccoroutine` besets me at the same time, so goddess hopes more knowledgeable guys just like you can **continue to improve it**.

### catalogs
`src`, major C-routines for `ccoroutine`.

`co_yield()`, `co_yield_from()`, `co_send()`, `co_loop()` implemented in `src`, they are just like the `yield`, `yield from`, `generator.send()`, `asyncio.loop()` in python respectively. In other words, `ccoroutine` is the program which implement python's `yield`(and so on) in C language.

the `SConscript` buildin `src` used to build ccoroutine library. using 
```python
lncc = SConscript('../../src/SConscript')
```
to get the library path by `SConstruct` in experiences/applications, say the `SConstruct` in `experiences/loop_e`. 

`context`, the coroutine-switching supporters, including assembly and ucontext.

`experiences`, namely examples for `ccoroutine`, `co_yield`, `co_send`, `co_yield_from` and `co_loop` included.

`doc`, development-diary for `ccoroutine` in chinese.

### running experience
let us run 20000000 simple coroutines such as `_co_yield_from_fn` && `_co_fn` in `loop_e` on a server-computer to experience `ccoroutine`.
10000000 coroutines(_co_fn) switching, other 10000000 coroutines(_co_yield_from_fn) used to synchronize the former 10000000 coroutines to terminate respectively.

running `scons -Q` to build the target program `loop_e`.
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

start 'top' on another terminal when `loop_e` running.
```C
top - 16:41:24 up 3 days,  8:02,  3 users,  load average: 0.49, 0.24, 0.15
Tasks:   1 total,   1 running,   0 sleeping,   0 stopped,   0 zombie
%Cpu(s):  3.6 us,  4.5 sy,  0.0 ni, 91.5 id,  0.1 wa,  0.0 hi,  0.3 si,  0.0 st

  PID USER    PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
18036  lxr    20   0    4348    352    276 R  28.2  0.0   0:12.37  loop_e
```
the memory consumption will increase more when the corotines' number and running-time growth, of course.
