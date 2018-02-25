


const char* input_name = "perf.data";
//#include <jni.h>
//#include <jni_md.h>

#include <pthread.h>
//#include "com_sm_App.h"

#include <unistd.h>
//#include <jni.h>
//#include <jni_md.h>
//#include <atomic>
//#include <iostream>

//#include <stdatomic.h>

// SPDX-License-Identifier: GPL-2.0
/*
 * perf.c
 *
 * Performance analysis utility.
 *
 * This is the main hub from which the sub-commands (perf stat,
 * perf top, perf record, perf report, etc.) are started.
 */
#include "builtin.h"

#include "util/env.h"
//#include <subcmd/exec-cmd.h>
#include "util/config.h"
#include "util/quote.h"
#include <subcmd/run-command.h>
#include "util/parse-events.h"
#include <subcmd/parse-options.h>
//#include "util/bpf-loader.h"
#include "util/debug.h"
#include "util/event.h"
#include <api/fs/fs.h>
#include <api/fs/tracing_path.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kernel.h>


//int read_data();

const char perf_usage_string[] =
	"perf [--version] [--help] [OPTIONS] COMMAND [ARGS]";

const char perf_more_info_string[] =
	"See 'perf help COMMAND' for more information on a specific command.";

struct option options[] = {
	OPT_ARGUMENT("help", "help"),
	OPT_ARGUMENT("version", "version"),
	OPT_ARGUMENT("exec-path", "exec-path"),
	OPT_ARGUMENT("html-path", "html-path"),
	OPT_ARGUMENT("paginate", "paginate"),
	OPT_ARGUMENT("no-pager", "no-pager"),
	OPT_ARGUMENT("debugfs-dir", "debugfs-dir"),
	OPT_ARGUMENT("buildid-dir", "buildid-dir"),
	OPT_ARGUMENT("list-cmds", "list-cmds"),
	OPT_ARGUMENT("list-opts", "list-opts"),
	OPT_ARGUMENT("debug", "debug"),
	OPT_END()
};

static int handle_options(const char ***argv, int *argc, int *envchanged)
{
	int handled = 0;

	while (*argc > 0) {
		const char *cmd = (*argv)[0];
		if (cmd[0] != '-')
			break;

		/*
		 * For legacy reasons, the "version" and "help"
		 * commands can be written with "--" prepended
		 * to make them look like flags.
		 */
		if (!strcmp(cmd, "--help") || !strcmp(cmd, "--version"))
			break;

		/*
		 * Shortcut for '-h' and '-v' options to invoke help
		 * and version command.
		 */
		if (!strcmp(cmd, "-h")) {
			(*argv)[0] = "--help";
			break;
		}

		if (!strcmp(cmd, "-v")) {
			(*argv)[0] = "--version";
			break;
		}

		/*
		 * Check remaining flags.
		 */
		if (strstarts(cmd, CMD_EXEC_PATH)) {
			cmd += strlen(CMD_EXEC_PATH);
			if (*cmd == '=')
				set_argv_exec_path(cmd + 1);
			else {
				puts(get_argv_exec_path());
				exit(0);
			}
		} else if (!strcmp(cmd, "--html-path")) {
			puts(system_path(PERF_HTML_PATH));
			exit(0);
		} else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--paginate")) {
//			use_pager = 1;
		} else if (!strcmp(cmd, "--no-pager")) {
//use_pager = 0;
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--debugfs-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --debugfs-dir.\n");
				usage(perf_usage_string);
			}
			tracing_path_set((*argv)[1]);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (!strcmp(cmd, "--buildid-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --buildid-dir.\n");
				usage(perf_usage_string);
			}
			set_buildid_dir((*argv)[1]);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (strstarts(cmd, CMD_DEBUGFS_DIR)) {
			tracing_path_set(cmd + strlen(CMD_DEBUGFS_DIR));
			fprintf(stderr, "dir: %s\n", tracing_path);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--list-cmds")) {
			unsigned int i;
/*
			for (i = 0; i < ARRAY_SIZE(commands); i++) {
				struct cmd_struct *p = commands+i;
				printf("%s ", p->cmd);
			}
*/
			putchar('\n');
			exit(0);
		} else if (!strcmp(cmd, "--list-opts")) {
			unsigned int i;

			for (i = 0; i < ARRAY_SIZE(options)-1; i++) {
				struct option *p = options+i;
				printf("--%s ", p->long_name);
			}
			putchar('\n');
			exit(0);
		} else if (!strcmp(cmd, "--debug")) {
			if (*argc < 2) {
				fprintf(stderr, "No variable specified for --debug.\n");
				usage(perf_usage_string);
			}
			if (perf_debug_option((*argv)[1]))
				usage(perf_usage_string);

			(*argv)++;
			(*argc)--;
		} else {
			fprintf(stderr, "Unknown option: %s\n", cmd);
			usage(perf_usage_string);
		}

		(*argv)++;
		(*argc)--;
		handled++;
	}
	return handled;
}

