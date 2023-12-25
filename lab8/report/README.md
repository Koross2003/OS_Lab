# Lab8实验报告
***

## 练习1: 完成读文件操作的实现（需要编码）
首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在 kern/fs/sfs/sfs_inode.c中 的sfs_io_nolock()函数，实现读文件中数据的代码。

#### inode

```c
/* inode (on disk) */
struct sfs_disk_inode {
    uint32_t size;                              //如果inode表示常规文件，则size是文件大小
    uint16_t type;                              //inode的文件类型
    uint16_t nlinks;                            //此inode的硬链接数
    uint32_t blocks;                            //此inode的数据块数的个数
    //如果 inode 表示的是文件，则成员变量 direct[]直接指向了保存文件内容数据的数据块索引值。
    uint32_t direct[SFS_NDIRECT];               //此inode的直接数据块索引值（有SFS_NDIRECT个）,为0表示这是一个无效索引(可能需要给他分配)
    uint32_t indirect;                          //此inode的一级间接数据块索引值,为0表示不使用一级索引块
    //indirect 间接指向了保存文件内容数据的数据块，
    //indirect 指向的是间接数据块（indirect block），此数据块实际存放的全部是数据块索引，
    //这些数据块索引指向的数据块才被用来存放文件内容数据。

    /*
    ucore 里 SFS_NDIRECT 是 12，即直接索引的数据页大小为 12 * 4k = 48k；
    当使用一级间接数据块索引时，ucore 支持最大的文件大小为 12 * 4k + 1024 * 4k = 48k + 4m
    */
};
```
- sfs中,block[0]是超级块，block[1]中放了一个 root-dir 的 inode，用来记录根目录的相关信息。，从block[2]开始连续多个块为freemap，用1bit来记录每个块是否被占用
- direct[SFS_NDIRECT]是直接索引，他存的就是某个数据快的索引值
- indirect是间接索引，他存的是一个block的索引值，这个block里存了一堆数据块的索引值，当direct不够用的时候就用这个间接索引来索引更多的数据块
- blocks是这个inode的block数，可以使用他来判断想要访问的某个block合不合法
- ucore 里 SFS_NDIRECT 是 12，即直接索引的数据页大小为 12 * 4k = 48k，当使用一级间接数据块索引时，ucore 支持最大的文件大小为 12 * 4k + 1024 * 4k = 48k + 4m


#### vfs

vfs提供的接口会做一些判断，然后调用sfs提供的接口，调用sfs提供的接口时，会通过各种`vop`宏

```c
static const struct inode_ops sfs_node_dirops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_open                       = sfs_opendir,
    .vop_close                      = sfs_close,
    .vop_fstat                      = sfs_fstat,
    .vop_fsync                      = sfs_fsync,
    .vop_namefile                   = sfs_namefile,
    .vop_getdirentry                = sfs_getdirentry,
    .vop_reclaim                    = sfs_reclaim,
    .vop_gettype                    = sfs_gettype,
    .vop_lookup                     = sfs_lookup,
};
```




#### 读写文件的流程

1. 在进程中调用相关库函数(如`file`)
2. 用户进程再调用内核提供的接口
3. 通过内核提供的接口继续调用抽象层vfs提供的接口
4. 通过vfs提供的接口才算真正进入文件系统sfs
5. 文件系统具体调用io接口函数实现各种操作




#### 打开文件的过程




**进入vfs之前**

1. 用户态调用open，进而调用sys_open进入内核态
2. 内核态下通过sysfile_open将用户态传入的文件路径复制一份(注意这时候要改信号量防止冲突)
3. 然后调用file_open函数，要在这里分配一个file结构体，这里分配时候会从fd数组中找一个空的直接分配(调用这个函数时也可以打开fd数组中特定的某一个file结构体)



