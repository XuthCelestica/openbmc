
FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://phalanx.conf \
           "

do_install_append() {
    install -d ${D}${sysconfdir}/sensors.d
    install -m 644 ../phalanx.conf ${D}${sysconfdir}/sensors.d/phalanx.conf
}
