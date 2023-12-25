#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <error.h>
#include <assert.h>

/*
 * get_device- Common code to pull the device name, if any, off the front of a
 *             path and choose the inode to begin the name lookup relative to.
 */
/*
如果输入的是一个相对路径，那么就试图从当前进程的filesp信息里面拿到当前进程的工作路径作为返回给这个inode，然后直接返回一个-E_NOENT。
如果来的是一个device:path格式的，那么找到这个device的根节点同时将path参数的device:部分切掉。
如果找的到根那么这个部分返回0.剩下的情况如果是/开头那么就找到系统根目录，
否则就是找到当前的路径（可能是个设备，所以拿到它的fs然后返回）
*/

static int
get_device(char *path, char **subpath, struct inode **node_store) {
    int i, slash = -1, colon = -1;
    for (i = 0; path[i] != '\0'; i ++) {
        if (path[i] == ':') { colon = i; break; }
        if (path[i] == '/') { slash = i; break; }
    }
    if (colon < 0 && slash != 0) {
        /* *
         * 斜杠之前没有冒号，因此未指定设备名称，而斜杠不是前导的或也不存在，
         * 因此这是一个相对路径或只是一个裸文件名。从当前目录开始，将整个内容用作子路径。
         * */
        *subpath = path;
        return vfs_get_curdir(node_store);//把当前目录的inode存到node_store
    }
    if (colon > 0) {
        /* device:path - get root of device's filesystem */
        path[colon] = '\0';

        /* device:/path - skip slash, treat as device:path */
        while (path[++ colon] == '/');
        *subpath = path + colon;
        return vfs_get_root(path, node_store);
    }

    /* *
     * we have either /path or :path
     * /path is a path relative to the root of the "boot filesystem"
     * :path is a path relative to the root of the current filesystem
     * */
    int ret;
    if (*path == '/') {
        if ((ret = vfs_get_bootfs(node_store)) != 0) {
            return ret;
        }
    }
    else {
        assert(*path == ':');
        struct inode *node;
        if ((ret = vfs_get_curdir(&node)) != 0) {
            return ret;
        }
        /* The current directory may not be a device, so it must have a fs. */
        assert(node->in_fs != NULL);
        *node_store = fsop_get_root(node->in_fs);
        vop_ref_dec(node);
    }

    /* ///... or :/... */
    while (*(++ path) == '/');
    *subpath = path;
    return 0;
}

/*
 * vfs_lookup - get the inode according to the path filename
 */
//vfs_lookup函数是一个针对目录的操作函数，它会调用vop_lookup函数来找到SFS文件系统中的目录下的文件

int
vfs_lookup(char *path, struct inode **node_store) {
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {//找到根目录"/"对应的inode,实际上get_device会继续调用vfs_get_bootfs
                                                      //这个inode就是位于vfs.c中的inode变量bootfs_node。
        return ret;                                   //这个变量在init_main函数（位于kern/process/proc.c）执行时获得了赋值。
    }
    if (*path != '\0') {//通过调用vop_lookup函数来查找到根目录“/”下对应文件sfs_filetest1的索引节点，如果找到就返回此索引节点。
        ret = vop_lookup(node, path, node_store);//这个会再去调用这个node对应的sys_lookup,如果是设备node那就去找设备的lookup,这个宏对应哪个函数在这个node创建时候就初始化好了
        vop_ref_dec(node);
        return ret;
    }
    *node_store = node;
    return 0;
}

/*
 * vfs_lookup_parent - Name-to-vnode translation.
 *  (In BSD, both of these are subsumed by namei().)
 */
/*
 * vfs_lookup_parent - 名称到 vnode 的转换。
 * （在 BSD 中，这两者都由 namei() 函数代替。）
 */
int
vfs_lookup_parent(char *path, struct inode **node_store, char **endp){
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return ret;
    }
    *endp = path;
    *node_store = node;
    return 0;
}
