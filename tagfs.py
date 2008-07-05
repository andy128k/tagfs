#!/usr/bin/env python

##############################################################################
#                                                                            #
#  TagFS: Tagged Filesystem                                                  #
#  Copyright (C) 2007 Andrey Kutejko <andy128k@gmail.com>                    #
#                                                                            #
#  This program can be distributed under the terms of the GNU GPL.           #
#  See the file COPYING.                                                     #
#                                                                            #
##############################################################################

import os, sys, subprocess, errno, stat, fcntl, sets, re
import fuse
from fuse import Fuse

if not hasattr(fuse, '__version__'):
    raise RuntimeError, "your fuse-py doesn't know of fuse.__version__, probably it's too old."

fuse.fuse_python_api = (0, 2)
fuse.feature_assert('stateful_files', 'has_init')

def get_djvu_metadata(filename):
    p = subprocess.Popen(('djvused', filename, '-e', 'print-meta'), stdout=subprocess.PIPE, universal_newlines=True)
    lines = p.stdout.read().split('\n')
    p.wait()

    result = {}
    for line in lines:
        k = line.split('\t')
        if len(k) == 2:
            key = k[0]
            val = k[1]
            if val[0] == '"' and val[-1] == '"':
                val = val[1:-1]
            result[key] = val

    return result

def get_pdf_metadata(filename):
    p = subprocess.Popen(('pdftk', filename, 'dump_data', 'output', '-'), stdout=subprocess.PIPE, universal_newlines=True)
    lines = p.stdout.read().split('\n')
    p.wait()

    result = {}
    st = 0
    k = ''
    for line in lines:
        if st == 0:
            if line[:9] == 'InfoKey: ':
                st = 1
                k = line[9:]
        else:
            if line[:11] == 'InfoValue: ':
                st = 0
                v = line[11:]
                result[k] = v
               
    return result

class File:
    name = ''
    real_path = ''
    tags = sets.Set()
    is_dir = False

    def __init__(self):
        pass

class Stat(fuse.Stat):
    def __init__(self):
        self.st_mode = 0
        self.st_ino = 0
        self.st_dev = 0
        self.st_nlink = 0
        self.st_uid = 0
        self.st_gid = 0
        self.st_size = 0
        self.st_atime = 0
        self.st_mtime = 0
        self.st_ctime = 0

class Tagfs(Fuse):
    def __init__(self, *args, **kw):
        Fuse.__init__(self, *args, **kw)

        self.tags = sets.Set()
        self.files = []

    def filter(self, files, tag):
        result = []
        for f in files:
            if tag in f.tags:
                result.append(f)
        return result

    def split_path(self, path):
        filtered_files = self.files
        excluded_tags = sets.Set()

        st = True
        path_cutted = ''
        for pp in path.split("/"):
            if pp != '':
                if pp not in self.tags:
                    st = False

                if st:
                    filtered_files = self.filter(filtered_files, pp)
                    excluded_tags.add(pp)
                else:
                    path_cutted = path_cutted + '/' + pp

        if path_cutted != '':
            path_cutted = path_cutted[1:]

        return (excluded_tags, path_cutted, filtered_files)

    # callbacks

    def getattr(self, path):
        tags, tail, files = self.split_path(path)

        if tail == '':
            result = Stat()
            result.st_mode = stat.S_IFDIR | 0755
            result.st_nlink = 2
            return result
        else:
            for f in files:
                if f.name == tail:
                    result = Stat()
                    result.st_nlink = 1
                    result.st_mode = os.stat(f.real_path).st_mode
                    if f.is_dir:
                        result.st_mode = result.st_mode & ~stat.S_IFDIR | stat.S_IFLNK
                    else:
                        result.st_mode = result.st_mode | stat.S_IFLNK
                    return result
            return -errno.ENOENT

    def access(self, path, mask):
        tags, tail, files = self.split_path(path)

        if tail == '':
            return 0
        else:
            for f in files:
                if f.name == tail:
                    if os.access(f.real_path, mask):
                        return 0
                    else:
                        return 1
            return -errno.ENOENT

    def readlink(self, path):
        tags, tail, files = self.split_path(path)

        if tail == '':
            return -errno.ENOENT
        else:
            for f in files:
                if f.name == tail:
                    return f.real_path
            return -errno.ENOENT

    def readdir(self, path, offset):
        yield fuse.Direntry(".")
        yield fuse.Direntry("..")

        tags, tail, files = self.split_path(path)
        if tail == '':
            subtags = sets.Set()
            for f in files:
                yield fuse.Direntry(f.name)
                subtags = subtags.union(f.tags)

            subtags = subtags.difference(tags)

            for tag in subtags:
                yield fuse.Direntry(tag)

