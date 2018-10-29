/*
 * fand
 *
 * Copyright 2016-present Celestica. All Rights Reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Daemon to manage the fan speed to ensure that we stay within a reasonable
 * temperature range.  We're using a simplistic algorithm to get started:
 *
 * If the fan is already on high, we'll move it to medium if we fall below
 * a top temperature.  If we're on medium, we'll move it to high
 * if the temperature goes over the top value, and to low if the
 * temperature falls to a bottom level.  If the fan is on low,
 * we'll increase the speed if the temperature rises to the top level.
 *
 * To ensure that we're not just turning the fans up, then back down again,
 * we'll require an extra few degrees of temperature drop before we lower
 * the fan speed.
 *
 * We check the RPM of the fans against the requested RPMs to determine
 * whether the fans are failing, in which case we'll turn up all of
 * the other fans and report the problem..
 *
 * TODO:  Implement a PID algorithm to closely track the ideal temperature.
 * TODO:  Determine if the daemon is already started.
 */

/* Yeah, the file ends in .cpp, but it's a C program.  Deal. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include "watchdog.h"
#include <fcntl.h>
#include "i2c-dev.h"


/* Sensor definitions */
#define INTERNAL_TEMPS(x) (x)
#define EXTERNAL_TEMPS(x) (x)
#define PWM_UNIT_MAX 32

#define uchar unsigned char
#define swab16(x) \
        ((unsigned short)( \
                (((unsigned short)(x) & (unsigned short)0x00ffU) << 8) | \
                (((unsigned short)(x) & (unsigned short)0xff00U) >> 8) ))

/*
 * The sensor for the uServer CPU is not on the CPU itself, so it reads
 * a little low.  We are special casing this, but we should obviously
 * be thinking about a way to generalize these tweaks, and perhaps
 * the entire configuration.  JSON file?
 */
#define USERVER_TEMP_FUDGE INTERNAL_TEMPS(0)
#define BAD_TEMP INTERNAL_TEMPS(-60)

#define FAN_FAILURE_THRESHOLD 2 /* How many times can a fan fail */
#define FAN_SHUTDOWN_THRESHOLD 60
#define FAN_LED_BLUE "1"
#define FAN_LED_RED "2"

#define REPORT_TEMP 720  /* Report temp every so many cycles */

/* Sensor limits and tuning parameters */
#define TEMP_TOP INTERNAL_TEMPS(70)
#define TEMP_BOTTOM INTERNAL_TEMPS(40)

/*
 * Toggling the fan constantly will wear it out (and annoy anyone who
 * can hear it), so we'll only turn down the fan after the temperature
 * has dipped a bit below the point at which we'd otherwise switch
 * things up.
 */

#define VOYAGER_FAN_LOW 41
#define VOYAGER_FAN_MEDIUM 51
#define VOYAGER_FAN_HIGH 71
#define VOYAGER_FAN_MAX 99
//#define VOYAGER_FAN_ONEFAILED_RAISE_PEC 20
#define VOYAGER_RAISING_TEMP_LOW 30
#define VOYAGER_RAISING_TEMP_HIGH 35
#define VOYAGER_FALLING_TEMP_LOW 28
#define VOYAGER_FALLING_TEMP_HIGH 33
#define VOYAGER_SYSTEM_WARN 42
#define VOYAGER_SYSTEM_LIMIT 45

#define VOYAGER_TEMP_I2C_BUS 1
#define VOYAGER_FANTRAY_I2C_BUS 8
#define VOYAGER_FANTRAY_CPLD_ADDR 0x33

#define VOYAGER_SYS_I2C_BUS 13
#define VOYAGER_SYS_CPLD_ADDR 0x31


#define VOYAGER_PSUS 2
#define VOYAGER_PSU_CHANNEL_I2C_BUS 7
#define VOYAGER_PSU_CHANNEL_I2C_ADDR 0x70

#define FANS 5

