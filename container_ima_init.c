/*
 * File: container_ima_init.c
 *      Functions for initialization and cleanup
 */
#include <linux/ima.h>

#include "container_ima.h"

struct tpm_chip *ima_tpm_chip;

/*
 * container_ima_vtpm_setup 
 *      Set up per container vTPM, PCR 10 for IMA
 * 
 * https://elixir.bootlin.com/linux/latest/source/drivers/char/tpm/tpm_vtpm_proxy.c 
 * https://www.kernel.org/doc/html/v4.13/security/tpm/tpm_vtpm_proxy.html
 * https://elixir.bootlin.com/linux/v6.0.5/source/drivers/char/tpm/tpm_vtpm_proxy.c#L624 
 */
long container_ima_vtpm_setup(int container_id, struct tpm_chip *ima_tpm_chip, struct container_data *data) 
{
	struct vtpm_proxy_new_dev *new_vtpm;
	long ret;
	int ioctl; 
	struct file *vtpm_file;
	const char *vtpm_fd_name;
	char id[10];
	int check;
	
	new_vtpm = kmalloc(sizeof(struct vtpm_proxy_new_dev), GFP_KERNEL);
	if (!new_vtpm) {
		pr_err("kmalloc failed\n");
	}

	check = sprintf(id, "%d", container_id);
	if (check < 0)
		pr_err("sprintf fails in vtpm setup \n");
	
	check = strcat_s("/dev/vtpm", id);
	if (check == -1)
		pr_err("strcat_s fails in vtpm setup\n");

	new_vtpm.flags = VTPM_PROXY_FLAG_TPM2;
	new_vtpm.tpm_num = container_id;
	new_vtpm.fd = "/dev/vtpm";
	new_vtpm.major = MAJOR(ima_tpm_chip->device->devt); // MAJOR(dev_t dev); major number of the TPM device
	new_vtp.minor = MINOR(ima_tpm_chip->device->devt); // MINOR(dev_t dev); minor number of the TPM device


	ret = vtpmx_ioc_new_dev(vtpm_file, ioctl, (unsigned long)&new_vtpm);
	
	if (ret != 0) {
		pr_err("Failed to create a new vTPM device\n");
	}

	data->vtpm = new_vtpm;
	return ret;
	
}
/*
 * init_container_ima_data
 * 		Note: figure out how to check if it exists already
 */
struct container_ima_data *init_container_ima_data(int container_id) 
{
	struct container_ima_data *data;
	/* check if container data exists, return that then */

	/* init policy lists */
	INIT_LIST_HEAD(&data->c_ima_default_rules);
	INIT_LIST_HEAD(&data->c_ima_policy_rules);

	data->c_ima_rules = (struct list_head __rcu *)(&data->c_ima_rules);
	
	/* init hash table */
	atomic_long_set(&data->hashc_ima_write_mutex_tbl.len, 0);
	atomic_long_set(&data->hash_tbl.violations, 0);
	memset(&data->hash_tbl.queue, 0, sizeof(data->hash_tbl));

	/* init ML */
	INIT_LIST_HEAD(&data->c_ima_measurements);
	mutex_init(&data->c_ima_write_mutex);
	
	data->valid_policy = 1;
	data->c_ima_fs_flags = 0;

	data->container_integrity_iint_tree = RB_ROOT;
	DEFINE_RWLOCK(data->container_integrity_iint_lock);

	return data;
}
/*
 * container_ima_init
 * 		Initalize container IMA
 * 		Create vTPM proxy using container_id as its number
 *		Create measurment log 
 * 		Default policy
 */
struct container_data *init_container_ima(int container_id, static struct dentry c_ima_dir, static struct dentry c_ima_symlink) 
{
	int ret;
	struct container_ima_data *data;
	/* check if container exist, then return container_data here */
	data = kmalloc(size_of(struct container_data), GFP_KERNEL);
	if (!data) {
		pr_error("kmalloc failed\n");
		return -1;
	}
	ima_tpm_chip = tpm_default_chip();
	if (!ima_tpm_chip)
		pr_info("No TPM chip found, activating TPM-bypass!\n");


