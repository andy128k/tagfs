env = Environment()

env.ParseConfig('pkg-config --cflags --libs glib-2.0')
env.MergeFlags('-Wall')
# env.MergeFlags('-g3')

plugins = [
    env.SharedObject('plugin_djvu.o', 'plugin_djvu.c'),
    env.SharedObject('plugin_pdf.o', 'plugin_pdf.c')
    ]

def fuse():
    env2 = env.Clone()
    env2.ParseConfig('pkg-config --cflags --libs fuse sqlite3')
    helpers = env2.Object('helpers.fuse.o', 'helpers.c')
    env2.Program('mount.tagfs', ['mount-tagfs.c'] + helpers + plugins)

def editor():
    env2 = env.Clone()
    env2.ParseConfig('pkg-config --cflags --libs gtk+-2.0')
    helpers = env2.Object('helpers.edit.o', 'helpers.c')
    env2.Program('tageditor', ['tageditor.c', 'core.c'] + helpers + plugins)

def extension():
    env2 = env.Clone()
    env2.ParseConfig('pkg-config --cflags --libs gtk+-2.0 libnautilus-extension')
    helpers = env2.SharedObject('helpers.ext.o', 'helpers.c')
    env2.SharedLibrary('nautilus-tageditor', ['nautilus-tageditor.c', 'core.c'] + helpers + plugins)

fuse()
editor()
extension()

