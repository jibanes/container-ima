/*
 * container_ima_fs.c
 *      Security file system for container measurment lists       
 *
 */
#include <linux/fcntl.h>
#include <linux/kernel_read_file.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/parser.h>
#include <linux/vmalloc.h>
#include <linux/ima.h>

#include "container_ima.h"

static const struct file_operations ima_measurements_ops = {
	.open = ima_measurements_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

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