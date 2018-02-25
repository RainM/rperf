MVN?=mvn
MVNFLAGS?=

JAVA?=java
JVMFLAGS?=

INSTRUMENTER_JAR=javaagent/target/instrumenter-1.2-SNAPSHOT-jar-with-dependencies.jar

check_defined = \
    $(strip $(foreach 1,$1, \
        $(call __check_defined,$1,$(strip $(value 2)))))
__check_defined = \
    $(if $(value $1),, \
        $(error Undefined $1$(if $2, ($2))$(if $(value @), \
                required by target '$@')))

help:
	@echo -e "Make for vvuilding libperf executable end the rest partd of rperf"
	@echo -e "Available targets:"
	@echo -e "\t* link-linux - links mainline linux kernel to use"
	@echo -e "\t* unlink-linux - unlink mainline linux"
	@echo -e "\t* build-libperf - builds libperf.so and jvmti agent"
	@echo -e "\t* build-javaagent - build javaagent to instrument"
	@echo -e "\t* build-all - build javaagent and libperf"
	@echo -e "\t* example-args - output arguments for JVM"

unlink-linux:
	-rm kernel_symlink

link-linux: kernel_symlink

kernel_symlink:
	@:$(call check_defined, LINUX_KERNEL_PATH, path to linux kernel sources)
	ln -s $(LINUX_KERNEL_PATH) kernel_symlink

build-libperf: kernel_symlink
	rm -rf kernel_symlink/tools/perf
	cp -r libperf kernel_symlink/tools/perf
	(cd -P kernel_symlink && cd tools/perf/ && make WERROR=0 DEBUG=0 NO_NEWT=1 NO_SLANG=1 NO_GTK=1 NO_DEMANGLE=1 NO_LIBELF= NO_LIBUNWIND=1 NO_BACKTRACE=1 NO_LIBNUMA=1 NO_LIBAUDIT=1 NO_LIBBIONIC=1 NO_LIBCRYPTO=1 NO_LIBDW_DWARF_UNWIND=1 NO_PERF_READ_VDSO32=1 NO_PERF_READ_VDSOX32=1 NO_ZLIB=1 NO_LZMA=1 NO_LIBBPF=1 NO_SDT=1 NO_JVMTI=1 NO_LIBPERL=1 NO_LIBPYTHON=1 NO_DWARF=1  V=1 VF=1 clean all)

$(INSTRUMENTER_JAR):
	( cd javaagent && $(MVN) $(MVNFLAGS) clean package )

build-javaagent: $(INSTRUMENTER_JAR)

build-all: build-javaagent build-libperf
	@echo "Build done"

example-args:
	@echo "LD_LIBRARY_PATH=\$$LD_LIBRARY_PATH:"$(shell readlink -f kernel_symlink/tools/perf/)   			\
		$(JAVA) $(JVMFLAGS)                                                	\
		-agentlib:perf=unfoldall						\
		-javaagent:$(shell readlink -f $(INSTRUMENTER_JAR))=               	\
		-DTRIGGER_COUNTDOWN=15000 					   	\
		-DTRIGGER_METHOD=decode 						\
		-DTRIGGER_CLASS=ru/raiffeisen/App					\
		-DOUTPUT_INSTRUMENTED_CLASSES=1

clean:
	(cd -P kernel_symlink && cd tools/perf/ && make WERROR=0 DEBUG=0 NO_NEWT=1 NO_SLANG=1 NO_GTK=1 NO_DEMANGLE=1 NO_LIBELF= NO_LIBUNWIND=1 NO_BACKTRACE=1 NO_LIBNUMA=1 NO_LIBAUDIT=1 NO_LIBBIONIC=1 NO_LIBCRYPTO=1 NO_LIBDW_DWARF_UNWIND=1 NO_PERF_READ_VDSO32=1 NO_PERF_READ_VDSOX32=1 NO_ZLIB=1 NO_LZMA=1 NO_LIBBPF=1 NO_SDT=1 NO_JVMTI=1 NO_LIBPERL=1 NO_LIBPYTHON=1 NO_DWARF=1  V=1 VF=1 clean)
	(cd javaagent && $(MVN) $(MVNFLAGS) clean)
