/*
 * Copyright 2014-present Facebook. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <sys/io.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "i2c-dev.h"


#define ISP_DEBUG 1
#ifdef ISP_DEBUG
#define DPRINTF(fmt, args...) printf(fmt, ##args);
#else
#define DPRINTF(FMT, args...)
#endif

#define PSU_MAX 2
#define SYS_CPLD_BUS 13
#define SYS_CPLD_I2C_ADDR 0x31
#define CHANNEL_I2C_BUS 7
#define CHANNEL_I2C_ADDR 0x70
#define PSU_STATUS_REG 0x42
#define CHANNEL_CTRL_REG 0x0
#define PSU1_CHANNEL_VALUE 0x40
#define PSU2_CHANNEL_VALUE 0x80
#define PSU1_I2C_ADDR 0x59
#define PSU2_I2C_ADDR 0x5a


#define PMBUS_READ_VIN			0x88
#define PMBUS_READ_IIN			0x89
#define PMBUS_READ_VOUT			0x8B
#define PMBUS_READ_IOUT			0x8C
#define PMBUS_READ_TEMPERATURE_1	0x8D
#define PMBUS_READ_TEMPERATURE_2	0x8E
#define PMBUS_READ_TEMPERATURE_3	0x8F
#define PMBUS_READ_FAN_SPEED_1		0x90
#define PMBUS_READ_POUT			0x96
#define PMBUS_READ_PIN			0x97
#define PMBUS_READ_REVISION		0x9b
#define PMBUS_READ_SN			0x9e

#define PSU_SN_SIZE 18
#define PSU_REVISION_SIZE 3

#define PSU_ABSENT 0x1
#define PSU_NOT_POWER 0x2


static int isp_i2c_read_block(int bus, int addr, unsigned char reg, unsigned char *buf);
static int isp_i2c_read(int bus, int addr, unsigned char reg, int size);
static int isp_i2c_write(int bus, int addr, unsigned char reg, unsigned value, int size);
static int open_i2c_dev(int i2cbus, char *filename, size_t size);
static int psu_channel_open(int id);
static int psu_status_check(int id);
static long reg2data_linear(int data, int linear_mode);
static int psu_info_show(int id);

static int isp_i2c_read_block(int bus, int addr, unsigned char reg, unsigned char *buf)
{
	int res = -1, file, count = 10;
	char filename[20];

	file = open_i2c_dev(bus, filename, sizeof(filename));
	if(file < 0) {
		DPRINTF("%s: open eror!\n", __func__);
		return -1;
	}
	if(ioctl(file, I2C_SLAVE_FORCE, addr) < 0) {
		DPRINTF("%s: ioctl eror!\n", __func__);
		close(file);
		return -1;
	}
	while(count-- && (res < 0)) {
		res = i2c_smbus_read_block_data(file, reg, buf);
	}

	close(file);
	return res;
}

static int isp_i2c_read(int bus, int addr, unsigned char reg, int size)
{
	int res = -1, file, count = 10;
	char filename[20];

	file = open_i2c_dev(bus, filename, sizeof(filename));
	if(file < 0) {
		DPRINTF("%s: open eror!\n", __func__);
		return -1;
	}
	if(ioctl(file, I2C_SLAVE_FORCE, addr) < 0) {
		DPRINTF("%s: ioctl eror!\n", __func__);
		close(file);
		return -1;
	}
	while(count-- && (res < 0)) {
		if(size == 1)
			res = i2c_smbus_read_byte_data(file, reg);
		else
			res = i2c_smbus_read_word_data(file, reg);
	}

	close(file);
	return res;
}

static int isp_i2c_write(int bus, int addr, unsigned char reg, unsigned int value, int size)
{
	int res = -1, file, count = 10;
	char filename[20];

	file = open_i2c_dev(bus, filename, sizeof(filename));
	if(file < 0) {
		return -1;
	}
	if(ioctl(file, I2C_SLAVE_FORCE, addr) < 0) {
		close(file);
		return -1;
	}
	while(count-- && (res < 0)) {
		if(size == 1)
			res = i2c_smbus_write_byte_data(file, reg, value);
		else
			res = i2c_smbus_write_word_data(file, reg, value);
	}

	close(file);
	return res;
}
static int open_i2c_dev(int i2cbus, char *filename, size_t size)
{
	int file;

	snprintf(filename, size, "/dev/i2c-%d", i2cbus);
	filename[size - 1] = '\0';
	file = open(filename, O_RDWR);

	if (file < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		sprintf(filename, "/dev/i2c-%d", i2cbus);
		file = open(filename, O_RDWR);
	}

	return file;
}

static int psu_channel_open(int id)
{
	int ret;
	int value = 0;

	if(id == 1) {
		value = PSU1_CHANNEL_VALUE;
	} else if(id == 2) {
		value = PSU2_CHANNEL_VALUE;
	} else {
		return -1;
	}
	ret = isp_i2c_write(CHANNEL_I2C_BUS, CHANNEL_I2C_ADDR, CHANNEL_CTRL_REG, value, 1);
	if(ret < 0) {
		return -1;
	}
	
	return 0;
}

static int psu_status_check(int id)
{
	int ret;
	int bits = 0;
	
	ret = isp_i2c_read(SYS_CPLD_BUS, SYS_CPLD_I2C_ADDR, PSU_STATUS_REG, 1);
	if(ret < 0) {
		printf("%s: isp_i2c_read error!\n",__func__);
		return -1;
	}

	if(id == 1) {
		bits = 0;
	} else if(id == 2) {
		bits = 4;
	} else {
		printf("PSU%d not support!\n", id);
		return -1;
	}

	if(ret & (0x4 << bits)) {
		printf("PSU%d is not present\n", id);
		return PSU_ABSENT;
	}
	if((ret & (0x2 << bits)) == 0) {
		printf("PSU%d is not power on\n", id);
		return PSU_NOT_POWER;
	}
	
	return 0;
}
static long reg2data_linear(int data, int linear_mode)
{
	short exponent;
	int mantissa;
	long val;

	if(linear_mode == 16) {
		exponent = -9;
		mantissa = (unsigned short)data;
	} else {
		exponent = ((short)data) >> 11;
		mantissa = ((short)((data & 0x7ff) << 5)) >> 5;
	}

	val = mantissa;
	val = val * 1000L;

	if (exponent >= 0)
		val <<= exponent;
	else
		val >>= -exponent;

	return val;
}

static int psu_info_show(int id)
{
	int ret, size;
	int psu_addr = 0;
	long val;
	unsigned char alarm_bit_mask = 0;
	unsigned char sn[PSU_SN_SIZE + 1];
	unsigned char revision[PSU_REVISION_SIZE + 1];

	if(id == 1) {
		psu_addr = PSU1_I2C_ADDR;
		alarm_bit_mask = 0x01;
	} else if(id == 2) {
		psu_addr = PSU2_I2C_ADDR;
		alarm_bit_mask = 0x10;
	} else {
		return -1;
	}
	/*SN*/
	size = isp_i2c_read_block(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_SN, sn);
	if(size < 0) {
		return -1;
	}
	sn[size] = '\0';
	printf("SN: %s\n", sn);

	/*revision*/
	size = isp_i2c_read_block(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_REVISION, revision);
	if(size < 0) {
		return -1;
	}
	revision[size] = '\0';
	printf("Revision: V%c.%c%c\n", revision[0], revision[1], revision[2]);

	/*VIN*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_VIN, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Vin:      %d.%02d V\n", val / 1000, val % 1000);

	/*Vout*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_VOUT, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 16);
	printf("Vout:      %d.%02d V\n", val / 1000, val % 1000);

	/*Iin*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_IIN, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Iin:      %d.%02d A\n", val / 1000, val % 1000);

	/*Iout*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_IOUT, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Iout:      %d.%02d A\n", val / 1000, val % 1000);

	/*Pin*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_PIN, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Pin:      %d.%02d W\n", val / 1000, val % 1000);

	/*Pout*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_POUT, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Pout:      %d.%02d W\n", val / 1000, val % 1000);

	/*temp1*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_TEMPERATURE_1, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Temp1:      %d.%02d C\n", val / 1000, val % 1000);

	/*temp1*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_TEMPERATURE_2, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Temp2:      %d.%02d C\n", val / 1000, val % 1000);

	/*temp3*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_TEMPERATURE_3, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Temp3:      %d.%02d C\n", val / 1000, val % 1000);

	/*FAN1*/
	ret = isp_i2c_read(CHANNEL_I2C_BUS, psu_addr, PMBUS_READ_FAN_SPEED_1, 2);
	if(ret < 0) {
		return -1;
	}
	val = reg2data_linear(ret, 11);
	printf("Fan1:      %d.%02d RPM\n", val / 1000, val % 1000);
    /*psu alarm */
    ret = isp_i2c_read(SYS_CPLD_BUS, SYS_CPLD_I2C_ADDR, PSU_STATUS_REG, 1);
    if(ret < 0) {
        return -1;
    }

	if((ret & alarm_bit_mask) == 0 ) {
		printf("ALARM:      alarm\n");
	} else {
		printf("ALARM:      normal\n");
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	int i;

	for(i = 1; i <= PSU_MAX; i++) {
		if(!psu_status_check(i)) {
			ret = psu_channel_open(i);
			if(ret < 0) {
				printf("Open PSU%d channel fail!\n", i);
			} else {
				printf("PSU%d info:\n", i);
				psu_info_show(i);
			}
		}
	}
	
	return 0;
}
