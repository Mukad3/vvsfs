/*
 * A Very Very Simple Filesystem
   Eric McCreath 2006, 2008, 2010 - GPL

   (based on the simplistic RAM filesystem McCreath 2001)   */

/* to make use:
      make -C /usr/src/linux-headers-2.6.32-23-generic/  SUBDIRS=$PWD modules
	  (or just make, with the accompanying Makefile)

   to load use:
      sudo insmod vvsfs.ko
	  (may need to copy vvsfs.ko to a local filesystem first)

   to make a suitable filesystem:
      dd of=myvvsfs.raw if=/dev/zero bs=512 count=100
      ./mkfs.vvsfs myvvsfs.raw
	  (could also use a USB device etc.)

   to mount use:
      mkdir testdir
      sudo mount -o loop -t vvsfs myvvsfs.raw testdir

   to use a USB device:
      create a suitable partition on USB device (exercise for reader)
	  ./mkfs.vvsfs /dev/sdXn
      where sdXn is the device name of the usb drive
      mkdir testdir
      sudo mount -t vvsfs /dev/sdXn testdir

   use the file system:
      cd testdir
      echo hello > file1
      cat file1
      cd ..

   unmount the filesystem:
      sudo umount testdir

   remove the module:
      sudo rmmod vvsfs 
*/

