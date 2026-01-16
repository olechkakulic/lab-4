#ifndef VTFS_H
#define VTFS_H

#include <linux/fs.h>
#include <linux/statfs.h>

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) pr_err("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)
#define MODULE_NAME "vtfs"

#define VTFS_MAGIC 0xDEADBABE

#define VTFS_FILE_NAME_LEN 255
#define VTFS_SB(sb) ((struct vtfs_fs *)(sb)->s_fs_info)

extern const struct file_operations vtfs_dir_ops;
extern const struct file_operations vtfs_file_ops;
extern const struct inode_operations vtfs_inode_ops;
extern const struct super_operations vtfs_super_ops;

struct vtfs_fs
{
    struct vtfs_node *root;
    u64 next_ino;
    struct super_block *sb;
};

struct vtfs_node
{
    char name[VTFS_FILE_NAME_LEN];
    ino_t ino;
    bool is_dir;
    umode_t mode;

    struct vtfs_node *parent;
    struct vtfs_node *first_child;
    struct vtfs_node *next_sibling;

    struct vtfs_node *link_target;

    char *data;
    size_t size;
    size_t capacity;
    struct mutex lock;
};

struct inode *vtfs_get_inode(struct super_block *sb, struct vtfs_node *node);
void vtfs_evict_inode(struct inode *inode);

struct vtfs_node *vtfs_alloc_node(const char *name, bool is_dir, umode_t mode);
int vtfs_unlink(struct inode *dir, struct dentry *dentry);
int vtfs_rmdir(struct inode *dir, struct dentry *dentry);

struct dentry *vtfs_mount(
    struct file_system_type *fs_type,
    int flags,
    const char *token,
    void *data);

struct dentry *vtfs_lookup(
    struct inode *parent_inode,
    struct dentry *child_dentry,
    unsigned int flag);

int vtfs_create(
    struct mnt_idmap *idmap,
    struct inode *parent_inode,
    struct dentry *child_dentry,
    umode_t mode,
    bool b);

int vtfs_iterate(struct file *dir, struct dir_context *ctx);

#endif
