# rperf

RPerf is a tool for profiling ultra-small pieces of code from dozens of mucroseconds to dozens of milliseconds.
This tool is based on perf from linux's kernel.

Traditional sampling profilers are unable to profile such code with high detalization. This tool uses *Intel ProcessorTrace* technology for dumping control-flow execution. After that rperf outputs top with functions which takes maximum time.

## How it works
### High-level overview
For example, we need to profile execution of code which takes 1ms. 

- First, javaagent instrument required method of target application and inserts calls to native library. 
- Second, jvmti agent records JITted code location.
- Native library (libperf.so) skips first N calls and will profile only N+1 run.
- libperf.so activates internal perf engine and record control-flow execution
- libperf.so outputs TOP hottest functions and text representation of execution trace

In fact, native library is slightly modified perf from linux's kernel.

## How to build

1. Download linux kernel
```
$ git clone https://github.com/torvalds/linux.git linux-kernel-mainline
```
2. Download RPerf
```
$ git clone https://github.com/RainM/rperf.git rperf
```
3. Link linux kernel to rperf
```
$ LINUX_KERNEL_PATH=linux-kernel-mainline make -C rperf link-linux
```
4. Build libperf.so and javaagent
```
$ make -C rperf build-all
```
5. Arguments to run RPerf may be obtained this way
```
$ make -C rperf example-args
```
