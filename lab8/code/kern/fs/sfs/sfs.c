#include <defs.h>
#include <sfs.h>
#include <error.h>
#include <assert.h>


/*
 * sfs_init - 在 disk0 上挂载 sfs
 *
 * 调用关系图:
 *   kern_init --> fs_init --> sfs_init
 */
/*//mark
在 sfs_fs.c 文件中的 sfs_do_mount 函数中，
完成了加载位于硬盘上的 SFS 文件系统的超级块 superblock 和 freemap 的工作。
这样，在内存中就有了 SFS 文件系统的全局信息。
*/
void
sfs_init(void) {
    int ret;
    if ((ret = sfs_mount("disk0")) != 0) {
        panic("failed: sfs: sfs_mount: %e.\n", ret);
    }
}

