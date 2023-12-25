#ifndef __KERN_FS_VFS_INODE_H__
#define __KERN_FS_VFS_INODE_H__

#include <defs.h>
#include <dev.h>
#include <sfs.h>
#include <atomic.h>
#include <assert.h>

struct stat;
struct iobuf;
/*
 * inode结构是文件的抽象表示。
 * 它是一个接口，允许内核的与文件系统无关的代码有用地与多个文件系统代码集进行交互。
 */
/*
 * 抽象低级文件。
 * 注意：in_info是文件系统特定的数据，in_type是inode类型。
 * open_count通过VOP_INCOPEN和VOP_DECOPEN由vfs_open()和vfs_close()进行管理。
 * 在VFS层之上的代码不需要担心它。
 */
struct inode {
    union {                                 //包含不同文件系统特定inode信息的union成员变量
        struct device __device_info;          //设备文件系统内存inode信息
        struct sfs_inode __sfs_inode_info;    //SFS文件系统内存inode信息
    } in_info;
    enum {
        inode_type_device_info = 0x1234,
        inode_type_sfs_inode_info,
    } in_type;                          //此inode所属文件系统类型
    int ref_count;                 //此inode的引用计数
    int open_count;                //打开此inode对应文件的个数
    struct fs *in_fs;                   //抽象的文件系统，包含访问文件系统的函数指针
    const struct inode_ops *in_ops;     //抽象的inode操作，包含访问inode的函数指针 // 对inode各种操作的函数列表
};


#define __in_type(type)                                             inode_type_##type##_info

#define check_inode_type(node, type)                                ((node)->in_type == __in_type(type))

