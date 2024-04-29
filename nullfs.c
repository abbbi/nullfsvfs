/*
 * Nullfs.
 *
 *
 *   Copyright (C) 2018  Michael Ablassmeier <abi@grinser.de>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
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
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#define NULLFS_MAGIC 0x19980123
#define NULLFS_DEFAULT_MODE  0755
#define NULLFS_SYSFS_MODE  0644
#define NULLFS_VERSION "0.17"

MODULE_AUTHOR("Michael Ablassmeier");
MODULE_LICENSE("GPL");
MODULE_VERSION(NULLFS_VERSION);

/*
 * POSIX ACL
 * setfacl is possible, but acls are not stored, of course
 *
 * For older kernel versions (3.x, used on rhel7/centos7 its required to
 * redefine some non-public functions to make it "work", so we skip..
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
static const struct xattr_handler *nullfs_xattr_handlers[] = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 2)
    &nop_posix_acl_access,
    &nop_posix_acl_default,
#else
    &posix_acl_access_xattr_handler,
    &posix_acl_default_xattr_handler,
#endif
    NULL
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_set_acl(struct mnt_idmap *idmap,
        struct dentry *dentry, struct posix_acl *acl, int type)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static int nullfs_set_acl(struct user_namespace *mnt_userns,
        struct dentry *dentry, struct posix_acl *acl, int type)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_set_acl(struct user_namespace *mnt_userns,
        struct inode *inode, struct posix_acl *acl, int type)
#else
static int nullfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
#endif
{
    return 0;
}
#endif

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_getattr(struct mnt_idmap *idmap,
        const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags)
{
		struct inode *inode = path->dentry->d_inode;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_getattr(struct user_namespace *mnt_userns,
        const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags)
{
		struct inode *inode = path->dentry->d_inode;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static int nullfs_getattr(const struct path *path, struct kstat *stat,
					 u32 request_mask, unsigned int flags)
{
		struct inode *inode = path->dentry->d_inode;
#else
		static int nullfs_getattr(struct vfsmount *mnt,
                struct dentry *dentry, struct kstat *stat)
		{
				struct inode *inode = dentry->d_inode;
#endif

	unsigned long npages;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	generic_fillattr(&nop_mnt_idmap, inode, stat);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	generic_fillattr(&init_user_ns, inode, stat);
#else
	generic_fillattr(inode, stat);
#endif
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

    /**
     * Pretend we have returned some data
     * during file read
     **/
    int nbytes;
    struct inode * inode = filp->f_inode;

    if (*offset >= inode->i_size) {
        return 0;
    }

    nbytes = min((size_t) inode->i_size, count);
    *offset += nbytes;

    return nbytes;
}

const struct file_operations nullfs_file_operations = {
    .write  = write_null,
    .read   = read_null,
    .llseek = noop_llseek,
    .fsync  = noop_fsync,
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    .set_acl    = nullfs_set_acl,
#endif
};
const struct inode_operations nullfs_special_inode_operations = {
    .setattr    = simple_setattr,
    .getattr    = nullfs_getattr,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    .set_acl    = nullfs_set_acl,
#endif
};
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0)
static const struct address_space_operations nullfs_aops = {
    .readpage    = simple_readpage,
    .write_begin = simple_write_begin,
    .write_end   = simple_write_end,
/**
 * RHEL kernel exports noop_direct_IO, SLES15 does not
 **/
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 0, 0)
#ifdef RHEL_MAJOR
    .direct_IO   = noop_direct_IO
#endif
#else
    .direct_IO   = noop_direct_IO
#endif
};
#endif

static const struct inode_operations nullfs_dir_inode_operations;
static const struct super_operations nullfs_ops;

struct nullfs_mount_opts {
    char *write;
    umode_t mode;
    kuid_t uid;
    kgid_t gid;
};

struct nullfs_fs_info {
    struct nullfs_mount_opts mount_opts;
};

enum {
    Opt_write,
    Opt_mode,
    Opt_uid,
    Opt_gid,
    Opt_err
};

static const match_table_t tokens = {
    {Opt_write, "write=%s"},
    {Opt_mode, "mode=%s"},
    {Opt_uid, "uid=%s"},
    {Opt_gid, "gid=%s"},
    {Opt_err, NULL}
};

static int nullfs_parse_options(char *data, struct nullfs_mount_opts *opts)
{
    substring_t args[MAX_OPT_ARGS];
    char *option;
    int token;
    int opt;
    char *p;
    kuid_t uid;
    kgid_t gid;
    opts->write = NULL;
    opts->mode = NULLFS_DEFAULT_MODE;
    opts->uid = GLOBAL_ROOT_UID;
    opts->gid = GLOBAL_ROOT_GID;
    // maybe use fs_parse here? Not sure which kernel versions
    // support it
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
            case Opt_uid:
                if (match_int(&args[0], &opt))
                    return -EINVAL;
                uid = make_kuid(current_user_ns(), opt);
                if (!uid_valid(uid))
                    return -EINVAL;
                opts->uid = uid;
            break;
            case Opt_gid:
                if (match_int(&args[0], &opt))
                    return -EINVAL;
                gid = make_kgid(current_user_ns(), opt);
                if (!gid_valid(gid))
                    return -EINVAL;
                opts->gid = gid;
            break;
            case Opt_mode:
                if (match_octal(&args[0], &opt))
                    return -EINVAL;
                opts->mode = opt & S_IALLUGO;
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
    if (!uid_eq(fsi->mount_opts.uid, GLOBAL_ROOT_UID))
        seq_printf(m, ",uid=%u",
               from_kuid_munged(&init_user_ns, fsi->mount_opts.uid));
    if (!gid_eq(fsi->mount_opts.gid, GLOBAL_ROOT_GID))
        seq_printf(m, ",gid=%u",
               from_kgid_munged(&init_user_ns, fsi->mount_opts.gid));
    if (fsi->mount_opts.mode != NULLFS_DEFAULT_MODE)
        seq_printf(m, ",mode=%o", fsi->mount_opts.mode);

    return 0;
}

