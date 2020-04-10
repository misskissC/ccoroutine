一种在C语言中用汇编指令和 ucontext 支撑实现的协程切换
---
### 1 简介
此次用C语言实现的协程切换程序被称作`ccoroutine`。

`ccoroutine`的主要特点是强大和轻量——同线程和进程相比，其拥有更大的并发上限（参与并发的协程所需内存资源更少，线程运行栈分配参考值为4M，ccoroutine协程为11K）；另外，`ccoroutine`的核心逻辑代码还未达800行，这是开发过程中持续简化的结果（保证阅读性前提下）。

话说回来，`ccoroutine`潜在瓶颈（文档中有所提及）也困扰着我，希望真正的饱学之士路过时能帮忙继续提升他。

### 2 目录结构
`src`，核心包含了`ccoroutine`的核心代码。

`src`中的`co_yield()`, `co_yield_from()`, `co_send()`以及`co_loop()`是被`python`中`yield`, `yield from`, `generator.send()`, `asyncio.loop()`的轻量优雅打动后编写的。也就是说，`ccoroutine`用C语言实现了`python`的`yield`（及其他）机制。

注：`co_yield()`和`co_send()`可以支撑协程切换（特适用于异步情形），`co_yield_from()`可用于支撑对协程的同步（`co_yield_from()`本身亦可是异步的），`co_loop()`用作协程调度，这些朴实功能的结合在实际开发中很受用。

`ccoroutine`现使用`scons`进行工程构建和管理（最初为`make`）。`ccoroutine`已在`src`中内置了构建其静态库的`SConscript`。也就是说，应用程序`SConstruct`中的语句 
```python
lncc = SConscript('~/src/SConscript')
```
即可将`ccoroutine`静态库的路径写到变量`lncc`中。可参考例子`experiences/loop_e`。

`context`，支持协程切换的实现，包括汇编和调用第三方库ucontext的实现。

`experiences`，体验`ccoroutine`所实现协程切换机制的例子，包括`co_yield`, `co_send`, `co_yield_from` and `co_loop`。

`doc`，`experiences`的开发笔记（中文）。

### 3 体验
就运行 20000000 个简单型协程体验下吧。其中 10000000 个协程来自`experiences/loop_e/loop_e.c`中的`_co_fn`，他通过`co_yield()`和`co_send()`实现协程切换；另外 10000000 协程来自`experiences/loop_e/loop_e.c`中的`_co_yield_from_fn`，他们被分别用来同步协程`_co_fn`的终止。

在`experiences/loop_e/`下通过命令
```python
scons -Q
```
运行构建`loop_e`的脚本`SConstruct`，然后运行`loop_e`
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
可见，`co_yield_from()`在同步子协程`co_fn`运行的同时，还支持着整个在协程层面的并发性。

再打开一个终端观察`loop_e`运行情况。
```C
top - 16:41:24 up 3 days,  8:02,  3 users,  load average: 0.49, 0.24, 0.15
Tasks:   1 total,   1 running,   0 sleeping,   0 stopped,   0 zombie
%Cpu(s):  3.6 us,  4.5 sy,  0.0 ni, 91.5 id,  0.1 wa,  0.0 hi,  0.3 si,  0.0 st

  PID USER    PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
18036  lxr    20   0    4348    352    276 R  28.2  0.0   0:12.37  loop_e
```
协程调度本身属于CPU密集型任务，由于调度协程中无如超时等待异步（如select）机制且协程并发量大，所以`loop_e`的CPU一定会高。考虑用户体验，`ccoroutine`在每调度一单元量的协程后就主动放弃CPU，当协程来自实际应用时（网络IO），可将主动放弃CPU的语句去除。

另外，当协程数量和协程运行时间同时大幅度增加时，`ccoroutine`所占内存资源也会随之上升。对于像'_co_fn'这样简单的协程，`ccoroutine`中`边运行边创建协程 + 内存即用即释放的机制`可以使其并发量无上限。


A coroutine switching implement by assembly && ucontext in C-language
----

### 1 brief
the coroutine switching implement in C-language called `ccoroutine` this time.

`ccoroutine` is powerful and lightweight, it spports much more concurrency upper limit than thread or process, and it‘s major routines always less than 800 lines. 

the potential bottleneck of `ccoroutine` besets me at the same time, so goddess hopes more knowledgeable guys just like you can **continue to improve it**.

### 2 catalogs
`src`, major C-routines for `ccoroutine`.

`co_yield()`, `co_yield_from()`, `co_send()` and `co_loop()` implemented in `src`, they are just like the `yield`, `yield from`, `generator.send()`, `asyncio.loop()` in python respectively. In other words, `ccoroutine` is the program which implement python's `yield`(and so on) in C language.

the `SConscript` buildin `src` used to build ccoroutine library. using 
```python
lncc = SConscript('../../src/SConscript')
```
to get the library path by `SConstruct` in experiences/applications, say the `SConstruct` in `experiences/loop_e`. 

`context`, the coroutine-switching supporters, including assembly and ucontext.

`experiences`, namely examples for `ccoroutine`, `co_yield`, `co_send`, `co_yield_from` and `co_loop` included.

`doc`, development-diary for `ccoroutine` in chinese.

### 3 running experience
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
