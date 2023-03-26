#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

#define bpf_target_x86
#define bpf_target_defined

char _license[] SEC("license") = "GPL";

struct mmap_args_t {
	void *addr;
	size_t length;
	int prot;
	int flags;
	int fd;
	int offset;
};
extern int bpfmeasurement(size_t length, int fd, int flags) __ksym;

// int mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
SEC("kprobe/__x64_sys_mmap")
int kprobe__sys_mmap(struct pt_regs *ctx) {

    int ret;
    struct task_struct *task;
    unsigned int id;
    struct mmap_args_t  *value;
    struct file *file;
    struct mmap_args_t mmap;
   

    __builtin_memset(&mmap, 0, sizeof(mmap));


    mmap.length = (int)PT_REGS_PARM2(ctx);
    mmap.prot = (int) PT_REGS_PARM3(ctx);
    mmap.flags = (int) PT_REGS_PARM4(ctx);
    mmap.fd = (int) PT_REGS_PARM5(ctx);
    mmap.offset = 0;

    if (mmap.prot == 0x04)
          ret = bpfmeasurement(mmap.length, mmap.fd, mmap.flags);

    return 0;

}
