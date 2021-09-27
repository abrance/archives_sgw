#ifndef SCANDIR_H
#define SCANDIR_H

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

/* 路径名的最长长度 */
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN (4096)
#endif

/* 写入文件的块大小 */
#ifndef BLOCK_SIZE
#define BLOCK_SIZE (8192)
#endif

/* 备份目录名 */
#ifndef BKDIRNAME
#define BKDIRNAME ".#__hide.youcantseeme__#"
#endif

extern int copy_file(const char *old_path, const char *new_path);
extern int set_rename_path(
    char *path,
    const char *oldpath, const char *newpath, const char *bakpath);

/*
 * 文件更新
 *
 * 原文件作为新增的备份，以原文件作为新增备份。成功后返回更新后的文件描
 * 述符（fd），备份的文件路径名，备份的文件路径名的时间戳以服务器的时间
 * 为准。备份文件名以原文件和时间组成。
 *
 * 成功返回文件描述符，失败返回 -1。
 */
extern int file_backup_update(const char *oldpath, char *bakpath);

/*
 * 文件粉碎
 *
 * 粉碎原文件和所有相关的备份文件。粉碎备份文件时，要扫描目录找到所有相
 * 关的备份文件，对这些备份文件做粉碎操作。
 *
 * 单个文件的粉碎操作是将随机内容写入整个文件，然后删除文件。由于对文件
 * 加密再写入文件，也只是改写文件的内容，和将随机的内容写入文件是一样的
 * 效果。所以，先将文件加密是冗余而且费时的。
 */
#define CRUSH_FILE (0x0001)
#define CRUSH_BACK (0x0010)
extern int file_backup_crush(const char *filepath, int how);

/*
 * 文件删除
 *
 * 删除文件，将文件移动到备份
 */
extern int file_backup_delete(const char *oldpath, char *newpath, int newlen);

struct bkrename {
    const char *origname;       /* 源文件名 */
    const char *new_dirpath;    /* 新目录路径名 */
    const char *new_filename;   /* 重命名的文件名 */
};

/*
 * 路径重命名
 *
 * 文件或目录重命名，即是路径重命名。重命名原路径名和所有相关的备份文件。
 * 重命名备份路径名时，要扫描目录找到所有相关的备份路径，对这些备份路径
 * 做重命名操作。
 *
 * 当备份路径名所在的目录不存在，将会创建不存在的目录。可以指定重命名单
 * 个路径名，和与路径名相关的所有路径名。
 */
#define BK_RENAME_FILE (0x0001)
#define BK_RENAME_BACK (0x0010)
extern int file_backup_rename(
    const char *oldpath, const char *newpath, int how);

/*
 * 复制文件，将复制文件作为副本
 */
int file_backup_copy(const char *oldpath, char *newpath, int newlen);

/*
 * 目录删除
 *
 * 目录被删除，将目录移动到备份。将目录移动后的最终路径名返回给调用者。
 */
extern int dir_backup_delete(const char *dirpath, char *bakpath);

/*
 * 目录被重命名
 *
 * 直接修改目录名，不需要创建备份。
 */
extern int dir_backup_rename(const char *oldpath, const char *newpath);

#endif  /* SCANDIR_H */
