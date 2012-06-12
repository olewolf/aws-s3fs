dist_sysconf_DATA = aws-s3fs.conf

logrotateconfdir = $(sysconfdir)/logrotate.d
dist_logrotateconf_DATA = logrotate.d/aws-s3fs

#dist_data_DATA = stathash.mph

install-data-local:
	if [ -n "$$OMITCACHECREATION" ]; then \
	    CACHEDIR="${localstatedir}/cache/aws-s3fs"; \
	    mkdir -p $$CACHEDIR; \
	    chmod 777 $$CACHEDIR; \
	    chmod +t $$CACHEDIR; \
	fi

uninstall:
	if [ -n "$$OMITCACHECREATION" ]; then \
	    CACHEDIR="${localstatedir}/cache/aws-s3fs"; \
	    rm -rf $$CACHEDIR; \
	fi