#define FAN_FAILURE_OFFSET 30

#define VOYAGER_FILENAME_LEN 100


struct sensor_info {
	uchar bus;
	uchar addr;
	uchar reg;
	uchar temp;
	uchar limit;
	uchar recover;
	int (*read_temp)(uchar bus, uchar addr, uchar reg);
};
struct fan_info_stu {
	const char *name;
	uchar front_fan_reg;
	uchar rear_fan_reg;
	uchar pwm_reg;
	uchar fan_led_reg;
	uchar fan_present_stat_reg;
	uchar fan_fail_stat_reg;
	uchar present;
	uchar failed;
};

struct voyager_board_info_stu {
	const char *name;
	struct sensor_info *critical;
	struct sensor_info *alarm;
};
struct voyager_psu_info_stu {
	const char *name;
	uchar psu_channal;
	uchar psu_addr;
	uchar present_reg;
	uchar present_bit;
	uchar present;
};

struct galaxy100_fan_failed_control_stu {
	int failed_num;
	int low_level;
	int mid_level;
	int high_level;
	int alarm_level;
};

static int voyager_i2c_read(int bus, int addr, uchar reg, int byte);
static int voyager_i2c_write(int bus, int addr, uchar reg, int value, int byte);
static int open_i2c_dev(int i2cbus, char *filename, size_t size);
static int read_tmp75_temp(uchar bus, uchar addr, uchar reg);
static int read_cpu_temp(uchar bus, uchar addr, uchar reg);
static int read_lm57_adc(uchar bus, uchar addr, uchar reg);
static int read_critical_max_temp(void);
static int read_alarm_max_temp(void);
static int calculate_falling_fan_pwm(int temp);
static int calculate_raising_fan_pwm(int temp);
static int voyager_set_fan(int fan, int value);
static int voyager_fan_is_okey(int fan);
static int voyager_write_fan_led(int fan, const char *color);
static int system_shutdown(const char *why);

struct sensor_info sensor_critical_info = {
	.bus = 3,
	.addr = 0x4a,
	.reg = 0,
	.temp = 0,
	.limit = 0,
	.recover = 0,
	.read_temp = &read_tmp75_temp,
};

struct sensor_info sensor_th_alarm_info = {
	.bus = 0xff,
	.addr = 0,
	.reg = 12,
	.temp = 0,
	.limit = 1476,
	.recover = 1582,
	.read_temp = &read_lm57_adc,
};

struct sensor_info sensor_cpu_alarm_info = {
	.bus = 0,
	.addr = 0x33,
	.reg = 0,
	.temp = 0,
	.limit = 100,
	.recover = 90,
	.read_temp = &read_cpu_temp,
};

struct sensor_info sensor_sys_outlet_alarm_info = {
	.bus = 3,
	.addr = 0x4b,
	.reg = 0,
	.temp = 0,
	.limit = 70,
	.recover = 60,
	.read_temp = &read_tmp75_temp,
};


struct voyager_board_info_stu voyager_board_info[] = {
{
	.name = "critical",
	.critical = &sensor_critical_info,
	.alarm = NULL,
},
{
	.name = "TH on-board",
	.critical = NULL,
	.alarm = &sensor_th_alarm_info,
},
{
	.name = "cpu internal",
	.critical = NULL,
	.alarm = &sensor_cpu_alarm_info,
},
{
	.name = "sys outlet",
	.critical = NULL,
	.alarm = &sensor_sys_outlet_alarm_info,
},

NULL,
};