/*
    Modifications by 
    {Zhansong Li (u5844206), Sina Eghbal (u5544352)} @anu.edu.au
    October 2016
    COMP3300 - Assignment 2
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/statfs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <asm/uaccess.h>


//added later
#include <linux/quotaops.h>
#include <linux/posix_acl_xattr.h>
#include "vvsfs.h"

#define DEBUG 1

static struct inode_operations vvsfs_file_inode_operations;
static struct file_operations vvsfs_file_operations;
static struct super_operations vvsfs_ops;

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);
static void
vvsfs_put_super(struct super_block *sb) {
  if (DEBUG) printk("vvsfs - put_super\n");
  return;
}

static int 
vvsfs_statfs(struct dentry *dentry, struct kstatfs *buf) {
  printk ("statf is called\n");
  if (DEBUG) printk("vvsfs - statfs\n");

  buf->f_namelen = MAXNAME;
  return 0;
}

// vvsfs_readblock - reads a block from the block device (this will copy over
//                      the top of inode)
static int
vvsfs_readblock(struct super_block *sb, int inum, struct vvsfs_inode *inode) {
  struct buffer_head *bh;

  if (DEBUG) printk("vvsfs - readblock : %d\n", inum);
  
  bh = sb_bread(sb,inum);
  memcpy((void *) inode, (void *) bh->b_data, BLOCKSIZE);
  brelse(bh);
  if (DEBUG) printk("vvsfs - readblock done : %d\n", inum);
  return BLOCKSIZE;
}

// vvsfs_writeblock - write a block from the block device(this will just mark the block
//                      as dirtycopy)
static int
vvsfs_writeblock(struct super_block *sb, int inum, struct vvsfs_inode *inode) {
  struct buffer_head *bh;

  if (DEBUG) printk("vvsfs - writeblock : %d\n", inum);

  bh = sb_bread(sb,inum);

  memcpy(bh->b_data, inode, BLOCKSIZE);
  mark_buffer_dirty(bh);
  sync_dirty_buffer(bh);
  brelse(bh);
  if (DEBUG) printk("vvsfs - writeblock done: %d\n", inum);
  return BLOCKSIZE;
}
// vvsfs_readdir - reads the directories given their entry
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
vvsfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
#else
vvsfs_readdir(struct file *filp, struct dir_context *ctx)
#endif
{
	
	struct inode *i;
	struct vvsfs_inode dirdata;
	int num_dirs;
	struct vvsfs_dir_entry *dent;
	int error, k;

	if (DEBUG) printk("vvsfs - readdir\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	i = filp->f_dentry->d_inode;
#else
	i = file_inode(filp);
#endif
	vvsfs_readblock(i->i_sb, i->i_ino, &dirdata);

	dirdata.mode = i->i_mode;
	num_dirs = dirdata.size / sizeof(struct vvsfs_dir_entry);

	if (DEBUG) printk("Number of entries %d fpos %Ld\n", num_dirs, filp->f_pos);

	error = 0;
	k=0;
	dent = (struct vvsfs_dir_entry *) &dirdata.data;
	while (!error && filp->f_pos < dirdata.size && k < num_dirs) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		error = filldir(dirent, 
		    dent->name, strlen(dent->name), filp->f_pos, dent->inode_number,DT_REG);
		if (error)
			break;
		filp->f_pos += sizeof(struct vvsfs_dir_entry);
#else
		if (dent->inode_number) {
			if (!dir_emit (ctx, dent->name, strnlen (dent->name, MAXNAME),
				dent->inode_number, DT_UNKNOWN)) return 0;
		}
		ctx->pos += sizeof(struct vvsfs_dir_entry);
#endif
		k++;
		dent++;
	}
	// update_atime(i);

	return 0;
}

// vvsfs_lookup - A directory name in a directory. It basically attaches the inode 
//                of the file to the directory entry.
static struct dentry *
vvsfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{

  int num_dirs;
  int k;
  struct vvsfs_inode dirdata;
  struct inode *inode = NULL;
  struct vvsfs_dir_entry *dent;

  if (DEBUG) printk("vvsfs - lookup\n");

  vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
  num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);

  for (k=0;k < num_dirs;k++) {
    dent = (struct vvsfs_dir_entry *) ((dirdata.data) + k*sizeof(struct vvsfs_dir_entry));
    
    if ((strlen(dent->name) == dentry->d_name.len) && 
	strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) == 0) {
      inode = vvsfs_iget(dir->i_sb, dent->inode_number);
 
      if (!inode)
	return ERR_PTR(-EACCES);
    
      d_add(dentry, inode);
      return NULL;

    }
  }
  d_add(dentry, inode);
  return NULL;
}

// vvsfs_empty_inode - finds the first free inode (returns -1 is unable to find one)
static int vvsfs_empty_inode(struct super_block *sb) {
  struct vvsfs_inode block;
  int k;
  for (k =0;k<NUMBLOCKS;k++) {
    vvsfs_readblock(sb,k,&block);
    if (block.is_empty) return k;   
  }
  return -1;
}

// vvsfs_new_inode - find and construct a new inode.
struct inode * vvsfs_new_inode(const struct inode * dir, umode_t mode)
{
  struct vvsfs_inode block;
  struct super_block * sb;
  struct inode * inode;
  int newinodenumber;

  if (DEBUG) printk("vvsfs - new inode\n");
  
  if (!dir) return NULL;
  sb = dir->i_sb;

  inode = new_inode(sb);
  if (!inode) return NULL;
  inode_init_owner(inode, dir, mode);
 
  /* find a spare inode in the vvsfs */
  newinodenumber = vvsfs_empty_inode(sb);
  if (newinodenumber == -1) {
    printk("vvsfs - inode table is full.\n");
    return NULL;
  }

  block.is_empty = false;
  block.size = 0;
  if(S_ISDIR(mode)){
      printk("vvsfs - NEW DIRECTORY!\n");
      block.is_directory = true;
  } else {
      block.is_directory = false;
  }
  block.mode = (unsigned int)mode;
  block.i_uid = i_uid_read(inode);
  block.i_gid = i_gid_read(inode);
  block.size = sizeof (struct vvsfs_inode) - sizeof (char[MAXFILESIZE]) + sizeof (block.data);

  vvsfs_writeblock(sb,newinodenumber,&block);

  inode->i_ino = newinodenumber;
  inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;

  i_uid_write (inode, block.i_uid);
  i_gid_write (inode, block.i_gid);
  inode->i_mode = block.mode;
   
  inode->i_op = NULL;
  
  insert_inode_hash(inode);
  
  return inode;
}

static struct inode_operations vvsfs_dir_inode_operations;
static struct file_operations vvsfs_dir_operations;

