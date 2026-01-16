#include "vtfs.h"

int vtfs_iterate(struct file *dir, struct dir_context *ctx)
{
  struct dentry *dentry = dir->f_path.dentry;
  struct inode *inode = dentry->d_inode;
  struct vtfs_node *dir_node;
  struct vtfs_node *child;

  if (!S_ISDIR(inode->i_mode))
    return -ENOTDIR;

  dir_node = inode->i_private;
  if (!dir_node)
    return -EINVAL;

  if (!dir_emit_dots(dir, ctx))
    return 0;

  loff_t pos = ctx->pos;

  uint idx = pos - 2;

  child = dir_node->first_child;
  while (child && idx > 0)
  {
    child = child->next_sibling;
    idx--;
  }

  while (child)
  {
    uint dtype = child->is_dir ? DT_DIR : DT_REG;
    size_t namelen = strnlen(child->name, VTFS_FILE_NAME_LEN);

    if (!dir_emit(ctx, child->name, namelen, child->ino, dtype))
      break;
    ctx->pos++;
    child = child->next_sibling;
  }

  return 0;
}

const struct file_operations vtfs_dir_ops = {
    .iterate_shared = vtfs_iterate,
};