struct fan_info_stu voyager_fantray_info[] = {
{
	.name = "fantray1",
	.front_fan_reg = 0x10,
	.rear_fan_reg = 0x11,
	.pwm_reg = 0x20,
	.fan_led_reg = 0x25,
	.fan_present_stat_reg = 0x08,
	.fan_fail_stat_reg = 0x09,
	.present = 1,
	.failed = 0,
},
{
	.name = "fantray2",
	.front_fan_reg = 0x12,
	.rear_fan_reg = 0x13,
	.pwm_reg = 0x21,
	.fan_led_reg = 0x25,
	.fan_present_stat_reg = 0x08,
	.fan_fail_stat_reg = 0x09,
	.present = 1,
	.failed = 0,
},
{
	.name = "fantray3",
	.front_fan_reg = 0x14,
	.rear_fan_reg = 0x15,
	.pwm_reg = 0x22,
	.fan_led_reg = 0x26,
	.fan_present_stat_reg = 0x08,
	.fan_fail_stat_reg = 0x09,
	.present = 1,
	.failed = 0,
},
{
	.name = "fantray4",
	.front_fan_reg = 0x16,
	.rear_fan_reg = 0x17,
	.pwm_reg = 0x23,
	.fan_led_reg = 0x26,
	.fan_present_stat_reg = 0x08,
	.fan_fail_stat_reg = 0x09,
	.present = 1,
	.failed = 0,
},		
{
	.name = "fantray5",
	.front_fan_reg = 0x18,
	.rear_fan_reg = 0x19,
	.pwm_reg = 0x24,
	.fan_led_reg = 0x27,
	.fan_present_stat_reg = 0x08,
	.fan_fail_stat_reg = 0x09,
	.present = 1,
	.failed = 0,
},
NULL,
};

struct voyager_psu_info_stu voyager_psu_info[] = {
{
	.name = "PSU1",
	.psu_channal = 0x40,
	.psu_addr = 0x59,
	.present_reg = 0x42,
	.present_bit = 0,
	.present = 0,
},
{
	.name = "PSU2",
	.psu_channal = 0x80,
	.psu_addr = 0x59,
	.present_reg = 0x42,
	.present_bit = 4,
	.present = 0,
},
NULL,
};


struct galaxy100_fan_failed_control_stu voyager_fan_failed_control[] = {
{
	.failed_num = 1,
	.low_level = 61,
	.mid_level = 71,
	.high_level = 91,
	.alarm_level = 99,
},
NULL,
};

#define BOARD_INFO_SIZE (sizeof(voyager_board_info) / sizeof(struct voyager_board_info_stu))
#define FANTRAY_INFO_SIZE (sizeof(voyager_fantray_info) / sizeof(struct fan_info_stu))
#define PSU_INFO_SIZE (sizeof(voyager_psu_info) / sizeof(struct voyager_psu_info_stu))


int fan_low = VOYAGER_FAN_LOW;
int fan_medium = VOYAGER_FAN_MEDIUM;
int fan_high = VOYAGER_FAN_HIGH;
int fan_max = VOYAGER_FAN_MAX;
int total_fans = FANS;
int fan_offset = 0;

int temp_bottom = TEMP_BOTTOM;
int temp_top = TEMP_TOP;

int report_temp = REPORT_TEMP;
bool verbose = false;

static int voyager_i2c_read(int bus, int addr, uchar reg, int byte)
{
	int res, file;
	char filename[20];

	file = open_i2c_dev(bus, filename, sizeof(filename));
	if(file < 0) {
		return -1;
	}
	if(ioctl(file, I2C_SLAVE_FORCE, addr) < 0) {
		close(file);
		return -1;
	}
	if(byte == 1)
		res = i2c_smbus_read_byte_data(file, reg);
	else if(byte == 2)
		res = i2c_smbus_read_word_data(file, reg);
	else {
		close(file);
		return -1;
	}

	close(file);
	return res;
}
static int voyager_i2c_write(int bus, int addr, uchar reg, int value, int byte)
{
	int res, file;
	char filename[20];

	file = open_i2c_dev(bus, filename, sizeof(filename));
	if(file < 0) {
		return -1;
	}
	if(ioctl(file, I2C_SLAVE_FORCE, addr) < 0) {
		close(file);
		return -1;
	}
	if(byte == 1)
		res = i2c_smbus_write_byte_data(file, reg, value);
	else if(byte == 2)
		res = i2c_smbus_write_word_data(file, reg, value);
	else {
		close(file);
		return -1;
	}

	close(file);
	return res;
}
static int open_i2c_dev(int i2cbus, char *filename, size_t size)
{
	int file;

	snprintf(filename, size, "/dev/i2c/%d", i2cbus);
	filename[size - 1] = '\0';
	file = open(filename, O_RDWR);
	if (file < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		sprintf(filename, "/dev/i2c-%d", i2cbus);
		file = open(filename, O_RDWR);
	}

	return file;
}


