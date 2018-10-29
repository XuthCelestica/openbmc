#!/usr/bin/python
#from __future__ import absolute_import
#from __future__ import division
#from __future__ import print_function
#from __future__ import unicode_literals

import os
import re
import sys
import time
import pexpect

IP = 'fe80::ff:31:1'
START_DELAY = 3 * 60
SSH_PASSWD = '123456'
confirm = 0

def authorized_key():
	global confirm
	COMMAND_STR = 'ssh-keygen -b 1024 -t rsa'
	#print COMMAND_STR
	scp = pexpect.spawn(COMMAND_STR)
	scp.setecho(True)
	scp.logfile_read=sys.stdout
	index_scp=scp.expect([pexpect.TIMEOUT, '(/home/root/.ssh/id_rsa)', pexpect.EOF], timeout=60)
	if index_scp == 1:
		scp.sendline("")
		index_scp=scp.expect(['(y/n)', pexpect.TIMEOUT, '(empty for no passphrase)', pexpect.EOF], timeout=60)
		if index_scp == 0:
			scp.sendline("y")
			index_scp=scp.expect(['(y/n)', pexpect.TIMEOUT, '(empty for no passphrase)', pexpect.EOF], timeout=60)
		if index_scp == 2:
			scp.sendline("")
			index_scp=scp.expect([pexpect.TIMEOUT, 'passphrase again', pexpect.EOF], timeout=60)
		if index_scp == 1:
			scp.sendline("")
			transfer_file('/home/root/.ssh/id_rsa.pub', '/root/.ssh/authorized_keys')
			confirm = 1
			return 0
	return -1

def transfer_file(file_src, file_dec):
	COMMAND_STR = 'scp -q ' + file_src + ' root@[' + IP + '%eth0.4088]:' + file_dec
	#print COMMAND_STR
	scp = pexpect.spawn(COMMAND_STR)
	scp.setecho(True)
	scp.logfile_read=sys.stdout

	index_scp=scp.expect(['password:', pexpect.TIMEOUT, '(yes/no)', pexpect.EOF], timeout=60)
	if index_scp==1:
		#print ('scp %s time out,please check network')
		scp.close()	
		return(-3)
	elif index_scp==0:
		scp.sendline(SSH_PASSWD)
		scp.read()
	elif index_scp==2:
		scp.sendline("yes")
		index_scp_1 = scp.expect(['password',pexpect.TIMEOUT,  pexpect.EOF], timeout=60)
		if(index_scp_1 == 0):
			scp.sendline(SSH_PASSWD)
		scp.read()
	elif index_scp==3:
		scp.close()	
		return 0
	scp.close()
	return 0

def main():
	global confirm
	cmd = 'rm /home/root/sensors.cfg 2>/dev/null;sensors >/home/root/sensors.cfg;psu_info >>/home/root/sensors.cfg'
	while True:
		os.popen(cmd)
		if confirm == 0:
			authorized_key()
		ret = transfer_file('/home/root/sensors.cfg', '/root')
		if ret != 0:
			time.sleep(60)
		else:
			time.sleep(5)

#time.sleep(START_DELAY)
print 'Start sync snsors to COMe'
rc = main()