	container_ima_vtpm = container_ima_vtpm_setup(container_id, ima_tpm_chip, data); // per container vTPM
 
	data = init_container_ima_data(container_id);

	ret = container_ima_fs_init(container_id, c_ima_dir, c_ima_symlink);
	//ret = integrity_init_keyring(INTEGRITY_KEYRING_IMA); // per container key ring

	//data->keyring = INTEGRITY_KEYRING_IMA;

	if (ret)
		return ret;
	ret = container_ima_crypto_init(data); // iterate over PCR banks and init the algorithms per bank  

	if (ret)
		return ret;

	ret = container_ima_ml_init(data); // set up directory for per container Measurment Log

	if (ret) 
		return ret;

	container_ima_policy_init(data); // start with default policy for all containers

	return data;
}

/*
 * container_ima_fs_init
 * 		Securityfs
 *      Create a secure place to store per container measurement logs
 * 		Idea: under /integrity/ima/containers/ have a directory per container named with container id
 * 			
 */
int container_ima_fs_init(struct container_ima_data *data, static struct dentry c_ima_dir, static struct dentry c_ima_symlink) 
{
	int res;
	char *dir_name = "integrity/ima/container/";
	char *id;

	sprintf(id, "%s", container_id);
	strcat(dir_name, id);

	data->container_dir = securityfs_create_dir("container_ima", dir_name);
	if (IS_ERR(data->container_dir))
		return -1;

	data->binary_runtime_measurements =
	securityfs_create_file("binary_runtime_measurements",
				   S_IRUSR | S_IRGRP, data->container_dir, NULL,
				   &ima_measurements_ops);

	if (IS_ERR(data->binary_runtime_measurements)) {
		ret = PTR_ERR(data->binary_runtime_measurements);
		goto out;
	}

	data->ascii_runtime_measurements =
	    securityfs_create_file("ascii_runtime_measurements",
				   S_IRUSR | S_IRGRP, data->container_dir, NULL,
				   &ima_ascii_measurements_ops);
	if (IS_ERR(data->ascii_runtime_measurements)) {
		ret = PTR_ERR(data->ascii_runtime_measurements);
		goto out;
	}

	data->runtime_measurements_count =
	    securityfs_create_file("runtime_measurements_count",
				   S_IRUSR | S_IRGRP,data->container_dir, NULL,
				   &ima_measurements_count_ops);
	if (IS_ERR(data->runtime_measurements_count)) {
		ret = PTR_ERR(data->runtime_measurements_count);
		goto out;
	}

	data->violations =
	    securityfs_create_file("violations", S_IRUSR | S_IRGRP,
				   data->container_dir, NULL, &ima_htable_violations_ops);
	if (IS_ERR(data->violations)) {
		ret = PTR_ERR(data->violations);
		goto out;
	}

	data->c_ima_policy = securityfs_create_file("policy", POLICY_FILE_FLAGS,
					    data->container_dir, NULL,
					    &ima_measure_policy_ops);
	if (IS_ERR(data->c_ima_policy)) {
		ret = PTR_ERR(data->c_ima_policy);
		goto out;
	}

	return 0;
out:
	securityfs_remove(data->c_ima_policy);
	securityfs_remove(data->violations);
	securityfs_remove(data->runtime_measurements_count);
	securityfs_remove(data->ascii_runtime_measurements);
	securityfs_remove(data->binary_runtime_measurements);
	securityfs_remove(data->container_dir);

	return res;
}

/*
 * container_keyring_init 
 * 
 * https://man7.org/linux/man-pages/man7/keyrings.7.html
 * https://man7.org/linux/man-pages/man2/add_key.2.html 
 */
int container_keyring_init()
{
	return 0;
}
/*
 * TODO
 */
int container_ima_cleanup() {
	
	return 0;
}

/*
 * container_ima_setup
 *
 * Set up environment to initalize container IMA
 * Malloc structure to hold container ids and other data to preserve state
 */
void container_ima_setup()
{
	ima_hash_setup();

}
/*
 * container_ima_crypto_init
 * 
 * Iterate over PCRs, check algorithm for PCR10 and record
 */
int container_ima_crypto_init(struct container_data *data)
{
	int ret;
	int i;


	return 0;

}