#define __vop_info(node, type)                                      \
    ({                                                              \
        struct inode *__node = (node);                              \
        assert(__node != NULL && check_inode_type(__node, type));   \
        &(__node->in_info.__##type##_info);                         \
     })

#define vop_info(node, type)                                        __vop_info(node, type)

#define info2node(info, type)                                       \
    to_struct((info), struct inode, in_info.__##type##_info)

struct inode *__alloc_inode(int type);

#define alloc_inode(type)                                           __alloc_inode(__in_type(type))

#define MAX_INODE_COUNT                     0x10000

int inode_ref_inc(struct inode *node);
int inode_ref_dec(struct inode *node);
int inode_open_inc(struct inode *node);
int inode_open_dec(struct inode *node);

void inode_init(struct inode *node, const struct inode_ops *ops, struct fs *fs);
void inode_kill(struct inode *node);

#define VOP_MAGIC                           0x8c4ba476


/*
 * inode的抽象操作。
 *
 * 这些操作以VOP_FOO(inode, args)的形式使用，这些宏展开为inode->inode_ops->vop_foo(inode, args)。操作"foo"包括：
 *
 *    vop_open        - 在文件的open()调用时调用。可用于拒绝非法或不希望的打开模式。请注意，可以在文件实际打开之前执行各种操作。
 *                      Inode不需要关心O_CREAT、O_EXCL或O_TRUNC，因为这些在VFS层中处理。
 *
 *                      不应直接从VFS层以上调用VOP_EACHOPEN - 使用vfs_open()打开inode。这会保持打开计数，以便在正确的时间调用VOP_LASTCLOSE。
 *
 *    vop_close       - 在文件的*最后*close()调用时调用。
 *
 *                      不应直接从VFS层以上调用VOP_LASTCLOSE - 使用vfs_close()关闭使用vfs_open()打开的inode。
 *
 *    vop_reclaim     - 当inode不再使用时调用。请注意，这可能在调用vop_lastclose之后相当长的时间。
 *
 *****************************************
 *
 *    vop_read        - 从文件读取数据到uio，读取偏移量在uio中指定，更新uio_resid以反映读取的数量，并更新uio_offset以匹配。
 *                      不允许在目录或符号链接上执行。
 *
 *    vop_getdirentry - 从目录中读取单个文件名到uio，根据uio中的偏移字段选择名称，并更新该字段。
 *                      与常规文件上的I/O不同，偏移字段的值在文件系统之外不被解释，因此不需要是字节计数。但是，uio_resid字段应以正常方式处理。
 *                      对于非目录对象，返回ENOTDIR。
 *
 *    vop_write       - 从uio向文件写入数据，在uio中指定偏移量，更新uio_resid以反映写入的数量，并更新uio_offset以匹配。
 *                      不允许在目录或符号链接上执行。
 *
 *    vop_ioctl       - 使用数据DATA对文件执行ioctl操作OP。数据的解释对每个ioctl都是特定的。
 *
 *    vop_fstat        - 返回有关文件的信息。指针是指向struct stat的指针；请参阅stat.h。
 *
 *    vop_gettype     - 返回文件类型。文件类型的值在sfs.h中定义。
 *
 *    vop_tryseek     - 检查是否可以将文件定位到指定位置。 （例如，在串行端口设备上，所有寻址都是非法的，而在大小固定的文件上可能也是如此。）
 *
 *    vop_fsync       - 强制将与此文件关联的任何脏缓冲区刷新到稳定存储。
 *
 *    vop_truncate    - 强制将文件的大小设置为传递的长度，丢弃任何多余的块。
 *
 *    vop_namefile    - 计算相对于文件系统根的文件路径并复制到指定的io缓冲区。无需处理非目录对象。
 *
 *****************************************
 *
 *    vop_creat       - 在传递的目录DIR中创建名为NAME的常规文件。如果布尔值EXCL为真，则如果文件已经存在，则失败；否则，如果存在文件，则使用现有文件。根据vop_lookup返回文件的inode。
 *
 *****************************************
 *
 *    vop_lookup      - 解析相对于传递的目录DIR的PATHNAME，并返回它引用的文件的inode。可能会破坏PATHNAME。应该增加返回的inode的引用计数。
 */

/*
 * Abstract operations on a inode.
 *
 * These are used in the form VOP_FOO(inode, args), which are macros
 * that expands to inode->inode_ops->vop_foo(inode, args). The operations
 * "foo" are:
 *
 *    vop_open        - Called on open() of a file. Can be used to
 *                      reject illegal or undesired open modes. Note that
 *                      various operations can be performed without the
 *                      file actually being opened.
 *                      The inode need not look at O_CREAT, O_EXCL, or 
 *                      O_TRUNC, as these are handled in the VFS layer.
 *
 *                      VOP_EACHOPEN should not be called directly from
 *                      above the VFS layer - use vfs_open() to open inodes.
 *                      This maintains the open count so VOP_LASTCLOSE can
 *                      be called at the right time.
 *
 *    vop_close       - To be called on *last* close() of a file.
 *
 *                      VOP_LASTCLOSE should not be called directly from
 *                      above the VFS layer - use vfs_close() to close
 *                      inodes opened with vfs_open().
 *
 *    vop_reclaim     - Called when inode is no longer in use. Note that
 *                      this may be substantially after vop_lastclose is
 *                      called.
 *
 *****************************************
 *
 *    vop_read        - Read data from file to uio, at offset specified
 *                      in the uio, updating uio_resid to reflect the
 *                      amount read, and updating uio_offset to match.
 *                      Not allowed on directories or symlinks.
 *
 *    vop_getdirentry - Read a single filename from a directory into a
 *                      uio, choosing what name based on the offset
 *                      field in the uio, and updating that field.
 *                      Unlike with I/O on regular files, the value of
 *                      the offset field is not interpreted outside
 *                      the filesystem and thus need not be a byte
 *                      count. However, the uio_resid field should be
 *                      handled in the normal fashion.
 *                      On non-directory objects, return ENOTDIR.
 *
 *    vop_write       - Write data from uio to file at offset specified
 *                      in the uio, updating uio_resid to reflect the
 *                      amount written, and updating uio_offset to match.
 *                      Not allowed on directories or symlinks.
 *
 *    vop_ioctl       - Perform ioctl operation OP on file using data
 *                      DATA. The interpretation of the data is specific
 *                      to each ioctl.
 *
 *    vop_fstat        -Return info about a file. The pointer is a 
 *                      pointer to struct stat; see stat.h.
 *
 *    vop_gettype     - Return type of file. The values for file types
 *                      are in sfs.h.
 *
 *    vop_tryseek     - Check if seeking to the specified position within
 *                      the file is legal. (For instance, all seeks
 *                      are illegal on serial port devices, and seeks
 *                      past EOF on files whose sizes are fixed may be
 *                      as well.)
 *
 *    vop_fsync       - Force any dirty buffers associated with this file
 *                      to stable storage.
 *
 *    vop_truncate    - Forcibly set size of file to the length passed
 *                      in, discarding any excess blocks.
 *
 *    vop_namefile    - Compute pathname relative to filesystem root
 *                      of the file and copy to the specified io buffer. 
 *                      Need not work on objects that are not
 *                      directories.
 *
 *****************************************
 *
 *    vop_creat       - Create a regular file named NAME in the passed
 *                      directory DIR. If boolean EXCL is true, fail if
 *                      the file already exists; otherwise, use the
 *                      existing file if there is one. Hand back the
 *                      inode for the file as per vop_lookup.
 *
 *****************************************
 *
 *    vop_lookup      - Parse PATHNAME relative to the passed directory
 *                      DIR, and hand back the inode for the file it
 *                      refers to. May destroy PATHNAME. Should increment
 *                      refcount on inode handed back.
 */
// mark 把sfs的接口提供给上层的vfs,这样更上层只要调用vfs的接口就可以了
struct inode_ops {
    unsigned long vop_magic;
    int (*vop_open)(struct inode *node, uint32_t open_flags);
    int (*vop_close)(struct inode *node);
    int (*vop_read)(struct inode *node, struct iobuf *iob);
    int (*vop_write)(struct inode *node, struct iobuf *iob);
    int (*vop_fstat)(struct inode *node, struct stat *stat);
    int (*vop_fsync)(struct inode *node);
    int (*vop_namefile)(struct inode *node, struct iobuf *iob);
    int (*vop_getdirentry)(struct inode *node, struct iobuf *iob);
    int (*vop_reclaim)(struct inode *node);
    int (*vop_gettype)(struct inode *node, uint32_t *type_store);
    int (*vop_tryseek)(struct inode *node, off_t pos);
    int (*vop_truncate)(struct inode *node, off_t len);
    int (*vop_create)(struct inode *node, const char *name, bool excl, struct inode **node_store);
    int (*vop_lookup)(struct inode *node, char *path, struct inode **node_store);
    int (*vop_ioctl)(struct inode *node, int op, void *data);
};

/*
 * Consistency check
 */
void inode_check(struct inode *node, const char *opstr);

#define __vop_op(node, sym)                                                                         \
    ({                                                                                              \
        struct inode *__node = (node);                                                              \
        assert(__node != NULL && __node->in_ops != NULL && __node->in_ops->vop_##sym != NULL);      \
        inode_check(__node, #sym);                                                                  \
        __node->in_ops->vop_##sym;                                                                  \
     })

#define vop_open(node, open_flags)                                  (__vop_op(node, open)(node, open_flags))
#define vop_close(node)                                             (__vop_op(node, close)(node))
#define vop_read(node, iob)                                         (__vop_op(node, read)(node, iob))
#define vop_write(node, iob)                                        (__vop_op(node, write)(node, iob))
#define vop_fstat(node, stat)                                       (__vop_op(node, fstat)(node, stat))
#define vop_fsync(node)                                             (__vop_op(node, fsync)(node))
#define vop_namefile(node, iob)                                     (__vop_op(node, namefile)(node, iob))
#define vop_getdirentry(node, iob)                                  (__vop_op(node, getdirentry)(node, iob))
#define vop_reclaim(node)                                           (__vop_op(node, reclaim)(node))
#define vop_ioctl(node, op, data)                                   (__vop_op(node, ioctl)(node, op, data))
#define vop_gettype(node, type_store)                               (__vop_op(node, gettype)(node, type_store))
#define vop_tryseek(node, pos)                                      (__vop_op(node, tryseek)(node, pos))
#define vop_truncate(node, len)                                     (__vop_op(node, truncate)(node, len))
#define vop_create(node, name, excl, node_store)                    (__vop_op(node, create)(node, name, excl, node_store))
#define vop_lookup(node, path, node_store)                          (__vop_op(node, lookup)(node, path, node_store))


#define vop_fs(node)                                                ((node)->in_fs)
#define vop_init(node, ops, fs)                                     inode_init(node, ops, fs)
#define vop_kill(node)                                              inode_kill(node)

/*
 * Reference count manipulation (handled above filesystem level)
 */
#define vop_ref_inc(node)                                           inode_ref_inc(node)
#define vop_ref_dec(node)                                           inode_ref_dec(node)
/*
 * Open count manipulation (handled above filesystem level)
 *
 * VOP_INCOPEN is called by vfs_open. VOP_DECOPEN is called by vfs_close.
 * Neither of these should need to be called from above the vfs layer.
 */
#define vop_open_inc(node)                                          inode_open_inc(node)
#define vop_open_dec(node)                                          inode_open_dec(node)


static inline int
inode_ref_count(struct inode *node) {
    return node->ref_count;
}

static inline int
inode_open_count(struct inode *node) {
    return node->open_count;
}

#endif /* !__KERN_FS_VFS_INODE_H__ */