struct inode *nullfs_get_inode(struct super_block *sb,
        const struct inode *dir, umode_t mode, dev_t dev, struct dentry *dentry)
{
    struct inode * inode = new_inode(sb);
    struct nullfs_fs_info *fsi = sb->s_fs_info;

    if (inode) {
        inode->i_ino = get_next_ino();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
        inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        inode_init_owner(&init_user_ns, inode, dir, mode);
#else
        inode_init_owner(inode, dir, mode);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
        inode->i_mapping->a_ops = &ram_aops;
#else
        inode->i_mapping->a_ops = &nullfs_aops;
#endif
        if (!uid_eq(fsi->mount_opts.uid, GLOBAL_ROOT_UID))
            inode->i_uid = fsi->mount_opts.uid;
        if (!gid_eq(fsi->mount_opts.gid, GLOBAL_ROOT_GID))
            inode->i_gid = fsi->mount_opts.gid;
        mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
        mapping_set_unevictable(inode->i_mapping);
#ifndef CURRENT_TIME
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
        simple_inode_init_ts(inode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        inode->i_atime = inode->i_mtime = inode_set_ctime_current(inode);
#else
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
#endif
#else
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
#endif
        switch (mode & S_IFMT) {
        default:
            init_special_inode(inode, mode, dev);
            inode->i_op = &nullfs_special_inode_operations;
            break;
        case S_IFREG:
            inode->i_op = &nullfs_file_inode_operations;
            if(fsi->mount_opts.write != NULL && dentry != NULL) {
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_mknod(struct mnt_idmap *idmap,
        struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_mknod(struct user_namespace *mnt_userns,
        struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
# else
static int nullfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
#endif
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
        inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        dir->i_mtime = inode_set_ctime_current(dir);
#else
        dir->i_mtime = dir->i_ctime = current_time(dir);
#endif
#else
        dir->i_mtime = dir->i_ctime = CURRENT_TIME;
#endif
    }
    return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_mkdir(struct mnt_idmap *idmap,
        struct inode * dir, struct dentry * dentry, umode_t mode)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_mkdir(struct user_namespace *mnt_userns,
        struct inode * dir, struct dentry * dentry, umode_t mode)
#else
static int nullfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
#endif
{

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    int retval = nullfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    int retval = nullfs_mknod(&init_user_ns, dir, dentry, mode | S_IFDIR, 0);
#else
    int retval = nullfs_mknod(dir, dentry, mode | S_IFDIR, 0);
#endif

    if (!retval)
        inc_nlink(dir);
    return retval;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_symlink(struct mnt_idmap *idmap,
        struct inode * dir, struct dentry *dentry, const char * symname)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_symlink(struct user_namespace *mnt_userns,
        struct inode * dir, struct dentry *dentry, const char * symname)
#else
static int nullfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
#endif
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
        inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
            dir->i_mtime = inode_set_ctime_current(dir);
#else
            dir->i_mtime = dir->i_ctime = current_time(dir);
#endif
#else
            dir->i_mtime = dir->i_ctime = CURRENT_TIME;
#endif
        } else
            iput(inode);
    }
    return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
#else
static int nullfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    return nullfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    return nullfs_mknod(&init_user_ns, dir, dentry, mode | S_IFREG, 0);
#else
    return nullfs_mknod(dir, dentry, mode | S_IFREG, 0);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int nullfs_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
				struct file *file, umode_t mode)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int nullfs_tmpfile(struct user_namespace *mnt_userns, struct inode *dir,
				struct file *file, umode_t mode)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int nullfs_tmpfile(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode)
#else
static int nullfs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
#endif
{
    struct inode *inode;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    inode = nullfs_get_inode(dir->i_sb, dir, mode, 0, file->f_path.dentry);
#else
    inode = nullfs_get_inode(dir->i_sb, dir, mode, 0, dentry);
#endif
    if (!inode)
        return -ENOSPC;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    d_tmpfile(file, inode);
#else
    d_tmpfile(dentry, inode);
#endif
    return 0;
}
#endif

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    .set_acl    = nullfs_set_acl,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
    .tmpfile	= nullfs_tmpfile,
#endif
};

int nullfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    /**
     * Software this is used with checks for free space
     * constantly, so we need to tell there is always free
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    sb->s_xattr          = nullfs_xattr_handlers;
    sb->s_flags         |= SB_POSIXACL;
#endif

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
    printk(KERN_INFO "nullfs: version [%s] initialized", NULLFS_VERSION);
    return 0;
}

static void __exit nullfs_exit(void)
{
    kobject_put(exclude_kobj);
    unregister_filesystem(&nullfs_type);
}

module_init(nullfs_init);
module_exit(nullfs_exit);
