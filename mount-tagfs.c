/*
 *  TagFS: Tagged Filesystem
 *  Copyright (C) 2007 Andrey Kutejko <andy128k@gmail.com>
 *
 *  This program can be distributed under the terms of the GNU GPL.
 *  See the file COPYING.
 *
 *  gcc -Wall `pkg-config fuse --cflags --libs` tagfs.c -o tagfs
 */

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <syslog.h>

#include <fuse.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <sqlite3.h>
#include <magic.h>

#include "plugin_interface.h"
#include "helpers.h"

static sqlite3* db = NULL;
static magic_t magic;
#define MAXDIGITS 15

static gint sql_exec(const char* sql)
{
  sqlite3_stmt *statement;
  if (sqlite3_prepare_v2(db, sql, -1, &statement, NULL) == SQLITE_OK)
    sqlite3_step(statement);
  sqlite3_finalize(statement);
  return sqlite3_last_insert_rowid(db);
}

static gint find_attr_id(const gchar* attr)
{
  sqlite3_stmt *statement;
  sqlite3_prepare_v2(db, "select id from attr where name = ?", -1, &statement, NULL);
  sqlite3_bind_text(statement, 1, g_utf8_strdown(attr, -1), -1, g_free);

  gint result = 0;
  if (sqlite3_step(statement) == SQLITE_ROW)
    result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

static gint find_attr_value_id(const gchar* value)
{
  sqlite3_stmt *statement;
  sqlite3_prepare_v2(db, "select id from attr_value where value = ?", -1, &statement, NULL);
  sqlite3_bind_text(statement, 1, value, -1, SQLITE_STATIC);

  gint result = 0;
  if (sqlite3_step(statement) == SQLITE_ROW)
    result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return result;
}

static gint insert_attr(const gchar* attr_)
{
  gchar* attr = g_utf8_strdown(attr_, -1);

  sqlite3_stmt *statement;

  sqlite3_prepare_v2(db, "select id from attr where name = ?", -1, &statement, NULL);
  sqlite3_bind_text(statement, 1, attr, -1, SQLITE_STATIC);

  if (sqlite3_step(statement) == SQLITE_ROW)
    {
      gint id = sqlite3_column_int(statement, 0);
      sqlite3_finalize(statement);

      g_free(attr);
      return id;
    }
  else
    {
      sqlite3_finalize(statement);

      sqlite3_prepare_v2(db, "insert into attr values (null, ?)", -1, &statement, NULL);
      sqlite3_bind_text(statement, 1, attr, -1, SQLITE_STATIC);
      sqlite3_step(statement);
      gint id = sqlite3_last_insert_rowid(db);
      sqlite3_finalize(statement);

      g_free(attr);
      return id;
    }
}

static gint insert_attr_value(const gchar* value)
{
  sqlite3_stmt *statement;

  sqlite3_prepare_v2(db, "select id from attr_value where value = ?", -1, &statement, NULL);
  sqlite3_bind_text(statement, 1, value, -1, SQLITE_STATIC);

  if (sqlite3_step(statement) == SQLITE_ROW)
    {
      gint id = sqlite3_column_int(statement, 0);
      sqlite3_finalize(statement);
      return id;
    }
  else
    {
      sqlite3_finalize(statement);

      sqlite3_prepare_v2(db, "insert into attr_value values (null, ?)", -1, &statement, NULL);
      sqlite3_bind_text(statement, 1, value, -1, SQLITE_STATIC);
      sqlite3_step(statement);
      gint id = sqlite3_last_insert_rowid(db);
      sqlite3_finalize(statement);
      return id;
    }
}

typedef struct tagPath
{
  gint attr_id;
  GArray* value_ids;
  gchar* tail;
} path_t;

static void free_path(path_t* ps)
{
  if (ps->value_ids != NULL)
    g_array_free(ps->value_ids, TRUE);
  if (ps->tail)
    g_free(ps->tail);
}

static path_t* split_path(const gchar* path)
{
  if (!strcmp(path, "/"))
    {
      path_t* ps = g_slice_new(path_t);
      ps->attr_id = 0;
      ps->value_ids = NULL;
      ps->tail = NULL;
      return ps;
    }

  gchar** pp = g_strsplit(path + 1, "/", 0); /* + 1 to skip leading '/' */

  gchar* attr = pp[0];
  gint attr_id = find_attr_id(attr);
  if (attr_id == 0)
    {
      g_strfreev(pp);
      return NULL;
    }

  path_t* ps = g_slice_new(path_t);
  ps->attr_id = attr_id;
  ps->value_ids = g_array_new(FALSE, FALSE, sizeof(gint));
  ps->tail = NULL;

  gboolean st = TRUE;
  gchar** p;
  for (p = pp + 1; *p != NULL; ++p)
    {
      if (**p == '\0') /* (*p) == "" */
	continue;

      if (st)
	{
	  gint value_id = find_attr_value_id(*p);
	  if (value_id != 0)
	    {
	      g_array_append_val(ps->value_ids, value_id);
	    }
	  else
	    {
	      st = FALSE;
	      ps->tail = g_strdup(*p);
	    }
	}
      else
	{
	  gchar* newtail = g_strdup_printf("%s/%s", ps->tail, *p);
	  g_free(ps->tail);
	  ps->tail = newtail;
	}
    }
  g_strfreev(pp);
  return ps;
}

static gchar* get_ids_string(GArray* arr)
{
  if (arr == NULL)
    return g_strdup("");

  gchar* ids = g_malloc(arr->len * (MAXDIGITS + 1) + 1);
  if (arr->len != 0)
    {
      gchar* ptr = ids;
      gint i;
      for (i = 0; i < arr->len; ++i)
	ptr += g_sprintf(ptr, "%d,", g_array_index(arr, gint, i));
      ptr[-1] = '\0'; /* remove last ',' */
    }
  else
    *ids = '\0';

  return ids;
}

static gchar* find_realpath(const char *path, gboolean* error)
{
  path_t* sp = split_path(path);
  if (sp == NULL)
    {
      if (error) *error = TRUE;
      return NULL;
    }

  if (sp->attr_id == 0) /* root */
    {
      free_path(sp);
      return NULL;
    }

  if (sp->tail == NULL)
    {
      free_path(sp);
      return NULL;
    }

  sqlite3_stmt *statement;
  if (sp->value_ids->len != 0)
    {
      gchar* ids = get_ids_string(sp->value_ids);
      gchar* sql = g_strdup_printf("select file.path from link, file where "
				   "link.file_id = file.id "
				   "and link.attr_id = ? "
				   "and link.value_id in (%s) "
				   "and file.name = ?",
				   ids);
      g_free(ids);

      sqlite3_prepare_v2(db, sql, -1, &statement, NULL);
      sqlite3_bind_int(statement, 1, sp->attr_id);
      sqlite3_bind_text(statement, 2, sp->tail, -1, SQLITE_STATIC);

      g_free(sql);
    }
  else
    {
      const gchar* sql =
	"select file.path from link, file where "
	"link.file_id = file.id "
	"and link.attr_id = ? "
	"and file.name = ?";

      sqlite3_prepare_v2(db, sql, -1, &statement, NULL);
      sqlite3_bind_int(statement, 1, sp->attr_id);
      sqlite3_bind_text(statement, 2, sp->tail, -1, SQLITE_STATIC);
    }

  gchar* realpath = NULL;
  if (sqlite3_step(statement) == SQLITE_ROW)
    realpath = g_strdup((const gchar*)sqlite3_column_text(statement, 0));
  sqlite3_finalize(statement);

  free_path(sp);

  return realpath;
}

static int tfs_getattr(const char *path, struct stat *stbuf)
{
  gboolean error = FALSE;
  gchar* filepath = find_realpath(path, &error);
  if (error)
    return -ENOENT;
  
  if (filepath == NULL)
    {
      memset(stbuf, 0, sizeof(struct stat));
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
      return 0;
    }
  else
    {
      int res = stat(filepath, stbuf);
      g_free(filepath);

      if (res == -1)
	return -errno;
      
      stbuf->st_nlink = 1;
      if (S_ISDIR(stbuf->st_mode))
	stbuf->st_mode = (stbuf->st_mode & ~S_IFDIR) | S_IFLNK;
      else
	stbuf->st_mode = stbuf->st_mode | S_IFLNK;
      return 0;
    }
}

static int tfs_access(const char *path, int mask)
{
  return 0;

  /*
  tfs_file_t file = find_file(path);
  if (!file)
    return -200;

  if (file == (tfs_file_t)1)
    {
      return 0;
    }

  int res;
  res = access(file->real_path, mask);
  if (res == -1)
    return -errno;

  return 0;
  */

  /***
        sp = self.split_path(path)
        if sp == None:
            return -errno.ENOENT
        if sp[0] == None: # root
            return 0

        ids = sp[1]
        tail = sp[2]

        if tail == '':
            return 0
        else:
            self.cursor.execute('select file.path from link, file where '
                                + 'link.file_id = file.id '
                                + 'and link.attr_id = ? '
                                + 'and link.value_id in (' + ','.join(ids) + ') '
                                + 'and file.name = ?',
                                (sp[0], tail))
            try:
                path = self.cursor.fetchone()[0]
                if os.access(f.real_path, mask):
                    return 0
                else:
                    return 1
            except StopIteration:
                return -errno.ENOENT
  ***/
}

static int tfs_readlink(const char *path, char *buf, size_t size)
{
  gchar* filepath = find_realpath(path, NULL);
  if (filepath == NULL)
    {
      return -ENOENT;
    }
  else
    {
      strncpy(buf, filepath, size-1);
      g_free(filepath);
      return 0;
    }
}

static int tfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
  path_t* sp = split_path(path);
  if (sp == NULL)
    return -1;

  if (sp->tail != NULL)
    return -2;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  if (sp->attr_id == 0) /* root */
    {
      sqlite3_stmt *statement;
      sqlite3_prepare_v2(db, "select name from attr", -1, &statement, NULL); // SQLITE_OK
      while (sqlite3_step(statement) == SQLITE_ROW)
	{
	  filler(buf, (const char*)sqlite3_column_text(statement, 0), NULL, 0);
	}
      sqlite3_finalize(statement);
    }
  else
    {
      gchar* ids = get_ids_string(sp->value_ids);

      gchar* sql;
      if (sp->value_ids != NULL && sp->value_ids->len != 0)
	{
	  sql = g_strdup_printf("select file.name from link, file where"
				" link.file_id = file.id and"
				" link.attr_id = %d and"
				" value_id in (%s)"
				" group by link.file_id"
				" having count(link.id) = %d",
				sp->attr_id, ids, sp->value_ids->len);
	}
      else
	{
	  sql = g_strdup_printf("select file.name from file, link where"
				" file.id = link.file_id and"
				" link.attr_id = %d"
				" group by link.file_id",
				sp->attr_id);
	}

      sqlite3_stmt *statement;
      sqlite3_prepare_v2(db, sql, -1, &statement, NULL); // SQLITE_OK
      g_free(sql);
      while (sqlite3_step(statement) == SQLITE_ROW)
	{
	  filler(buf, (const char*)sqlite3_column_text(statement, 0), NULL, 0);
	}
      sqlite3_finalize(statement);

      if (sp->value_ids != NULL && sp->value_ids->len != 0)
	{
	  sql = g_strdup_printf("select attr_value.value from attr_value, link where "
				"attr_value.id = link.value_id and "
				"link.attr_id = %d and "
				"link.file_id in ( "
				" select file_id from link where "
				" attr_id = %d and "
				" value_id in (%s) "
				" group by file_id "
				" having count(id) = %d "
				") and "
				"link.value_id not in (%s) "
				"group by attr_value.id",
				sp->attr_id, sp->attr_id, ids, sp->value_ids->len, ids);
	}
      else
	{
	  sql = g_strdup_printf("select attr_value.value from attr_value, link where "
				" attr_value.id = link.value_id and "
				" link.attr_id = %d"
				" group by attr_value.id",
				sp->attr_id);
	}

      sqlite3_prepare_v2(db, sql, -1, &statement, NULL);
      g_free(sql);
      while (sqlite3_step(statement) == SQLITE_ROW)
	{
	  filler(buf, (const char*)sqlite3_column_text(statement, 0), NULL, 0);
	}
      sqlite3_finalize(statement);

      g_free(ids);
    }
  return 0;
}

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

