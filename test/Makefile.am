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


AM_CFLAGS = -I../src

TESTSCRIPTS = testsuite.at commandline.at config.at

noinst_PROGRAMS = test-decodecmdline test-config test-configfile

SHAREDTESTSOURCE = dispatch.c src/aws-s3fs.h testfunctions.h

test_decodecmdline_SOURCES = $(SHAREDTESTSOURCE) test-decodecmdline.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_config_SOURCES = $(SHAREDTESTSOURCE) test-config.c \
	../src/decodecmdline.c ../src/config.c ../src/configfile.c \
	../src/common.c
test_configfile_SOURCES = $(SHAREDTESTSOURCE) test-configfile.c \
	../src/configfile.c ../src/config.c ../src/decodecmdline.c \
	../src/common.c



# See 
# http://www.lrde.epita.fr/~akim/ccmp/doc/gnuprog2/The-GNU-M4-Test-Suite.html#The%20GNU%20M4%20Test%20Suite

TESTSUITE = testsuite

check-local: atconfig $(TESTSUITE)
	$(SHELL) $(srcdir)/$(TESTSUITE) AUTOTEST_PATH=src:test

#installcheck-local: atconfig atlocal $(TESTSUITE)
#	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)

$(TESTSUITE): $(TESTSCRIPTS) atlocal.in
	autom4te -l autotest $(TESTSUITE).at -o $(TESTSUITE)

CLEANFILES = $(noinst_PROGRAMS)

clean-local:
	./$(TESTSUITE) --clean

distclean-local:
	rm -f atconfig atlocal
	rm $(TESTSUITE)

EXTRA_DIST = $(TESTSCRIPTS) atlocal.in

