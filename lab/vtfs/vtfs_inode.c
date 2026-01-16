#include "vtfs.h"

struct vtfs_node *vtfs_alloc_node(const char *name, bool is_dir, umode_t mode)
{
  struct vtfs_node *node = kzalloc(sizeof(struct vtfs_node), GFP_KERNEL);
  umode_t inode_mode = mode;

  if (!node)
    return NULL;

  if (!(inode_mode & S_IFMT))
    inode_mode |= is_dir ? S_IFDIR : S_IFREG;

  strscpy(node->name, name, sizeof(node->name));

  node->is_dir = is_dir;
  node->mode = inode_mode;
  node->data = NULL;
  node->size = 0;
  node->capacity = 0;
  mutex_init(&node->lock);

  LOG("Allocated node, name=%s, mode=%hu", name, mode);
  return node;
}

struct dentry *vtfs_lookup(
    struct inode *parent_inode,
    struct dentry *child_dentry,
    unsigned int flag)
{
  struct vtfs_node *parent = parent_inode->i_private;
  struct vtfs_node *child;
  struct inode *inode = NULL;
  const char *name = child_dentry->d_name.name;

  if (!parent)
  {
    d_add(child_dentry, NULL);
    return NULL;
  }

  for (child = parent->first_child; child; child = child->next_sibling)
  {
    if (strcmp(child->name, name) == 0)
    {
      inode = vtfs_get_inode(parent_inode->i_sb, child);
      if (!inode)
        return ERR_PTR(-ENOMEM);
      break;
    }
  }

  d_add(child_dentry, inode);
  return NULL;
}

int vtfs_unlink(struct inode *dir, struct dentry *dentry)
{
  struct vtfs_node *parent = dir->i_private;
  struct inode *inode = d_inode(dentry);
  struct vtfs_node *node = inode ? inode->i_private : NULL;
  struct vtfs_node *cur, *prev = NULL;

  if (!parent || !node)
    return -EINVAL;

  if (node->is_dir)
    return -EISDIR;

  LOG("vtfs_unlink: name=%.*s, parent_ino=%lu, ino=%lu",
      dentry->d_name.len, dentry->d_name.name,
      (unsigned long)parent->ino,
      (unsigned long)inode->i_ino);

  cur = parent->first_child;
  while (cur)
  {
    if (cur == node)
    {
      if (prev)
        prev->next_sibling = cur->next_sibling;
      else
        parent->first_child = cur->next_sibling;

      node->parent = NULL;
      node->next_sibling = NULL;
      break;
    }
    prev = cur;
    cur = cur->next_sibling;
  }

  if (!cur)
    return -ENOENT;

  clear_nlink(inode);
  mark_inode_dirty(inode);
  mark_inode_dirty(dir);

  return 0;
}

int vtfs_create(
    struct mnt_idmap *idmap,
    struct inode *parent_inode,
    struct dentry *child_dentry,
    umode_t mode,
    bool excl)
{
  struct vtfs_node *parent_node = parent_inode->i_private;
  struct vtfs_node *node;
  struct inode *inode;
  struct vtfs_fs *fs = VTFS_SB(parent_inode->i_sb);

  const char *name = child_dentry->d_name.name;

  if (!parent_node || !fs)
    return -EINVAL;

  LOG("Create: name=%s, parent_ino=%lu", name, parent_inode->i_ino);

  node = vtfs_alloc_node(name, false, mode);
  if (!node)
    return -ENOMEM;

  node->ino = fs->next_ino++;
  node->parent = parent_node;

  node->next_sibling = parent_node->first_child;
  parent_node->first_child = node;

  inode = vtfs_get_inode(parent_inode->i_sb, node);
  if (!inode)
  {
    parent_node->first_child = node->next_sibling;
    kfree(node);
    return -ENOMEM;
  }

  d_instantiate(child_dentry, inode);
  mark_inode_dirty(parent_inode);

  LOG("Created file '%s' ino=%lu in dir ino=%lu",
      node->name, inode->i_ino, parent_inode->i_ino);

  return 0;
}