#/////////////////////////////////////////////////////////////////////////////

        '''
        /* DO IT */
        static int tfs_mknod(const char *path, mode_t mode, dev_t rdev)
        {
            int res;

            /* On Linux this could just be 'mknod(path, mode, rdev)' but this
               is more portable */
            if (S_ISREG(mode)) {
                res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
                if (res >= 0)
                    res = close(res);
            } else if (S_ISFIFO(mode))
                res = mkfifo(path, mode);
            else
                res = mknod(path, mode, rdev);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_mkdir(const char *path, mode_t mode)
        {
            int res;

            res = mkdir(path, mode);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_unlink(const char *path)
        {
            int res;

            res = unlink(path);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_rmdir(const char *path)
        {
            int res;

            res = rmdir(path);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_symlink(const char *from, const char *to)
        {
            int res;

            res = symlink(from, to);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_rename(const char *from, const char *to)
        {
            int res;

            res = rename(from, to);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_link(const char *from, const char *to)
        {
            int res;

            res = link(from, to);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_chmod(const char *path, mode_t mode)
        {
            int res;

            res = chmod(path, mode);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_chown(const char *path, uid_t uid, gid_t gid)
        {
            int res;

            res = lchown(path, uid, gid);
            if (res == -1)
                return -errno;

            return 0;
        }

        static int tfs_utimens(const char *path, const struct timespec ts[2])
        {
            int res;
            struct timeval tv[2];

            tv[0].tv_sec = ts[0].tv_sec;
            tv[0].tv_usec = ts[0].tv_nsec / 1000;
            tv[1].tv_sec = ts[1].tv_sec;
            tv[1].tv_usec = ts[1].tv_nsec / 1000;

            res = utimes(path, tv);
            if (res == -1)
                return -errno;

            return 0;
        }

        /* DO IT */
        static int tfs_statfs(const char *path, struct statvfs *stbuf)
        {
            int res;

            res = statvfs(path, stbuf);
            if (res == -1)
                return -errno;

            return 0;
        }

        static int tfs_release(const char *path, struct fuse_file_info *fi)
        {
            /* Just a stub.  This method is optional and can safely be left
               unimplemented */

            (void) path;
            (void) fi;
            return 0;
        }

        static int tfs_fsync(const char *path, int isdatasync,
                             struct fuse_file_info *fi)
        {
            /* Just a stub.  This method is optional and can safely be left
               unimplemented */

            (void) path;
            (void) isdatasync;
            (void) fi;
            return 0;
        }

        #ifdef HAVE_SETXATTR
        /* xattr operations are optional and can safely be left unimplemented */
        static int tfs_setxattr(const char *path, const char *name, const char *value,
                                size_t size, int flags)
        {
            int res = lsetxattr(path, name, value, size, flags);
            if (res == -1)
                return -errno;
            return 0;
        }

        static int tfs_getxattr(const char *path, const char *name, char *value,
                            size_t size)
        {
            int res = lgetxattr(path, name, value, size);
            if (res == -1)
                return -errno;
            return res;
        }

        static int tfs_listxattr(const char *path, char *list, size_t size)
        {
            int res = llistxattr(path, list, size);
            if (res == -1)
                return -errno;
            return res;
        }

        static int tfs_removexattr(const char *path, const char *name)
        {
            int res = lremovexattr(path, name);
            if (res == -1)
                return -errno;
            return 0;
        }
        #endif /* HAVE_SETXATTR */
        '''

    def scan_dir(self, path):
        for e in os.listdir(path):
            full_name = path + '/' + e
            if os.path.isfile(full_name):
                f = File()
                f.name = e
                f.real_path = full_name

                suffix = os.path.basename(e).split('.')[-1]
                if suffix == 'djvu':
                    tags = []

                    metadata = get_djvu_metadata(full_name)
                    if metadata != None and metadata.has_key('keywords'):
                        keywords = metadata['keywords'].split(',')
                        for k in keywords:
                            if k.strip() != '':
                                tags.append(k)
                    
                    f.tags = sets.Set(tags)

                elif suffix == 'pdf':
                    tags = []

                    metadata = get_pdf_metadata(full_name)
                    if metadata != None and metadata.has_key('Keywords'):
                        keywords = metadata['Keywords'].split(',')
                        for k in keywords:
                            if k.strip() != '':
                                tags.append(k)

                    f.tags = sets.Set(tags)

                else:
                    f.tags = sets.Set(re.findall("\[([^\]]+)\]", e))

                self.tags.update(f.tags)
                self.files.append(f)
            elif os.path.isdir(full_name):
                tags = sets.Set(re.findall("\[([^\]]+)\]", e))
                if len(tags) == 0:
                    self.scan_dir(full_name)
                else:
                    f = File()
                    f.name = e
                    f.real_path = full_name
                    f.tags = tags
                    f.is_dir = True
                    
                    self.tags.update(f.tags)
                    self.files.append(f)

def main():
    usage = "TagFS: tagged filesystem\n" + Fuse.fusage
    server = Tagfs(version="%prog " + fuse.__version__, usage=usage, dash_s_do='setsingle')
    server.parser.add_option(mountopt='root', metavar="PATH", help="Root (required)")
    server.parse(values=server, errex=1)

    if not hasattr(server, 'root'): 
        print >> sys.stderr, "-o root=PATH is required."
        sys.exit(1)

    try:
        if server.fuse_args.mount_expected():
            os.chdir(server.root)
    except OSError:
        print >> sys.stderr, "can't enter root of underlying filesystem."
        sys.exit(1)

    server.scan_dir(server.root)
    server.main()

if __name__ == '__main__':
    main()
