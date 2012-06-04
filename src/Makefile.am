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

AM_CFLAGS = -O2 -Wall -Werror $(libfuse_CFLAGS) $(libconfigxx_CFLAGS)
AM_CXXFLAGS = -O2 -Wall -Werror $(libfuse_CFLAGS) $(libconfigxx_CFLAGS)

bin_PROGRAMS = aws-s3fs
aws_s3fs_SOURCES = aws-s3fs.c decodecmdline.c configfile.c common.c fix-i386-cc.c config.c aws-s3fs.h splint.h
aws_s3fs_LDADD = $(libfuse_LIBS) $(libconfigxx_LIBS)

#splint.h: splint.generate-h
#	sed < $<  >splint.h 's/^BEGIN-.\+-END$$/#define SPLINT_INSTALLED/'
#
#CLEANFILES = splint.h
#
#decodecmdline.o: splint.h
#
#splint.h: splint.generate-h
#	sed < $<  >splint.h 's/^BEGIN-.\+-END$$/#define SPLINT_INSTALLED/'