static struct fuse_operations tfs_oper = {
    .getattr	= tfs_getattr,
    .access	= tfs_access,
    .readlink	= tfs_readlink,
    .readdir	= tfs_readdir,
#if 0
    .mknod	= tfs_mknod,
    .mkdir	= tfs_mkdir,
    .symlink	= tfs_symlink,
    .unlink	= tfs_unlink,
    .rmdir	= tfs_rmdir,
    .rename	= tfs_rename,
    .link	= tfs_link,
    .chmod	= tfs_chmod,
    .chown	= tfs_chown,
    .utimens	= tfs_utimens,
    .statfs	= tfs_statfs,
    .release	= tfs_release,
    .fsync	= tfs_fsync,
#else
    .mknod	= NULL,
    .mkdir	= NULL,
    .symlink	= NULL,
    .unlink	= NULL,
    .rmdir	= NULL,
    .rename	= NULL,
    .link	= NULL,
    .chmod	= NULL,
    .chown	= NULL,
    .utimens	= NULL,
    .statfs	= NULL,
    .release	= NULL,
    .fsync	= NULL,
#endif
#ifdef HAVE_SETXATTR
    .setxattr	= tfs_setxattr,
    .getxattr	= tfs_getxattr,
    .listxattr	= tfs_listxattr,
    .removexattr= tfs_removexattr,
#endif
};

