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
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://bios_flash.sh \
           file://bcm5387.sh \
           file://at93cx6_util.sh \
           file://at93cx6_util.py \
           file://at93cx6.py \
           file://lsb_release \
           file://mount_data0.sh \
           file://rc.early \
           file://sol.sh \
           file://us_console.sh \
           file://voyager_power.sh \
           file://power-on.sh \
           file://board-utils.sh \
           file://create_vlan_intf \
           file://voyager_bmc.py \
           file://sensors_setup.sh \
           file://lm57_temp.sh \
          "

OPENBMC_UTILS_FILES += " \
  bios_flash.sh bcm5387.sh at93cx6_util.sh at93cx6.py at93cx6_util.py sol.sh us_console.sh voyager_power.sh power-on.sh board-utils.sh lm57_temp.sh \
  "

RDEPENDS_${PN} += " python3-core bash"
DEPENDS_append += " update-rc.d-native"

do_install_board() {
  # for backward compatible, create /usr/local/fbpackages/utils/ast-functions
  olddir="/usr/local/fbpackages/utils"
  localbindir="/usr/local/bin"
  install -d ${D}${olddir}
  install -d ${D}${localbindir}
  ln -s "/usr/local/bin/openbmc-utils.sh" ${D}${olddir}/ast-functions

  install -m 0755 bios_flash.sh ${D}${localbindir}/bios_flash.sh
  install -m 0755 bcm5387.sh ${D}${localbindir}/bcm5387.sh
  install -m 0755 at93cx6_util.sh ${D}${localbindir}/at93cx6_util.sh
  install -m 0755 at93cx6_util.py ${D}${localbindir}/at93cx6_util.py
  install -m 0755 at93cx6.py ${D}${localbindir}/at93cx6.py
  install -m 0755 lsb_release ${D}${localbindir}/lsb_release
  install -m 0755 voyager_power.sh ${D}${localbindir}/voyager_power.sh
  install -m 0755 board-utils.sh ${D}${localbindir}/board-utils.sh
  install -m 0755 voyager_bmc.py ${D}${localbindir}/voyager_bmc.py
  install -m 0755 sensors_setup.sh ${D}${localbindir}/sensors_setup.sh
  install -m 0755 lm57_temp.sh ${D}${localbindir}/lm57_temp.sh

  # init
  install -d ${D}${sysconfdir}/init.d
  install -d ${D}${sysconfdir}/rcS.d
  # the script to mount /mnt/data
  install -m 0755 ${WORKDIR}/mount_data0.sh ${D}${sysconfdir}/init.d/mount_data0.sh
  #update-rc.d -r ${D} mount_data0.sh start 03 S .
  #install -m 0755 ${WORKDIR}/rc.early ${D}${sysconfdir}/init.d/rc.early
  #update-rc.d -r ${D} rc.early start 04 S .
  # create VLAN intf automatically
  install -d ${D}/${sysconfdir}/network/if-up.d
  install -m 755 create_vlan_intf ${D}${sysconfdir}/network/if-up.d/create_vlan_intf

  install -m 0755 power-on.sh ${D}${sysconfdir}/init.d/power-on.sh
  update-rc.d -r ${D} power-on.sh start 85 S .
  install -m 0755 ${WORKDIR}/rc.local ${D}${sysconfdir}/init.d/rc.local
  update-rc.d -r ${D} rc.local start 99 2 3 4 5 .
}

do_install_append() {
  do_install_board
}

FILES_${PN} += "${sysconfdir}"
