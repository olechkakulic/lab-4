#include "vtfs.h"

static inline struct vtfs_node *vtfs_data_node(struct vtfs_node *node)
{
  return node->link_target ? node->link_target : node;
}

static ssize_t vtfs_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
  struct inode *inode = file_inode(file);
  struct vtfs_node *node = vtfs_data_node(inode->i_private);
  loff_t pos = *ppos;
  size_t available;
  size_t to_copy;
  ssize_t ret;

  if (!node)
    return -EIO;

  if (node->is_dir)
    return -EISDIR;

  if (pos < 0)
    return -EINVAL;

  LOG("Read: ino=%lu, len=%zu, pos=%lld",
      inode->i_ino, len, (long long)pos);
  mutex_lock(&node->lock);

  if (!node->data || pos >= node->size)
  {
    LOG("Read: EOF ino=%lu, pos=%lld, size=%zu",
        inode->i_ino, (long long)pos, node->size);
    ret = 0;
    goto out_unlock;
  }

  available = node->size - pos;
  to_copy = min_t(size_t, available, len);

  if (to_copy == 0)
  {
    LOG("Read: nothing to copy ino=%lu (available=%zu, len=%zu)",
        inode->i_ino, available, len);
    ret = 0;
    goto out_unlock;
  }

  if (copy_to_user(buf, node->data + pos, to_copy))
  {
    LOG_ERR("Read: copy_to_user failed ino=%lu", inode->i_ino);
    ret = -EFAULT;
    goto out_unlock;
  }

  *ppos = pos + to_copy;
  ret = to_copy;

  LOG("Read: read %zu bytes, ino=%lu, new_pos=%lld",
      to_copy, inode->i_ino, (long long)*ppos);

out_unlock:
  mutex_unlock(&node->lock);
  return ret;
}

static ssize_t vtfs_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
  struct inode *inode = file_inode(file);
  struct vtfs_node *node = vtfs_data_node(inode->i_private);
  loff_t pos;
  size_t end_pos;
  size_t new_capacity;
  char *new_data;
  ssize_t ret;

  if (!node)
    return -EIO;

  if (node->is_dir)
    return -EISDIR;

  if (file->f_flags & O_APPEND)
    pos = node->size;
  else
    pos = *ppos;

  if (pos < 0)
  {
    LOG_ERR("Write: negative offset pos=%lld, ino=%lu",
            (long long)pos, inode->i_ino);
    return -EINVAL;
  }

  if (len == 0)
    return 0;

  LOG("Write: ino=%lu, len=%zu, pos=%lld, append=%d",
      inode->i_ino, len, (long long)pos, !!(file->f_flags & O_APPEND));

  if (pos > SIZE_MAX)
  {
    LOG_ERR("Write: pos=%lld exceeds SIZE_MAX, ino=%lu",
            (long long)pos, inode->i_ino);
    return -EFBIG;
  }
  if (len > SIZE_MAX - (size_t)pos)
  {
    LOG_ERR("Write: pos+len overflow (pos=%lld, len=%zu), ino=%lu",
            (long long)pos, len, inode->i_ino);
    return -EFBIG;
  }

  end_pos = (size_t)pos + len;

  mutex_lock(&node->lock);

  if (end_pos > node->capacity)
  {
    new_capacity = node->capacity;

    if (new_capacity == 0)
      new_capacity = PAGE_SIZE;

    while (new_capacity < end_pos)
    {
      if (new_capacity > SIZE_MAX / 2)
      {
        LOG_ERR("Write: capacity overflow request (end_pos=%zu), ino=%lu",
                end_pos, inode->i_ino);
        ret = -EFBIG;
        goto out_unlock;
      }
      new_capacity *= 2;
    }

    LOG("Write: growing buffer ino=%lu, old_cap=%zu, new_cap=%zu",
        inode->i_ino, node->capacity, new_capacity);
    new_data = krealloc(node->data, new_capacity, GFP_KERNEL);

    if (!new_data)
    {
      LOG_ERR("Write: krealloc failed, new_capacity=%zu, ino=%lu",
              new_capacity, inode->i_ino);
      ret = -ENOMEM;
      goto out_unlock;
    }

    if (new_capacity > node->capacity)
      memset(new_data + node->capacity, 0, new_capacity - node->capacity);

    node->data = new_data;
    node->capacity = new_capacity;
  }

  if ((size_t)pos > node->size)
    memset(node->data + node->size, 0, (size_t)pos - node->size);

  if (copy_from_user(node->data + pos, buf, len))
  {
    size_t not_copied = copy_from_user(node->data + pos, buf, len);
    size_t written = len - not_copied;

    if (not_copied)
    {
      LOG_ERR("Write: copy_from_user failed ino=%lu, not_copied=%zu",
              inode->i_ino, not_copied);

      if ((size_t)pos + written > node->size)
        node->size = (size_t)pos + written;

      inode->i_size = node->size;
      file_update_time(file);
      mark_inode_dirty(inode);

      *ppos = pos + written;
      ret = written ? (ssize_t)written : -EFAULT;
      goto out_unlock;
    }
  }

  if (end_pos > node->size)
    node->size = end_pos;

  inode->i_size = node->size;
  file_update_time(file);
  mark_inode_dirty(inode);

  *ppos = pos + len;
  ret = len;

  LOG("Wrote %zu bytes, ino=%lu, new_size=%zu, new_pos=%lld",
      len, inode->i_ino, node->size, (long long)*ppos);

out_unlock:
  mutex_unlock(&node->lock);
  return ret;
}

static int vtfs_open(struct inode *inode, struct file *file)
{
  struct vtfs_node *node = vtfs_data_node(inode->i_private);

  if (!node)
    return -EIO;

  if (file->f_flags & O_TRUNC)
  {
    mutex_lock(&node->lock);
    kfree(node->data);
    node->data = NULL;
    node->size = 0;
    node->capacity = 0;
    inode->i_size = 0;
    mutex_unlock(&node->lock);
    LOG("Truncated file ino=%lu", inode->i_ino);
  }

  return 0;
}

const struct file_operations vtfs_file_ops = {
    .owner = THIS_MODULE,
    .open = vtfs_open,
    .read = vtfs_read,
    .write = vtfs_write,
    .llseek = generic_file_llseek,
    .fsync = generic_file_fsync,
};