/* main */

extern PluginInterface djvu_interface;
extern PluginInterface pdf_interface;

static PluginInterface* s_plugins[] = {
  &djvu_interface,
  &pdf_interface
};
#define PLUGINS_COUNT (sizeof(s_plugins)/sizeof(*s_plugins))

static void put_metainfo_to_db(GQuark key_id, gpointer data, gpointer user_data)
{
  const gint file_id = GPOINTER_TO_INT(user_data);

  const gchar* attr = g_quark_to_string(key_id);
  const gint attr_id = insert_attr(attr);

  if (!g_ascii_strcasecmp(attr, "keywords") || !g_ascii_strcasecmp(attr, "author"))
    {
      gchar** vals = g_strsplit((gchar*)data, ",", 0);
      gchar** val;
      for (val = vals; *val; ++val)
	{
	  g_strstrip(*val);

	  const gint value_id = insert_attr_value(*val);

	  gchar* sql = g_strdup_printf("insert into link values (null, %d, %d, %d)", file_id, attr_id, value_id);
	  sql_exec(sql);
	  g_free(sql);
	}
      g_strfreev(vals);
    }
  else
    {
      gint value_id = insert_attr_value((gchar*)data);

      gchar* sql = g_strdup_printf("insert into link values (null, %d, %d, %d)", file_id, attr_id, value_id);
      sql_exec(sql);
      g_free(sql);
    }
}

