#include "profiler.hpp"
#include "profiler-backend.hpp"
#include <stdlib.h> // I have no idea why it clashes with perf.h ;-(
#include "perf.h"
#include <stdio.h>
#include <stdio.h>
#include <locale.h>

#include <pthread.h>

pthread_mutex_t __wait_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t __wait_condition = PTHREAD_COND_INITIALIZER;

int start_happens = 0;
int stop_happens = 0;
volatile int should_start = 0;
volatile pid_t tid_to_profile = -1;
volatile int __once_start = 0;

int countdown_counter;

extern "C" int is_near_to_poll(); // from builtin-record.h
extern "C" void set_stop_record(); // from builtin-record.h

extern "C" void dump_perf_file(); // from jvmti-agent.cpp

static void* __thread_func(void* arg) {
    pthread_mutex_lock(&__wait_mutex);
    while (!should_start) {
	pthread_cond_wait(&__wait_condition, &__wait_mutex);
    }
    should_start = 0;
    pthread_mutex_unlock(&__wait_mutex);

    __atomic_store_n(&start_happens, 1, __ATOMIC_SEQ_CST);

    ::do_perf_record(tid_to_profile);

    ::printf("Record done\n");
    ::fflush(stdout);
    
    ::printf("Dumping symbols\n");
    ::dump_perf_file();
    
    ::printf("Processing top\n");
    ::do_perf_top();
    __atomic_store_n(&stop_happens, 1, __ATOMIC_SEQ_CST);

    return NULL;
}

void init(int cntr) {
    int prev_value = __atomic_exchange_n(&__once_start, 1, __ATOMIC_SEQ_CST);
    if (prev_value == 0) {
	setlocale(LC_NUMERIC, "");

	pthread_t thread;
	pthread_create(&thread, NULL, __thread_func, NULL);
	countdown_counter = cntr;

	printf("------------------------------------------\n");
	printf("-------LIBPERF PROFILER INITIALIZED-------\n");
	printf("Init countdown: %d\n", countdown_counter);
	printf("------------------------------------------\n");
    } else {
	printf("Already initialized! skipping this initialization!\n");
    }
}

void start() {
    int prev_value = __atomic_fetch_sub(&countdown_counter, 1, __ATOMIC_SEQ_CST);

    if (prev_value == 1) {
	pthread_mutex_lock(&__wait_mutex);
	should_start = 1;
	tid_to_profile = syscall (SYS_gettid);
	pthread_cond_signal(&__wait_condition);
	pthread_mutex_unlock(&__wait_mutex);

	while (! __atomic_load_n(&start_happens, __ATOMIC_SEQ_CST));
	while (!is_near_to_poll());
    }
}

void stop() {
    if (start_happens) {
	auto current_tid = syscall(SYS_gettid);
	if (current_tid == tid_to_profile) {
	    set_stop_record();
	    __atomic_store_n(&start_happens, 0, __ATOMIC_SEQ_CST);
	    while (!__atomic_load_n(&stop_happens, __ATOMIC_SEQ_CST)) ;
	    __atomic_store_n(&stop_happens, 0, __ATOMIC_SEQ_CST);
	    
	    ::exit(1);
	}
    }
}
