/*
 * 模拟实现在存储网关里实现文件创建，文件修改，文件重命名，文件粉碎的
 * 功能。
 *
 * 这个实现不记录一个文件的备份文件列表，实现各个操作的思路：
 *
 * 文件创建：upload
 * 文件修改：rename, upload
 * 文件重命名：rename, scan, rename backup
 * 文件粉碎：crush, scan, crush backup
 *
 * 最简单最直接的方案，如果目录下的文件太多，扫描时会耗时很长时间。备
 * 份文件的命名规则：filename.year.month.day.hour.minute.second.microsecond
 *
 * 例如：filename.2020.03.20.164815.876
 *
 * 在文件所在目录创建一个备份目录，当前目录的所有备份文件都放在这个备
 * 份目录中。备份目录名称定为“.#__hide.youcantseeme__#”。
 *
 * mike
 * 2020-03-20
 */

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

#include "mt_log.h"

#define MAX_PATH_LEN (4096)
#define BLOCK_SIZE (8192)
#define BKDIRNAME ".#__hide.youcantseeme__#"

int write_zero_file(const char *filepath, off_t filesize)
{
    int fd = open(filepath, O_CREAT | O_TRUNC | O_RDWR,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd >= 0) {
        int rc, retcode;
        char buffer[BLOCK_SIZE];
        memset(buffer, 0, BLOCK_SIZE);
        off_t leftsize = filesize;
        while (leftsize > 0) {
            int blocksize = leftsize >= BLOCK_SIZE ? BLOCK_SIZE : leftsize;
            int n = write(fd, buffer, blocksize);
            if (n < 0) {
                log_error("write failed: fd %d, size %lld, %s",
                       fd, (long long int)filesize, strerror(errno));
                retcode = -1;
                goto out;
            } else {
                leftsize = leftsize - blocksize;
            }
        }
        assert(leftsize == 0);
        retcode = 0;
    out:
        rc = close(fd);
        if (rc == 0) {
            return retcode;
        } else {
            log_error("close failed: fd %d", fd);
            return -1;
        }
    } else {
        log_error("write_file failed: filepath %s, %s",
               filepath, strerror(errno));
        return -1;
    }
}

int read_zero_file(const char *filepath, off_t len)
{
    int retcode;

    int fd = open(filepath, O_RDONLY);
    assert(fd >= 0);
    char zero[BLOCK_SIZE];
    memset(zero, 0, BLOCK_SIZE);
    char buffer[BLOCK_SIZE];
    off_t leftsize = len;
    while (leftsize > 0) {
        int size = leftsize >= BLOCK_SIZE ? BLOCK_SIZE : leftsize;
        int n = read(fd, buffer, size);
        if (n > 0) {
            int rc1 = memcmp(buffer, zero, n);
            if (rc1 == 0) {
                leftsize = leftsize - n;
                retcode = 0;
            } else {
                retcode = -1;
                goto out;
            }
        } else {
            retcode = -1;
            goto out;
        }
    }
    retcode = 0;

    int rc2;
out:
    rc2 = close(fd);
    assert(rc2 == 0);
    return retcode;
}

int mkdirs(const char *dirpath)
{
    char path[MAX_PATH_LEN];
    char *p;

    if (dirpath[0] == '/') {
        sprintf(path, "%s/", dirpath);
        p = path + 1;
    } else {
        char curdirpath[MAX_PATH_LEN];
        char *cwd = getcwd(curdirpath, MAX_PATH_LEN);
        if (cwd) {
            int len = sprintf(path, "%s/", curdirpath);
            p = path + len;
            sprintf(path+len, "%s", dirpath);
        } else {
            log_error("getcwd failed: %s", strerror(errno));
            return -1;
        }
    }

    while (*p != '\0') {
        if (*p == '/') {
            *p = '\0';
            int rc = mkdir(path, 0755);
            *p = '/';
            if (rc == 0) {
                /* 创建目录成功，继续处理 */
            } else {
                int ec = errno;
                if (ec == EEXIST) {
                    /* 目录已存在，继续处理 */
                    *p = '/';
                } else {
                    log_error("mkdir failed: path %s, %s", path, strerror(ec));
                    return -1;
                }
            }
        } else {
            /* 不是目录分隔符，跳过 */
        }
        p = p + 1;
    }
    return 0;
}

int filepath_create(const char *filepath, off_t filesize)
{
    char path1[MAX_PATH_LEN];
    snprintf(path1, MAX_PATH_LEN, "%s", filepath);
    char *dirpath = dirname(path1);
    assert(dirpath);
    int rc1 = mkdirs(dirpath);
    if (rc1 == 0) {
        int fd = open(filepath,
                      O_CREAT|O_TRUNC|O_RDWR,
                      S_IRUSR|S_IWUSR|S_IRGRP);
        if (fd >= 0) {
            char buffer[BLOCK_SIZE];
            off_t leftsize = filesize;
            while (leftsize > 0) {
                int size = leftsize >= BLOCK_SIZE ? BLOCK_SIZE : leftsize;
                int n = write(fd, buffer, size);
                assert(n == size);
                leftsize = leftsize - n;
            }
            assert(leftsize == 0);
            int rc2 = close(fd);
            if (rc2 == 0) {
                return 0;
            } else {
                log_error("close failed: fd %d, filepath %s",
                       fd, filepath);
                return -1;
            }
        } else {
            log_error("open failed: filepath %s", filepath);
            return -1;
        }
    } else {
        log_error("mkdirs failed: dirpath %s", dirpath);
        return -1;
    }
}

/* 备份的时间，是以客户端传过来的为准，还是以服务端的时间为准？ */
int make_backup_name(char *bakname, const char *srcname, struct timeval *tv)
{
    /* 暂时实现以服务端的时间为准 */
    time_t epoch = (time_t)tv->tv_sec;
    struct tm *tm = localtime(&epoch);
    if (tm) {
        if (srcname[0] == '/') {
            /* 绝对路径 */
            char path1[MAX_PATH_LEN], path2[MAX_PATH_LEN];
            snprintf(path1, MAX_PATH_LEN, "%s", srcname);
            snprintf(path2, MAX_PATH_LEN, "%s", srcname);
            char *dname = dirname(path1);
            char *bname = basename(path2);
            sprintf(bakname, "%s/%s/%s.%d.%02d.%d.%d%d%d.%d",
                    dname, BKDIRNAME, bname,
                    1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec,
                    (int)tv->tv_usec);
        } else {
            /* 相对路径 */
            sprintf(bakname, "%s/%s.%d.%02d.%d.%d%d%d.%d",
                    BKDIRNAME, srcname,
                    1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec,
                    (int)tv->tv_usec);
        }
        return 0;
    } else {
        log_error("localtime failed: %s", strerror(errno));
        return -1;
    }
}