**在vfs_open中**
1. 调用vfs_lookup，
2. 拿到对应的inode(传入的路径可以是相对路径，也可以是device，会根据情况处理这个path)
3. 然后调用vop_lookup(也就是sys_lookup)，跳转到sfs
4. 如果lookup没有，可能需要创建一个file(看create让不让)，创建调用vop_creat，也即sys_creat
5. 此时或者是look_up到，或者是create的，总之我们已经有了这个file的索引fd，这个fd会被一路返回给用户
6. 接下来打开这个file，调用vop_open


**在vop_open中**

1. 如果打开文件夹，vop_open会转给sfs_opendir
2. 如果打开的是具体的文件，那么会转给sfs_openfile









#### sfs_io_nolock()


```c
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, off_t offset, size_t *alenp, bool write) {
    struct sfs_disk_inode *din = sin->din;
    assert(din->type != SFS_TYPE_DIR);
    off_t endpos = offset + *alenp, blkoff;
    *alenp = 0;
	// calculate the Rd/Wr end position
    if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos) {
        return -E_INVAL;
    }
    if (offset == endpos) {
        return 0;
    }
    if (endpos > SFS_MAX_FILE_SIZE) {
        endpos = SFS_MAX_FILE_SIZE;
    }
    if (!write) {
        if (offset >= din->size) {
            return 0;
        }
        if (endpos > din->size) {
            endpos = din->size;
        }
    }

    int (*sfs_buf_op)(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
    int (*sfs_block_op)(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
    if (write) {
        sfs_buf_op = sfs_wbuf, sfs_block_op = sfs_wblock;
    }
    else {
        sfs_buf_op = sfs_rbuf, sfs_block_op = sfs_rblock;
    }

    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    uint32_t blkno = offset / SFS_BLKSIZE;          // The NO. of Rd/Wr begin block
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno;  // The size of Rd/Wr blocks

    if ((blkoff = offset % SFS_BLKSIZE) != 0) {
        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset);
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
            goto out;
        }

        alen += size;
        buf += size;

        if (nblks == 0) {
            goto out;
        }

        blkno++;
        nblks--;
    }

    if (nblks > 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_block_op(sfs, buf, ino, nblks)) != 0) {
            goto out;
        }

        alen += nblks * SFS_BLKSIZE;
        buf += nblks * SFS_BLKSIZE;
        blkno += nblks;
        nblks -= nblks;
    }
    if ((size = endpos % SFS_BLKSIZE) != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
            goto out;
        }
        alen += size;
    }
out:
    *alenp = alen;
    if (offset + alen > sin->din->size) {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }
    return ret;
}

```

- offset是上一轮读取结束的位置，alenp是现在需要读取的长度。
- 首先进行一些是否越界的判断。
- 读对上一次读取的块是否读取完进行判断。如果offset 不能整除 SFS_BLKSIZE，则得到的余数是上一个被读取的块被读取了多少。拿SFS_BLKSIZE减去这个余数就是上一个块还剩多少需要被读取。
  - 如果这一轮需要开始读取的块是当前inode索引的第一个块，那么说明上次出现了一些情况导致只读取了这个块的一部分，于是这次要读取的大小就是这个块的剩余部分endpos - offset。
  - 如果当前块不是第一个，那么就首先得把上一个块的剩下这么多SFS_BLKSIZE - blkoff数据给读出来。这部分会通过sfs_bmap_load_nolock进入sfs_bmap_get_nolock。


## 练习2: 完成基于文件系统的执行程序机制的实现（需要编码）
改写proc.c中的load_icode函数和其他相关函数，实现基于文件系统的执行程序机制。执行：make qemu。如果能看看到sh用户程序的执行界面，则基本成功了。如果在sh用户界面上可以执行”ls”,”hello”等其他放置在sfs文件系统中的其他执行程序，则可以认为本实验基本成功。

### 1.改写相关函数

#### 1.1 `alloc_proc`函数

