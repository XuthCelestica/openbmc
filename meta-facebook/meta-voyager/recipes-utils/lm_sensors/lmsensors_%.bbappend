# Copyright 2014-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://voyager.conf \
           "

do_install_board_config() {
    install -d ${D}${sysconfdir}/sensors.d
    install -m 644 ../voyager.conf ${D}${sysconfdir}/sensors.d/voyager.conf
}

do_install_append() {
    do_install_board_config
}
