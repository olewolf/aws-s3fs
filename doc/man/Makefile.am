man1_MANS = aws-s3fs.1
man5_MANS = aws-s3fs.conf.5

%.1 : %.1.src
	REPLACE="$$""{sysconfdir}"; \
	cat $< | sed "s^$$REPLACE^${sysconfdir}^" > $@

%.5 : %.5.src
	REPLACE="$$""{sysconfdir}"; \
	cat $< | sed "s^$$REPLACE^${sysconfdir}^" > $@


install-data-hook:
	manpages=( ${man1_MANS} ); \
	for manpage in $${manpages[@]}; do \
		if [ -f ${mandir}/man1/$$manpage ]; then gzip -f -9 ${mandir}/man1/$$manpage; fi; \
	done; \
	manpages=( ${man5_MANS} ); \
	for manpage in $${manpages[@]}; do \
		if [ -f ${mandir}/man1/$$manpage ]; then gzip -f -9 ${mandir}/man5/$$manpage; fi; \
	done;

uninstall-hook:
	manpages=( ${man1_MANS} ); \
	for manpage in $${manpages[@]}; do \
		rm -f ${mandir}/man1/$$manpage.gz; \
	done; \
	manpages=( ${man5_MANS} ); \
	for manpage in $${manpages[@]}; do \
		rm -f ${mandir}/man5/$$manpage.gz; \
	done
