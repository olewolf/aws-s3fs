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

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src data doc test

metadocsdir = $(docdir)
nobase_dist_metadocs_DATA = README NEWS AUTHORS ChangeLog

install-data-hook:
	if [ -f $(docdir)/ChangeLog ]; then \
	    mv $(docdir)/ChangeLog $(docdir)/changelog; \
	    gzip -f -9 $(docdir)/changelog; \
	fi

uninstall-hook:
	rm -rf $(docdir)/changelog.gz; \
	rmdir --ignore-fail-on-non-empty $(docdir)

doxygen:
	@if [ "$(HAVE_DOXYGEN)" = "true" -a "$(HAVE_GRAPHVIZ)" = "true" ]; then \
	    cd doc; make doxygen; \
	else \
	    echo "You need Doxygen and GraphViz in order to generate source code documentation."; \
	fi

splint:
	@if [ "$(HAVE_SPLINT)" = "true" ]; then \
	    @SPLINT@ src/*.h src/*.c; \
	else \
	    echo "You need splint in order to perform a link check."; \
	fi

rats:
	@if [ "$(HAVE_RATS)" = "true" ]; then \
	    @RATS@ src/*.h src/*.c; \
	else \
	    echo "You need rats in order to perform a rats check."; \
	fi

flawfinder:
	@if [ "$(HAVE_FLAWFINDER)" = "true" ]; then \
	    @FLAWFINDER@ src/*.h src/*.c; \
	else \
	    echo "You need flawfinder in order to perform a flawfinder check."; \
	fi

pscan:
	@if [ "$(HAVE_PSCAN)" = "true" ]; then \
	    @PSCAN@ -v src/*.h src/*.c; \
	else \
	    echo "You need pscan in order to perform a pscan check."; \
	fi


#AUX_DIST = $(ac_aux_dir)/config.guess \
#$(ac_aux_dir)/config.sub \
#$(ac_aux_dir)/install-sh \
#$(ac_aux_dir)/ltconfig \
#$(ac_aux_dir)/ltmain.sh \
#$(ac_aux_dir)/mdate-sh \
#$(ac_aux_dir)/missing \
#$(ac_aux_dir)/mkinstalldirs
#AUX_DIST_EXTRA          = $(ac_aux_dir)/readline.m4 \
#EXTRA_DIST                = bootstrap
#
#MAINTAINERCLEANFILES         = Makefile.in aclocal.m4 configure config-h.in \
#                        stamp-h.in $(AUX_DIST)
#
#dist-hook:
#        (cd $(distdir) && mkdir $(ac_aux_dir))
#        for file in $(AUX_DIST) $(AUX_DIST_EXTRA); do \
#          cp $$file $(distdir)/$$file; \
#        done
