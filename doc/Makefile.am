SUBDIRS = man

metadocsdir = $(docdir)
nobase_dist_metadocs_DATA = copyright

clean:
	rm -rf doxygen

doxygen: Doxyfile main.doc
	if [ "z$(DOXYGEN)" != "z" ]; then \
	    @DOXYGEN@; \
	fi

.PHONY: doxygen

targets = doxygen

all-local: $(targets)
