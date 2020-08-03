/*
 * Nullfs.
 *
 * Copyright 2018 Michael Ablassmeier <abi@grinser.de>
 * 
 * This file may be redistributed under the terms of the GNU GPL.
 *
 * Create a file system that stores its structure in memory but
 * written data is sent to a blackhole. May be used for performance
 * testing etc..
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/module.h>
#include <linux/pagemap.h> 
#include <linux/fs.h> 
#include <linux/slab.h>
#include <linux/statfs.h>

#include <linux/kobject.h>
#include <linux/sysfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Ablassmeier");

#define NULLFS_MAGIC 0x19980123
#define NULLFS_DEFAULT_MODE  0755
#define NULLFS_SYSFS_MODE  0644

/*
 * sysfs handlers
 */
static char exclude[100] = "\0";
static ssize_t exclude_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s", exclude);
}

static ssize_t exclude_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    char *p;
    p = strchr(buf,'\n');
    if (p)
        *p = '\0';
    strncpy(exclude, buf, sizeof(exclude));
	printk(KERN_INFO "nullfs: will keep data for files matching: [%s]",
            exclude);
	return count;
}

static struct kobj_attribute exclude_attribute =
	__ATTR(exclude, NULLFS_SYSFS_MODE, exclude_show, exclude_store);

static struct attribute *attrs[] = {
	&exclude_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *exclude_kobj;


/**
 * regular filesystem handlers, inode handling etc..
 **/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static int nullfs_getattr(const struct path *path, struct kstat *stat,
					 u32 request_mask, unsigned int flags)
{
		struct inode *inode = path->dentry->d_inode;
#else
		static int nullfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
		{
				struct inode *inode = dentry->d_inode;
#endif

	unsigned long npages;
	generic_fillattr(inode, stat);
	npages = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	stat->blocks = npages << (PAGE_SHIFT - 9);
	return 0;
}

static ssize_t write_null(struct file *filp, const char *buf,
                size_t count, loff_t *offset) {
    /**
     * keep track of size
     **/
    struct inode *inode = file_inode(filp);
    i_size_write(inode, (inode->i_size + count));
    return count;
}

static ssize_t read_null(struct file *filp, char *buf,
                size_t count, loff_t *offset) {
    return 0;
}

const struct file_operations nullfs_file_operations = {
    .write  = write_null,
    .read   = read_null,
    .llseek = noop_llseek,
    .fsync  = noop_fsync
};

const struct file_operations nullfs_real_file_operations = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
    .read_iter  = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
#else
    .aio_read   = generic_file_aio_read,
    .aio_write  = generic_file_aio_write,
#endif
    .mmap       = generic_file_mmap,
    .fsync      = noop_fsync,
    .llseek     = generic_file_llseek,
};

const struct inode_operations nullfs_file_inode_operations = {
    .setattr    = simple_setattr,
    .getattr    = nullfs_getattr,
};

static const struct address_space_operations nullfs_aops = {
    .readpage    = simple_readpage,
    .write_begin = simple_write_begin,
    .write_end   = simple_write_end
};

static const struct inode_operations nullfs_dir_inode_operations;
static const struct super_operations nullfs_ops;

struct nullfs_mount_opts {
    char *write;
    umode_t mode;
};

struct nullfs_fs_info {
    struct nullfs_mount_opts mount_opts;
};

enum {
    Opt_write,
    Opt_mode,
    Opt_err
};

static const match_table_t tokens = {
    {Opt_write, "write=%s"},
    {Opt_err, NULL}
};

static int nullfs_parse_options(char *data, struct nullfs_mount_opts *opts)
{
    substring_t args[MAX_OPT_ARGS];
    char *option;
    int token;
    char *p;
    opts->write = NULL;
    opts->mode = NULLFS_DEFAULT_MODE;
    while ((p = strsep(&data, ",")) != NULL) {
        if (!*p)
            continue;

        token = match_token(p, tokens, args);
        switch (token) {
        case Opt_write:
	    option = match_strdup(&args[0]);
	    opts->write = option;
            strncpy(exclude, option, sizeof(exclude));
            break;
        }
    }
    if(opts->write != NULL)
	    printk(KERN_INFO "nullfs: will keep data for files matching: [%s]",
		    opts->write);
    return 0;
}

static int nullfs_show_options(struct seq_file *m, struct dentry *root)
{
    struct nullfs_fs_info *fsi = root->d_sb->s_fs_info;
    if(fsi->mount_opts.write != NULL)
	seq_printf(m, ",write=%s", fsi->mount_opts.write);
    return 0;
}

struct inode *nullfs_get_inode(struct super_block *sb,
                const struct inode *dir, umode_t mode, dev_t dev, struct dentry *dentry)
{
    struct inode * inode = new_inode(sb);
    struct nullfs_fs_info *fsi = sb->s_fs_info;

    if (inode) {
        inode->i_ino = get_next_ino();
        inode_init_owner(inode, dir, mode);
        inode->i_mapping->a_ops = &nullfs_aops; 
        mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
        mapping_set_unevictable(inode->i_mapping);
#ifndef CURRENT_TIME
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
#else
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
#endif
        switch (mode & S_IFMT) {
        default:
            init_special_inode(inode, mode, dev);
            break;
        case S_IFREG:
            inode->i_op = &nullfs_file_inode_operations;
	        if(fsi->mount_opts.write != NULL) {
		        if(strstr(dentry->d_iname, fsi->mount_opts.write) ||
                        strstr(dentry->d_iname, exclude)) {
			        inode->i_fop = &nullfs_real_file_operations;
			        break;
		        }
	        }
            inode->i_fop = &nullfs_file_operations;
            break;
        case S_IFDIR:
            inode->i_op = &nullfs_dir_inode_operations;
            inode->i_fop = &simple_dir_operations;
            inc_nlink(inode);
            break;
        case S_IFLNK:
            inode->i_op = &page_symlink_inode_operations;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
            inode_nohighmem(inode);
#endif
            break;
        }
    }
    return inode;
}

static int
nullfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct inode * inode;
    int error = -ENOSPC;