#define RUN_SETUP	(1<<0)
#define USE_PAGER	(1<<1)


static void pthread__block_sigwinch(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void pthread__unblock_sigwinch(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGWINCH);
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
#define cache_line_size(cacheline_sizep) *cacheline_sizep = sysconf(_SC_LEVEL1_DCACHE_LINESIZE)
#else
static void cache_line_size(int *cacheline_sizep)
{
	if (sysfs__read_int("devices/system/cpu/cpu0/cache/index0/coherency_line_size", cacheline_sizep))
		pr_debug("cannot determine cache line size");
}
#endif

/////////////////////////////////////////////////////////////////////////////////////

/*
int __do_some_profiling();
void set_stop_record(); // from builtin-record.c
int is_near_to_poll();
static void start();
static void init(int i);
static void stop();
*/

int do_perf_record(pid_t tid_) {
  	int err;
	const char *cmd;
	int value;

	char tid[100] = {};
	sprintf(tid, "%d", tid_);

	char** argv = (char**)malloc(20 * sizeof(char*));

	int argc = 0;
	
	argv[argc++] = "perf";
	argv[argc++] = "record";
	argv[argc++] = "-e";
	argv[argc++] = "intel_pt/cyc,cyc_thresh=0/u";
	argv[argc++] = "--tid";
	argv[argc++] = tid;


	/* The page_size is placed in util object. */
	page_size = sysconf(_SC_PAGE_SIZE);
	cache_line_size(&cacheline_size);

	if (sysctl__read_int("kernel/perf_event_max_stack", &value) == 0)
		sysctl_perf_event_max_stack = value;

	if (sysctl__read_int("kernel/perf_event_max_contexts_per_stack", &value) == 0)
		sysctl_perf_event_max_contexts_per_stack = value;

	cmd = extract_argv0_path(argv[0]);
	if (!cmd)
		cmd = "perf-help";

	srandom(time(NULL));

	perf_config__init();
	err = perf_config(perf_default_config, NULL);
	if (err)
		return err;
	set_buildid_dir(NULL);

	/* get debugfs/tracefs mount point from /proc/mounts */
	tracing_path_mount();

	/* Look for flags.. */
	argv++;
	argc--;
	handle_options(&argv, &argc, NULL);
	//commit_pager_choice();

	if (argc > 0) {
		if (strstarts(argv[0], "--"))
			argv[0] += 2;
	} else {
		/* The user didn't specify a command; give them help */
		printf("\n usage: %s\n\n", perf_usage_string);
		//list_common_cmds_help();
		printf("\n %s\n\n", perf_more_info_string);
		return 1;
	}
	cmd = argv[0];

	test_attr__init();

	/*
	 * We use PATH to find perf commands, but we prepend some higher
	 * precedence paths: the "--exec-path" option, the PERF_EXEC_PATH
	 * environment, and the $(perfexecdir) from the Makefile at build
	 * time.
	 */
	setup_path();
	/*
	 * Block SIGWINCH notifications so that the thread that wants it can
	 * unblock and get syscalls like select interrupted instead of waiting
	 * forever while the signal goes to some other non interested thread.
	 */
	pthread__block_sigwinch();

	perf_debug_setup();

	cmd_record(argc, argv);
}

int do_perf_top() {
    char** argv = (char**)malloc(sizeof(*argv) * 20);
    int argc = 0;

    argv[argc++] = "script";
    argv[argc++] = "--ns";

    cmd_script(argc, argv);
}

/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////

void start();
void init(int i);
void stop();

void* __test_workload(void* w) {
    char* p = (char*)w;
    int r = 0;
    system("sleep 1");
    for (int i =0; i < 10; ++i) {
	for (int ii = 1; ii < 100; ++ii) {
	    r += i % ii;
	}
    }
    system("sleep 1");
    set_stop_record();
    return p + r;
}


int main() {
    init(1);

    start();

    __test_workload("");

    stop();
}