在 proc.c 中，根据注释我们需要先初始化 fs 中的进程控制结构，即在 alloc_proc 函数中我们需要做—下修改，加上—句 proc->ﬁlesp = NULL; 从而完成初始化。—个文件需要在 VFS 中变为—个进程才能被执行。

```cpp
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        // 无需改动内容
        // ...

        //LAB8:EXERCISE2 YOUR CODE HINT:need add some code to init fs in proc_struct, ...
        // LAB8 添加一个filesp指针的初始化
        proc->filesp = NULL;
    }
    return proc;
}
```

#### 1.2 `do_fork`函数

在该函数中需要补充将当前进程的fs复制到fork出的进程中，同时添加如果复制失败，则需要重置原先的操作。

```cpp
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
//无需改动内容省略
//...

//lab8
if (copy_files(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_kstack;}
//...
fork_out:
    return ret;
bad_fork_cleanup_fs:  //for LAB8
//...
 }
```

#### 1.3 `load_icode` 函数

对于lab5的该函数，主要**修改**第三部分：将TEXT/DATA/BSS部分以二进制拷贝到进程内存空间。

- lab5中，读取可执行文件是直接读取内存的，但在这里需要使用函数`load_icode_read`来从文件系统中读取`ELF header`以及各个段的数据

- 其余的读取elf的部分不需要改变，因为elf的组成不变。第3部分的将文件逐个段加载到内存中，这里要设置虚拟地址与物理地址之间的映射

```cpp
// LAB8 这里要从文件中读取ELF header
    struct elfhdr __elf, *elf = &__elf;
    if ((ret = load_icode_read(fd, elf, sizeof(struct elfhdr), 0)) != 0) {
        goto bad_elf_cleanup_pgdir;
    }
    // 判断读取入的elf header是否正确
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }
    // 根据每一段的大小和基地址来分配不同的内存空间
    struct proghdr __ph, *ph = &__ph;
    uint32_t vm_flags, perm, phnum;
    for (phnum = 0; phnum < elf->e_phnum; phnum ++) {
        //lab8 从文件特定偏移处读取每个段的详细信息（包括大小、基地址等等）
        off_t phoff = elf->e_phoff + sizeof(struct proghdr) * phnum;
        if ((ret = load_icode_read(fd, ph, sizeof(struct proghdr), phoff)) != 0) {
            goto bad_cleanup_mmap;
        }
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            // continue ;
            // do nothing here since static variables may not occupy any space
        }
        vm_flags = 0, perm = PTE_U | PTE_V;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
        // modify the perm bits here for RISC-V
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;
        // 为当前段分配内存空间
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap;
        }
        off_t offset = ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

        end = ph->p_va + ph->p_filesz;
        while (start < end) {
             // 设置该内存所对应的页表项
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            // LAB8 读取elf对应段内的数据并写入至该内存中
            if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0) {
                goto bad_cleanup_mmap;
            }
            start += size, offset += size;
        }
        end = ph->p_va + ph->p_memsz;
        // 对于段中当前页中剩余的空间（复制elf数据后剩下的空间），将其置为0
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        // 对于段中剩余页中的空间（复制elf数据后的多余页面），将其置为0
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }
    // 关闭读取的ELF
    sysfile_close(fd);
```



主要**添加**第六部分：设置用户栈中的uargc和uargv

- Lab5的`load_icode`函数中并没有对`execve`所执行的程序传入参数，现在lab8补充这个实现

```cpp
// LAB8 设置execve所启动的程序参数
    uint32_t argv_size=0, i;
    //计算参数总长度
    for (i = 0; i < argc; i ++) {
        argv_size += strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1;
    }

    uintptr_t stacktop = USTACKTOP - (argv_size/sizeof(long)+1)*sizeof(long);
    // 直接将传入的参数压入至新栈的底部
    char** uargv=(char **)(stacktop  - argc * sizeof(char *));
    
    argv_size = 0;
    //存储参数
    for (i = 0; i < argc; i ++) {
        //uargv[i]保存了kargv[i]在栈中的起始位置
        uargv[i] = strcpy((char *)(stacktop + argv_size), kargv[i]);
        argv_size +=  strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1;
    }
    
    //由于栈地址从高向低增长，所以第一个参数的地址即为栈顶地址，同时压入一个int用于保存argc
    stacktop = (uintptr_t)uargv - sizeof(int);
    //在栈顶存储参数数量
    *(int *)stacktop = argc;
```








