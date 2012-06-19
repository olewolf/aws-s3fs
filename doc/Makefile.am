SUBDIRS = man

metadocsdir = $(docdir)
nobase_dist_metadocs_DATA = copyright

clean:
	rm -rf doxygen

if MAKE_DOXYGEN
targets = doxygen
endif

doxygen: Doxyfile main.doc
	@if [ "z$(DOXYGEN)" != "z" ]; then \
	    @DOXYGEN@; \
	fi

.PHONY: doxygen

all-local: $(targets)
