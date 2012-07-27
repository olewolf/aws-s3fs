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


TESTSCRIPTS = commandline.at config.at common.at logging.at cache.at hash.at \
	s3if.at filecache.at downloadqueue.at
#TESTSCRIPTS = downloadqueue.at


noinst_PROGRAMS = test-decodecmdline test-config test-common \
	test-logging test-cache test-hash test-s3if test-filecache \
	test-downloadqueue test-process aws-s3fs-queued

test_decodecmdline_SOURCES = $(SHAREDTESTSOURCE) test-decodecmdline.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_config_SOURCES = $(SHAREDTESTSOURCE) test-config.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_common_SOURCES = $(SHAREDTESTSOURCE) test-common.c ../src/common.c
test_logging_SOURCES = $(SHAREDTESTSOURCE) test-logging.c \
	../src/logger.c ../src/common.c
test_cache_SOURCES= $(SHAREDTESTSOURCE) test-cache.c ../src/statcache.c \
	../src/logger.c
test_hash_SOURCES= $(SHAREDTESTSOURCE) test-hash.c ../src/digest.c \
	../src/base64.c src/base64.h
test_s3if_SOURCES= $(SHAREDTESTSOURCE) test-s3if.c ../src/s3if.c \
	../src/digest.c src/digest.h src/statcache.h ../src/statcache.c \
	../src/logger.c ../src/base64.c src/base64.h ../src/dircache.c \
	src/dircache.h src/s3comms.h ../src/s3comms.c \
	../src/filecacheclient.c fakesocket.c
test_filecache_SOURCES= $(SHAREDTESTSOURCE) test-filecache.c src/filecache.h \
	../src/filecache.c ../src/filecachedb.c src/socket.h fakesocket.c \
	../src/downloadqueue.c ../src/grant.c ../src/s3comms.c src/s3comms.h \
	../src/digest.c src/digest.h src/base64.h ../src/base64.c
test_downloadqueue_SOURCES= $(SHAREDTESTSOURCE) test-downloadqueue.c \
	../src/filecache.h ../src/downloadqueue.c ../src/filecachedb.c \
	../src/filecache.c ../src/grant.c s3comms.h \
	../src/digest.c ../src/digest.h ../src/base64.c ../src/base64.h \
	fakesocket.c
test_process_SOURCES= $(SHAREDTESTSOURCE) test-process.c \
	../src/downloadqueue.c ../src/s3comms.c ../src/digest.c ../src/base64.c \
	../src/digest.h ../src/base64.h ../src/s3comms.h \
	filecache.h ../src/filecache.c ../src/grant.c ../src/filecachedb.c
aws_s3fs_queued_SOURCES = ../src/config.h aws-s3fs.h sysdirs.h filecache.h \
	../src/base64.h digest.h s3comms.h ../src/socket.h \
	../src/filecache.c ../src/socket.c ../src/filecachedb.c \
	../src/downloadqueue.c ../src/grant.c ../src/base64.c ../src/digest.c \
	../src/s3comms.c ../src/aws-s3fs-queued.c

SHAREDTESTSOURCE = dispatch.c aws-s3fs.h shared.c testfunctions.h

AM_CFLAGS = -I../src -DAUTOTEST -DCACHE_DIRECTORY="./cachedir" -DDEBUG -O0 -g
if MAKE_OPENSSL_TESTS
AM_CFLAGS += -DMAKE_OPENSSL_TESTS
endif
if HAVE_SQLITE_UTIL
AM_CFLAGS += -DHAVE_SQLITE_UTIL
endif

test_filecache_CFLAGS = $(AM_CFLAGS) -DAUTOTEST_SKIP_COMMUNICATIONS \
	-DAUTOTEST_WITH_FILECACHE
test_downloadqueue_CFLAGS = $(AM_CFLAGS) -DAUTOTEST_WITH_FILECACHE
test_process_CFLAGS = $(AM_CFLAGS) -DAUTOTEST_WITH_FILECACHE
aws_s3fs_queued_CFLAGS = $(AM_CFLAGS) -DAUTOTEST_WITH_FILECACHE


# See 
# http://www.lrde.epita.fr/~akim/ccmp/doc/gnuprog2/The-GNU-M4-Test-Suite.html#The%20GNU%20M4%20Test%20Suite

AM_LIBS = pthread

TESTSUITE = testsuite

check-local: atconfig $(TESTSUITE)
	$(SHELL) $(srcdir)/$(TESTSUITE) AUTOTEST_PATH=src:test

#installcheck-local: atconfig atlocal $(TESTSUITE)
#	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)

$(TESTSUITE): $(TESTSUITE).at $(TESTSCRIPTS) atlocal.in \
	../configure ../configure.ac \
	Makefile.am Makefile.in Makefile
	autom4te -l autotest $(TESTSUITE).at -o $(TESTSUITE)

$(TESTSUITE).at : $(TESTSCRIPTS) atlocal.in ../configure ../configure.ac \
	Makefile.am Makefile.in Makefile
	@echo >$@ "# This file is auto-generated. Do not edit!"; echo >>$@ "";
	@echo >>$@ "m4_define([AT_PACKAGE_STRING],    [aws-s3fs S3 FUSE Filesystem])";
	@echo >>$@ "m4_define([AT_PACKAGE_BUGREPORT], [wolf@blazingangles.com])";
	@echo >>$@ "AT_INIT([Regression Tests])";
	@for f in $(TESTSCRIPTS); do echo >>$@ "m4_include([$$f])"; done

CLEANFILES = $(noinst_PROGRAMS) $(TESTSUITE).at

clean-local:
	if [ -f $(TESTSUITE) ]; then ./$(TESTSUITE) --clean; fi

distclean-local:
	rm -f atconfig atlocal
	rm -f $(TESTSUITE)

EXTRA_DIST = $(TESTSCRIPTS) $(TESTSUITE).at atlocal.in
