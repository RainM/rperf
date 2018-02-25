#if !defined(__PROFILER_HPP__)
#define __PROFILER_HPP__

#if defined(__cplusplus)
#define __API__ extern "C"
#else
#define __API__
#endif

__API__ void init(int cntr);
__API__ void start();
__API__ void stop();

#endif
