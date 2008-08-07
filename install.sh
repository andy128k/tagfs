#!/bin/sh

if [ -z $1 ]
then
    echo 'usage: install.sh <destdir>'
    exit
fi

DESTDIR=$1

cp mount.tagfs $DESTDIR/usr/bin/
cp tageditor $DESTDIR/usr/bin/
cp libnautilus-tageditor.so $DESTDIR/usr/lib/nautilus/extensions-1.0/

