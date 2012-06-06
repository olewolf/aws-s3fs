#!/bin/sh

prefix="`sed <sysconffile.generate-h -n 's/^\/\* prefix: \(.\+\) \*\/$/\1/p'`"
tmpsysconffile=`mktemp`
sed <sysconffile.generate-h "s^\${prefix}^$prefix^" > $tmpsysconffile
sysconfdir=`sed <$tmpsysconffile -n "s/^\/\* sysconfdir: \(.\+\) \*\/$/\1/p"`
sed <sysconffile.generate-h "s^\${sysconfdir}^$sysconfdir^" >sysconffile.h
