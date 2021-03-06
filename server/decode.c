#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "decode.h"
#include "exit.h"
#include "socketinfo.h"
#include "trinity.h"
#include "types.h"
#include "udp.h"
#include "utils.h"

static void decode_main_started(char *buf)
{
	struct msg_mainstarted *mainmsg;

	mainmsg = (struct msg_mainstarted *) buf;
	printf("Main started. pid:%d number of children: %d. shm:%p-%p\n",
		mainmsg->hdr.pid, mainmsg->num_children, mainmsg->shm_begin, mainmsg->shm_end);
}

static void decode_main_exiting(char *buf)
{
	struct msg_mainexiting *mainmsg;

	mainmsg = (struct msg_mainexiting *) buf;
	printf("Main exiting. pid:%d Reason: %s\n", mainmsg->hdr.pid, decode_exit(mainmsg->reason));
}

static void decode_child_spawned(char *buf)
{
	struct msg_childspawned *childmsg;

	childmsg = (struct msg_childspawned *) buf;
	printf("Child spawned. id:%d pid:%d\n", childmsg->childno, childmsg->hdr.pid);
}

static void decode_child_exited(char *buf)
{
	struct msg_childexited *childmsg;

	childmsg = (struct msg_childexited *) buf;
	printf("Child exited. id:%d pid:%d\n", childmsg->childno, childmsg->hdr.pid);
}

static void decode_child_signalled(char *buf)
{
	struct msg_childsignalled *childmsg;

	childmsg = (struct msg_childsignalled *) buf;
	printf("Child signal. id:%d pid:%d signal: %s\n",
		childmsg->childno, childmsg->hdr.pid, strsignal(childmsg->sig));
}

static void decode_obj_created_file(char *buf)
{
	struct msg_objcreatedfile *objmsg;

	objmsg = (struct msg_objcreatedfile *) buf;

	if (objmsg->fopened) {
		printf("%s file object created at %p by pid %d: fd %d = fopen(\"%s\") ; fcntl(fd, 0x%x)\n",
			objmsg->hdr.global ? "local" : "global",
			objmsg->hdr.address, objmsg->hdr.pid,
			objmsg->fd, objmsg->filename,
			objmsg->fcntl_flags);
	} else {
		printf("%s file object created at %p by pid %d: fd %d = open(\"%s\", 0x%x)\n",
			objmsg->hdr.global ? "local" : "global",
			objmsg->hdr.address, objmsg->hdr.pid,
			objmsg->fd, objmsg->filename, objmsg->flags);
	}
}

