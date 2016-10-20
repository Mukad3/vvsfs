# VVSFS (Very Very Simple File System)
Zhansong Li, Sina Eghbal
{u5844206, u5544352}@anu.edu.au
October 2016

## VVSFS
VVFSF is a very ver simple file system based on the simplistic RAM filesystem.
However, the filesystem does not support simple features such as removing directories,
truncating the files, showing the stat of the files, etc. A series of modifications
have been done on the VVSFS and certain features have been added to the filesystem which 
we will discuss in what follows.

### Features addedd
- Removing and truncating files: The ability to remove and truncate files have been added to the filesystem.
This has been done by adding the missing unlink function to the directory operations.
```c
static int vvsfs_unlink (struct inode *dir, struct dentry *dentry);
```
The above function iterates through the directories and checks whether the directory's name matched
the name given to be deleted. Once it finds the matching name, it will break from the loop and
shifts the inodes after that one entry to the left.
- Truncating files: Truncating or shortening the files is done by setting their size in the
** setattr ** function. The setattr function is used to set/modify attributes of the inode. The attributes
are passed in a structure called  *iattr* and truncating will be done by calling truncate_setsize if the value of ATTR_SIZE is true.

- Removing directories: Removing directories will be done by unlinking the inode and the dentry in the *vvsfs_rmdir*
function. The vvsfs_rmdir function is registered in the *dir_inode_operations* as rmdir.

- Creating directories: Creating directories is done by the *vvsfs_mkdir* directory which is merely calling the *vvsfs_mknod* function/
*vvsfs_mkdir* is also registered in vvsfs_dir_inode_operations as rmdir.

- Recording the file stats: To record the file stats we initially extended the very simple inode structure of vvsfs.
Therefore, we add the following entries to our structure.
```c
uid_t i_uid;
gid_t i_gid;
unsigned short mode;
```
Since we have changed the structure, we need to modify our MAXFILESIZE macro by subtracting the size of the variables we are using.
Hence, our file size will be calculated as follows:
```c
#define MAXFILESIZE (BLOCKSIZE - 3*sizeof (int) - sizeof (uid_t) - sizeof (gid_t) - sizeof (unsigned short))
// Block size - size of all the variables we're using in our structure.
```
This will provide us with the ability to store the user id, group id, and mode of our file. 