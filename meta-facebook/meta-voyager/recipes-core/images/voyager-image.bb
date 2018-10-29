# Copyright 2018-present Facebook. All Rights Reserved.

inherit kernel_fitimage

require recipes-core/images/fb-openbmc-image.bb

# Changing the image compression from gz to lzma achieves 30% saving (~3M).
IMAGE_FSTYPES += "cpio.lzma.u-boot"

# Include modules in rootfs

IMAGE_INSTALL += " \
  packagegroup-openbmc-base \
  packagegroup-openbmc-net \
  packagegroup-openbmc-python3 \
  packagegroup-openbmc-rest3 \
  at93cx6-util \
  ast-mdio \
  bcm5396-util \
  openbmc-utils \
  bitbang \
  cpldupdate \
  libcpldupdate-dll-echo \
  libcpldupdate-dll-gpio \
  openbmc-utils \
  watchdog-ctrl \
  bc \
  fan-ctrl \
  iproute2 \
  dhcp-client \
  flashrom \
  memtester \
  mkeeprom \
  voyager-eeprom \
  rest-api \
  stress \
  psu-info \
  "

IMAGE_FEATURES += " \
  ssh-server-openssh \
  tools-debug \
  "

DISTRO_FEATURES += " \
  ext2 \
  ipv6 \
  nfs \
  usbgadget \
  usbhost \
  "