## 扩展练习 Challenge1：完成基于“UNIX的PIPE机制”的设计方案
如果要在ucore里加入UNIX的管道（Pipe）机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的PIPE机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）


1. **系统调用：** 进程通过`pipe`系统调用创建一个管道，该系统调用返回两个文件描述符，一个用于读取（读取端），另一个用于写入（写入端）。

2. **文件描述符传递：** 进程通过系统调用`fork`创建一个子进程，子进程复制了父进程的文件描述符表，包括管道的文件描述符。这样，父子进程都可以通过文件描述符访问管道。

3. **端口关闭：** 为了正确使用管道，进程需要关闭不需要的文件描述符。例如，父进程关闭读取端，而子进程关闭写入端。这样做有助于确保进程不会意外地对管道进行不必要的读写操作。

4. **数据传输：** 进程通过文件描述符进行数据传输。一个进程向管道的写入端写入数据，而另一个进程从管道的读取端读取数据。数据在管道中传输，实现了进程间通信。

5. **阻塞和同步：** 管道是有限容量的，当管道的缓冲区已满时，写入操作可能会被阻塞，直到有足够的空间。同样，当管道为空时，读取操作可能会被阻塞，直到有数据可用。这种阻塞行为有助于实现进程间的同步。

6. **虚拟文件系统实现：** 可以 VFS 中实现管道机制。这样，管道就可以像其他文件一样使用标准的I/O函数进行读写操作。在VFS中，每个管道都可以作为一个特殊的文件类型，当进程打开文件时，将创建两个新的fd，并将该fd分别与管道的读写端口相关联。然后，进程就可以使用标准的I/O函数进行读写操作了。




## 扩展练习 Challenge2：完成基于“UNIX的软连接和硬连接机制”的设计方案

如果要在ucore里加入UNIX的软连接和硬连接机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个（或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的软连接和硬连接机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）


### 数据结构：

1. **Inode结构：**
   - 描述文件的元数据，包括文件类型（普通文件、目录等）、权限信息、链接计数等。
   - 增加字段以存储连接信息，例如硬链接的数量。
   
   ```c
   struct inode {
       // ... 其他字段
       int link_count; // 硬链接数量
       // ... 其他字段
   };
   ```

2. **file结构：**
   - 描述文件，包括文件名和对应的Inode号。
   ```c
   struct file {
       char name[MAX_FILENAME_LEN];
       uint32_t inode_number;
   };
   ```

### 接口：

1. **创建硬链接：**
   - 接口：`int hard_link(const char *existing_path, const char *new_path);`
   - 语义：创建一个新的硬链接，将已存在文件关联到新的路径上。需要更新Inode和目录中的信息。

2. **创建软连接：**
   - 接口：`int soft_link(const char *target_path, const char *link_path);`
   - 语义：创建一个新的软链接，指向目标文件。软链接是一个包含目标路径的特殊文件，需要在inode中标识这是一个软链接。

3. **删除链接：**
   - 接口：`int unlink(const char *path);`
   - 语义：删除指定路径上的链接。如果是硬链接，减少相应inode的链接计数，如果计数为零，则释放相应的inode。如果是软链接，直接删除。

4. **读取链接信息：**
   - 接口：`ssize_t read_link(const char *path, char *buffer, size_t size);`
   - 语义：读取软链接的目标路径信息。

5. **同步和互斥：**
   - 需要使用锁来确保在多线程或多进程环境下的并发安全性，特别是在增加或减少链接计数时。
