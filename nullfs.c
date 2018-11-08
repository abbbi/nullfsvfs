/*
 * Nullfs.
 *
 * Copyright 2018 Michael Ablassmeier <abi@grinser.de>
 * 
 * Originated and based on work by:
 *
 * Copyright 2002, 2003 Jonathan Corbet <corbet@lwn.net>
 * 
 * And ramfs from the linux kernel
 *
 * This file may be redistributed under the terms of the GNU GPL.
 *
 * Create a file system that stores its structure in memory but
 * written data is sent to a blackhole. May be used for performance
 * testing etc..
 */
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h> 
#include <linux/fs.h> 
#include <linux/slab.h>
#include "internal.h"
#include <linux/mm.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Ablassmeier");
#define RAMFS_MAGIC 0x19980123
#define RAMFS_DEFAULT_MODE  0755


static ssize_t write_null(struct file *filp, const char *buf,
                size_t count, loff_t *offset) {

    return count;
}
static ssize_t read_null(struct file *filp, char *buf,
                size_t count, loff_t *offset) {
    return 0;
}

const struct file_operations nullfs_file_operations = {
    .write  = write_null,
    .read   = read_null
};

const struct inode_operations nullfs_file_inode_operations = {
    .setattr    = simple_setattr,
    .getattr    = simple_getattr,
};

static const struct address_space_operations nullfs_aops = {
    .readpage   = simple_readpage,
    .write_begin    = simple_write_begin,
    .write_end  = simple_write_end
    //.set_page_dirty = __set_page_dirty_no_writeback,
};


static const struct inode_operations nullfs_dir_inode_operations;

struct nullfs_mount_opts {
    umode_t mode;
};

struct nullfs_fs_info {
    struct nullfs_mount_opts mount_opts;
};

struct inode *nullfs_get_inode(struct super_block *sb,
                const struct inode *dir, umode_t mode, dev_t dev)
{
    struct inode * inode = new_inode(sb);

    if (inode) {
        inode->i_ino = get_next_ino();
        inode_init_owner(inode, dir, mode);
        inode->i_mapping->a_ops = &nullfs_aops; 
        mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
        mapping_set_unevictable(inode->i_mapping);
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        switch (mode & S_IFMT) {
        default:
            init_special_inode(inode, mode, dev);
            break;
        case S_IFREG:
            inode->i_op = &nullfs_file_inode_operations;
            inode->i_fop = &nullfs_file_operations;
            break;
        case S_IFDIR:
            inode->i_op = &nullfs_dir_inode_operations;
            inode->i_fop = &simple_dir_operations;
            inc_nlink(inode);
            break;
		
        case S_IFLNK:
            inode->i_op = &page_symlink_inode_operations;
            inode_nohighmem(inode);
            break;
		
        }
    }
    return inode;
}


static int
nullfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct inode * inode = nullfs_get_inode(dir->i_sb, dir, mode, dev);
    int error = -ENOSPC;

    if (inode) {
        d_instantiate(dentry, inode);
        dget(dentry);   /* Extra count - pin the dentry in core */
        error = 0;
        dir->i_mtime = dir->i_ctime = current_time(dir);
    }
    return error;
}

static int nullfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
    int retval = nullfs_mknod(dir, dentry, mode | S_IFDIR, 0);
    if (!retval)
        inc_nlink(dir);
    return retval;
}

static int nullfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
    struct inode *inode;
    int error = -ENOSPC;

    inode = nullfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
    if (inode) {
        int l = strlen(symname)+1;
        error = page_symlink(inode, symname, l);
        if (!error) {
            d_instantiate(dentry, inode);
            dget(dentry);
            dir->i_mtime = dir->i_ctime = current_time(dir);
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
};

static const struct super_operations nullfs_ops = {
    .statfs     = simple_statfs
};

int nullfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct nullfs_fs_info *fsi;
    struct inode *inode;

    fsi = kzalloc(sizeof(struct nullfs_fs_info), GFP_KERNEL);
    sb->s_fs_info = fsi;
    if (!fsi)
        return -ENOMEM;

    sb->s_maxbytes      = MAX_LFS_FILESIZE;
    sb->s_blocksize     = PAGE_SIZE;
    sb->s_blocksize_bits    = PAGE_SHIFT;
    sb->s_magic     = RAMFS_MAGIC;
    sb->s_time_gran     = 1;

    inode = nullfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
    sb->s_root = d_make_root(inode);
    if (!sb->s_root)
        return -ENOMEM;

    return 0;
}


/*
 * Stuff to pass in when registering the filesystem.
 */
static struct dentry * lfs_get_super(struct file_system_type *fst,
        int flags, const char *devname, void *data)
{
    return mount_nodev(fst, flags, data, nullfs_fill_super);
}

static struct file_system_type lfs_type = {
//    .owner = THIS_MODULE,
    .name    = "nullfs",
    .mount  = lfs_get_super,
    .kill_sb    = kill_litter_super,
};

/*
 * Get things set up.
 */
static int __init lfs_init(void)
{
    return register_filesystem(&lfs_type);
}

static void __exit lfs_exit(void)
{
    unregister_filesystem(&lfs_type);
}

module_init(lfs_init);
module_exit(lfs_exit);

