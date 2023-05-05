#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <string.h>

#define bpf_target_x86
#define bpf_target_defined

#define FMODE_READ	0x1
#define O_DIRECT	00040000
#define TPM_ALG_SHA256	0x000B
#define TPM_ALG_SHA1	0x0004 //to do
#define MAP_ANONYMOUS	0x20	
char _license[] SEC("license") = "GPL";

struct ima_hash {
            struct ima_digest_data hdr;
            char digest[2048];
};

struct ima_data {
        unsigned int inum;
        struct ima_hash hash;
        struct file *file;
        struct inode *inode;
        char *f_buf;
        fmode_t f_mode;
        unsigned int f_flags;
        const unsigned char *f_name;
        int (*cra_init)(struct crypto_tfm *tfm);
        struct crypto_shash *shash;
        struct crypto_tfm base;
	struct shash_desc *desc;
	int size;
	unsigned int host;
	void *digest;
	struct tpm_digest *tpm_digest;
};

struct ima {
        char digest[2048];
        char *f_name;
        int size;
        int algo;
};
extern unsigned int bpfmeasurement(unsigned int inum) __ksym;
extern struct file *container_ima_retrieve_file(int fd) __ksym;
extern struct ima_hash ima_hash_setup(void) __ksym;
extern struct crypto_shash *ima_shash_init(void) __ksym;
extern void *ima_crypto(struct file *filp, struct crypto_tfm *base, int (*cra_init)(struct crypto_tfm *tfm)) __ksym;
extern int ima_pcr_extend(struct tpm_digest *digests_arg, int pcr) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, struct ima_data);
	__uint(max_entries, 256);
} map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, struct ima);
	__uint(max_entries, 256);
} ima SEC(".maps");

// int mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
SEC("kprobe/__x64_sys_mmap")
int BPF_KPROBE_SYSCALL(kprobe___sys_mmap, void *addr, unsigned long length, unsigned long prot, unsigned long flags, unsigned long fd) {

    int ret, len;
    u32 key;
    struct ima_data *data;
    struct task_struct *task;
    struct file *file;
    struct crypto_shash *shash;
    struct ima_file_buffer *f_buf;
    struct ima_data *ima_data;
    u32 map_key =1;
    
    if (prot & 0x04) {
    
        if (flags & MAP_ANONYMOUS)
		return 0;

        task = (void *) bpf_get_current_task();

        bpf_printk("Integrity measurement for fd %d\n", fd);

        key = 0; 
        data = (struct ima_data *) bpf_map_lookup_elem(&map, &key);
        if (!data) {
                bpf_printk("Map element lookup failed\n");
                return 0;
        }

        data->inum = BPF_CORE_READ(task, nsproxy, cgroup_ns, ns.inum);
        bpf_printk("BPF INUM %d\n", data->inum);
        data->host = bpfmeasurement(data->inum);

        bpf_printk("INUM comparision returns %d\n", data->host);
        if (data->host == data->inum) {

                bpf_printk("PRE RETRIEVE FILE\n");
                file = container_ima_retrieve_file(fd);
                bpf_printk("POST CHECK\n"); 	    
                if (file) {

                                bpf_printk("FILE RETRIEVED\n");
                                data->inode = BPF_CORE_READ(file, f_inode);
                                data->f_name = BPF_CORE_READ(file, f_path.dentry, d_name.name);
                                ima_data->f_name = data->f_name;

                                bpf_map_update_elem(&ima, &map_key, &ima_data);
                                bpf_printk("FILE INODE AND DENTRY NAME\n");
                                //data->hash = ima_hash_setup();

                                bpf_printk("HASH SET UP\n");
                                data->f_flags = BPF_CORE_READ(file, f_flags); 
                                if (data->f_flags & O_DIRECT) {
                                        return 0;
                                }
                                bpf_printk("FLAGS \n");
                                data->f_mode = BPF_CORE_READ(file, f_mode);
                                if (!(data->f_mode & FMODE_READ)) {
                                        return 0;
                                }
                                bpf_printk("MODE\n");
                                
                                shash = ima_shash_init();
                                bpf_printk("SHASH INIT\n");
                                
                                data->cra_init = BPF_CORE_READ(shash,base.__crt_alg, cra_init);
                                data->base = BPF_CORE_READ(shash, base);
                                data->digest = ima_crypto(file, &data->base, data->cra_init);

                                strncpy(&data->tpm_digest->digest[0], &data->hash.hdr.digest[0], sizeof(data->hash.hdr.digest));
                                
                                data->tpm_digest->alg_id = TPM_ALG_SHA256;
                                
                                ret = ima_pcr_extend(data->tpm_digest, 10);
                                
                                return 0;

                        }

                 }

        }
    return 0;

}
