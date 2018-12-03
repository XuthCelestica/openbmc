FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://rsyslog.conf \
            file://rsyslog.logrotate \
            file://rotate_console_log \
"

do_install_append() {
  dst="${D}/usr/local/fbpackages/rotate"
  install -d $dst
  install -m 644 ${WORKDIR}/rsyslog.conf ${D}${sysconfdir}/rsyslog.conf
  install -m 644 ${WORKDIR}/rsyslog.logrotate ${D}${sysconfdir}/logrotate.rsyslog
  install -m 755 ${WORKDIR}/rotate_console_log ${dst}/console_log

  sed -i "s/__PLATFORM_NAME__/${MACHINE}/g;s/__OPENBMC_VERSION__/${OPENBMC_VERSION}/g" ${D}${sysconfdir}/rsyslog.conf
}

FILES_${PN} += "/usr/local/fbpackages/rotate"