    inode = nullfs_get_inode(dir->i_sb, dir, mode, dev, dentry);

    if (inode) {
        /**
         * pretend created directories some size
         **/
        if(mode & S_IFDIR) {
            inode->i_size = PAGE_SIZE;
        }
        d_instantiate(dentry, inode);
        dget(dentry);   /* Extra count - pin the dentry in core */
        error = 0;
#ifndef CURRENT_TIME
        dir->i_mtime = dir->i_ctime = current_time(dir);
#else
        dir->i_mtime = dir->i_ctime = CURRENT_TIME;
#endif
    }
    return error;
}

static int 
nullfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
    int retval = nullfs_mknod(dir, dentry, mode | S_IFDIR, 0);
    if (!retval)
        inc_nlink(dir);
    return retval;
}

static int
nullfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
    struct inode *inode;
    int error = -ENOSPC;

    inode = nullfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0, dentry);
    if (inode) {
        int l = strlen(symname)+1;
        error = page_symlink(inode, symname, l);
        if (!error) {
            d_instantiate(dentry, inode);
            dget(dentry);
#ifndef CURRENT_TIME
            dir->i_mtime = dir->i_ctime = current_time(dir);
#else
            dir->i_mtime = dir->i_ctime = CURRENT_TIME;
#endif
        } else
            iput(inode);
    }
    return error;
}

static int nullfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    return nullfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static const struct inode_operations nullfs_dir_inode_operations = {
    .create     = nullfs_create,
    .lookup     = simple_lookup,
    .link       = simple_link,
    .unlink     = simple_unlink,
    .symlink    = nullfs_symlink,
    .mkdir      = nullfs_mkdir,
    .rmdir      = simple_rmdir,
    .mknod      = nullfs_mknod,
    .rename     = simple_rename,
    .getattr    = nullfs_getattr,
};

int nullfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    /**
     * Software this is used with checks for free space
     * constantly, so we need to tell there is allways free 
     * space
     *
     * Filesystem      Size  Used Avail Use% Mounted on
     * none            382G   39G  344G  10% /my
     **/
    buf->f_type  = dentry->d_sb->s_magic;
    buf->f_bsize = dentry->d_sb->s_blocksize;
    buf->f_blocks = 100000000;
    buf->f_bfree =  90000000;
    buf->f_bavail = 90000000;
    buf->f_namelen = NAME_MAX;
    return 0;
}

static const struct super_operations nullfs_ops = {
    .statfs       = nullfs_statfs,
    .drop_inode   = generic_delete_inode,
    .show_options = nullfs_show_options
};

int nullfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct nullfs_fs_info *fsi;
    struct inode *inode;
    int err;

    fsi = kzalloc(sizeof(struct nullfs_fs_info), GFP_KERNEL);
    sb->s_fs_info = fsi;
    if (!fsi)
        return -ENOMEM;

    err = nullfs_parse_options(data, &fsi->mount_opts);
    if(err)
	return err;

    sb->s_maxbytes       = MAX_LFS_FILESIZE;
    sb->s_blocksize      = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_magic          = NULLFS_MAGIC;
    sb->s_op             = &nullfs_ops;
    sb->s_time_gran      = 1;

    inode = nullfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0, sb->s_root);
    sb->s_root = d_make_root(inode);
    if (!sb->s_root)
        return -ENOMEM;

    return 0;
}

/**
 * setup / register and destroy filesystem
 **/
static void nullfs_kill_sb(struct super_block *sb)
{
        kfree(sb->s_fs_info);
        kill_litter_super(sb);
}

static struct dentry * nullfs_get_super(struct file_system_type *fst,
        int flags, const char *devname, void *data)
{
    return mount_nodev(fst, flags, data, nullfs_fill_super);
}

static struct file_system_type nullfs_type = {
    .name       = "nullfs",
    .mount      = nullfs_get_super,
    .kill_sb    = nullfs_kill_sb,
    .owner      = THIS_MODULE
};

static int __init nullfs_init(void)
{
	int retval;
	exclude_kobj = kobject_create_and_add("nullfs", fs_kobj);
	if (!exclude_kobj)
		return -ENOMEM;

	retval = sysfs_create_group(exclude_kobj, &attr_group);
	if (retval)
		kobject_put(exclude_kobj);

    register_filesystem(&nullfs_type);
    return 0;
}

static void __exit nullfs_exit(void)
{
    kobject_put(exclude_kobj);
    unregister_filesystem(&nullfs_type);
}

module_init(nullfs_init);
module_exit(nullfs_exit);