static void decode_obj_created_map(char *buf)
{
	struct msg_objcreatedmap *objmsg;
	const char *maptypes[] = {
		"initial anon mmap",
		"child created anon mmap",
		"mmap'd file",
	};
	objmsg = (struct msg_objcreatedmap *) buf;

	printf("%s map object created at %p by pid %d: start:%p size:%ld name:%s prot:%x type:%s\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->start, objmsg->size, objmsg->name, objmsg->prot, maptypes[objmsg->type - 1]);
}

static void decode_obj_created_pipe(char *buf)
{
	struct msg_objcreatedpipe *objmsg;
	objmsg = (struct msg_objcreatedpipe *) buf;

	printf("%s pipe object created at %p by pid %d: fd:%d flags:%x [%s]\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->fd, objmsg->flags,
		objmsg->reader ? "reader" : "writer");
}

static void decode_obj_created_perf(char *buf)
{
	struct msg_objcreatedperf *objmsg;
	char *p;
	int i;

	objmsg = (struct msg_objcreatedperf *) buf;
	printf("%s perf object created at %p by pid %d: fd:%d pid:%d cpu:%d group_fd:%d flags:%lx eventattr len:%d\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->fd, objmsg->pid, objmsg->cpu, objmsg->group_fd, objmsg->flags,
		objmsg->eventattrsize);

	printf("perf_event_attr: ");
	p = (char *) &objmsg->eventattr;
	for (i = 0; i < objmsg->eventattrsize; i++) {
		printf("%02x ", (unsigned char) p[i]);
	}
	printf("\n");
}

static void decode_obj_created_epoll(char *buf)
{
	struct msg_objcreatedepoll *objmsg;
	objmsg = (struct msg_objcreatedepoll *) buf;

	printf("%s epoll object created at %p by pid %d: fd:%d create1: %s flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd,
		objmsg->create1 ? "false" : "true",
		objmsg->flags);
}

static void decode_obj_created_eventfd(char *buf)
{
	struct msg_objcreatedeventfd *objmsg;
	objmsg = (struct msg_objcreatedeventfd *) buf;

	printf("%s eventfd object created at %p by pid %d: fd:%d count: %d flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd,
		objmsg->count, objmsg->flags);
}


static void decode_obj_created_timerfd(char *buf)
{
	struct msg_objcreatedtimerfd *objmsg;
	objmsg = (struct msg_objcreatedtimerfd *) buf;

	printf("%s timerfd object created at %p by pid %d: fd:%d clockid: %d flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd,
		objmsg->clockid, objmsg->flags);
}

static void decode_obj_created_testfile(char *buf)
{
	struct msg_objcreatedfile *objmsg;

	objmsg = (struct msg_objcreatedfile *) buf;

	if (objmsg->fopened) {
		printf("%s testfile object created at %p by pid %d: fd %d = fopen(\"%s\") ; fcntl(fd, 0x%x)\n",
			objmsg->hdr.global ? "local" : "global",
			objmsg->hdr.address, objmsg->hdr.pid,
			objmsg->fd, objmsg->filename,
			objmsg->fcntl_flags);
	} else {
		printf("%s testfile object created at %p by pid %d: fd %d = open(\"%s\", 0x%x)\n",
			objmsg->hdr.global ? "local" : "global",
			objmsg->hdr.address, objmsg->hdr.pid,
			objmsg->fd, objmsg->filename, objmsg->flags);
	}
}

static void decode_obj_created_memfd(char *buf)
{
	struct msg_objcreatedmemfd *objmsg;
	objmsg = (struct msg_objcreatedmemfd *) buf;

	printf("%s memfd object created at %p by pid %d: fd:%d name: %s flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd,
		objmsg->name, objmsg->flags);
}

static void decode_obj_created_drm(char *buf)
{
	struct msg_objcreateddrm *objmsg;
	objmsg = (struct msg_objcreateddrm *) buf;

	printf("%s drm object created at %p by pid %d: fd:%d\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd);
}

static void decode_obj_created_inotify(char *buf)
{
	struct msg_objcreatedinotify *objmsg;
	objmsg = (struct msg_objcreatedinotify *) buf;

	printf("%s inotify object created at %p by pid %d: fd:%d flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd, objmsg->flags);
}

static void decode_obj_created_userfault(char *buf)
{
	struct msg_objcreateduserfault *objmsg;
	objmsg = (struct msg_objcreateduserfault *) buf;

	printf("%s userfault object created at %p by pid %d: fd:%d flags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd, objmsg->flags);
}

static void decode_obj_created_fanotify(char *buf)
{
	struct msg_objcreatedfanotify *objmsg;
	objmsg = (struct msg_objcreatedfanotify *) buf;

	printf("%s fanotify object created at %p by pid %d: fd:%d flags:%x eventflags:%x\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->fd,
		objmsg->flags, objmsg->eventflags);
}

static void decode_obj_created_bpfmap(char *buf)
{
	struct msg_objcreatedbpfmap *objmsg;
	const char *bpfmaptypes[] = {
		"hash", "array", "prog array", "perf_event_array",
		"percpu hash", "percpu array", "stack trace", "cgroup array",
		"lru hash", "lru hash (no common LRU)", "LRU percpu hash", "LPM TRIE",
	};

	objmsg = (struct msg_objcreatedbpfmap *) buf;

	printf("%s bpf map object created at %p by pid %d: fd:%d type:%s\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->map_fd,
		bpfmaptypes[objmsg->map_type]);
}

static void decode_obj_created_socket(char *buf)
{
	struct msg_objcreatedsocket *objmsg;
	objmsg = (struct msg_objcreatedsocket *) buf;

	printf("%s socket object created at %p by pid %d: fd:%d family:%d type:%d protocol:%d\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid, objmsg->si.fd,
		objmsg->si.triplet.family,
		objmsg->si.triplet.type,
		objmsg->si.triplet.protocol);
}

static void decode_obj_created_futex(char *buf)
{
	struct msg_objcreatedfutex *objmsg;
	objmsg = (struct msg_objcreatedfutex *) buf;

	printf("%s futex object created at %p by pid %d: futex:%d owner:%d\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->futex, objmsg->owner);
}

static void decode_obj_created_shm(char *buf)
{
	struct msg_objcreatedshm *objmsg;
	objmsg = (struct msg_objcreatedshm *) buf;

	printf("%s shm object created at %p by pid %d: id:%u size:%zu flags:%x ptr:%p\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->id, objmsg->size, objmsg->flags, objmsg->ptr);
}

static void decode_obj_destroyed(char *buf)
{
	struct msg_objdestroyed *objmsg;
	objmsg = (struct msg_objdestroyed *) buf;

	printf("%s object at %p destroyed by pid %d. type:%d\n",
		objmsg->hdr.global ? "local" : "global",
		objmsg->hdr.address, objmsg->hdr.pid,
		objmsg->hdr.type);
}

static void decode_syscalls_enabled(char *buf)
{
	struct msg_syscallsenabled *scmsg;
	int nr;
	int i;

	scmsg = (struct msg_syscallsenabled *) buf;
	nr = scmsg->nr_enabled;
	if (scmsg->arch_is_biarch == TRUE) {
		printf("Enabled %d %s bit syscalls : { ", nr, scmsg->is_64 ? "64" : "32");
		for (i = 0 ; i < nr; i++)
			printf("%d ", scmsg->entries[i]);
		printf("}\n");
	} else {
		printf("Enabled %d syscalls : { ", nr);
		for (i = 0 ; i < nr; i++)
			printf("%d ", scmsg->entries[i]);
		printf("}\n");
	}
}

const struct msgfunc decodefuncs[MAX_LOGMSGTYPE] = {
	[MAIN_STARTED] = { decode_main_started },
	[MAIN_EXITING] = { decode_main_exiting },
	[CHILD_SPAWNED] = { decode_child_spawned },
	[CHILD_EXITED] = { decode_child_exited },
	[CHILD_SIGNALLED] = { decode_child_signalled },
	[OBJ_CREATED_FILE] = { decode_obj_created_file },
	[OBJ_CREATED_MAP] = { decode_obj_created_map },
	[OBJ_CREATED_PIPE] = { decode_obj_created_pipe },
	[OBJ_CREATED_PERF] = { decode_obj_created_perf },
	[OBJ_CREATED_EPOLL] = { decode_obj_created_epoll },
	[OBJ_CREATED_EVENTFD] = { decode_obj_created_eventfd },
	[OBJ_CREATED_TIMERFD] = { decode_obj_created_timerfd },
	[OBJ_CREATED_TESTFILE] = { decode_obj_created_testfile },
	[OBJ_CREATED_MEMFD] = { decode_obj_created_memfd },
	[OBJ_CREATED_DRM] = { decode_obj_created_drm },
	[OBJ_CREATED_INOTIFY] = { decode_obj_created_inotify },
	[OBJ_CREATED_USERFAULT] = { decode_obj_created_userfault },
	[OBJ_CREATED_FANOTIFY] = { decode_obj_created_fanotify },
	[OBJ_CREATED_BPFMAP] = { decode_obj_created_bpfmap },
	[OBJ_CREATED_SOCKET] = { decode_obj_created_socket },
	[OBJ_CREATED_FUTEX] = { decode_obj_created_futex },
	[OBJ_CREATED_SHM] = { decode_obj_created_shm },
	[OBJ_DESTROYED] = { decode_obj_destroyed },
	[SYSCALLS_ENABLED] = { decode_syscalls_enabled },
};