/* vvsfs_mknod - create a node inside the given directory */
static int
vvsfs_mknod(struct inode *dir, struct dentry* dentry, umode_t mode)
{
  struct vvsfs_inode dirdata;
  int num_dirs;
  struct vvsfs_dir_entry *dent;

  struct inode * inode;

  if (DEBUG) printk("vvsfs - create : %s\n",dentry->d_name.name);

  inode = vvsfs_new_inode(dir,mode);
  if (!inode || IS_ERR (inode)) return -ENOSPC;

  if(S_ISDIR(mode)){
      inode->i_op = &vvsfs_dir_inode_operations;
      inode->i_fop = &vvsfs_dir_operations;
  } else {
      inode->i_op = &vvsfs_file_inode_operations;
      inode->i_fop = &vvsfs_file_operations;
  }

  if (!dir) return -1;

  vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);

  num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
  dent = (struct vvsfs_dir_entry *) ((dirdata.data) + num_dirs*sizeof(struct vvsfs_dir_entry));

  strncpy(dent->name, dentry->d_name.name,dentry->d_name.len);
  dent->name[dentry->d_name.len] = '\0';
  
  dirdata.size = (num_dirs + 1) * sizeof(struct vvsfs_dir_entry); 
  dirdata.mode = (unsigned int) mode;

  dent->inode_number = inode->i_ino;
  
  vvsfs_writeblock(dir->i_sb,dir->i_ino,&dirdata);

  d_instantiate(dentry, inode);
  dget(dentry);

  printk("Node created %ld\n",inode->i_ino);
  printk ("uid : %d\n", i_uid_read (inode));
  printk ("mode: %d\n", dirdata.mode);
  //FIXME
  //update_atime(dir);

  return 0;
}

// vvsfs_create - create a new file in a directory 
static int
vvsfs_create(struct inode *dir, struct dentry* dentry, umode_t mode, bool excl)
{
    return vvsfs_mknod(dir,dentry,S_IRUGO|S_IWUGO|S_IFREG);
}

// vvsfs_mkdir - create a new directory inside the given directory
static int 
vvsfs_mkdir(struct inode * dir,struct dentry * dentry, umode_t mode)
{
    return vvsfs_mknod(dir,dentry,S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR);
}
// vvsfs_unlink - for removing the files
static int
vvsfs_unlink(struct inode* dir, struct dentry* dentry)
{
  struct vvsfs_inode dirdata;
  struct vvsfs_dir_entry *dent;
  int num_dirs,error,k,j;

  struct inode* inode = d_inode(dentry);

  vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);

  error = 0;
  k=0;
  dent = (struct vvsfs_dir_entry *) &dirdata.data;
  num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
  while (!error && k < num_dirs) {
      printk("vvsfs - unlink, trying, name : %s ino : %d\n",dent->name, dent->inode_number);

      if(dent->inode_number == inode->i_ino){
          break;
      }

      k++;
      dent++;
  }
  // if the entry was not found
  if (k == num_dirs) return -ENOENT;
  if (S_ISDIR (inode->i_mode)) return -EIO;
  for(j=k;j<num_dirs-1;j++){
      memcpy(dent,dent+1,sizeof(struct vvsfs_dir_entry));
      dent->inode_number = (dent+1)->inode_number;

      dent++;
  }
  dirdata.size -= sizeof(struct vvsfs_dir_entry);
  vvsfs_writeblock(dir->i_sb,dir->i_ino,&dirdata);

  // clear inode
  vvsfs_readblock(dir->i_sb,inode->i_ino,&dirdata);
  dirdata.size = 0;
  dirdata.is_directory = false;
  dirdata.is_empty = true;
  vvsfs_writeblock(dir->i_sb,inode->i_ino,&dirdata);

  drop_nlink(inode);
  dput(dentry);

  return 0;
}

