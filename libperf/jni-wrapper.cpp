#include "profiler.hpp"
#include "ru_raiffeisen_PerfPtProf.h"

/*
 * Class:     ru_raiffeisen_PerfPtProf
 * Method:    init
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_ru_raiffeisen_PerfPtProf_init
(JNIEnv *, jclass, jint countdown) {
    init(countdown);
}

/*
 * Class:     ru_raiffeisen_PerfPtProf
 * Method:    start
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_ru_raiffeisen_PerfPtProf_start
(JNIEnv *, jclass) {
    start();
}

/*
 * Class:     ru_raiffeisen_PerfPtProf
 * Method:    stop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_ru_raiffeisen_PerfPtProf_stop
(JNIEnv *, jclass) {
    stop();
}
