# Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
#
# This file is part of aws-s3fs.
# 
# aws-s3fs is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AM_LIBS = pthread
AM_CFLAGS = -D_GNU_SOURCE

bin_PROGRAMS = aws-s3fs
aws_s3fs_SOURCES = aws-s3fs.h sysdirs.h aws-s3fs.c \
	decodecmdline.c configfile.c common.c fix-i386-cc.c config.c \
	base64.c base64.h digest.c digest.h logger.c daemon.c \
	fuseif.c
#statcache.c s3request.c

BUILT_SOURCES = sysdirs.h
CLEANFILES = sysdirs.h

sysdirs.h : sysdirs.h.src
	prefix="${prefix}"; \
	exec_prefix="${exec_prefix}"; \
	bindir="${bindir}"; \
	sbindir="${sbindir}"; \
	libexecdir="${libexecdir}"; \
	sharedstatedir="${sharedstatedir}"; \
	includedir="${includedir}"; \
	sysconfdir="${sysconfdir}"; \
	localstatedir="${localstatedir}"; \
	datadir="${datadir}"; \
	datarootdir="${datarootdir}"; \
	localedir="${localedir}"; \
	mandir="${mandir}"; \
	docdir="${docdir}"; \
	infodir="${infodir}"; \
	htmldir="${htmldir}"; \
	dvidir="${dvidir}"; \
	pdfdir="${pdfdir}"; \
	psdir="${psdir}"; \
	libdir="${libdir}"; \
	TMPFILE1=`mktemp`; \
	TMPFILE2=`mktemp`; \
	cat $< > $$TMPFILE1; \
	for REPLACE in prefix exec_prefix bindir sbindir libexecdir sharedstatedir includedir datarootdir infodir htmldir dvidir pdfdir psdir sysconfdir localstatedir datadir localedir mandir docdir libdir; do \
	    REPLACESTR="$$""{$$REPLACE}"; \
	    ACTUALDIR=`eval echo "$$""{$$REPLACE}"`; \
	    echo "Debug: $$REPLACESTR -> $$ACTUALDIR"; \
	    sed "s^$$REPLACESTR^$$ACTUALDIR^" >$$TMPFILE2 $$TMPFILE1; \
	    cp $$TMPFILE2 $$TMPFILE1; \
	done; \
	cp $$TMPFILE2 $@