static int read_tmp75_temp(uchar bus, uchar addr, uchar reg)
{
	int ret;
	int value;

	ret = voyager_i2c_read(bus, addr, reg, 2);
	value = (ret < 0) ? ret : swab16(ret);
	if(value < 0) {
		syslog(LOG_ERR, "%s: failed to read temperature bus %d, addr 0x%x", __func__, bus, addr);
		return -1;
	}
	usleep(11000);

	return ((short)value / 128) / 2;
}

static int read_cpu_temp(uchar bus, uchar addr, uchar reg)
{
	int ret;
	int value;

	ret = voyager_i2c_read(bus, addr, reg, 2);
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to read temperature bus %d, addr 0x%x", __func__, bus, addr);
		return -1;
	}
	usleep(11000);

	return ret;
}
static int read_lm57_adc(uchar bus, uchar addr, uchar reg)
{
	int ret;
	int value;
	int file;
	char filename[VOYAGER_FILENAME_LEN];

	sprintf(filename, "/sys/devices/platform/ast_adc.%d/in%d_input", addr, reg);
	file = open(filename, O_RDONLY);
	if(file < 0) {
		syslog(LOG_ERR, "%s: %s open failed", __func__, filename);
		return -1;
	}
	ret = read(file, &value, sizeof(int));
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to read adc%d value", __func__, reg);
		close(file);
		return -1;
	}
	//printf("%s: read adc %d\n", __func__, value);
	usleep(11000);

	return value;
}

static int read_critical_max_temp(void)
{
	int i;
	int temp, max_temp = 0;
	struct voyager_board_info_stu *info;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &voyager_board_info[i];
		if(info->critical) {
			temp = info->critical->read_temp(info->critical->bus, info->critical->addr, info->critical->reg);
			if(temp != -1) {
				info->critical->temp = temp;
				if(temp > max_temp)
					max_temp = temp;
			}
		}
	}
	//printf("%s: critical: max_temp=%d\n", __func__, max_temp);

	return max_temp;
}
static int read_alarm_max_temp(void)
{
	int i;
	int temp, max_temp = 0;
	struct voyager_board_info_stu *info;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &voyager_board_info[i];
		if(info->alarm) {
			temp = info->alarm->read_temp(info->alarm->bus, info->alarm->addr, info->alarm->reg);
			if(temp != -1) {
				info->alarm->temp = temp;
				if(temp > max_temp)
					max_temp = temp;
			}
		}
	}
	//printf("%s: alarm: max_temp=%d\n", __func__, max_temp);

	return max_temp;
}
static int calculate_raising_fan_pwm(int temp)
{
	if(temp < VOYAGER_RAISING_TEMP_LOW) {
		return VOYAGER_FAN_LOW;
	} else if(temp >= VOYAGER_RAISING_TEMP_LOW && temp < VOYAGER_RAISING_TEMP_HIGH) {
		return VOYAGER_FAN_MEDIUM;
	} else if(temp >= VOYAGER_RAISING_TEMP_HIGH && temp < VOYAGER_SYSTEM_WARN) {
		return VOYAGER_FAN_HIGH;
	} else {
		return VOYAGER_FAN_MAX;
	}
	return VOYAGER_FAN_HIGH;
}
static int calculate_falling_fan_pwm(int temp)
{
	if(temp < VOYAGER_FALLING_TEMP_LOW) {
		return VOYAGER_FAN_LOW;
	} else if(temp >= VOYAGER_FALLING_TEMP_LOW && temp < VOYAGER_FALLING_TEMP_HIGH) {
		return VOYAGER_FAN_MEDIUM;
	} else if(temp >= VOYAGER_FALLING_TEMP_HIGH && temp < VOYAGER_SYSTEM_WARN) {
		return VOYAGER_FAN_HIGH;
	} else {
		return VOYAGER_FAN_MAX;
	}

	return VOYAGER_FAN_HIGH;
}
static int voyager_set_fan(int fan, int value)
{
	int ret;
	struct fan_info_stu *info;

	info = &voyager_fantray_info[fan];
	
	ret = voyager_i2c_write(VOYAGER_FANTRAY_I2C_BUS, VOYAGER_FANTRAY_CPLD_ADDR, info->pwm_reg, value, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to set fantray%d pwm, value 0x%x", __func__, fan + 1, value);
		return -1;
	}
	usleep(11000);


	return 0;
}


