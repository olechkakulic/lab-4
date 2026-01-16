#include <linux/init.h>
#include <linux/module.h>
#include <linux/mnt_idmapping.h>
#include <linux/mm.h>

#include "vtfs.h"

static void vtfs_free_node_recursive(struct vtfs_node *node);

struct inode *vtfs_get_inode(struct super_block *sb, struct vtfs_node *node)
{
  struct inode *inode = new_inode(sb);
  if (!inode)
    return NULL;

  inode->i_ino = node->ino;
  inode_init_owner(&nop_mnt_idmap, inode, NULL, node->mode);
  inode->i_op = &vtfs_inode_ops;
  inode->i_private = node;

  if (node->is_dir)
  {
    inode->i_op = &vtfs_inode_ops;
    inode->i_fop = &vtfs_dir_ops;
    set_nlink(inode, 2);
  }
  else
  {
    inode->i_fop = &vtfs_file_ops;
    set_nlink(inode, 1);
  }

  return inode;
}

static int vtfs_fill_super(struct super_block *sb, void *data, int silent)
{
  struct vtfs_fs *fs;
  struct vtfs_node *root_node = NULL;
  struct inode *root_inode = NULL;

  fs = kzalloc(sizeof(*fs), GFP_KERNEL);
  if (!fs)
    return -ENOMEM;

  sb->s_fs_info = fs;
  fs->sb = sb;
  fs->next_ino = 1;

  sb->s_magic = VTFS_MAGIC;
  sb->s_op = &vtfs_super_ops;
  sb->s_maxbytes = MAX_LFS_FILESIZE;
  sb->s_blocksize = PAGE_SIZE;
  sb->s_blocksize_bits = PAGE_SHIFT;
  sb->s_time_gran = 1;

  root_node = vtfs_alloc_node("/", true, S_IFDIR | 0777);
  if (!root_node)
  {
    LOG_ERR("Can't create root node");
    goto out_fs;
  }

  root_node->ino = fs->next_ino++;
  fs->root = root_node;

  root_inode = vtfs_get_inode(sb, root_node);
  if (!root_inode)
  {
    LOG_ERR("Can't link inode with the node");
    goto out_root_node;
  }

  sb->s_root = d_make_root(root_inode);
  if (!sb->s_root)
  {
    LOG_ERR("Error during d_make_root");
    goto out_inode;
  }

  LOG("Root node is created");
  return 0;

out_inode:
  iput(root_inode);
  root_inode = NULL;
  fs->root = NULL;
  goto out_fs;

out_root_node:
  kfree(root_node);
  root_node = NULL;
  fs->root = NULL;

out_fs:
  kfree(fs);
  sb->s_fs_info = NULL;
  return -ENOMEM;
}

struct dentry *vtfs_mount(
    struct file_system_type *fs_type,
    int flags,
    const char *token,
    void *data)
{
  struct dentry *ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (IS_ERR(ret))
  {
    LOG_ERR("Can't mount file system, err=%ld", PTR_ERR(ret));
  }
  else
  {
    LOG("Mounted successfully");
  }
  return ret;
}

static void vtfs_kill_sb(struct super_block *sb)
{
  LOG("Killing super block...");
  kill_block_super(sb);
  LOG("Super block is destroyed. Unmount successfully.");
}

struct file_system_type vtfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb};

static int __init vtfs_init(void)
{
  int ret;
  ret = register_filesystem(&vtfs_fs_type);
  if (ret != 0)
  {
    LOG_ERR("Cannot register the filesystem, err=%d", ret);
    return ret;
  }
  LOG("Joined the kernel");
  return 0;
}

static void __exit vtfs_exit(void)
{
  int ret;
  ret = unregister_filesystem(&vtfs_fs_type);
  if (ret != 0)
  {
    LOG_ERR("Cannot unregster the filesystem, err=%d", ret);
  }
  LOG("Left the kernel\n");
}

static void vtfs_free_node_recursive(struct vtfs_node *node)
{
  if (!node)
    return;

  struct vtfs_node *child = node->first_child;
  while (child)
  {
    struct vtfs_node *next = child->next_sibling;
    vtfs_free_node_recursive(child);
    child = next;
  }
  if (!node->link_target)
    kfree(node->data);
  kfree(node);
}

static void vtfs_put_super(struct super_block *sb)
{
  struct vtfs_fs *fs = VTFS_SB(sb);
  LOG("vtfs_put_super() called");
  if (!fs)
    return;

  if (fs->root)
  {
    LOG("Recursively freeing nodes...");
    vtfs_free_node_recursive(fs->root);
  }

  kfree(fs);
  sb->s_fs_info = NULL;
}

static int vtfs_statfs(struct dentry *dentry, struct kstatfs *stat)
{
  stat->f_type = VTFS_MAGIC;
  stat->f_namelen = VTFS_FILE_NAME_LEN;
  return 0;
}

void vtfs_evict_inode(struct inode *inode)
{
  struct vtfs_node *node = inode->i_private;

  if (!node)
    return;

  LOG("Evicting inode %s (ino=%lu)", node->name, node->ino);

  truncate_inode_pages_final(&inode->i_data);
  clear_inode(inode);

  if (inode->i_nlink)
    return;

  if (!node->link_target)
    kfree(node->data);

  kfree(node);
  inode->i_private = NULL;
}

const struct super_operations vtfs_super_ops = {
    .statfs = vtfs_statfs,
    .put_super = vtfs_put_super,
    .evict_inode = vtfs_evict_inode,
};

module_init(vtfs_init);
module_exit(vtfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Theodor");
MODULE_DESCRIPTION("A simple file system kernel module");
