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
#AM_CFLAGS = -O2 -g
AM_CFLAGS = -O0 -g

bin_PROGRAMS = aws-s3fs
aws_s3fs_LDADD = libaws-s3fs-@VERSION@.la
#bin_PROGRAMS = aws-s3fs aws-s3fs-queue
aws_s3fs_SOURCES = config.h aws-s3fs.h sysdirs.h aws-s3fs.c \
	decodecmdline.c configfile.c common.c fix-i386-cc.c config.c \
	logger.c dircache.h dircache.c s3comms.h \
	fuseif.c fuseif.h s3if.c s3if.h statcache.c statcache.h

#aws_s3fs_queue_SOURCES = config.h aws-s3fs.h socket.h filecache.c socket.c

#noinst_LTLIBRARIES = libaws-s3fs-@VERSION@.la
lib_LTLIBRARIES = libaws-s3fs-@VERSION@.la
libaws_s3fs_@VERSION@_la_SOURCES = sysdirs.h digest.h digest.c \
	base64.h base64.c s3comms.h s3comms.c
libaws_s3fs_@VERSION@_la_LDFLAGS = -version-info $(AWS_S3FS_SO_VERSION)

## For debugging: libaws_s3fs_@AWS_S3FS_API_VERSION@_la_LDFLAGS = -version-info $(AWS_S3FS_SO_VERSION) -all-static
##library_includedir=$(includedir)/aws-s3fs-0.1/aws-s3fs
##library_include_HEADERS = aws-s3fs/foo.h aws-s3fs/bar.h



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
	    sed "s^$$REPLACESTR^$$ACTUALDIR^" >$$TMPFILE2 $$TMPFILE1; \
	    cp $$TMPFILE2 $$TMPFILE1; \
	done; \
	cp $$TMPFILE2 $@

