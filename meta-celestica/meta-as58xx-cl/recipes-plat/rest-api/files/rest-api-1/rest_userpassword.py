#!/usr/bin/python
import os
import sys
import pexpect

def user_password(user, newpassword):
    if len(user) < 1 or len(newpassword) < 1:
        return 0
    
    # cmd = "sed 's/:.*//' /etc/passwd"
    # cmd = "cut -d: -f1 /etc/passwd"
    # child = pexpect.spawnu(cmd)
    # child.setecho(True)
    # child.logfile_read=sys.stdout
    # if child.expect([pexpect.TIMEOUT, "{0}".format(user), pexpect.EOF], timeout=1) != 1:
        # if user not exist, add new user
    ret = 0
    try:
        cmd = "useradd {0}".format(user)
        child = pexpect.spawnu(cmd)
        child.setecho(True)
        child.logfile_read=sys.stdout

        # Change user's password
        cmd = "passwd {0}".format(user)
        child = pexpect.spawnu(cmd)
        child.setecho(True)
        child.logfile_read=sys.stdout
        # if child.expect([pexpect.TIMEOUT, "Old password", pexpect.EOF], timeout=1) == 1:
        #     child.sendline(oldpassword)
        if child.expect([pexpect.TIMEOUT, "New password", pexpect.EOF], timeout=1) == 1:
            child.sendline(newpassword)
        if child.expect([pexpect.TIMEOUT, "Re-enter new password", pexpect.EOF], timeout=1) == 1:
            child.sendline(newpassword)
        if child.expect([pexpect.TIMEOUT, "password changed", pexpect.EOF], timeout=1) == 1:
            # Success
            ret = 1
        if child.expect([pexpect.TIMEOUT, "unchanged", pexpect.EOF], timeout=1) == 1:
            ret = 0
    except pexpect.exceptions.ExceptionPexpect:
        ret = 0
    os.popen("cp /etc/passwd /mnt/data/etc/passwd")
    os.popen("cp /etc/shadow /mnt/data/etc/shadow")
    return ret

def userpassword_action(data):
    result = {}
    username = data["user"]
    newpassword = data["newpassword"]

    if user_password(username, newpassword) == 1:
        result = {"result": "success"}
    else:
        result = {"result": "fail"}

    return result