// vvsfs_file_write - write to a file
static ssize_t
vvsfs_file_write(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
  struct vvsfs_inode filedata; 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
  struct inode *inode = filp->f_dentry->d_inode;
#else
  struct inode *inode = filp->f_path.dentry->d_inode;
#endif
  ssize_t pos;
  struct super_block * sb;
  char * p;

  if (DEBUG) printk("vvsfs - file write - count : %zu ppos %Ld\n",count,*ppos);

  if (!inode) {
    printk("vvsfs - Problem with file inode\n");
    return -EINVAL;
  }
  
  if (!(S_ISREG(inode->i_mode))) {
    printk("vvsfs - not regular file\n");
    return -EINVAL;
  }
  if (*ppos > inode->i_size || count <= 0) {
    printk("vvsfs - attempting to write over the end of a file.\n");
    return 0;
  }  
  sb = inode->i_sb;

  vvsfs_readblock(sb,inode->i_ino,&filedata);

  if (filp->f_flags & O_APPEND)
    pos = inode->i_size;
  else
    pos = *ppos;

  if (pos + count > MAXFILESIZE) return -ENOSPC;
  filedata.size = pos+count;
  p = filedata.data + pos;
  if (copy_from_user(p,buf,count))
    return -ENOSPC;
  *ppos = pos;
  buf += count;

  inode->i_size = filedata.size;
  inode->i_mode = (umode_t)filedata.mode;
  inode->i_blocks = (filedata.size / BLOCKSIZE) + 1;
  vvsfs_writeblock(sb,inode->i_ino,&filedata);
  
  if (DEBUG) printk("vvsfs - file write done : %zu ppos %Ld\n",count,*ppos);
  
  return count;
}

// vvsfs_file_read - read data from a file
static ssize_t
vvsfs_file_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
  struct vvsfs_inode filedata; 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
  struct inode *inode = filp->f_dentry->d_inode;
#else
  struct inode *inode = filp->f_path.dentry->d_inode;
#endif
  char                    *start;
  ssize_t                  offset, size;

  struct super_block * sb;
  
  if (DEBUG) printk("vvsfs - file read - count : %zu ppos %Ld\n",count,*ppos);

  if (!inode) {
    printk("vvsfs - Problem with file inode\n");
    return -EINVAL;
  }
  
  if (!(S_ISREG(inode->i_mode))) {
    printk("vvsfs - not regular file\n");
    return -EINVAL;
  }
  if (*ppos > inode->i_size || count <= 0) {
    printk("vvsfs - attempting to write over the end of a file.\n");
    return 0;
  }  
  sb = inode->i_sb;

  printk("r : readblock\n");
  vvsfs_readblock(sb,inode->i_ino,&filedata);
  printk ("mode is: %i\n", filedata.mode);
  start = buf;
  printk("rr\n");
  size = MIN (inode->i_size - *ppos,count);

  printk("readblock : %zu\n", size);
  offset = *ppos;            
  *ppos += size;

  printk("r copy_to_user\n");

  if (copy_to_user(buf,filedata.data + offset,size)) 
    return -EIO;
  buf += size;
  
  printk("r return\n");
  return size;
}

// vvsfs_setattr - setattr function for truncating the files and changing chmod
int vvsfs_setattr (struct dentry *dentry, struct iattr *iattr) {
        struct inode *inode = d_inode (dentry);
        int error;
	struct vvsfs_inode vvsfs_i;

        error = inode_change_ok (inode, iattr);
        if (error) return error;
        if (iattr->ia_valid & ATTR_SIZE) truncate_setsize (inode, iattr->ia_size);
        setattr_copy (inode, iattr);
	
	vvsfs_readblock (inode->i_sb, inode->i_ino, &vvsfs_i);
	vvsfs_i.mode = (unsigned int)inode->i_mode; 
	vvsfs_writeblock (inode->i_sb, inode->i_ino, &vvsfs_i);
        mark_inode_dirty (inode);
        return 0;
}


static struct file_operations vvsfs_file_operations = {
        read: vvsfs_file_read,        /* read */
        write: vvsfs_file_write,       /* write */
};

static struct inode_operations vvsfs_file_inode_operations = {
	.setattr = vvsfs_setattr,
};