static int voyager_fan_is_okey(int fan)
{
	int ret;
	int error = 0;
	struct fan_info_stu *info;

	info = &voyager_fantray_info[fan];

	ret = voyager_i2c_read(VOYAGER_FANTRAY_I2C_BUS, VOYAGER_FANTRAY_CPLD_ADDR, info->fan_present_stat_reg, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to read fan%d present status 0x%x", __func__, fan + 1, info->fan_present_stat_reg);
		error++;
	} else {
		usleep(11000);
		if(ret & (0x1 << fan)) {
			if(info->present == 1)
				printf("fantray%d is removed\n", fan + 1);
			info->present = 0;
			error++;
		} else {
			if(info->present == 0)
				printf("fantray%d is inserted\n", fan + 1);
			info->present = 1;
		}
	}

	ret = voyager_i2c_read(VOYAGER_FANTRAY_I2C_BUS, VOYAGER_FANTRAY_CPLD_ADDR, info->fan_fail_stat_reg, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to read fan%d fail status 0x%x", __func__, fan + 1, info->fan_fail_stat_reg);
		error++;
	} else {
		usleep(11000);
		if(ret & (0x1 << fan)) {
			if(info->failed == 1)
				printf("fantray%d is good\n", fan + 1);
			info->failed = 0;
		} else {
			if(info->failed == 0)
				printf("fantray%d is failed\n", fan + 1);
			info->failed = 1;
			error++;
		}
	}

	if(error)
		return -1;
	else
		return 0;
}

static int voyager_write_fan_led(int fan, const char *color)
{
	int ret, value;
	struct fan_info_stu *info;

	info = &voyager_fantray_info[fan];

	if(!strcmp(color, FAN_LED_BLUE))
		value = 1; //blue
	else
		value = 2; //red
	value = value << ((fan % 2) ? 4 : 0);

	ret = voyager_i2c_write(VOYAGER_FANTRAY_I2C_BUS, VOYAGER_FANTRAY_CPLD_ADDR, info->fan_led_reg, value, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "%s: failed to set fantray%d led, value 0x%x", __func__, fan + 1, value);
		return -1;
	}
	usleep(11000);

	return 1;
}
static int system_shutdown(const char *why)
{
	int ret;
	int i;
	struct voyager_psu_info_stu *info;
	
	syslog(LOG_EMERG, "Shutting down:  %s", why);
	for(i = 0; i < VOYAGER_PSUS; i++) {
		info = &voyager_psu_info[i];
		ret = voyager_i2c_read(VOYAGER_SYS_I2C_BUS, VOYAGER_SYS_CPLD_ADDR, info->present_reg, 1);
		if(ret < 0) {
			syslog(LOG_ERR, "%s: failed to read PSU%d present status reg 0x%x", __func__, i, info->present_reg);
			return -1;
		}
		if(ret & (0x2 << info->present_bit)) {
			ret = voyager_i2c_write(VOYAGER_PSU_CHANNEL_I2C_BUS, VOYAGER_PSU_CHANNEL_I2C_ADDR, 0x0, info->psu_channal, 1);
			if(ret < 0) {
				syslog(LOG_ERR, "%s: failed to set PSU channel 0x%x, value 0x%x", __func__, VOYAGER_PSU_CHANNEL_I2C_ADDR, info->psu_channal);
				return -1;
			}
			usleep(11000);
			ret = voyager_i2c_write(VOYAGER_PSU_CHANNEL_I2C_BUS, info->psu_addr, 0x1, 0x0, 1);
			if(ret < 0) {
				syslog(LOG_ERR, "%s: failed to set PSU shutdown 0x1, value 0x0", __func__);
				return -1;
			}
		}
	}
	stop_watchdog();

	sleep(2);
	exit(2);
}


