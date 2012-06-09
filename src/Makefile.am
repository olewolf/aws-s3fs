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

bin_PROGRAMS = aws-s3fs
aws_s3fs_SOURCES = aws-s3fs.h sysdirs.h aws-s3fs.c \
	decodecmdline.c configfile.c common.c fix-i386-cc.c config.c \
	logger.c daemon.c singlelist.c singlelist.h

BUILT_SOURCES = sysdirs.h
CLEANFILES = sysdirs.h

sysdirs.h : sysdirs.h.src
	TMPFILE=`mktemp`; \
	REPLACE="$$""{sysconfdir}"; \
	sed < $< "s^$$REPLACE^${sysconfdir}^" >$$TMPFILE; \
	REPLACE="$$""{localstatedir}"; \
	sed < $$TMPFILE "s^$$REPLACE^${localstatedir}^" > $@
