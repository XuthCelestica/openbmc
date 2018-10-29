#!/bin/sh
#
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
#

ACTION="$1"
CMD="voyager_bmc.py"
case "$ACTION" in
  start)
    echo -n "Setting up sensors sync to COMe handler: "
    pid=$(ps | grep -v grep | grep $CMD | awk '{print $1}')
    if [ $pid ]; then
      echo "already running"
    else
      echo "done."
      $CMD &
    fi
    ;;
  stop)
    echo -n "Stopping sensors sync to COMe handler: "
    pid=$(ps | grep -v grep | grep $CMD | awk '{print $1}')
    if [ $pid ]; then
      kill -9 $pid
    fi
    echo "done."
    ;;
  restart)
    echo -n "Restarting sensors sync to COMe handler: "
    pid=$(ps | grep -v grep | grep $CMD | awk '{print $1}')
    if [ $pid ]; then
      kill -9 $pid
    fi
    sleep 1
    echo "done."
    $CMD &
    ;;
  status)
    if [[ -n $(ps | grep -v grep | grep $CMD | awk '{print $1}') ]]; then
      echo "sensors sync to COMe handler is running"
    else
      echo "sensors sync to COMe is stopped"
    fi
    ;;
  *)
    N=${0##*/}
    N=${N#[SK]??}
    echo "Usage: $N {start|stop|status|restart}" >&2
    exit 1
    ;;
esac

exit 0