void usage() {
  fprintf(stderr,
          "fand [-v] [-l <low-pct>] [-m <medium-pct>] "
          "[-h <high-pct>]\n"
          "\t[-b <temp-bottom>] [-t <temp-top>] [-r <report-temp>]\n\n"
          "\tlow-pct defaults to %d%% fan\n"
          "\tmedium-pct defaults to %d%% fan\n"
          "\thigh-pct defaults to %d%% fan\n"
          "\ttemp-bottom defaults to %dC\n"
          "\ttemp-top defaults to %dC\n"
          "\treport-temp defaults to every %d measurements\n\n"
          "fand compensates for uServer temperature reading %d degrees low\n"
          "kill with SIGUSR1 to stop watchdog\n",
          fan_low,
          fan_medium,
          fan_high,
          EXTERNAL_TEMPS(temp_bottom),
          EXTERNAL_TEMPS(temp_top),
          report_temp,
          EXTERNAL_TEMPS(USERVER_TEMP_FUDGE));
  exit(1);
}

int fan_speed_okay(const int fan, const int speed, const int slop) {
	return !voyager_fan_is_okey(fan);
}

/* Set fan speed as a percentage */

int write_fan_speed(const int fan, const int value) {
  /*
   * The hardware fan numbers for pwm control are different from
   * both the physical order in the box, and the mapping for reading
   * the RPMs per fan, above.
   */
	int unit = value * PWM_UNIT_MAX / 100;

    if (unit == PWM_UNIT_MAX)
      unit--;

	return voyager_set_fan(fan, unit);
}



/* Set up fan LEDs */

int write_fan_led(const int fan, const char *color) {
	return voyager_write_fan_led(fan, color);
}


/* Gracefully shut down on receipt of a signal */

void fand_interrupt(int sig)
{
  int fan;
  for (fan = 0; fan < total_fans; fan++) {
    write_fan_speed(fan + fan_offset, fan_max);
  }

  syslog(LOG_WARNING, "Shutting down fand on signal %s", strsignal(sig));
  if (sig == SIGUSR1) {
    stop_watchdog();
  }
  exit(3);
}

