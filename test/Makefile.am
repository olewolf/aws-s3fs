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


TESTSCRIPTS = commandline.at config.at common.at logging.at cache.at hash.at

noinst_PROGRAMS = test-decodecmdline test-config test-configfile test-common \
	test-logging test-cache test-hash

test_decodecmdline_SOURCES = $(SHAREDTESTSOURCE) test-decodecmdline.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_config_SOURCES = $(SHAREDTESTSOURCE) test-config.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_configfile_SOURCES = $(SHAREDTESTSOURCE) test-configfile.c \
	../src/configfile.c ../src/config.c ../src/decodecmdline.c \
	../src/common.c
test_common_SOURCES = $(SHAREDTESTSOURCE) test-common.c ../src/common.c
test_logging_SOURCES = $(SHAREDTESTSOURCE) test-logging.c \
	../src/logger.c ../src/common.c
test_cache_SOURCES= $(SHAREDTESTSOURCE) test-cache.c ../src/statcache.c \
	../src/logger.c
test_hash_SOURCES= $(SHAREDTESTSOURCE) test-hash.c ../src/digest.c \
	../src/base64.c


SHAREDTESTSOURCE = dispatch.c src/aws-s3fs.h shared.c testfunctions.h

AM_CFLAGS = -I../src -DAUTOTEST
if MAKE_OPENSSL_TESTS
AM_CFLAGS += -DMAKE_OPENSSL_TESTS
endif


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

$(TESTSUITE).at : $(TESTSCRIPTS)
	echo >$@ "# This file is auto-generated. Do not edit!"; echo >>$@ "";
	echo >>$@ "m4_define([AT_PACKAGE_STRING],    [aws-s3fs S3 FUSE Filesystem])";
	echo >>$@ "m4_define([AT_PACKAGE_BUGREPORT], [wolf@blazingangles.com])";
	echo >>$@ "AT_INIT([Regression Tests])";
	for f in $(TESTSCRIPTS); do echo >>$@ "m4_include([$$f])"; done

CLEANFILES = $(noinst_PROGRAMS) $(TESTSUITE).at

clean-local:
	if [ -f $(TESTSUITE) ]; then ./$(TESTSUITE) --clean; fi

distclean-local:
	rm -f atconfig atlocal
	rm -f $(TESTSUITE)

EXTRA_DIST = $(TESTSCRIPTS) $(TESTSUITE).at atlocal.in
