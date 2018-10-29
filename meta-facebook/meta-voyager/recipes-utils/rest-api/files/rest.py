#!/usr/bin/env python
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


from ctypes import *
from bottle import route, run, template, request, response, ServerAdapter
from bottle import abort
from wsgiref.simple_server import make_server, WSGIRequestHandler, WSGIServer
import json
#import ssl
import socket
import os
import rest_fruid
import rest_server
import rest_sensors
import rest_psu
import rest_led1
import rest_led2
import rest_bmc
import rest_fan
import rest_fan_ctrl

CONSTANTS = {
    'certificate': '/usr/lib/ssl/certs/rest_server.pem',
}

# Handler for root resource endpoint
@route('/api')
def rest_api():
   result = {
                "Information": {
                    "Description": "Voyager RESTful API Entry",
                    },
                "Actions": [],
                "Resources": [ "sys"],
             }

   return result

# Handler for sys resource endpoint
@route('/api/sys')
def rest_sys():
    result = {
                "Information": {
                    "Description": "Voyager System",
                    },
                "Actions": [],
                "Resources": [ "mb", "bmc", "server", "sensors", "psu", "led1", "led2", "fan", "fan_ctrl"],
             }

    return result

# Handler for sys/mb resource endpoint
@route('/api/sys/mb')
def rest_sys():
    result = {
                "Information": {
                    "Description": "System Motherboard",
                    },
                "Actions": [],
                "Resources": [ "fruid"],
             }

    return result

# Handler for sys/mb/fruid resource endpoint
@route('/api/sys/mb/fruid')
def rest_fruid_hdl():
  return rest_fruid.get_fruid()

# Handler for sys/bmc resource endpoint
@route('/api/sys/bmc')
def rest_bmc_hdl():
    return rest_bmc.get_bmc()

# Handler for sys/server resource endpoint
@route('/api/sys/server')
def rest_server_hdl():
    return rest_server.get_server()

# Handler for uServer resource endpoint
@route('/api/sys/server', method='POST')
def rest_server_act_hdl():
    data = json.load(request.body)
    return rest_server.server_action(data)

# Handler for sensors resource endpoint
@route('/api/sys/sensors')
def rest_sensors_hdl():
  return rest_sensors.get_sensors()

# Handler for psu resource endpoint
@route('/api/sys/psu')
def rest_psu_hdl():
  return rest_psu.get_psu()

# Handler for psu setting resource endpoint
@route('/api/sys/psu', method='POST')
def rest_psu_act_hdl():
    data = json.load(request.body)
    return rest_psu.psu_action(data)

# Handler for LED1 resource endpoint
@route('/api/sys/led1')
def rest_led1_hdl():
  return rest_led1.get_led1()

# Handler for LED1 setting resource endpoint
@route('/api/sys/led1', method='POST')
def rest_led1_act_hdl():
    data = json.load(request.body)
    return rest_led1.led1_action(data)

# Handler for LED2 resource endpoint
@route('/api/sys/led2')
def rest_led2_hdl():
  return rest_led2.get_led2()

# Handler for LED2 setting resource endpoint
@route('/api/sys/led2', method='POST')
def rest_led2_act_hdl():
    data = json.load(request.body)
    return rest_led2.led2_action(data)

# Handler for fan resource endpoint
@route('/api/sys/fan')
def rest_fan_hdl():
  return rest_fan.get_fan()

# Handler for fan setting resource endpoint
@route('/api/sys/fan', method='POST')
def rest_fan_act_hdl():
    data = json.load(request.body)
    return rest_fan.fan_action(data)

# Handler for fan_ctrl resource endpoint
@route('/api/sys/fan_ctrl')
def rest_fan_ctrl_hdl():
  return rest_fan_ctrl.get_fan_ctrl()

# Handler for fan_ctrl setting resource endpoint
@route('/api/sys/fan_ctrl', method='POST')
def rest_fan_ctrl_act_hdl():
    data = json.load(request.body)
    return rest_fan_ctrl.fan_ctrl_action(data)


run(host = "::", port = 8080)

# SSL Wrapper for Rest API
class SSLWSGIRefServer(ServerAdapter):
    def run(self, handler):
        if self.quiet:
            class QuietHandler(WSGIRequestHandler):
                def log_request(*args, **kw): pass
            self.options['handler_class'] = QuietHandler

        # IPv6 Support
        server_cls = self.options.get('server_class', WSGIServer)

        if ':' in self.host:
            if getattr(server_cls, 'address_family') == socket.AF_INET:
                class server_cls(server_cls):
                    address_family = socket.AF_INET6

        srv = make_server(self.host, self.port, handler,
                server_class=server_cls, **self.options)
        srv.socket = ssl.wrap_socket (
                srv.socket,
                certfile=CONSTANTS['certificate'],
                server_side=True)
        srv.serve_forever()

# Use SSL if the certificate exists. Otherwise, run without SSL.
if os.access(CONSTANTS['certificate'], os.R_OK):
    run(server=SSLWSGIRefServer(host="::", port=8443))
else:
    run(host = "::", port = 8080)
