SUBDIRS = icons

upstart_jobsdir = $(datadir)/upstart/sessions
upstart_jobs_DATA = indicator-messages.conf

%.conf: %.conf.in
	sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

gsettings_SCHEMAS = com.canonical.indicator.messages.gschema.xml
@GSETTINGS_RULES@

EXTRA_DIST = \
	$(gsettings_SCHEMAS) \
	indicator-messages.conf.in

CLEANFILES = indicator-messages.conf