/*
 * 将 oldpath 移动到 newpath，如果 newpath 所在的目录不存在，则创建不
 * 存在的目录。
 */
int move_file(const char *oldpath, const char *newpath)
{
    char path1[MAX_PATH_LEN];
    sprintf(path1, "%s", newpath);
    char *dname = dirname(path1);
    int rc1 = mkdirs(dname);
    if (rc1 == 0) {
        int rc2 = rename(oldpath, newpath);
        if (rc2 == 0) {
            return 0;
        } else {
            log_error("rename failed: oldpath %s, newpath %s, %s",
                   oldpath, newpath, strerror(errno));
            return -1;
        }
    } else {
        log_error("mkdirs failed: dirname %s", dname);
        return -1;
    }
}

int copy_file(const char *old_path, const char *new_path)
{
    char path1[MAX_PATH_LEN];
    sprintf(path1, "%s", new_path);
    char *dname = dirname(path1);
    int rc = mkdirs(dname);
    if (rc == 0) {
        /* 创建目录成功，继续处理，进行文件复制 */
    } else {
        log_error("mkdirs failed: dirname %s", dname);
        return -1;
    }

    int src = open(old_path, O_RDONLY);
    if (src >= 0) {
        int dst = open(new_path, O_CREAT|O_RDWR|O_TRUNC,
                       S_IRUSR|S_IWUSR|S_IRGRP);
        if (dst >= 0) {
            struct stat s;
            int rc = fstat(src, &s);
            if (rc == 0) {
                char buffer[BLOCK_SIZE];
                off_t leftsize = s.st_size;
                while (leftsize > 0) {
                    int x = read(src, buffer, BLOCK_SIZE);
                    if (x > 0) {
                        int y = write(dst, buffer, x);
                        if (y == x) {
                            leftsize = leftsize - x;
                        } else {
                            log_error("write failed: %d want, %d write", x, y);
                            close(src);
                            close(dst);
                            return -1;
                        }
                    } else {
                        log_error("read failed: %d want, %d read",
                               BLOCK_SIZE, x);
                        close(src);
                        close(dst);
                        return -1;
                    }
                }
                assert(leftsize == 0);
                close(src);
                close(dst);
                return 0;
            } else {
                log_error("fstat failed: %s", strerror(errno));
                close(src);
                close(dst);
                return -1;
            }
        } else {
            log_error("open failed: new_path %s, %s",
                   new_path, strerror(errno));
            close(src);
            return -1;
        }
    } else {
        log_error("open failed: old_path %s, %s",
               old_path, strerror(errno));
        return -1;
    }
}

int file_backup_update(const char *oldpath, char *bakpath)
{
    int rc;

    struct timeval tv;
    rc = gettimeofday(&tv, NULL);
    if (rc == 0) {
        char realpath[MAX_PATH_LEN];
        rc = make_backup_name(realpath, oldpath, &tv);
        if (rc == 0) {
            rc = move_file(oldpath, realpath);
            if (rc == 0) {
                sprintf(bakpath, "%s", realpath);
                return 0;
            } else {
                log_error("move_file failed: oldpath %s, newpath %s",
                       oldpath, realpath);
                return -1;
            }
        } else {
            log_error("make_backup_name failed: oldpath %s", oldpath);
            return -1;
        }
    } else {
        log_error("gettimeofday failed: %s", strerror(errno));
        return -1;
    }
}

/*
 * 对文件描述符 fd 指向的文件，进行内容覆盖写。由于对文件加密再写入文
 * 件，也只是改写文件的内容，和将随机的内容写入文件是一样的效果。所以，
 * 将加密后的内容再写入文件不是必须的。
 */
int __file_crush(int fd)
{
    struct stat s;
    int rc1 = fstat(fd, &s);
    if (rc1 == 0) {
        char buffer[BLOCK_SIZE];
        off_t leftsize = s.st_size;
        while (leftsize > 0) {
            int blocksize = leftsize >= BLOCK_SIZE ? BLOCK_SIZE : leftsize;
            int n = write(fd, buffer, blocksize);
            if (n < 0) {
                log_error("write failed: fd %d, size %d, %s",
                       fd, blocksize, strerror(errno));
                return -1;
            } else {
                leftsize = leftsize - blocksize;
            }
        }
        assert(leftsize == 0);
        int rc2 = fdatasync(fd);
        if (rc2 == 0) {
            return 0;
        } else {
            log_error("fdatasync failed: fd %d, %s",
                   fd, strerror(errno));
            return -1;
        }
    } else {
        log_error("fstat failed: fd %d, %s", fd, strerror(errno));
        return -1;
    }
}

