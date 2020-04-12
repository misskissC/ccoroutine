the `SConscript` here would build out ccoroutine library. use `lncc = SConscript('~/src/SConscript')` to get the library path to `lncc` by `SConstruct` in your experiences/applications, say `experiences/loop_e/SConstruct`.

some predefined macros would decide if ccoroutines to 
own more efficient on scheduling.

[1] -DRUNNING_WHEN_CREATEING=1, running created 
     coroutines when creating new one.

[2] -DMEMORY_ALLOC_PRE=1, pre-allocate memory
    for coroutines.
    
[3] -DUSE_LNPF=1, using the `ln_printf()` to instead 
    `print family in gilbc` to reduce stack.
    **the stack consuming is very small(less than 1Kib) 
    if no error-logs for coroutines.**

the MEMORY_ALLOC_PRE=0, RUNNING_WHEN_CREATING=1,
USE_LNPF=0 are defaulted. you can make different 
choices on different computers.