int main(int argc, char **argv) {
  /* Sensor values */
  int critical_temp;
  int alarm_temp;
  int old_temp = VOYAGER_RAISING_TEMP_HIGH;
  int raising_pwm;
  int falling_pwm;
  struct fan_info_stu *info;
  int fan_speed = fan_medium;
  int failed_speed = 0;

  int bad_reads = 0;
  int fan_failure = 0;
  int fan_speed_changes = 0;
  int old_speed;
  int count = 0;

  int fan_bad[FANS];
  int fan;

  unsigned log_count = 0; // How many times have we logged our temps?
  int opt;
  int prev_fans_bad = 0;

  struct sigaction sa;

  sa.sa_handler = fand_interrupt;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);

  // Start writing to syslog as early as possible for diag purposes.

  openlog("fand", LOG_CONS, LOG_DAEMON);

  while ((opt = getopt(argc, argv, "l:m:h:b:t:r:v")) != -1) {
    switch (opt) {
    case 'l':
      fan_low = atoi(optarg);
      break;
    case 'm':
      fan_medium = atoi(optarg);
      break;
    case 'h':
      fan_high = atoi(optarg);
      break;
    case 'b':
      temp_bottom = INTERNAL_TEMPS(atoi(optarg));
      break;
    case 't':
      temp_top = INTERNAL_TEMPS(atoi(optarg));
      break;
    case 'r':
      report_temp = atoi(optarg);
      break;
    case 'v':
      verbose = true;
      break;
    default:
      usage();
      break;
    }
  }

  if (optind > argc) {
    usage();
  }

  if (temp_bottom > temp_top) {
    fprintf(stderr,
            "Should temp-bottom (%d) be higher than "
            "temp-top (%d)?  Starting anyway.\n",
            EXTERNAL_TEMPS(temp_bottom),
            EXTERNAL_TEMPS(temp_top));
  }

  if (fan_low > fan_medium || fan_low > fan_high || fan_medium > fan_high) {
    fprintf(stderr,
            "fan RPMs not strictly increasing "
            "-- %d, %d, %d, starting anyway\n",
            fan_low,
            fan_medium,
            fan_high);
  }

  daemon(1, 0);

  if (verbose) {
    syslog(LOG_DEBUG, "Starting up;  system should have %d fans.",
           total_fans);
  }

  for (fan = 0; fan < total_fans; fan++) {
    fan_bad[fan] = 0;
    write_fan_speed(fan + fan_offset, fan_speed);
    write_fan_led(fan + fan_offset, FAN_LED_BLUE);
  }

  /* Start watchdog in manual mode */
  start_watchdog(0);

  /* Set watchdog to persistent mode so timer expiry will happen independent
   * of this process's liveliness. */
  set_persistent_watchdog(WATCHDOG_SET_PERSISTENT);

  sleep(5);  /* Give the fans time to come up to speed */

  while (1) {
    int max_temp;
    old_speed = fan_speed;

    /* Read sensors */
	critical_temp = read_critical_max_temp();
	alarm_temp = 0;/*zmzhan remove temp read_alarm_max_temp();*/
	if ((critical_temp == BAD_TEMP || alarm_temp == BAD_TEMP)) {
      bad_reads++;
    }

    if (log_count++ % report_temp == 0) {
      syslog(LOG_DEBUG,
			"critical temp %d, alarm temp %d, fan speed %d, speed changes %d",
			critical_temp, alarm_temp, fan_speed, fan_speed_changes);
    }

    /* Protection heuristics */
	if(critical_temp > VOYAGER_SYSTEM_WARN) {
		printf("Critical temp warning reached\n");
	}

	if(critical_temp > VOYAGER_SYSTEM_LIMIT) {
		system_shutdown("Critical temp limit reached");
	}

    /*
     * Calculate change needed -- we should eventually
     * do something more sophisticated, like PID.
     *
     * We should use the intake temperature to adjust this
     * as well.
     */

    /* Other systems use a simpler built-in table to determine fan speed. */
	raising_pwm = calculate_raising_fan_pwm(critical_temp);
	falling_pwm = calculate_falling_fan_pwm(critical_temp);
	if(old_temp <= critical_temp) {
		/*raising*/
		if(raising_pwm >= fan_speed) {
			fan_speed = raising_pwm;
		}
	} else {
		/*falling*/
		if(falling_pwm <= fan_speed ) {
			fan_speed = falling_pwm;
		}
	}
	old_temp = critical_temp;

    /*
     * Update fans only if there are no failed ones. If any fans failed
     * earlier, all remaining fans should continue to run at max speed.
     */
    if (fan_failure == 0 && fan_speed != old_speed) {
      syslog(LOG_NOTICE,
             "Fan speed changing from %d to %d",
             old_speed,
             fan_speed);
      fan_speed_changes++;
      for (fan = 0; fan < total_fans; fan++) {
        write_fan_speed(fan + fan_offset, fan_speed);
      }
    }

    /*
     * Wait for some change.  Typical I2C temperature sensors
     * only provide a new value every second and a half, so
     * checking again more quickly than that is a waste.
     *
     * We also have to wait for the fan changes to take effect
     * before measuring them.
     */

    sleep(5);


    /* Check fan RPMs */

    for (fan = 0; fan < total_fans; fan++) {
      /*
       * Make sure that we're within some percentage
       * of the requested speed.
       */
      if (fan_speed_okay(fan + fan_offset, fan_speed, FAN_FAILURE_OFFSET)) {
        if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
          write_fan_led(fan + fan_offset, FAN_LED_BLUE);
          syslog(LOG_CRIT,
                 "Fan %d has recovered",
                 fan);
        }
        fan_bad[fan] = 0;
      } else {
		info = &voyager_fantray_info[fan];
		if(info->present == 0 | info->failed == 1)
			fan_bad[fan]++;

      }
    }

    fan_failure = 0;
    for (fan = 0; fan < total_fans; fan++) {
      if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
        fan_failure++;
        write_fan_led(fan + fan_offset, FAN_LED_RED);
      }
    }
    if (fan_failure > 0) {
      if (prev_fans_bad != fan_failure) {
        syslog(LOG_CRIT, "%d fans failed", fan_failure);
      }

      /*
       * If fans are bad, we need to blast all of the
       * fans at 100%;  we don't bother to turn off
       * the bad fans, in case they are all that is left.
       *
       * Note that we have a temporary bug with setting fans to
       * 100% so we only do fan_max = 99%.
       */
		if (fan_failure > 0) {
		int not_present = 0;
		int fan_failed = 0;
		
        for (fan = 0; fan < total_fans; fan++) {
			info = &voyager_fantray_info[fan];
				if(info->failed == 1 | info->present == 0)
					fan_failed++;
        }

		if(fan_failed > 0 && fan_failed <= 1) {
			if(fan_speed == VOYAGER_FAN_LOW) {
				failed_speed = voyager_fan_failed_control[fan_failed - 1].low_level;
			} else if(fan_speed == VOYAGER_FAN_MEDIUM) {
				failed_speed = voyager_fan_failed_control[fan_failed - 1].mid_level;
			} else if(fan_speed == VOYAGER_FAN_HIGH) {
				failed_speed = voyager_fan_failed_control[fan_failed - 1].high_level;
			} else if(fan_speed == VOYAGER_FAN_MAX) {
				failed_speed = voyager_fan_failed_control[fan_failed - 1].alarm_level;
			}
		} else {
			failed_speed = fan_max;
		}
	    for (fan = 0; fan < total_fans; fan++) {
	        write_fan_speed(fan + fan_offset, failed_speed);
	    }
      }

      /*
       * Fans can be hot swapped and replaced; in which case the fan daemon
       * will automatically detect the new fan and (assuming the new fan isn't
       * itself faulty), automatically readjust the speeds for all fans down
       * to a more suitable rpm. The fan daemon does not need to be restarted.
       */
    } else if(prev_fans_bad != 0 && fan_failure == 0){
		old_temp = VOYAGER_RAISING_TEMP_HIGH;
		fan_speed = fan_medium;
		for (fan = 0; fan < total_fans; fan++) {
       		write_fan_speed(fan + fan_offset, fan_speed);
		}
    }

    /* Suppress multiple warnings for similar number of fan failures. */
    prev_fans_bad = fan_failure;

    /* if everything is fine, restart the watchdog countdown. If this process
     * is terminated, the persistent watchdog setting will cause the system
     * to reboot after the watchdog timeout. */
    kick_watchdog();
  }
}