static struct file_operations vvsfs_dir_operations = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	.readdir =	vvsfs_readdir,          /* readdir */
	.setattr =      vvsfs_setattr,          /* setattr */
#else
	.llseek =	generic_file_llseek,
	.read	=	generic_read_dir,
	.iterate =	vvsfs_readdir,
	.fsync	=	generic_file_fsync,
#endif
};

static struct inode_operations vvsfs_dir_inode_operations = {
   create:     vvsfs_create,        /* create */
   lookup:     vvsfs_lookup,        /* lookup */
   mkdir:      vvsfs_mkdir,         /* mkdir */
   unlink:     vvsfs_unlink,        /* unlink */
};

// vvsfs_iget - get the inode from the super block
struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct vvsfs_inode filedata; 

    uid_t i_uid;
    gid_t i_gid;
    umode_t mode;

    if (DEBUG) {
        printk("vvsfs - iget - ino : %d", (unsigned int) ino);
        printk(" super %p\n", sb);  
    }

    inode = iget_locked(sb, ino);
    if(!inode)
        return ERR_PTR(-ENOMEM);
    if(!(inode->i_state & I_NEW))
        return inode;

    vvsfs_readblock(inode->i_sb,inode->i_ino,&filedata);

	inode->i_size = filedata.size;
	// Write the uid and gid to the inode
	i_uid = filedata.i_uid;
	i_gid = filedata.i_gid;
	mode = (umode_t)filedata.mode;

	i_uid_write (inode, i_uid);
	i_gid_write (inode, i_gid);

	inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_mode = mode;
    if (filedata.is_directory) {
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    } else {
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
    }
    	inode->i_blocks = (filedata.size / BLOCKSIZE) + 1;
    unlock_new_inode(inode);
    return inode;
}

// vvsfs_fill_super - read the super block (this is simple as we do not
//                    have one in this file system)
static int vvsfs_fill_super(struct super_block *s, void *data, int silent)
{
  struct inode *i;
  int hblock;

  if (DEBUG) printk("vvsfs - fill super\n");

  s->s_flags = MS_NOSUID | MS_NOEXEC;
  s->s_op = &vvsfs_ops;

  i = new_inode(s);

  i->i_sb = s;
  i->i_ino = 0;
  i->i_flags = 0;
  i->i_mode = S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR;
  i->i_size = s->s_count;
  i->i_op = &vvsfs_dir_inode_operations;
  i->i_fop = &vvsfs_dir_operations; 
  printk("inode %p\n", i);
  //inode_init_owner(i, NULL, 755);

  hblock = bdev_logical_block_size(s->s_bdev);
  if (hblock > BLOCKSIZE) {
     printk("device blocks are too small!!");
     return -1;
  }

  set_blocksize(s->s_bdev, BLOCKSIZE);
  s->s_blocksize = BLOCKSIZE;
  s->s_blocksize_bits = BLOCKSIZE_BITS;
  s->s_root = d_make_root(i);

  return 0;
}

static struct super_operations vvsfs_ops = {
  statfs: vvsfs_statfs,
  put_super: vvsfs_put_super,
};

static struct dentry *vvsfs_mount(struct file_system_type *fs_type,
				    int flags, const char *dev_name, void *data)
{
  return mount_bdev(fs_type, flags, dev_name, data, vvsfs_fill_super);
}

static struct file_system_type vvsfs_type = {
  .owner	= THIS_MODULE,
  .name		= "vvsfs",
  .mount	= vvsfs_mount,
  .kill_sb	= kill_block_super,
  .fs_flags	= FS_REQUIRES_DEV,
};

static int __init vvsfs_init(void)
{
  printk("Registering vvsfs\n");
  return register_filesystem(&vvsfs_type);
}

static void __exit vvsfs_exit(void)
{
  printk("Unregistering the vvsfs.\n");
  unregister_filesystem(&vvsfs_type);
}

module_init(vvsfs_init);
module_exit(vvsfs_exit);
MODULE_LICENSE("GPL");