static int vtfs_mkdir(struct mnt_idmap *idmap,
                      struct inode *dir,
                      struct dentry *dentry,
                      umode_t mode)
{
  struct vtfs_node *parent_node;
  struct vtfs_node *node, *cur;
  struct inode *inode;
  struct vtfs_fs *fs;
  const char *name = dentry->d_name.name;

  parent_node = dir->i_private;
  fs = VTFS_SB(dir->i_sb);

  if (!parent_node || !fs)
    return -EINVAL;

  LOG("Mkdir: name=%s, parent_ino=%lu", name, dir->i_ino);

  for (cur = parent_node->first_child; cur; cur = cur->next_sibling)
  {
    if (strcmp(cur->name, name) == 0)
      return -EEXIST;
  }

  if (!(mode & S_IFMT))
    mode |= S_IFDIR;

  node = vtfs_alloc_node(name, true, mode);
  if (!node)
    return -ENOMEM;

  node->ino = fs->next_ino++;
  node->parent = parent_node;

  node->next_sibling = parent_node->first_child;
  parent_node->first_child = node;

  inode = vtfs_get_inode(dir->i_sb, node);
  if (!inode)
  {
    parent_node->first_child = node->next_sibling;
    node->next_sibling = NULL;
    node->parent = NULL;
    kfree(node->data);
    kfree(node);
    return -ENOMEM;
  }

  inc_nlink(dir);
  mark_inode_dirty(dir);

  d_instantiate_new(dentry, inode);
  mark_inode_dirty(inode);

  LOG("Mkdir: created dir '%s' ino=%lu in dir ino=%lu",
      node->name, inode->i_ino, dir->i_ino);

  return 0;
}

int vtfs_rmdir(struct inode *dir, struct dentry *dentry)
{
  struct vtfs_node *parent = dir->i_private;
  struct inode *inode = d_inode(dentry);
  struct vtfs_node *node = inode ? inode->i_private : NULL;
  struct vtfs_node *cur, *prev = NULL;

  if (!parent || !node)
    return -EINVAL;

  if (!node->is_dir)
    return -ENOTDIR;

  if (node->first_child)
    return -ENOTEMPTY;

  LOG("Rmdir: name=%.*s, parent_ino=%lu, ino=%lu",
      dentry->d_name.len, dentry->d_name.name,
      (unsigned long)parent->ino,
      (unsigned long)inode->i_ino);

  cur = parent->first_child;
  while (cur)
  {
    if (cur == node)
    {
      if (prev)
        prev->next_sibling = cur->next_sibling;
      else
        parent->first_child = cur->next_sibling;

      node->parent = NULL;
      node->next_sibling = NULL;
      break;
    }
    prev = cur;
    cur = cur->next_sibling;
  }

  if (!cur)
    return -ENOENT;

  clear_nlink(inode);
  drop_nlink(dir);

  mark_inode_dirty(inode);
  mark_inode_dirty(dir);

  return 0;
}

static int vtfs_link(struct dentry *old_dentry,
                     struct inode *dir,
                     struct dentry *new_dentry)
{
  struct inode *inode = d_inode(old_dentry);
  struct vtfs_node *old_node = inode->i_private;
  struct vtfs_node *parent_node = dir->i_private;
  struct vtfs_node *new_node;
  struct vtfs_node *data_node;
  const char *name = new_dentry->d_name.name;

  if (!inode || !old_node || !parent_node)
    return -EINVAL;

  if (S_ISDIR(inode->i_mode))
    return -EPERM;

  LOG("Link: creating hardlink '%s' -> '%s'", name, old_node->name);

  data_node = old_node->link_target ? old_node->link_target : old_node;

  new_node = kzalloc(sizeof(struct vtfs_node), GFP_KERNEL);
  if (!new_node)
    return -ENOMEM;

  strscpy(new_node->name, name, sizeof(new_node->name));
  new_node->ino = old_node->ino;
  new_node->is_dir = false;
  new_node->mode = old_node->mode;
  new_node->parent = parent_node;
  new_node->link_target = data_node;
  mutex_init(&new_node->lock);

  new_node->next_sibling = parent_node->first_child;
  parent_node->first_child = new_node;

  inc_nlink(inode);
  ihold(inode);
  d_instantiate(new_dentry, inode);

  mark_inode_dirty(inode);
  mark_inode_dirty(dir);

  LOG("Link: created hardlink '%s' (nlink=%u)", name, inode->i_nlink);
  return 0;
}

const struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .link = vtfs_link,
    .unlink = vtfs_unlink,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
};
