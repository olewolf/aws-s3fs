SUBDIRS = man

metadocsdir = $(docdir)
nobase_dist_metadocs_DATA = copyright

clean:
	rm -rf doxygen

doxygen:
	if [ "z$(DOXYGEN)" != "z" ]; then \
	    doxygen; \
	fi