int file_crush(int fd, int nr_crush)
{
    int i;
    int rc1;
    for (i = 0; i < nr_crush; i++) {
        int rc2 = lseek(fd, 0, SEEK_SET);
        if (rc2 == 0) {
            rc1 = __file_crush(fd);
            if (rc1 == 0) {
                /* 文件粉碎一次成功 */
            } else {
                log_error("__file_crush failed: fd %d", fd);
                return -1;
            }
        } else {
            log_error("lseek failed: fd %d, offset 0, %s",
                   fd, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* 将随机内容写入整个文件，然后删除文件 */
int filepath_crush(const char *filepath, int nr_crush)
{
    assert(nr_crush >= 1);
    int fd = open(filepath, O_RDWR);
    if (fd >= 0) {
        int retcode;
        int rc1 = file_crush(fd, nr_crush/*粉碎文件多少次*/);
        if (rc1 == 0) {
            int rc2 = remove(filepath);
            if (rc2 == 0) {
                retcode = 0;
                goto out;
            } else {
                log_error("remove failed: filepath %s, %s",
                       filepath, strerror(errno));
                retcode = -1;
                goto out;
            }
        } else {
            log_error("file_crush failed: fd %d, filepath %s, %d times",
                   fd, filepath, nr_crush);
            retcode = -1;
            goto out;
        }
        int rc3;
    out:
        rc3 = close(fd);
        if (rc3 == 0) {
            return retcode;
        } else {
            log_error("close failed: fd %d, filepath %s, %s",
                   fd, filepath, strerror(errno));
            return -1;
        }
    } else {
        log_error("open failed: filepath %s, %s",
               filepath, strerror(errno));
        return -1;
    }
}

int path_split(const char *filepath, char *dirpath, char *filename)
{
    if (filepath[0] == '/') {
        /* 文件名是绝对路径 */
        char tmppath1[MAX_PATH_LEN], tmppath2[MAX_PATH_LEN];
        snprintf(tmppath1, MAX_PATH_LEN, "%s", filepath);
        char *dname = dirname(tmppath1);
        snprintf(tmppath2, MAX_PATH_LEN, "%s", filepath);
        char *fname = basename(tmppath2);
        if (strcmp(fname, ".") == 0  ||
            strcmp(fname, "..") == 0 ||
            strcmp(fname, "/") == 0)
        {
            log_error("filepath %s has no filename!", filepath);
            return -1;
        } else {
            sprintf(dirpath,  "%s", dname);
            sprintf(filename, "%s", fname);
            return 0;
        }
    } else {
        /* 文件名是相对路径 */
        char curdirpath[MAX_PATH_LEN];
        char *cwd = getcwd(curdirpath, MAX_PATH_LEN);
        if (cwd) {
            char tmppath1[MAX_PATH_LEN], tmppath2[MAX_PATH_LEN];
            snprintf(tmppath1, MAX_PATH_LEN, "%s/%s", curdirpath, filepath);
            char *dname = dirname(tmppath1);
            snprintf(tmppath2, MAX_PATH_LEN, "%s/%s", curdirpath, filepath);
            char *fname = basename(tmppath2);
            if (strcmp(fname, ".") == 0  ||
                strcmp(fname, "..") == 0 ||
                strcmp(fname, "/") == 0)
            {
                log_error("filepath %s has no filename!", filepath);
                return -1;
            } else {
                sprintf(dirpath,  "%s", dname);
                sprintf(filename, "%s", fname);
                return 0;
            }
        } else {
            log_error("getcwd failed: %s", strerror(errno));
            return -1;
        }
    }
}

/* 对文件路径进行判断，如果满足条件则粉碎该文件 */
int filepath_crush_cb(const char *path, const struct stat *stat, void *user)
{
    if (S_ISREG(stat->st_mode)) {
        char dirpath[MAX_PATH_LEN];
        char filename[MAX_PATH_LEN];
        int rc1 = path_split(path, dirpath, filename);
        if (rc1 == 0) {
            const char *origname = (const char *)user;
            int namelen = strlen(origname); /* 不计算最后的'\0'结束符 */
            if (strncmp(filename, origname, namelen) == 0) {
                const int max_nr_crush = 3;
                return filepath_crush(path, max_nr_crush);
            } else {
                /* log_error("filename: %s, origname: %s", filename, origname); */
                /* log_error("skip filepath %s: not a target", path); */
                return 0; /* 不是目标文件，跳过 */
            }
        } else {
            log_error("path_split failed: filepath %s", path);
            return -1;
        }
    } else {
        return 0; /* 不是普通文件，跳过 */
    }
}

int walk_tree(const char *dirpath,
              int (*fn)(const char *path,
                        const struct stat *stat,
                        void *user),
              void *opaque)
{
    DIR *dirp = opendir(dirpath);
    if (dirp) {
        struct dirent entry;
        struct dirent *result;
        int retcode;
        for (;;) {
            int rc1 = readdir_r(dirp, &entry, &result);
            if (rc1 == 0) {
                if (result) {
                    if (strncmp(entry.d_name, ".", 2) == 0 ||
                        strncmp(entry.d_name, "..", 3) == 0 ||
                        strncmp(entry.d_name, BKDIRNAME, strlen(BKDIRNAME)) == 0)
                    {
                        /*
                         * 跳过当前目录和上一级目录，如果不跳过会造成
                         * 无限循环
                         */
                    } else {
                        char filepath[MAX_PATH_LEN];
                        snprintf(filepath, MAX_PATH_LEN, "%s/%s", dirpath, entry.d_name);
                        struct stat s;
                        int rc2 = stat(filepath, &s);
                        if (rc2 == 0) {
                            int rc3;
                            if (S_ISREG(s.st_mode)) {
                                rc3 = fn(filepath, &s, opaque);
                                if (rc3 == 0) {
                                    /* 处理文件成功，继续处理 */
                                } else {
                                    log_error("callback failed: filepath %s", filepath);
                                    retcode = -1;
                                    goto out;
                                }
                            } else if (S_ISDIR(s.st_mode)) {
                                rc3 = walk_tree(filepath, fn, opaque);
                                if (rc3 == 0) {
                                    /* 遍历子目录成功，继续处理 */
                                } else {
                                    log_error("walk_tree failed: dirpath %s", filepath);
                                    retcode = -1;
                                    goto out;
                                }
                            } else {
                                /* 不是文件，也不是目录，跳过 */
                            }
                        } else {
                            log_error("stat failed: filepath %s, %s",
                                   filepath, strerror(errno));
                            retcode = -1;
                            goto out;
                        }
                    }
                } else {
                    /* 遍历目录完成 */
                    retcode = 0;
                    goto out;
                }
            } else {
                log_error("readdir failed: dirpath %s, %s",
                       dirpath, strerror(errno));
                retcode = -1;
                goto out;
            }
        }
        int rc4;
    out:
        rc4 = closedir(dirp);
        if (rc4 == 0) {
            return retcode;
        } else {
            log_error("closedir failed: dirpath %s, %s",
                   dirpath, strerror(errno));
            return -1;
        }
    } else {
        log_error("opendir failed: dirpath %s, %s",
               dirpath, strerror(errno));
        return -1;
    }
}

/*
 * 粉碎原文件和所有相关的备份文件。粉碎备份文件时，要扫描目录找到所有
 * 相关的备份文件，对这些备份文件做粉碎操作。
 */
#define CRUSH_FILE (0x0001)
#define CRUSH_BACK (0x0010)
int file_backup_crush(const char *filepath, int how)
{
    assert(how != 0);
    assert(filepath);

    const int max_nr_crush = 3;
    char dirpath[MAX_PATH_LEN];
    char filename[MAX_PATH_LEN];
    int rc1 = path_split(filepath, dirpath, filename);
    if (rc1 == 0) {
        int retcode;
        char realpath[MAX_PATH_LEN];
        snprintf(realpath, MAX_PATH_LEN, "%s/%s", dirpath, filename);

        if (how & CRUSH_FILE) {
            /* 粉碎文件 */
            int rc2 = filepath_crush(realpath, max_nr_crush);
            if (rc2 == 0) {
                /*
                 * 粉碎文件成功，继续处理看看是否需要粉碎该文件的所有
                 * 备份
                 */
                retcode = 0;
            } else {
                log_error("filepath_crush failed: realpath %s", realpath);
                return -1;
            }
        } else {
            /* 不用粉碎原文件 */
        }

        if (how & CRUSH_BACK) {
            /* 粉碎备份目录下的所有该文件的备份文件 */
            return walk_tree(dirpath, filepath_crush_cb, filename);
        } else {
            /* 不用粉碎该文件的备份 */
        }
        return retcode;
    } else {
        log_error("path_split failed: filepath %s", filepath);
        return -1;
    }
}

/*
 * 复制文件，将复制文件作为副本
 */
int file_backup_copy(const char *oldpath, char *newpath, int newlen)
{
    struct timeval tv;
    int rc;

    rc = gettimeofday(&tv, NULL);
    if (rc == 0) {
        char backpath[MAX_PATH_LEN];
        rc = make_backup_name(backpath, oldpath, &tv);
        if (rc == 0) {
            rc = copy_file(oldpath, backpath);
            if (rc == 0) {
                snprintf(newpath, newlen, "%s", backpath);
                return 0;
            } else {
                log_error("copy_file failed: oldpath %s, newpath %s",
                       oldpath, backpath);
                return -1;
            }
        } else {
            log_error("make_backup_name failed: filepath %s", oldpath);
            return -1;
        }
    } else {
        log_error("gettimeofday failed: %s", strerror(errno));
        return -1;
    }
    return -1;
}

/*
 * 删除文件，将文件移动到备份
 */
int file_backup_delete(const char *oldpath, char *newpath, int newlen)
{
    /* 获取当前服务器时间 */
    struct timeval tv;
    int rc1 = gettimeofday(&tv, NULL);
    if (rc1 == 0) {
        /* 构造备份文件名 */
        char backpath[MAX_PATH_LEN];
        int rc2 = make_backup_name(backpath, oldpath, &tv);
        if (rc2 == 0) {
            /* 移动文件到备份 */
            int rc3 = move_file(oldpath, backpath);
            if (rc3 == 0) {
                snprintf(newpath, newlen, "%s", backpath);
                return 0;
            } else {
                log_error("move_file failed: filepath %s, backpath %s",
                       oldpath, backpath);
                return -1;
            }
        } else {
            log_error("make_backup_name failed: filepath %s", oldpath);
            return -1;
        }
    } else {
        log_error("gettimeofday failed: %s", strerror(errno));
        return -1;
    }
}

/* 将备份文件路径转换为重命名后的文件路径 */
int set_rename_path(
    char *path,
    const char *oldpath,
    const char *newpath,
    const char *bakpath)
{
    // oldpath: /123/aaa
    // newpath: /789/bbb
    // bakpath: /123/.#__.hide.youcantseeme__#/aaa.2020.03.20.134623.123
    // path: /789/.#__.hide.youcantseeme__#/bbb.2020.03.20.134623.123
    int rc;
    char oldpath_dirpath[MAX_PATH_LEN], oldpath_basename[MAX_PATH_LEN];
    char newpath_dirpath[MAX_PATH_LEN], newpath_basename[MAX_PATH_LEN];
    char bakpath_dirpath[MAX_PATH_LEN], bakpath_basename[MAX_PATH_LEN];

    rc = path_split(oldpath, oldpath_dirpath, oldpath_basename);
    if (rc == 0) {
        /* pass */
    } else {
        log_error("path_split failed: oldpath %s", oldpath);
        return -1;
    }
    rc = path_split(newpath, newpath_dirpath, newpath_basename);
    if (rc == 0) {
        /* pass */
    } else {
        log_error("path_split failed: newpath %s", newpath);
        return -1;
    }
    rc = path_split(bakpath, bakpath_dirpath, bakpath_basename);
    if (rc == 0) {
        /* pass */
    } else {
        log_error("path_split: bakpath %s", bakpath);
        return -1;
    }

    int bakpath_basename_len = strlen(oldpath_basename);
    sprintf(path, "%s/%s/%s%s",
            newpath_dirpath, BKDIRNAME, newpath_basename,
            bakpath_basename + bakpath_basename_len);
    return 0;
}

struct bkrename {
    const char *origname;       /* 源文件名 */
    const char *new_dirpath;    /* 新目录路径名 */
    const char *new_filename;   /* 重命名的文件名 */
};

/* 回调函数，判断路径 path 是否需要重命名 */
int file_backup_rename_cb(const char *backpath, const struct stat *stat,
                          void *user)
{
    if (S_ISREG(stat->st_mode)) {
        char back_dirpath[MAX_PATH_LEN];
        char back_filename[MAX_PATH_LEN];
        int rc1 = path_split(backpath, back_dirpath, back_filename);
        if (rc1 == 0) {
            struct bkrename *bk = (struct bkrename *)user;
            int origname_len = strlen(bk->origname);
            if (strncmp(back_filename, bk->origname, origname_len) == 0) {
                char new_fullpath[MAX_PATH_LEN];
                snprintf(new_fullpath, MAX_PATH_LEN, "%s/%s/%s%s",
                         bk->new_dirpath, BKDIRNAME,
                         bk->new_filename, back_filename + origname_len);
                int rc2 = rename(backpath, new_fullpath);
                if (rc2 == 0) {
                    return 0; /* 重命名文件成功 */
                } else {
                    log_error("rename failed: oldpath %s, newpath %s",
                           backpath, new_fullpath);
                    return -1;
                }
            } else {
                return 0; /* 备份文件不是目标文件，跳过 */
            }
        } else {
            log_error("path_split failed: backpath: %s", backpath);
            return -1;
        }
    } else {
        return 0; /* 不是普通文件，跳过 */
    }
}

/*
 * 重命名原文件和所有相关的备份文件。重命名备份文件时，要扫描目录找到
 * 所有相关的备份文件，对这些备份文件做重命名操作
 */
#define BK_RENAME_FILE (0x0001)
#define BK_RENAME_BACK (0x0010)
int file_backup_rename(const char *oldpath, const char *newpath, int how)
{
    char old_dirpath[MAX_PATH_LEN], old_filename[MAX_PATH_LEN];
    int rc1 = path_split(oldpath, old_dirpath, old_filename);
    if (rc1 == 0) {
        /* 分割 oldpath 目录路径和文件名成功，继续处理 */
    } else {
        log_error("path_split failed: oldpath %s", oldpath);
        return -1;
    }

    char new_dirpath[MAX_PATH_LEN], new_filename[MAX_PATH_LEN];
    int rc2 = path_split(newpath, new_dirpath, new_filename);
    if (rc2 == 0) {
        /* 分割 newpath 目录路径和文件名成功 */
        int rc3 = mkdirs(new_dirpath);
        if (rc3 == 0) {
            /* 创建目录成功，继续执行 */
        } else {
            log_error("mkdirs failed: new_dirpath %s", new_dirpath);
            return 0;
        }
    } else {
        log_error("path_split failed: newpath %s", newpath);
        return -1;
    }

    int retcode;
    if (how & BK_RENAME_FILE) {
        int rc3 = rename(oldpath, newpath);
        if (rc3 == 0) {
            /* 重命名原文件成功，继续处理可能的重命名备份 */
            retcode = 0;
        } else {
            log_error("rename failed: oldpath: %s, newpath %s, %s",
                   oldpath, newpath, strerror(errno));
            return -1;
        }
    } else {
        /* 调用者指定无需重命名原文件，算是成功 */
        retcode = 0;
    }

    if (how & BK_RENAME_BACK) {
        char new_backdir[MAX_PATH_LEN];
        snprintf(new_backdir, MAX_PATH_LEN, "%s/%s", new_dirpath, BKDIRNAME);
        int rc4 = mkdirs(new_backdir);
        if (rc4 == 0) {
            char old_backdir[MAX_PATH_LEN];
            snprintf(old_backdir, MAX_PATH_LEN, "%s/%s", old_dirpath, BKDIRNAME);
            struct bkrename bk;
            bk.origname = old_filename;
            bk.new_dirpath = new_dirpath;
            bk.new_filename = new_filename;
            return walk_tree(old_backdir, file_backup_rename_cb, &bk);
        } else {
            log_error("mkdirs failed: new_back_dirpath %s", new_backdir);
            return -1;
        }
    } else {
        /* 返回码在前面经过的分支一定是已经初始化了 */
        return retcode;
    }
}

/*
 * 目录被删除，将目录移动到备份。将目录移动后的最终路径名返回给调用者。
 */
int dir_backup_delete(const char *dirpath, char *bakpath)
{
    struct timeval tv;
    int rc;

    rc = gettimeofday(&tv, NULL);
    if (rc == 0) {
        char dname[MAX_PATH_LEN];
        char bname[MAX_PATH_LEN];
        rc = path_split(dirpath, dname, bname);
        if (rc == 0) {
            char backdir[MAX_PATH_LEN];
            snprintf(backdir, MAX_PATH_LEN, "%s/%s",
                     dname, BKDIRNAME);
            rc = mkdirs(backdir);
            if (rc == 0) {
                char realpath[MAX_PATH_LEN];
                rc = make_backup_name(realpath, dirpath, &tv);
                if (rc == 0) {
                    rc = rename(dirpath, realpath);
                    if (rc == 0) {
                        sprintf(bakpath, "%s", realpath);
                        return 0;
                    } else {
                        log_error("rename failed: dirpath %s, realpath %s, %s",
                               dirpath, realpath, strerror(errno));
                        return -1;
                    }
                } else {
                    log_error("make_backup_name failed: dirpath %s", dirpath);
                    return -1;
                }
            } else {
                log_error("mkdirs failed: backdir %s", backdir);
                return -1;
            }
        } else {
            log_error("path_split failed: dirpath %s", dirpath);
            return -1;
        }
    } else {
        log_error("gettimeofday failed: %s", strerror(errno));
        return -1;
    }
}

/*
 * 目录被重命名。直接修改目录名。
 */
int dir_backup_rename(const char *oldpath, const char *newpath)
{
    int rc = rename(oldpath, newpath);
    if (rc == 0) {
        return 0;
    } else {
        log_error("rename failed: oldpath %s, newpath %s, %s",
               oldpath, newpath, strerror(errno));
        return -1;
    }
}

////////////////////////////////////////////////////////////////////////
// 测试用例
////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_UNITTEST

void test_mkdirs(void)
{
    printf("test_mkdirs: ");

    struct stat s;

    const char *dirpath1 = "/tmp/aaa";
    int rc1 = mkdirs(dirpath1);
    assert(rc1 == 0);
    int rc2 = stat(dirpath1, &s);
    assert(rc2 == 0);
    assert(S_ISDIR(s.st_mode));

    const char *dirpath2 = "/tmp/aaa/bbb/ccc";
    int rc3 = mkdirs(dirpath2);
    assert(rc3 == 0);
    int rc4 = stat(dirpath2, &s);
    assert(rc4 == 0);
    assert(S_ISDIR(s.st_mode));

    rmdir("/tmp/aaa/bbb/ccc");
    rmdir("/tmp/aaa/bbb");
    rmdir("/tmp/aaa");

    printf("success\n");
}

void test_filepath_create(void)
{
    printf("test_filepath_create: ");

    srandom(time(NULL));
    const char *filepath = "/tmp/filepath_create/test123";
    off_t filesize = random() % 123456;
    int rc1 = filepath_create(filepath, filesize);
    assert(rc1 == 0);

    struct stat s;
    int rc2 = stat(filepath, &s);
    assert(rc2 == 0);
    assert(filesize == s.st_size);

    printf("success\n");
}

void test_make_backup_name(void)
{
    printf("test_make_backup_name: ");

    struct timeval tv;
    tv.tv_sec = 1584950361;     /* 2020-03-23 16:00:57 */
    tv.tv_usec = 123;
    int year = 2020;
    int month = 3;
    int day = 23;
    int hour = 15;
    int minute = 59;
    int second = 21;

    char backpath[MAX_PATH_LEN];
    char goodpath[MAX_PATH_LEN];

    memset(backpath, 0, MAX_PATH_LEN);
    memset(goodpath, 0, MAX_PATH_LEN);
    const char *srcname1 = "test2";
    int rc2 = make_backup_name(backpath, srcname1, &tv);
    assert(rc2 == 0);
    sprintf(goodpath, "%s/%s.%d.%02d.%d.%d%d%d.%d",
            BKDIRNAME, srcname1, year, month, day, hour, minute, second,
            (int)tv.tv_usec);
    int rc3 = memcmp(backpath, goodpath, MAX_PATH_LEN);
    assert(rc3 == 0);

    memset(backpath, 0, MAX_PATH_LEN);
    memset(goodpath, 0, MAX_PATH_LEN);
    char fullpath[MAX_PATH_LEN];
    const char *dirpath2 = "/aaa/bbb/ccc/ddd";
    const char *srcname2 = "teeeeeest2";
    sprintf(fullpath, "%s/%s", dirpath2, srcname2);
    int rc4 = make_backup_name(backpath, fullpath, &tv);
    assert(rc4 == 0);
    sprintf(goodpath, "%s/%s/%s.%d.%02d.%d.%d%d%d.%d",
            dirpath2, BKDIRNAME, srcname2,
            year, month, day, hour, minute, second,
            (int)tv.tv_usec);
    int rc5 = memcmp(backpath, goodpath, MAX_PATH_LEN);
    assert(rc5 == 0);

    printf("success\n");
}

void test_move_file(void)
{
    printf("test_move_file: ");

    struct timeval tv;
    int rc1 = gettimeofday(&tv, NULL);
    assert(rc1 == 0);
    time_t epoch = (time_t)tv.tv_sec;
    struct tm *tm = localtime(&epoch);
    assert(tm);

    char oldpath[MAX_PATH_LEN], newpath[MAX_PATH_LEN];
    sprintf(oldpath, "%s.%d.%d.%d.%d%d%d",
            "/tmp/test1", 1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    sprintf(newpath, "%s.%d.%d.%d.%d%d%d",
            "/tmp/movefile/test1",
            1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    #define MAXBUFLEN 8192
    char oldbuf[MAXBUFLEN];
    int fd1 = open(oldpath, O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP);
    assert(fd1 >= 0);
    int n1 = write(fd1, oldbuf, MAXBUFLEN);
    assert(n1 == MAXBUFLEN);
    int rc2 = close(fd1);
    assert(rc2 == 0);

    int rc3 = move_file(oldpath, newpath);
    assert(rc3 == 0);

    char newbuf[MAXBUFLEN];
    int fd2 = open(newpath, O_RDONLY);
    assert(fd2 >= 0);
    int n2 = read(fd2, newbuf, MAXBUFLEN);
    assert(n2 == MAXBUFLEN);
    int rc4 = close(fd2);
    assert(rc4 == 0);

    int rc5 = memcmp(oldbuf, newbuf, MAXBUFLEN);
    assert(rc5 == 0);

    printf("success\n");
}

void test_file_backup_update(void)
{
    printf("test_file_backup_update: ");

    int rc;
    struct stat s;
    off_t filesize = 12345;

    const char *oldpath = "/tmp/test_file_backup_update/abc";
    rc = filepath_create(oldpath, filesize);
    assert(rc == 0);

    char newpath[MAX_PATH_LEN];
    rc = file_backup_update(oldpath, newpath); assert(rc == 0);
    rc = stat(oldpath, &s); assert(rc == 0 && s.st_size == 0);
    rc = stat(newpath, &s); assert(rc == 0 && s.st_size == filesize);

    printf("success\n");
}

void test_file_crush(void)
{
    printf("test_file_crush: ");

    time_t t = time(NULL);
    assert(t >= (time_t)0);
    srandom((unsigned int)t);

    char filepath1[MAX_PATH_LEN];
    off_t filesize;
    int i;
    for (i = 0; i < 3; i++) {
        /*最大测试文件大小是128MB*/
        filesize = (off_t)random() % (1024*1024*128);
        sprintf(filepath1, "/tmp/test_file_crush.%d.%lld",
                i, (long long int)filesize);
        int rc1 = write_zero_file(filepath1, filesize);
        assert(rc1 == 0);
        int fd = open(filepath1, O_RDWR);
        assert(fd >= 0);
        int rc2 = file_crush(fd, 3/*文件粉碎次数*/);
        assert(rc2 == 0);
        int rc3 = close(fd);
        assert(rc3 == 0);
        int rc4 = read_zero_file(filepath1, filesize);
        assert(rc4 == -1);
    }

    printf("success\n");
}

void test_filepath_crush(void)
{
    printf("test_filepath_crush: ");

    time_t t = time(NULL);
    assert(t >= (time_t)0);
    srandom((unsigned int)t);

    char filepath1[MAX_PATH_LEN];
    off_t filesize;
    int i;
    for (i = 0; i < 3; i++) {
        /*最大测试文件大小是128MB*/
        filesize = (off_t)random() % (1024*1024*128);
        sprintf(filepath1, "/tmp/test_file_path_crush.%d.%lld",
                i, (long long int)filesize);
        int rc1 = write_zero_file(filepath1, filesize);
        assert(rc1 == 0);
        int rc2 = filepath_crush(filepath1, 3/*最大粉碎次数*/);
        assert(rc2 == 0);
        int fd = open(filepath1, O_RDWR);
        assert(fd < 0 && errno == ENOENT);
    }

    printf("success\n");
}

void test_path_split(void)
{
    printf("test_path_split: ");

    const char *dirpath10 =  "/tmp/111";
    const char *filename10 = "split111";
    char fullpath10[MAX_PATH_LEN];
    snprintf(fullpath10, MAX_PATH_LEN, "%s/%s", dirpath10, filename10);
    char dirpath11[MAX_PATH_LEN], filename11[MAX_PATH_LEN];
    int rc1 = path_split(fullpath10, dirpath11, filename11);
    assert(rc1 == 0);
    int rc2 = strncmp(dirpath10, dirpath11, 9);
    assert(rc2 == 0);
    int rc3 = strncmp(filename10, filename11, 9);
    assert(rc3 == 0);

    const char *relative_path20 = "aaa/bbb/ccc/222.txt";
    char curdirpath20[MAX_PATH_LEN];
    char *cwd = getcwd(curdirpath20, MAX_PATH_LEN);
    assert(cwd);
    char dirpath20[MAX_PATH_LEN];
    sprintf(dirpath20, "%s/aaa/bbb/ccc", curdirpath20);
    int dirpath20len = strlen(dirpath20) + 1;
    char fullpath20[MAX_PATH_LEN];
    sprintf(fullpath20, "%s/%s", curdirpath20, relative_path20);
    char dirpath21[MAX_PATH_LEN], filename21[MAX_PATH_LEN];
    int rc4 = path_split(fullpath20, dirpath21, filename21);
    assert(rc4 == 0);
    int rc5 = strncmp(dirpath20, dirpath21, dirpath20len);
    assert(rc5 == 0);
    int rc6 = strncmp(filename21, "222.txt", 8);
    assert(rc6 == 0);

    printf("success\n");
}

void test_file_backup_crush(void)
{
    /* 新建文件和它的备份，测试是否能够粉碎成功 */
    printf("test_file_backup_crush: ");

    off_t filesize = 1024*42;

    int rc1;
    const char *dirpath = "/tmp/file_backup_crush";
    rc1 = mkdirs(dirpath);
    assert(rc1 == 0);

    int rc2;
    const char *bakdirpath = "/tmp/file_backup_crush/" BKDIRNAME;
    rc2 = mkdirs(bakdirpath);
    assert(rc2 == 0);

    int rc3;
    const char *filepath = "/tmp/file_backup_crush/test111";
    rc3 = write_zero_file(filepath, filesize);
    assert(rc3 == 0);

    int rc4;
    struct timeval tv4;
    tv4.tv_sec = 1585035412, tv4.tv_usec = 0;
    char bakpath4[MAX_PATH_LEN];
    rc4 = make_backup_name(bakpath4, filepath, &tv4);
    assert(rc4 == 0);
    rc4 = write_zero_file(bakpath4, filesize);
    assert(rc4 == 0);

    int rc5;
    struct timeval tv5;
    tv5.tv_sec = 1585035453, tv5.tv_usec = 0;
    char bakpath5[MAX_PATH_LEN];
    rc5 = make_backup_name(bakpath5, filepath, &tv5);
    assert(rc5 == 0);
    rc5 = write_zero_file(bakpath5, filesize);

    int rc6;
    rc6 = file_backup_crush(filepath, CRUSH_FILE | CRUSH_BACK);
    assert(rc6 == 0);

    int fd0 = open(filepath, O_RDONLY);
    assert(fd0 < 0 && errno == ENOENT);
    int fd1 = open(bakpath4, O_RDONLY);
    assert(fd1 < 0 && errno == ENOENT);
    int fd2 = open(bakpath5, O_RDONLY);
    assert(fd2 < 0 && errno == ENOENT);

    printf("success\n");
}

void test_file_backup_delete(void)
{
    printf("test_file_backup_delete: ");

    srandom(time(NULL));
    const char *oldpath = "/tmp/file_backup_delete/test333";
    off_t filesize = random() % 123456;
    int rc1 = filepath_create(oldpath, filesize);
    assert(rc1 == 0);

    char newpath[MAX_PATH_LEN];
    int rc2 = file_backup_delete(oldpath, newpath, MAX_PATH_LEN);
    assert(rc2 == 0);

    struct stat s;
    int rc3 = stat(oldpath, &s);
    assert(rc3 == -1 && errno == ENOENT);
    int rc4 = stat(newpath, &s);
    assert(rc4 == 0);
    assert(s.st_size == filesize);

    printf("success\n");
}

void test_rename_path(void)
{
    printf("test_rename_path: ");

    int rc;
    char path[MAX_PATH_LEN];

    const char *oldpath = "/test/aaa.txt";
    const char *newpath = "/xxxx/bbb.jpg";
    const char *bakpath = "/test/" BKDIRNAME "/aaa.txt.2020.03.30";
    const char *setpath = "/xxxx/" BKDIRNAME "/bbb.jpg.2020.03.30";

    rc = set_rename_path(path, oldpath, newpath, bakpath);
    assert(rc == 0);
    rc = strcmp(path, setpath);
    assert(rc == 0);

    printf("success\n");
}

void __test_file_backup_rename(const char *oldpath, const char *newpath)
{
    struct timeval tv;
    int rc;
    srandom(time(NULL));
    off_t filesize;

    /* 创建原文件 */
    filesize = random() % 123456;
    rc = filepath_create(oldpath, filesize);
    assert(rc == 0);

    /* 模拟创建备份 */
    char old_bakpath1[MAX_PATH_LEN];
    char new_bakpath1[MAX_PATH_LEN];
    rc = gettimeofday(&tv, NULL); assert(rc == 0);
    rc = make_backup_name(old_bakpath1, oldpath, &tv); assert(rc == 0);
    rc = make_backup_name(new_bakpath1, newpath, &tv); assert(rc == 0);
    rc = filepath_create(old_bakpath1, filesize); assert(rc == 0);

    /* 再次模拟创建备份 */
    char old_bakpath2[MAX_PATH_LEN];
    char new_bakpath2[MAX_PATH_LEN];
    rc = gettimeofday(&tv, NULL); assert(rc == 0);
    rc = make_backup_name(old_bakpath2, oldpath, &tv); assert(rc == 0);
    rc = make_backup_name(new_bakpath2, newpath, &tv); assert(rc == 0);
    rc = filepath_create(old_bakpath2, filesize); assert(rc == 0);

    /* 现在有一个原文件，两个原文件的备份，开始测试原文件重命名 */
    rc = file_backup_rename(oldpath, newpath,
                            BK_RENAME_FILE | BK_RENAME_BACK);
    assert(rc == 0);

    /* 检查文件 */
    struct stat s;
    rc = stat(oldpath, &s);  assert(rc == -1 && errno == ENOENT);
    rc = stat(old_bakpath1, &s); assert(rc == -1 && errno == ENOENT);
    rc = stat(old_bakpath2, &s); assert(rc == -1 && errno == ENOENT);
    rc = stat(newpath, &s); assert(rc == 0 && s.st_size == filesize);
    rc = stat(new_bakpath1, &s); assert(rc == 0 && s.st_size == filesize);
    rc = stat(new_bakpath2, &s); assert(rc == 0 && s.st_size == filesize);
}

void test_file_backup_rename1(void)
{
    printf("test_file_backup_rename1: ");

    const char *oldpath = "/tmp/file_backup_rename1/oldpath";
    const char *newpath = "/tmp/file_backup_rename1/newpath";
    __test_file_backup_rename(oldpath, newpath);

    printf("success\n");
}

void test_file_backup_rename2(void)
{
    printf("test_file_backup_rename2: ");

    const char *oldpath = "/tmp/file_backup_rename2/oldpath";
    const char *newpath = "/tmp/file_backup_rename3/newpath";
    __test_file_backup_rename(oldpath, newpath);

    printf("success\n");
}

void test_dir_backup_delete(void)
{
    printf("test_dir_backup_delete: ");

    int rc;
    struct stat s;

    const char *old_dirpath = "/tmp/test_dir_backup_delete";
    const char *filename = "abc";
    char filepath[MAX_PATH_LEN];
    sprintf(filepath, "%s/%s", old_dirpath, filename);
    rc = filepath_create(filepath, 0);
    assert(rc == 0);

    char new_dirpath[MAX_PATH_LEN];
    rc = dir_backup_delete(old_dirpath, new_dirpath); assert(rc == 0);
    rc = stat(old_dirpath, &s); assert(rc == -1 && errno == ENOENT);
    rc = stat(new_dirpath, &s); assert(rc == 0 && S_ISDIR(s.st_mode));

    printf("success\n");
}

void test_dir_backup_rename(void)
{
    printf("test_dir_backup_rename: ");

    int rc;
    struct stat s;

    const char *oldpath = "/tmp/test_dir_backup_rename1";
    const char *filename = "hello";
    const char *newpath = "/tmp/test_dir_backup_rename2";

    char filepath[MAX_PATH_LEN];
    sprintf(filepath, "%s/%s", oldpath, filename);
    rc = filepath_create(filepath, 0); assert(rc == 0);
    rc = dir_backup_rename(oldpath, newpath); assert(rc == 0);
    rc = stat(oldpath, &s); assert(rc == -1 && errno == ENOENT);
    rc = stat(newpath, &s); assert(rc == 0 && S_ISDIR(s.st_mode));
    sprintf(filepath, "%s/%s", newpath, filename);
    rc = remove(filepath); assert(rc == 0);
    rc = rmdir(newpath); assert(rc == 0);

    printf("success\n");
}

void test_write_zero_file(void)
{
    printf("test_write_zero_file: ");

    const char *filepath = "/tmp/test_write_zero_file";
    off_t filesize = 123456;

    int rc = write_zero_file(filepath, filesize);
    assert(rc == 0);

    int fd = open(filepath, O_RDONLY);
    assert(fd >= 0);

    struct stat s;
    rc = fstat(fd, &s);
    assert(rc == 0);
    assert(filesize == s.st_size);

    off_t leftsize = filesize;
    char zero[4096];
    char buffer[4096];
    memset(zero, 0, sizeof(zero));
    while (leftsize > 0) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            rc = memcmp(buffer, zero, n);
            assert(rc == 0);
            leftsize = leftsize - n;
        } else {
            close(fd);
            abort();
        }
    }

    rc = close(fd);
    assert(rc == 0);

    printf("success\n");
}

void test_read_zero_file(void)
{
    printf("test_read_zero_file: ");

    int rc;
    char zero[MAXBUFLEN];
    memset(zero, 0, sizeof(zero));

    off_t filesize = 123456;
    const char *filepath = "/tmp/test_read_zero_file.1";

    int fd = open(filepath, O_CREAT|O_TRUNC|O_RDWR,
                  S_IRUSR|S_IWUSR|S_IRGRP);
    assert(fd >= 0);
    off_t leftsize = filesize;
    while (leftsize > 0) {
        int blocksize = leftsize < BLOCK_SIZE ? leftsize : BLOCK_SIZE;
        int n = write(fd, zero, blocksize);
        if (n > 0) {
            leftsize = leftsize - n;
        } else {
            abort();
        }
    }
    rc = close(fd);
    assert(rc == 0);

    rc = read_zero_file(filepath, filesize);
    assert(rc == 0);

    printf("success\n");
}

void test_set_rename_path(void)
{
    printf("test_set_rename_path: ");

    const char *oldpath = "/123/a.c";
    const char *bakpath = "/123/" BKDIRNAME "/a.c.2020.03.31.173823.743834";
    const char *newpath = "/789/b.txt";
    char respath[MAX_PATH_LEN];
    int rc = set_rename_path(respath, oldpath, newpath, bakpath);
    assert(rc == 0);

    const char *pinpath = "/789/" BKDIRNAME "/b.txt.2020.03.31.173823.743834";
    int pinpathlen = strlen(pinpath) + 1;
    rc = strncmp(respath, pinpath, pinpathlen);
    assert(rc == 0);

    printf("success\n");
}

void test_copy_file(void)
{
    printf("test_copy_file: ");

    const char *oldpath = "/tmp/test_copy_file.1";
    const char *newpath = "/tmp/4/5/6/test_copy_file.2";
    int rc;

    off_t filesize = 12345;
    rc = write_zero_file(oldpath, filesize); assert(rc == 0);
    rc = copy_file(oldpath, newpath); assert(rc == 0);
    int oldfd = open(oldpath, O_RDONLY); assert(oldfd >= 0);
    int newfd = open(newpath, O_RDONLY); assert(newfd >= 0);

    char oldbuf[BLOCK_SIZE];
    char newbuf[BLOCK_SIZE];
    off_t leftsize = filesize;
    while (leftsize > 0) {
        int n = read(oldfd, oldbuf, sizeof(oldbuf));
        if (n > 0) {
            leftsize = leftsize - n;
            int newleft = n;
            int newread = 0;
            while (newleft > 0) {
                int x = read(newfd, newbuf + newread, newleft);
                if (x > 0) {
                    newleft = newleft - x;
                    newread = newread + x;
                } else {
                    abort();
                }
            }
            rc = memcmp(oldbuf, newbuf, n);
            assert(rc == 0);
        } else {
            abort();
        }
    }
    rc = close(oldfd); assert(rc == 0);
    rc = close(newfd); assert(rc == 0);

    printf("success\n");
}

void test_filepath_crush_cb(void)
{
    printf("test_filepath_crush_cb: ");

    int rc;

    const char *path1 = "/tmp/test_filepath_crush_cb/test1.txt.2020";
    const char *path2 = "/tmp/test_filepath_crush_cb/test2.txt.2020";
    char *origname = "test1";

    rc = filepath_create(path1, 123); assert(rc == 0);
    rc = filepath_create(path2, 456); assert(rc == 0);

    struct stat s;
    rc = stat(path1, &s); assert(rc == 0);
    rc = filepath_crush_cb(path1, &s, origname); assert(rc == 0);
    rc = stat(path1, &s); assert(rc == -1);
    rc = filepath_crush_cb(path2, &s, origname); assert(rc == 0);
    rc = stat(path2, &s); assert(rc == 0);

    printf("\n");
}

#endif