static void get_attrs(const char* name, const char* path)
{
  GData* metainfo;
  g_datalist_init(&metainfo);

  const gchar* mime = magic_file(magic, name);

  gint i;
  for (i = 0; i < PLUGINS_COUNT; ++i)
    {
      if (s_plugins[i]->check_file(path, mime))
	{
	  metainfo = s_plugins[i]->get_metainfo(path, NULL);
	  // print error??
	  break;
	}
    }

  gint file_id;
  {
    sqlite3_stmt *statement;
    sqlite3_prepare_v2(db, "insert into file values (null, ?, ?)", -1, &statement, NULL);
    sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, path, -1, SQLITE_STATIC);
    sqlite3_step(statement);
    file_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(statement);
  }

  g_datalist_foreach(&metainfo, put_metainfo_to_db, GINT_TO_POINTER(file_id));
}

static void scan_dir(const char* path)
{
  DIR* d;
  d = opendir(path);
  if (!d)
      return;

  struct dirent* e;
  while ((e = readdir(d)) != 0)
    {
      char* full_name = g_strdup_printf("%s/%s", path, e->d_name);

      struct stat st;
      stat(full_name, &st);

      if (S_ISREG(st.st_mode))
	{
	  get_attrs(e->d_name, full_name);
	}
      else if (S_ISDIR(st.st_mode))
	{
	  if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
	    scan_dir(full_name);
	}
      g_free(full_name);
    }
  closedir(d);
}

static int opt_process(void *data,
		       const char *arg,
		       int key,
		       struct fuse_args *outargs)
{
  static int found = 0;

  /*
   * Grab the first non-option argument as the query text, but make sure
   * to leave the second argument (the mount point) alone.
   */
  if (found == 0 && key == FUSE_OPT_KEY_NONOPT)
    {
      found = 1;
      if (g_path_is_absolute(arg))
	{
	  *(gchar**)data = g_strdup(arg);
	}
      else
	{
	  gchar *pwd = g_get_current_dir();
	  *(gchar**)data = g_build_filename(pwd, arg, NULL);
	  g_free(pwd);
	}
      return 0;
    }

  return 1;
}

static void create_db()
{
  sqlite3_open(":memory:", &db);
  // sqlite3_open("/home/andy/tags.db", &db);

  sql_exec("create table file ("
	   "id integer primary key,"
	   "name varchar(255),"
	   "path varchar(255))");

  sql_exec("create table attr ("
	   "id integer primary key,"
	   "name varchar(255))");

  sql_exec("create table attr_value ("
	   "id integer primary key,"
	   "value varchar(255))");

  sql_exec("create table link ("
	   "id integer primary key,"
	   "file_id integer,"
	   "attr_id integer,"
	   "value_id integer)");
}

int main(int argc, char *argv[])
{
  magic = magic_open(MAGIC_MIME_TYPE);
  g_assert(magic != NULL);

  int magic_load_result = magic_load(magic, NULL);
  g_assert(magic_load_result == 0);

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  gchar* root = NULL;
  if (fuse_opt_parse(&args, &root, NULL, opt_process) == -1)
    {
      fprintf(stderr, "usage: %s <dir> <mount point> [OPTIONS...]\n", argv[0]);
      return 1;
    }

  if (root == NULL)
    {
      fprintf(stderr, "usage: %s <dir> <mount point> [OPTIONS...]\n", argv[0]);
      return 1;
    }

  openlog(argv[0], 0, LOG_USER);
  syslog(LOG_INFO, "Started successfully");

  create_db();
  scan_dir(root);

  int result = fuse_main(args.argc, args.argv, &tfs_oper, NULL);

  sqlite3_close(db);

  syslog(LOG_INFO, "Exiting");
  closelog();

  magic_close(magic);

  return result;
}
