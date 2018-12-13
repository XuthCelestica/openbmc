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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <dirent.h>
#include "watchdog.h"
#include <fcntl.h>
#include <openbmc/obmc-i2c.h>

//#define DEBUG
//#define FOR_F2B
//#define FANCTRL_SIMULATION 1
//#define CONFIG_PSU_FAN_CONTROL_INDEPENDENT 1
#define CONFIG_FSC_CONTROL_PID 1 //for PID control

#define TOTAL_FANS 5
#define TOTAL_PSUS 4
#define FAN_MEDIUM 128
#define FAN_HIGH 100
#define FAN_MAX 255
#define FAN_MIN 115
#define FAN_NORMAL_MAX FAN_MAX
#define FAN_NORMAL_MIN FAN_MIN
#define FAN_ONE_FAIL_MAX FAN_MAX
#define FAN_ONE_FAIL_MIN FAN_MAX
#define RAISING_TEMP_LOW 25
#define RAISING_TEMP_HIGH 40
#define FALLING_TEMP_LOW 23
#define FALLING_TEMP_HIGH 40
#define SYSTEM_LIMIT 80

#define ALARM_TEMP_THRESHOLD 1
#define ALARM_START_REPORT 3

#define CRITICAL_TEMP_HYST 2

#define REPORT_TEMP 720  /* Report temp every so many cycles */
#define FAN_FAILURE_OFFSET 30
#define FAN_FAILURE_THRESHOLD 3 /* How many times can a fan fail */
#define FAN_LED_GREEN 1
#define FAN_LED_RED 2
#define SHUTDOWN_DELAY_TIME 72 /*if trigger shutdown event, delay 6mins to shutdown */

#define BAD_TEMP (-60)
#define ERROR_TEMP_MAX 5

#define FAN_FAIL_COUNT 10
#define FAN_FAIL_RPM 1000
#define FAN_FRONTT_SPEED_MAX 18000
#define FAN_REAR_SPEED_MAX 18000
#define PSU_SPEED_MAX 26496

#define WARN_RECOVERY_COUNT (100) //remain 5mins to recovery nomal speed

#define FAN_DIR_B2F 0
#define FAN_DIR_F2B 1

#define PWM_UNIT_MAX 255

#define PATH_CACHE_SIZE 256

#define FAN_WDT_TIME (0x3c) //5 * 60
#define FAN_WDT_ENABLE_SYSFS "/sys/bus/i2c/devices/i2c-8/8-000d/wdt_en"
#define FAN_WDT_TIME_SYSFS "/sys/bus/i2c/devices/i2c-8/8-000d/wdt_time"
#define PSU1_SHUTDOWN_SYSFS "/sys/bus/i2c/devices/i2c-25/25-0058/control/shutdown"
#define PSU2_SHUTDOWN_SYSFS "/sys/bus/i2c/devices/i2c-26/26-0058/control/shutdown"
#define PSU3_SHUTDOWN_SYSFS "/sys/bus/i2c/devices/i2c-28/28-0058/control/shutdown"
#define PSU4_SHUTDOWN_SYSFS "/sys/bus/i2c/devices/i2c-29/29-0058/control/shutdown"
#define PSU_SPEED_CTRL_NODE "fan1_cfg"
#define PSU_SPEED_CTRL_ENABLE 0x90

#define PID_CONFIG_PATH "/mnt/data/pid_config_v2.ini"
#define PID_FILE_LINE_MAX 100

#define uchar unsigned char

#define DISABLE 0
#define LOW_WARN_BIT (0x1 << 0)
#define HIGH_WARN_BIT (0x1 << 1)
#define PID_CTRL_BIT (0x1 << 2)
#define SWITCH_SENSOR_BIT (0x1 << 3)
#define CRITICAL_SENSOR_BIT (0x1 << 4)

#define NORMAL_K	((float)(FAN_NORMAL_MAX - FAN_NORMAL_MIN) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW))
#define ONE_FAIL_K	((float)(FAN_ONE_FAIL_MAX - FAN_ONE_FAIL_MIN) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW))

struct point {
	int temp;
	int speed;
};

static int calculate_line_speed(struct sensor_info_sysfs *sensor, struct line_policy *line);
struct line_policy {
	int temp_hyst;
	struct point begin;
	struct point end;
	int (*get_speed)(struct sensor_info_sysfs *sensor, struct line_policy *line);
};

static int calculate_pid_speed(struct pid_policy *pid);
struct pid_policy {
	int cur_temp;
	int t1;
	int t2;
	int set_point;
	float kp;
	float ki;
	float kd;
	int last_output;
	int max_output;
	int min_output;
	int (*get_speed)(struct pid_policy pid);
};

struct line_policy phalanx_f2b_normal = {
	.temp_hyst = CRITICAL_TEMP_HYST,
	.begin = {
		.temp = RAISING_TEMP_LOW,
		.speed = FAN_NORMAL_MIN,
	},
	.end = {
		.temp = RAISING_TEMP_HIGH,
		.speed = FAN_NORMAL_MAX,
	},
	.get_speed = calculate_line_speed,
};

struct line_policy phalanx_f2b_onefail = {
	.temp_hyst = CRITICAL_TEMP_HYST,
	.begin = {
		.temp = RAISING_TEMP_LOW,
		.speed = FAN_ONE_FAIL_MIN,
	},
	.end = {
		.temp = RAISING_TEMP_HIGH,
		.speed = FAN_ONE_FAIL_MAX,
	},
	.get_speed = calculate_line_speed,
};

struct line_policy phalanx_b2f_normal = {
	.temp_hyst = CRITICAL_TEMP_HYST,
	.begin = {
		.temp = RAISING_TEMP_LOW,
		.speed = FAN_NORMAL_MIN,
	},
	.end = {
		.temp = RAISING_TEMP_HIGH,
		.speed = FAN_NORMAL_MAX,
	},
	.get_speed = calculate_line_speed,
};

struct line_policy phalanx_b2f_onefail = {
	.temp_hyst = CRITICAL_TEMP_HYST,
	.begin = {
		.temp = RAISING_TEMP_LOW,
		.speed = FAN_ONE_FAIL_MIN,
	},
	.end = {
		.temp = RAISING_TEMP_HIGH,
		.speed = FAN_ONE_FAIL_MAX,
	},
	.get_speed = calculate_line_speed,
};

struct sensor_info_sysfs {
  char* prefix;
  char* suffix;
  uchar error_cnt;
  int temp;
  int t1;
  int t2;
  int old_pwm;
  float setpoint;
  float p;
  float i;
  float d;
  float min_output;
  float max_output;
  int (*read_sysfs)(struct sensor_info_sysfs *sensor);
  char path_cache[PATH_CACHE_SIZE];
};

struct fan_info_stu_sysfs {
  char *prefix;
  char *front_fan_prefix;
  char *rear_fan_prefix;
  char *pwm_prefix;
  char *fan_led_prefix;
  char *fan_present_prefix;
  char *fan_status_prefix;
  //uchar present; //for chassis using, other ignore it
  uchar front_failed;  //for single fan fail
  uchar rear_failed;
};

struct psu_info_sysfs {
  char* sysfs_path;
  char* shutdown_path;
  int value_to_shutdown;
};


struct board_info_stu_sysfs {
  const char *name;
  uint slot_id;
  int correction;
  int lwarn;
  int hwarn;
  int warn_count;
  int flag;
  struct sensor_info_sysfs *critical;
  struct sensor_info_sysfs *alarm;
};

struct fantray_info_stu_sysfs {
  const char *name;
  int present;
  int status;
  int failed; //for fantray fail
  struct fan_info_stu_sysfs fan1;
};

struct rpm_to_pct_map {
  uint pct;
  uint rpm;
};
struct dictionary_t {
	char name[20];
	int value;
};

static int calculate_fan_normal_pwm(int cur_temp, int last_temp);
static int calculate_fan_one_fail_pwm(int cur_temp, int last_temp);

struct thermal_policy {
	int pwm;
	int old_pwm;
	line_policy *line;
	// int (*calculate_pwm)(int cur_temp, int last_temp);
};

static int read_temp_sysfs(struct sensor_info_sysfs *sensor);
static int read_temp_directly_sysfs(struct sensor_info_sysfs *sensor);


static struct sensor_info_sysfs linecard1_onboard_u25 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/42-0049",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs linecard1_onboard_u26 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/42-0048",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs linecard2_onboard_u25 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/43-0049",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs linecard2_onboard_u26 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/43-0048",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs switchboard_onboard_u33 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/7-004d",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs switchboard_onboard_u148 = {
  .prefix = "/sys/bus/i2c/drivers/max31730/7-004c",
  .suffix = "temp3_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs baseboard_onboard_u3 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/7-004e",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs baseboard_onboard_u41 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/7-004f",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs pdbboard_onboard_u8 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/31-0048",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs pdbboard_onboard_u10 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/31-0049",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs fcbboard_onboard_u8 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/39-0049",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs fcbboard_onboard_u10 = {
  .prefix = "/sys/bus/i2c/drivers/lm75/39-0049",
  .suffix = "temp1_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_sysfs,
};

static struct sensor_info_sysfs come_inlet_u7 = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .suffix = "temp2_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs sensor_cpu_inlet_critical_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .suffix = "temp2_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_directly_sysfs,
};

static struct sensor_info_sysfs sensor_optical_inlet_critical_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .suffix = "temp3_input",
  .error_cnt = 0,
  .temp = 0,
  .t1 = 0,
  .t2 = 0,
  .old_pwm = 0,
  .setpoint = 0,
  .p = 0,
  .i = 0,
  .d = 0,
  .min_output = FAN_MIN,
  .max_output = FAN_MAX,
  .read_sysfs = &read_temp_directly_sysfs,
};


/* fantray info*/
static struct fan_info_stu_sysfs fan1_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-8/8-000d",
  .front_fan_prefix = "fan1_input",
  .rear_fan_prefix = "fan2_input",
  .pwm_prefix = "fan1_pwm",
  .fan_led_prefix = "fan1_led",
  .fan_present_prefix = "fan1_present",
  .fan_status_prefix = NULL,
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan2_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-8/8-000d",
  .front_fan_prefix = "fan3_input",
  .rear_fan_prefix = "fan4_input",
  .pwm_prefix = "fan2_pwm",
  .fan_led_prefix = "fan2_led",
  .fan_present_prefix = "fan2_present",
  .fan_status_prefix = NULL,
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan3_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-8/8-000d",
  .front_fan_prefix = "fan5_input",
  .rear_fan_prefix = "fan6_input",
  .pwm_prefix = "fan3_pwm",
  .fan_led_prefix = "fan3_led",
  .fan_present_prefix = "fan3_present",
  .fan_status_prefix = NULL,
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan4_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-8/8-000d",
  .front_fan_prefix = "fan7_input",
  .rear_fan_prefix = "fan8_input",
  .pwm_prefix = "fan4_pwm",
  .fan_led_prefix = "fan4_led",
  .fan_present_prefix = "fan4_present",
  .fan_status_prefix = NULL,
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs fan5_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-8/8-000d",
  .front_fan_prefix = "fan9_input",
  .rear_fan_prefix = "fan10_input",
  .pwm_prefix = "fan5_pwm",
  .fan_led_prefix = "fan5_led",
  .fan_present_prefix = "fan5_present",
  .fan_status_prefix = NULL,
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu4_fan_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .front_fan_prefix = "fan1_input",
  .rear_fan_prefix = "/sys/bus/i2c/devices/i2c-29/29-0058",
  .pwm_prefix = "fan1_pct",
  .fan_led_prefix = "psu_led",
  .fan_present_prefix = "psu_4_present",
  .fan_status_prefix = "psu_4_status",
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu3_fan_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .front_fan_prefix = "fan1_input",
  .rear_fan_prefix = "/sys/bus/i2c/devices/i2c-28/28-0058",
  .pwm_prefix = "fan1_pct",
  .fan_led_prefix = "psu_led",
  .fan_present_prefix = "psu_3_present",
  .fan_status_prefix = "psu_3_status",
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu2_fan_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .front_fan_prefix = "fan1_input",
  .rear_fan_prefix = "/sys/bus/i2c/devices/i2c-25/25-0058",
  .pwm_prefix = "fan1_pct",
  .fan_led_prefix = "psu_led",
  .fan_present_prefix = "psu_2_present",
  .fan_status_prefix = "psu_2_status",
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};

static struct fan_info_stu_sysfs psu1_fan_info = {
  .prefix = "/sys/bus/i2c/devices/i2c-0/0-000d",
  .front_fan_prefix = "fan1_input",
  .rear_fan_prefix = "/sys/bus/i2c/devices/i2c-24/24-0058",
  .pwm_prefix = "fan1_pct",
  .fan_led_prefix = "psu_led",
  .fan_present_prefix = "psu_1_present",
  .fan_status_prefix = "psu_1_status",
  //.present = 1,
  .front_failed = 0,
  .rear_failed = 0,
};




/************board and fantray info*****************/
static struct board_info_stu_sysfs board_info[] = {
	/*B2F*/
	{
		.name = "pdbboard_onboard_u8",
		.slot_id = FAN_DIR_B2F,
		.correction = -2,
		.lwarn = BAD_TEMP,
		.hwarn = BAD_TEMP,
		.warn_count = 0,
		.flag = DISABLE,//CRITICAL_SENSOR_BIT,
		.critical = &pdbboard_onboard_u8,
		.alarm = &pdbboard_onboard_u8,
	},
	{
		.name = "pdbboard_onboard_u10",
		.slot_id = FAN_DIR_B2F,
		.correction = -2,
		.lwarn = BAD_TEMP,
		.hwarn = BAD_TEMP,
		.warn_count = 0,
		.flag = DISABLE,//CRITICAL_SENSOR_BIT,
		.critical = &pdbboard_onboard_u10,
		.alarm = &pdbboard_onboard_u10,
	},
#ifdef CONFIG_FSC_CONTROL_PID
	{
		.name = "BCM56980_inlet",
		.slot_id = FAN_DIR_B2F,
		.correction = 17,
		.lwarn = BAD_TEMP,
		.hwarn = BAD_TEMP,
		.warn_count = 0,
		.flag = PID_CTRL_BIT,
		.critical = &switchboard_onboard_u148,
		.alarm = &switchboard_onboard_u148,
	},
	{
		.name = "cpu_inlet",
		.slot_id = FAN_DIR_B2F,
		.correction = 0,
		.lwarn = BAD_TEMP,
		.hwarn = BAD_TEMP,
		.warn_count = 0,
		.flag = DISABLE,//PID_CTRL_BIT,
		.critical = &sensor_cpu_inlet_critical_info,
		.alarm = &sensor_cpu_inlet_critical_info,
	},
	{
		.name = "optical_inlet",
		.slot_id = FAN_DIR_B2F,
		.correction = 0,
		.lwarn = BAD_TEMP,//68,
		.hwarn = BAD_TEMP,//70,
		.warn_count = 0,
		.flag = DISABLE,//PID_CTRL_BIT,
		.critical = &sensor_optical_inlet_critical_info,
		.alarm = &sensor_optical_inlet_critical_info,
	},
#endif
	/*F2B*/
	{
		.name = "pdbboard_onboard_u10",
		.slot_id = FAN_DIR_F2B,
		.correction = -4,
		.lwarn = 40,
		.hwarn = 43,
		.warn_count = 0,
		.flag = CRITICAL_SENSOR_BIT,
		.critical = &pdbboard_onboard_u10,
		.alarm = &pdbboard_onboard_u10,
	},
#ifdef CONFIG_FSC_CONTROL_PID
	{
		.name = "BCM56980_inlet",
		.slot_id = FAN_DIR_F2B,
		.correction = 0,
		.lwarn = 105,
		.hwarn = 110,
		.warn_count = 0,
		.flag = PID_CTRL_BIT,
		.critical = &switchboard_onboard_u148,
		.alarm = &switchboard_onboard_u148,
	},
	{
		.name = "cpu_inlet",
		.slot_id = FAN_DIR_F2B,
		.correction = 0,
		.lwarn = 101,
		.hwarn = 103,
		.warn_count = 0,
		.flag = PID_CTRL_BIT,//PID_CTRL_BIT,
		.critical = &sensor_cpu_inlet_critical_info,
		.alarm = &sensor_cpu_inlet_critical_info,
	},
	{
		.name = "optical_inlet",
		.slot_id = FAN_DIR_F2B,
		.correction = 0,
		.lwarn = BAD_TEMP,//68,
		.hwarn = BAD_TEMP,//70,
		.warn_count = 0,
		.flag = DISABLE,//PID_CTRL_BIT,
		.critical = &sensor_optical_inlet_critical_info,
		.alarm = &sensor_optical_inlet_critical_info,
	},
#endif
	NULL,
};

static struct fantray_info_stu_sysfs fantray_info[] = {
  {
    .name = "Fantray 1",
    .present = 1,
    .status = 1,
    .failed = 0,
    .fan1 = fan1_info,
  },
  {
    .name = "Fantray 2",
    .present = 1,
    .status = 1,
    .failed = 0,
    .fan1 = fan2_info,
  },
  {
    .name = "Fantray 3",
    .present = 1,
    .status = 1,
    .failed = 0,
    .fan1 = fan3_info,
  },
  {
    .name = "Fantray 4",
    .present = 1,
    .status = 1,
    .failed = 0,
    .fan1 = fan4_info,
  },
  {
    .name = "Fantray 5",
    .present = 1,
    .status = 1,
    .failed = 0,
    .fan1 = fan5_info,
  },
  {
	.name = "PSU 1",
	.present = 1,
	.status = 1,
	.failed = 0,
	.fan1 = psu1_fan_info,
  },
  {
	.name = "PSU 2",
	.present = 1,
	.status = 1,
	.failed = 0,
	.fan1 = psu2_fan_info,
  },
  {
	.name = "PSU 3",
	.present = 1,
    .status = 1,
	.failed = 0,
	.fan1 = psu3_fan_info,
  },
  {
	.name = "PSU 4",
	.present = 1,
    .status = 1,
	.failed = 0,
	.fan1 = psu4_fan_info,
  },
  NULL,
};



#define BOARD_INFO_SIZE (sizeof(board_info) \
                        / sizeof(struct board_info_stu_sysfs))
#define FANTRAY_INFO_SIZE (sizeof(fantray_info) \
                        / sizeof(struct fantray_info_stu_sysfs))

static struct rpm_to_pct_map rpm_front_map[] = {{20, 4950},
                                         {25, 6150},
                                         {30, 7500},
                                         {35, 8700},
                                         {40, 9900},
                                         {45, 11250},
                                         {50, 12300},
                                         {55, 13650},
                                         {60, 14850},
                                         {65, 16050},
                                         {70, 17400},
                                         {75, 18600},
                                         {80, 19950},
                                         {85, 21000},
                                         {90, 22350},
                                         {95, 23550},
                                         {100, 24150}};

static struct rpm_to_pct_map rpm_rear_map[] = {{20, 6000},
                                        {25, 7500},
                                        {30, 8850},
                                        {35, 10500},
                                        {40, 12150},
                                        {45, 13350},
                                        {50, 14850},
                                        {55, 16650},
                                        {60, 18000},
                                        {65, 19350},
                                        {70, 20850},
                                        {75, 22350},
                                        {80, 24000},
                                        {85, 25350},
                                        {90, 26850},
                                        {95, 28350},
                                        {100, 28950}};

static struct rpm_to_pct_map psu_rpm_map[] = {{20, 8800},
                                        {25, 8800},
                                        {30, 8800},
                                        {35, 8800},
                                        {40, 8800},
                                        {45, 9920},
                                        {50, 11520},
                                        {55, 13120},
                                        {60, 14560},
                                        {65, 16192},
                                        {70, 17760},
                                        {75, 19296},
                                        {80, 20800},
                                        {85, 21760},
                                        {90, 23424},
                                        {95, 24800},
                                        {100, 26496}};

#define FRONT_MAP_SIZE (sizeof(rpm_front_map) / sizeof(struct rpm_to_pct_map))
#define REAR_MAP_SIZE (sizeof(rpm_rear_map) / sizeof(struct rpm_to_pct_map))
#define PSU_MAP_SIZE (sizeof(psu_rpm_map) / sizeof(struct rpm_to_pct_map))

static struct thermal_policy f2b_normal_policy = {
	.pwm = FAN_NORMAL_MIN,
	.old_pwm = FAN_NORMAL_MIN,
	.line = &phalanx_f2b_normal,
	// .calculate_pwm = calculate_fan_normal_pwm,
};

static struct thermal_policy f2b_one_fail_policy = {
	.pwm = FAN_ONE_FAIL_MIN,
	.old_pwm = FAN_ONE_FAIL_MIN,
	.line = &phalanx_f2b_onefail,
	// .calculate_pwm = calculate_fan_one_fail_pwm,
};

static struct thermal_policy b2f_normal_policy = {
	.pwm = FAN_NORMAL_MIN,
	.old_pwm = FAN_NORMAL_MIN,
	.line = &phalanx_b2f_normal,
	// .calculate_pwm = calculate_fan_normal_pwm,
};

static struct thermal_policy b2f_one_fail_policy = {
	.pwm = FAN_ONE_FAIL_MIN,
	.old_pwm = FAN_ONE_FAIL_MIN,
	.line = &phalanx_b2f_onefail,
	// .calculate_pwm = calculate_fan_one_fail_pwm,
};

static struct thermal_policy *policy = NULL;
static int pid_using = 0;
static int direction = 0;


static int write_fan_led(const int fan, const int color);
static int write_fan_speed(const int fan, const int value);
static int write_psu_fan_speed(const int fan, int value);

/*
 * Initialize path cache by writing 0-length string
 */
static int init_path_cache(void)
{
	int i = 0;
	// Temp Sensor datastructure
	for (i = 0; i < BOARD_INFO_SIZE; i++)
	{
		if (board_info[i].alarm != NULL)
			snprintf(board_info[i].alarm->path_cache, PATH_CACHE_SIZE, "");
		if (board_info[i].critical != NULL)
			snprintf(board_info[i].critical->path_cache, PATH_CACHE_SIZE, "");
	}

	return 0;
}

/*
 * Helper function to probe directory, and make full path
 * Will probe directory structure, then make a full path
 * using "<prefix>/hwmon/hwmonxxx/<suffix>"
 * returns < 0, if hwmon directory does not exist or something goes wrong
 */
int assemble_sysfs_path(const char* prefix, const char* suffix,
                        char* full_name, int buffer_size)
{
	int rc = 0;
	int dirname_found = 0;
	char temp_str[PATH_CACHE_SIZE];
	DIR *dir = NULL;
	struct dirent *ent;

	if (full_name == NULL)
		return -1;

	snprintf(temp_str, (buffer_size - 1), "%s/hwmon", prefix);
	dir = opendir(temp_str);
	if (dir == NULL) {
		rc = ENOENT;
		goto close_dir_out;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (strstr(ent->d_name, "hwmon")) {
			// found the correct 'hwmon??' directory
			snprintf(full_name, buffer_size, "%s/%s/%s",
			       temp_str, ent->d_name, suffix);
			dirname_found = 1;
			break;
		}
	}

close_dir_out:
	if (dir != NULL) {
		closedir(dir);
	}

	if (dirname_found == 0) {
		rc = ENOENT;
	}

	return rc;
}

static int adjust_sysnode_path(const char* prefix, const char* suffix,
                        char* full_name, int buffer_size)
{
	int rc = 0;
	FILE *fp;
	int dirname_found = 0;
	char temp_str[PATH_CACHE_SIZE];
	DIR *dir = NULL;
	struct dirent *ent;

	if (full_name == NULL)
		return -1;
	snprintf(temp_str, (buffer_size - 1), "%s/%s", prefix, suffix);
	fp = fopen(temp_str, "r");
	if(fp) {
		fclose(fp);
		return 0;
	}

	/*adjust the path, because the hwmon id may be changed*/
	snprintf(temp_str, (buffer_size - 1), "%s/hwmon", prefix);
	dir = opendir(temp_str);
	if (dir == NULL) {
		rc = ENOENT;
		goto close_dir_out;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (strstr(ent->d_name, "hwmon")) {
			// found the correct 'hwmon??' directory
			snprintf(full_name, buffer_size, "%s/%s/%s",
				   temp_str, ent->d_name, suffix);
			dirname_found = 1;
			break;
		}
	}

close_dir_out:
	if (dir != NULL) {
		closedir(dir);
	}

	if (dirname_found == 0) {
		rc = ENOENT;
	}

	return rc;

}

// Functions for reading from sysfs stub
static int read_sysfs_raw_internal(const char *device, char *value, int log)
{
	FILE *fp;
	int rc, err;

	fp = fopen(device, "r");
	if (!fp) {
		if (log) {
			err = errno;
			syslog(LOG_INFO, "failed to open device %s for read: %s",
			     device, strerror(err));
			errno = err;
		}
		return -1;
	}

	rc = fscanf(fp, "%s", value);
	fclose(fp);

	if (rc != 1) {
		if (log) {
			err = errno;
			syslog(LOG_INFO, "failed to read device %s: %s",
			     device, strerror(err));
			errno = err;
		}
		return -1;
	}

  return 0;
}

static int read_sysfs_raw(char *sysfs_path, char *buffer)
{
	return read_sysfs_raw_internal(sysfs_path, buffer, 1);
}

// Returns 0 for success, or -1 on failures.
static int read_sysfs_int(char *sysfs_path, int *buffer)
{
	int rc;
	char readBuf[PATH_CACHE_SIZE];

	if (sysfs_path == NULL || buffer == NULL) {
		errno = EINVAL;
		return -1;
	}

	rc = read_sysfs_raw(sysfs_path, readBuf);
	if (rc == 0)
	{
		if (strstr(readBuf, "0x") || strstr(readBuf, "0X"))
			sscanf(readBuf, "%x", buffer);
		else
			sscanf(readBuf, "%d", buffer);
	}
	return rc;
}

static int write_sysfs_raw_internal(const char *device, char *value, int log)
{
	FILE *fp;
	int rc, err;

	fp = fopen(device, "w");
	if (!fp) {
		if (log) {
			err = errno;
			syslog(LOG_INFO, "failed to open device %s for write : %s",
			     device, strerror(err));
			errno = err;
		}
		return err;
	}

	rc = fputs(value, fp);
	fclose(fp);

	if (rc < 0) {
		if (log) {
			err = errno;
			syslog(LOG_INFO, "failed to write to device %s", device);
			errno = err;
		}
		return -1;
	}

  return 0;
}

static int write_sysfs_raw(char *sysfs_path, char* buffer)
{
	return write_sysfs_raw_internal(sysfs_path, buffer, 1);
}

// Returns 0 for success, or -1 on failures.
static int write_sysfs_int(char *sysfs_path, int buffer)
{
	int rc;
	char writeBuf[PATH_CACHE_SIZE];

	if (sysfs_path == NULL) {
		errno = EINVAL;
		return -1;
	}

	snprintf(writeBuf, PATH_CACHE_SIZE, "%d", buffer);
	return write_sysfs_raw(sysfs_path, writeBuf);
}

static int read_temp_directly_sysfs(struct sensor_info_sysfs *sensor)
{
	int ret;
	int value;
	char fullpath[PATH_CACHE_SIZE];
	bool use_cache = false;
	int cache_str_len = 0;

	if (sensor == NULL) {
		syslog(LOG_NOTICE, "sensor is null\n");
		return BAD_TEMP;
	}
	// Check if cache is available
	if (sensor->path_cache != NULL) {
		cache_str_len = strlen(sensor->path_cache);
		if (cache_str_len != 0)
			use_cache = true;
	}

	if (use_cache == false) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s", sensor->prefix, sensor->suffix);
		// Update cache, if possible.
		if (sensor->path_cache != NULL)
			snprintf(sensor->path_cache, (PATH_CACHE_SIZE - 1), "%s", fullpath);
		use_cache = true;
	}
	/*
	* By the time control reaches here, use_cache is always true
	* or this function already returned -1. So assume the cache is always on
	*/
	ret = read_sysfs_int(sensor->path_cache, &value);

	/*  Note that Kernel sysfs stub pre-converts raw value in xxxxx format,
	*  which is equivalent to xx.xxx degree - all we need to do is to divide
	*  the read value by 1000
	*/
	if (ret < 0)
		value = ret;
	else
		value = value / 1000;

	if (value < 0) {
		syslog(LOG_ERR, "failed to read temperature bus %s", fullpath);
		return BAD_TEMP;
	}

	usleep(11000);
	return value;
}


static int read_temp_sysfs(struct sensor_info_sysfs *sensor)
{
	int ret;
	int value;
	char fullpath[PATH_CACHE_SIZE];
	bool use_cache = false;
	int cache_str_len = 0;

	if (sensor == NULL) {
		syslog(LOG_NOTICE, "sensor is null\n");
		return BAD_TEMP;
	}
	// Check if cache is available
	if (sensor->path_cache != NULL) {
		cache_str_len = strlen(sensor->path_cache);
		if (cache_str_len != 0)
			use_cache = true;
	}

	if (use_cache == false) {
		// No cached value yet. Calculate the full path first
		ret = assemble_sysfs_path(sensor->prefix, sensor->suffix, fullpath, sizeof(fullpath));
		if(ret != 0) {
			syslog(LOG_NOTICE, "%s: I2C bus %s not available. Failed reading %s\n", __FUNCTION__, sensor->prefix, sensor->suffix);
			return BAD_TEMP;
		}
		// Update cache, if possible.
		if (sensor->path_cache != NULL)
			snprintf(sensor->path_cache, (PATH_CACHE_SIZE - 1), "%s", fullpath);
		use_cache = true;
	}

	/*
	* By the time control reaches here, use_cache is always true
	* or this function already returned -1. So assume the cache is always on
	*/
	ret = read_sysfs_int(sensor->path_cache, &value);

	/*  Note that Kernel sysfs stub pre-converts raw value in xxxxx format,
	*  which is equivalent to xx.xxx degree - all we need to do is to divide
	*  the read value by 1000
	*/
	if (ret < 0)
		value = ret;
	else
		value = value / 1000;

	if (value < 0) {
		syslog(LOG_ERR, "failed to read temperature bus %s", fullpath);
		return BAD_TEMP;
	}

	usleep(11000);
	return value;
}


static int read_critical_max_temp(void)
{
	int i;
	int temp, max_temp = BAD_TEMP;
#ifdef FANCTRL_SIMULATION
	static int t = 50;
	static int div = -2;
#endif

	struct board_info_stu_sysfs *info;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if(info->slot_id != direction)
			continue;
		if(info->critical && (info->flag & CRITICAL_SENSOR_BIT)) {
			temp = info->critical->read_sysfs(info->critical);
			if(temp != BAD_TEMP) {
				if(info->critical->error_cnt)
					syslog(LOG_WARNING, "Sensor [%s] temp lost recovered, set fan normal speed", info->name);
				info->critical->error_cnt = 0;
				temp += info->correction;
				if(info->critical->t2 == BAD_TEMP)
					info->critical->t2 = temp;
				else
					info->critical->t2 = info->critical->t1;
				if(info->critical->t1 == BAD_TEMP)
					info->critical->t1 = temp;
				else
					info->critical->t1 = info->critical->temp;
				info->critical->temp = temp;
			} else {
				if(info->critical->error_cnt < ERROR_TEMP_MAX)
					info->critical->error_cnt++;
				if(info->critical->error_cnt == 1)
					syslog(LOG_WARNING, "Sensor [%s] temp lost detected, set fan normal speed", info->name);
			}
			if(info->critical->temp > max_temp)
				max_temp = info->critical->temp;
		}
	}
#ifdef FANCTRL_SIMULATION
	if(t <= 15) {
		div = 2;
	} else if(t >= 50) {
		div = -2;
	}
	t += div;
	max_temp = t;
#endif
#ifdef DEBUG
	syslog(LOG_DEBUG, "[zmzhan]%s: critical: max_temp=%d", __func__, max_temp);
#endif
	return max_temp;
}

static int calculate_line_pwm(void)
{
	int max_pwm = 0;
	int pwm = 0;
	struct board_info_stu_sysfs *info;
	int i;
	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if(info->slot_id != direction)
			continue;
		if(info->critical && (info->flag & CRITICAL_SENSOR_BIT)) {
			if(info->critical->error_cnt) {
				if(info->critical->error_cnt == ERROR_TEMP_MAX) {
					if(policy->old_pwm != FAN_MAX)
						syslog(LOG_WARNING, "Sensor [%s] temp lost time out, set fan max speed", info->name);
					pwm = FAN_MAX;
				}
				else {
					pwm = policy->old_pwm;
				}
			} else {
					pwm = policy->line->get_speed(info->critical, policy->line);
			}
			if(max_pwm < pwm)
				max_pwm = pwm;
		}
	}

	return max_pwm;
}

static int read_pid_max_temp(void)
{
	int i;
	int temp, max_temp = BAD_TEMP;
	struct board_info_stu_sysfs *info;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if(info->slot_id != direction)
			continue;
		if(info->critical && (info->flag & PID_CTRL_BIT)) {
			temp = info->critical->read_sysfs(info->critical);
			if(temp != BAD_TEMP) {
				if(info->critical->error_cnt)
					syslog(LOG_WARNING, "Sensor [%s] temp lost recovered, set fan normal speed", info->name);
				info->critical->error_cnt = 0;
				temp += info->correction;
				if(info->critical->t2 == BAD_TEMP)
					info->critical->t2 = temp;
				else
					info->critical->t2 = info->critical->t1;
				if(info->critical->t1 == BAD_TEMP)
					info->critical->t1 = temp;
				else
					info->critical->t1 = info->critical->temp;
				info->critical->temp = temp;
			} else {
				if(info->critical->error_cnt < ERROR_TEMP_MAX) {
					info->critical->error_cnt++;
					if(info->critical->error_cnt == 1)
						syslog(LOG_WARNING, "Sensor [%s] temp lost detected, set fan normal speed", info->name);
				}
			}
			if(info->critical->temp > max_temp)
				max_temp = info->critical->temp;
#ifdef DEBUG
			syslog(LOG_DEBUG, "[zmzhan]%s: %s: temp=%d", __func__, info->name, temp);
#endif
		}
	}

	return max_temp;
}

static int calculate_pid_pwm(int fan_pwm)
{
	int i;
	int pwm, max_pwm = 0;
	struct board_info_stu_sysfs *info;
	struct sensor_info_sysfs *critical;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if(info->slot_id != direction)
			continue;
		if(info->critical && (info->flag & PID_CTRL_BIT)) {
			critical = info->critical;
			// if(critical->old_pwm == 0)
			critical->old_pwm = fan_pwm;

			if(critical->error_cnt) {
				if(critical->error_cnt == ERROR_TEMP_MAX) {
					if(critical->old_pwm != FAN_MAX)
						syslog(LOG_WARNING, "Sensor [%s] temp lost time out, set fan max speed", info->name);
					pwm = FAN_MAX;
				}
				else {
					pwm = critical->old_pwm;
				}
			} else {
				pwm = critical->old_pwm + critical->p * (critical->temp - critical->t1) + 
					  critical->i * (critical->temp - critical->setpoint) + 
					  critical->d * (critical->temp + critical->t2 - 2 * critical->t1);
				// if(critical->temp < critical->setpoint) {
				// 	pwm = 0;
				// }
			}
#ifdef DEBUG
			syslog(LOG_DEBUG, "[zmzhan]%s: %s: pwm=%d, old_pwm=%d, p=%f, i=%f, d=%f, setpoint=%f \
				temp=%d, t1=%d, t2=%d", __func__, info->name, pwm, critical->old_pwm, critical->p,
				critical->i, critical->d, critical->setpoint, critical->temp, critical->t1, critical->t2);
#endif
			if(pwm < critical->min_output)
				pwm = critical->min_output;
			if(pwm > max_pwm)
				max_pwm = pwm;
			if(max_pwm > critical->max_output)
				max_pwm = critical->max_output;
#ifdef DEBUG
			syslog(LOG_DEBUG, "[zmzhan]%s: %s: pwm=%d, old_pwm=%d, p=%f, i=%f, d=%f, setpoint=%f \
				temp=%d, t1=%d, t2=%d", __func__, info->name, pwm, critical->old_pwm, critical->p,
				critical->i, critical->d, critical->setpoint, critical->temp, critical->t1, critical->t2);
#endif
		}
	}

	return max_pwm;
}


static int alarm_temp_update(int *alarm)
{
	int i, fan;
	int temp, max_temp = 0;
	struct board_info_stu_sysfs *info;

	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if((info->slot_id != direction) || (info->flag == DISABLE))
			continue;
		if(info->alarm) {
			temp = info->alarm->read_sysfs(info->alarm);
			if(temp != -1) {
				temp += info->correction;
				info->alarm->temp = temp;
				if(info->hwarn != BAD_TEMP && 
					(temp >= info->hwarn || ((info->flag & HIGH_WARN_BIT) && (info->hwarn - temp <= ALARM_TEMP_THRESHOLD) && info->warn_count))) {
					if(++info->warn_count >= ALARM_START_REPORT) {
						syslog(LOG_WARNING, "Major temp alarm: %s arrived %d C(High Warning: >= %d C)",
							info->name, temp, info->hwarn);
						info->warn_count = 0;
						for (fan = 0; fan < TOTAL_FANS; fan++) {
							write_fan_speed(fan, FAN_MAX);
						}
						write_psu_fan_speed(fan, FAN_MAX);
						info->flag |= HIGH_WARN_BIT;
						*alarm |= HIGH_WARN_BIT;
					}
				} else if(info->lwarn != BAD_TEMP && 
						(temp >= info->lwarn || ((info->flag & LOW_WARN_BIT) && (info->lwarn - temp <= ALARM_TEMP_THRESHOLD) && info->warn_count))) {
					if(++info->warn_count >= ALARM_START_REPORT) {
						syslog(LOG_WARNING, "Minor temp alarm: %s arrived %d C(Low Warning: >= %d C)",
							info->name, temp, info->lwarn);
						info->warn_count = 0;
						info->flag |= LOW_WARN_BIT;
						*alarm |= LOW_WARN_BIT;
					}
				} else {
					if(info->flag & HIGH_WARN_BIT) {
						syslog(LOG_INFO, "Recovery major temp alarm: %s arrived %d C", info->name, temp);
						info->flag &= ~HIGH_WARN_BIT;
						*alarm &= ~HIGH_WARN_BIT;
					} else if(info->flag & LOW_WARN_BIT) {
						syslog(LOG_INFO, "Recovery minor temp alarm: %s arrived %d C", info->name, temp);
						if(++info->warn_count >= WARN_RECOVERY_COUNT) {
							info->flag &= ~LOW_WARN_BIT;
							*alarm &= ~LOW_WARN_BIT;
						}
					}
				}
			}
		}
	}
	//printf("%s: alarm: max_temp=%d\n", __func__, max_temp);

	return max_temp;
}

static inline int check_fan_normal_pwm(int pwm)
{
	if(pwm > FAN_NORMAL_MAX)
		return FAN_NORMAL_MAX;
	if(pwm < FAN_NORMAL_MIN)
		return FAN_NORMAL_MIN;
	return pwm;
}

static inline int check_fan_one_fail_pwm(int pwm)
{
	if(pwm > FAN_ONE_FAIL_MAX)
		return FAN_ONE_FAIL_MAX;
	if(pwm < FAN_ONE_FAIL_MIN)
		return FAN_ONE_FAIL_MIN;
	return pwm;
}

static inline int check_fan_speed(int speed, struct line_policy *line)
{
	if(speed > line->end.speed)
		return line->end.speed;
	if(speed < line->begin.speed)
		return line->begin.speed;
	return speed;
}

static inline float get_line_k(struct point begin, struct point end)
{
	return (float)(end.speed - begin.speed) / (end.temp - begin.temp);
}

static inline int check_fall_temp(int temp, struct line_policy *line)
{
	if(temp > line->end.temp)
		return line->end.temp;
	if(temp < line->begin.temp)
		return line->begin.temp;
	return temp;
}

static inline int get_fall_temp(int speed, struct line_policy *line)
{
	float k = get_line_k(line->begin, line->end);
	int fall_temp = (speed + 1 - line->begin.speed) / k + line->begin.temp;
	return check_fall_temp(fall_temp, line);
}

static int calculate_line_speed(struct sensor_info_sysfs *sensor, struct line_policy *line)
{
	float k = get_line_k(line->begin, line->end);
	int fall_temp = get_fall_temp(policy->old_pwm, line);
	int speed;
	int cur_temp = sensor->temp;
	int old_temp = sensor->t1;

	if(cur_temp > old_temp) {
		speed = (int)(k * (cur_temp - line->begin.temp) + line->begin.speed);
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d cal_last_temp=%d k=%f Raising line_pwm=%d", 
		__func__, cur_temp, fall_temp, k, speed);
#endif
	} else {
		if(fall_temp - cur_temp <= line->temp_hyst) {
			speed = (int)(k * (fall_temp - line->begin.temp) + line->begin.speed);
		} else {
			speed = (int)(k * (cur_temp - line->begin.temp) + line->begin.speed);
		}
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d cal_last_temp=%d k=%f Falling line_pwm=%d", 
		__func__, cur_temp, fall_temp, k, speed);
#endif
	}

	return check_fan_speed(speed, line);
}

static int calculate_pid_speed(struct pid_policy *pid)
{
	int output = 0;
	output = pid->last_output + pid->kp * (pid->cur_temp - pid->t1) + 
			 pid->ki * (pid->cur_temp - pid->set_point) + 
			 pid->kd * (pid->cur_temp + pid->t2 - 2 * pid->t1);
	if(output > pid->max_output) {
		return pid->max_output;
	}
	if(output < pid->min_output) {
		return pid->min_output;
	}
	return output;
}

static int calculate_fan_normal_pwm(int cur_temp, int last_temp)
{
	int value;
	int fall_temp = (policy->old_pwm + 1 - FAN_NORMAL_MIN) / NORMAL_K + RAISING_TEMP_LOW;
	if(fall_temp > RAISING_TEMP_HIGH) fall_temp = RAISING_TEMP_HIGH;
	if(fall_temp < RAISING_TEMP_LOW) fall_temp = RAISING_TEMP_LOW;
	
	if(cur_temp >= fall_temp) {
		value = (int)(NORMAL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
		// if(value < policy->old_pwm)	value = policy->old_pwm;
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f Raising line_pwm=%d", 
		__func__, cur_temp, last_temp, fall_temp, NORMAL_K, value);
#endif
	} else {
		if(fall_temp - cur_temp <= CRITICAL_TEMP_HYST) {
			value = (int)(NORMAL_K * (fall_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
		}
		else {
			value = (int)(NORMAL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_NORMAL_MIN);
		}
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f Falling line_pwm=%d", 
		__func__, cur_temp, last_temp, fall_temp, NORMAL_K, value);
#endif
	}

	return check_fan_normal_pwm(value);
}

static int calculate_fan_one_fail_pwm(int cur_temp, int last_temp)
{
	return FAN_ONE_FAIL_MAX;
	int value;
	int fall_temp = (policy->old_pwm + 1 - FAN_ONE_FAIL_MIN) / ONE_FAIL_K + RAISING_TEMP_LOW;
	if(fall_temp > RAISING_TEMP_HIGH) fall_temp = RAISING_TEMP_HIGH;
	if(fall_temp < RAISING_TEMP_LOW) fall_temp = RAISING_TEMP_LOW;
	
	if(cur_temp >= fall_temp) {
		value = (int)(ONE_FAIL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
		// if(value < policy->old_pwm)	value = policy->old_pwm;
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f One fail raising line_pwm=%d", 
		__func__, cur_temp, last_temp, fall_temp, ONE_FAIL_K, value);
#endif
	} else {
		if(fall_temp - cur_temp <= CRITICAL_TEMP_HYST) {
			value = (int)(ONE_FAIL_K * (fall_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
		}
		else {
			value = (int)(ONE_FAIL_K * (cur_temp - RAISING_TEMP_LOW) + FAN_ONE_FAIL_MIN);
		}
#ifdef DEBUG
		syslog(LOG_DEBUG, "[xuth]%s: cur_temp=%d last_temp=%d cal_last_temp=%d k=%f One fail falling line_pwm=%d", 
		__func__, cur_temp, last_temp, fall_temp, ONE_FAIL_K, value);
#endif
	}

	return check_fan_one_fail_pwm(value);
}

#define PSU_FAN_LOW 45
static int calculate_psu_raising_fan_pwm(int temp)
{
	int slope;
	int val;

	if(temp < RAISING_TEMP_LOW) {
		return PSU_FAN_LOW;
	} else if(temp >= RAISING_TEMP_LOW && temp < RAISING_TEMP_HIGH) {
		slope = (FAN_HIGH - PSU_FAN_LOW) / (RAISING_TEMP_HIGH - RAISING_TEMP_LOW);
		val = PSU_FAN_LOW + slope * temp;
		return val;
	} else  {
		return FAN_HIGH;
	}
		return FAN_HIGH;
}

static int calculate_psu_falling_fan_pwm(int temp)
{
	int slope;
	int val;

	if(temp < FALLING_TEMP_LOW) {
		return PSU_FAN_LOW;
	} else if(temp >= FALLING_TEMP_LOW && temp < FALLING_TEMP_HIGH) {
		slope = (FAN_HIGH - PSU_FAN_LOW) / (FALLING_TEMP_HIGH - FALLING_TEMP_LOW);
		val = PSU_FAN_LOW + slope * temp;
		return val;		
	} else  {
		return FAN_HIGH;
	}

	return FAN_HIGH;
}


/*
 * Fan number here is 0-based
 * Note that 1 means present
 */
static int fan_is_present_sysfs(int fan, struct fan_info_stu_sysfs *fan_info)
{
	int ret;
	char buf[PATH_CACHE_SIZE];
	int rc = 0;
	struct fantray_info_stu_sysfs *fantray;
	fantray = &fantray_info[fan];

	snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_present_prefix);

	rc = read_sysfs_int(buf, &ret);
	if(rc < 0) {
		syslog(LOG_ERR, "failed to read module present %s node", fan_info->fan_present_prefix);
		return -1;
	}

	usleep(11000);

	if (ret != 0) {
		if(fantray->present == 1) {
			if(fan < TOTAL_FANS)
				syslog(LOG_ERR, "%s: Fantray-%d not present", __func__, fan + 1);
			else
				syslog(LOG_ERR, "%s: PSU-%d not present", __func__, fan - TOTAL_FANS + 1);
		}
	} else {
		if(fan < TOTAL_FANS)
			return 1;
		snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_status_prefix);
		rc = read_sysfs_int(buf, &ret);
		if(rc < 0) {
			syslog(LOG_ERR, "failed to read PSU %d status %s node", fan - TOTAL_FANS + 1, fan_info->fan_present_prefix);
			return -1;
		}

		usleep(11000);

		if (ret == 0) {
			if((fantray->present == 1) && (fantray->status == 1)) {
				fantray->status = 0;
				syslog(LOG_ERR, "%s: PSU-%d power off", __func__, fan - TOTAL_FANS + 1);
			}
		} else {
			if((fantray->present == 1) && (fantray->status == 0)) {
				fantray->status = 1;
				syslog(LOG_ERR, "%s: PSU-%d power on", __func__, fan - TOTAL_FANS + 1);
			}
			return 1;
		}
	}

	return 0;
}


// Note that the fan number here is 0-based
static int set_fan_sysfs(int fan, int value)
{
	int ret;
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;

	fantray = &fantray_info[fan];
	fan_info = &fantray->fan1;
	
	char fullpath[PATH_CACHE_SIZE];

	ret = fan_is_present_sysfs(fan, fan_info);
	if(ret == 0) {
		fantray->present = 0; //not present
		return -1;
	} else if(ret == 1) {
		fantray->present = 1;
	} else {
		return -1;
	}

	snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->pwm_prefix);
	adjust_sysnode_path(fan_info->prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
	ret = write_sysfs_int(fullpath, value);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
		fan_info->prefix, fan_info->pwm_prefix, value);
		return -1;
	}
	usleep(11000);

	return 0;
}

static int write_fan_led_sysfs(int fan, const int color)
{
	int ret;
	char fullpath[PATH_CACHE_SIZE];
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;
	
	fantray = &fantray_info[fan];
	fan_info = &fantray->fan1;
	

	ret = fan_is_present_sysfs(fan, fan_info);
	if(ret == 0) {
		fantray->present = 0; //not present
		return -1;
	} else if(ret == 1) {
		fantray->present = 1;
	} else {
		return -1;
	}

	snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->fan_led_prefix);
	ret = write_sysfs_int(fullpath, color);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
		fan_info->prefix, fan_info->fan_led_prefix, color);
		return -1;
	}
	usleep(11000);

	return 0;
}


/* Set fan speed as a percentage */
static int write_fan_speed(const int fan, const int value)
{
 	return set_fan_sysfs(fan, value);
}

/* Set up fan LEDs */
static int write_fan_led(const int fan, const int color)
{
	if(fan >= TOTAL_FANS)
		return 0; //not support write PSU LED
	return write_fan_led_sysfs(fan, color);
}

static int get_psu_pwm(void)
{
	int i;
	int ret;
	int pwm = 0, tmp = 0;
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;

	for(i = TOTAL_FANS; i < TOTAL_FANS + TOTAL_PSUS; i++) {
		fantray = &fantray_info[i];
		fan_info = &fantray->fan1;
		
		char fullpath[PATH_CACHE_SIZE];
		
		ret = fan_is_present_sysfs(i, fan_info);
		if(ret == 0) {
			fantray->present = 0; //not present
			continue;
		} else if(ret == 1) {
			fantray->present = 1;
		} else {
			continue;
		}
		
		snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->pwm_prefix);
		adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
		read_sysfs_int(fullpath, &tmp);
		if(tmp > 100)
			tmp = 0;
		if(tmp > pwm)
			pwm = tmp;
		usleep(11000);
	}

	pwm = pwm * FAN_MAX / 100;

	return pwm;
}
/* Set PSU fan speed as a percentage */
static int write_psu_fan_speed(const int fan, int value)
{
	int i;
	int ret;
	char fullpath[PATH_CACHE_SIZE];
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;


	value = value * 100 /FAN_MAX; //convert it to pct
	for(i = TOTAL_FANS; i < TOTAL_FANS + TOTAL_PSUS; i++) {
		fantray = &fantray_info[i];
		fan_info = &fantray->fan1;
		
		ret = fan_is_present_sysfs(i, fan_info);
		if(ret == 0) {
			fantray->present = 0; //not present
			continue;
		} else if(ret == 1) {
			fantray->present = 1;
		} else {
			continue;
		}
		snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, PSU_SPEED_CTRL_NODE);
		adjust_sysnode_path(fan_info->rear_fan_prefix, PSU_SPEED_CTRL_NODE, fullpath, sizeof(fullpath));
		ret = write_sysfs_int(fullpath, PSU_SPEED_CTRL_ENABLE);
		if(ret < 0) {
			syslog(LOG_ERR, "failed to enable control PSU speed");
		}

		snprintf(fullpath, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->pwm_prefix);
		adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->pwm_prefix, fullpath, sizeof(fullpath));
		ret = write_sysfs_int(fullpath, value);
		if(ret < 0) {
			syslog(LOG_ERR, "failed to set fan %s/%s, value %#x",
			fan_info->prefix, fan_info->pwm_prefix, value);
			continue;
		}
		usleep(11000);
	}

	return 0;
}

/* Set up fan LEDs */
static int write_psu_fan_led(const int fan, const int color)
{
	int err;
	
	err = write_fan_led_sysfs(TOTAL_FANS, color);
	err += write_fan_led_sysfs(TOTAL_FANS, color);

	return err;
}


static int fan_rpm_to_pct(const struct rpm_to_pct_map *table, const int table_len, int rpm)
{
	int i;
	
	for (i = 0; i < table_len; i++) {
		if (table[i].rpm > rpm) {
			break;
		}
	}

	/*
	 * If the fan RPM is lower than the lowest value in the table,
	 * we may have a problem -- fans can only go so slow, and it might
	 * have stopped.  In this case, we'll return an interpolated
	 * percentage, as just returning zero is even more problematic.
	 */

	if (i == 0) {
		return (rpm * table[i].pct) / table[i].rpm;
	} else if (i == table_len) { // Fell off the top?
		return table[i - 1].pct;
	}
	
	// Interpolate the right percentage value:
	
	int percent_diff = table[i].pct - table[i - 1].pct;
	int rpm_diff = table[i].rpm - table[i - 1].rpm;
	int fan_diff = table[i].rpm - rpm;

	return table[i].pct - (fan_diff * percent_diff / rpm_diff);
}


/*return: 1 OK, 0 not OK*/
int fan_speed_okay(const int fan, int speed, const int slop)
{
	int ret;
	char buf[PATH_CACHE_SIZE];
	int rc = 1;
	int front_speed, front_pct;
	int rear_speed, rear_pct;
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;
	
	fantray = &fantray_info[fan];
	fan_info = &fantray->fan1;

	ret = fan_is_present_sysfs(fan, fan_info);
	if(ret == 0) {
		fantray->present = 0; //not present
		return 0;
	} else if(ret == 1) {
		fantray->present = 1;
	}

	snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->front_fan_prefix);

	rc = read_sysfs_int(buf, &ret);
	if(rc < 0) {
		syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
		return -1;
	}
	front_speed = ret;
	usleep(11000);
	if(front_speed < FAN_FAIL_RPM) {
		fan_info->front_failed++;
		if(fan_info->front_failed == 1)
			syslog(LOG_WARNING, "%s front speed %d, less than %d detected", 
				fantray->name, front_speed, FAN_FAIL_RPM);
		if(fan_info->front_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s front speed %d, less than %d time out", 
				fantray->name, front_speed, FAN_FAIL_RPM);
		if(fan_info->front_failed > FAN_FAIL_COUNT)
			fan_info->front_failed = FAN_FAIL_COUNT;
	} else if(speed == FAN_MAX && (front_speed < (FAN_FRONTT_SPEED_MAX * (100 - slop) / 100))){
		fan_info->front_failed++;
		if(fan_info->front_failed == 1)
			syslog(LOG_WARNING, "%s front speed %d, less than %d%% of max speed(%d) detected", 
				fantray->name, front_speed, 100 - slop, speed);
		if(fan_info->front_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s front speed %d, less than %d%% of max speed(%d) time out", 
				fantray->name, front_speed, 100 - slop, speed);
		if(fan_info->front_failed > FAN_FAIL_COUNT)
			fan_info->front_failed = FAN_FAIL_COUNT;
	} else {
		if(fan_info->front_failed)
			syslog(LOG_WARNING, "%s front speed resumed normal", fantray->name);
		fan_info->front_failed = 0;
	}

	memset(buf, 0, PATH_CACHE_SIZE);
	snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->prefix, fan_info->rear_fan_prefix);

	rc = read_sysfs_int(buf, &ret);
	if(rc < 0) {
		syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
		return -1;
	}
	rear_speed = ret;
	if(rear_speed < FAN_FAIL_RPM) {
		fan_info->rear_failed++;
		if(fan_info->rear_failed == 1)
			syslog(LOG_WARNING, "%s rear speed %d, less than %d detected", 
				fantray->name, rear_speed, FAN_FAIL_RPM);
		if(fan_info->rear_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s rear speed %d, less than %d time out", 
				fantray->name, rear_speed, FAN_FAIL_RPM);
		if(fan_info->rear_failed > FAN_FAIL_COUNT)
			fan_info->rear_failed = FAN_FAIL_COUNT;
	} else if(speed == FAN_MAX && (rear_speed < (FAN_REAR_SPEED_MAX * (100 - slop) / 100))){
		fan_info->rear_failed++;
		if(fan_info->rear_failed == 1)
			syslog(LOG_WARNING, "%s rear speed %d, less than %d%% of max speed(%d) detected", 
				fantray->name, rear_speed, 100 - slop, speed);
		if(fan_info->rear_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s rear speed %d, less than %d%% of max speed(%d) time out", 
				fantray->name, rear_speed, 100 - slop, speed);
		if(fan_info->rear_failed > FAN_FAIL_COUNT)
			fan_info->rear_failed = FAN_FAIL_COUNT;
	} else {
		if(fan_info->rear_failed)
			syslog(LOG_WARNING, "%s rear speed resumed normal", fantray->name);
		fan_info->rear_failed = 0;
	}

	if(fan_info->front_failed >= FAN_FAIL_COUNT && fan_info->rear_failed >= FAN_FAIL_COUNT) {
		// fan_info->front_failed = 0;
		// fan_info->rear_failed = 0;
		fantray->failed = 1;
	}
	//syslog(LOG_DEBUG, "[zmzhan]%s: front_speed = %d, rear_speed = %d", __func__, front_speed, rear_speed);
	if(fan_info->front_failed >= FAN_FAIL_COUNT || fan_info->rear_failed >= FAN_FAIL_COUNT) {
		 //syslog(LOG_WARNING, "%s front %d, rear %d, expected %d",
		 //	fantray->name, front_speed, rear_speed, speed);
		 return 0;
	}

	return 1;

}

/*return: 1 OK, 0 not OK*/
int psu_speed_okay(const int fan, int speed, const int slop)
{
	int ret;
	char buf[PATH_CACHE_SIZE];
	int rc = 0;
	int psu_speed, pct;
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;
	
	fantray = &fantray_info[fan];
	fan_info = &fantray->fan1;

	ret = fan_is_present_sysfs(fan, fan_info);
	if(ret == 0) {
		fantray->present = 0; //not present
		return 0;
	} else if(ret == 1) {
		fantray->present = 1;
	}

	snprintf(buf, PATH_CACHE_SIZE, "%s/%s", fan_info->rear_fan_prefix, fan_info->front_fan_prefix);
	adjust_sysnode_path(fan_info->rear_fan_prefix, fan_info->front_fan_prefix, buf, sizeof(buf));
	rc = read_sysfs_int(buf, &ret);
	if(rc < 0) {
		syslog(LOG_ERR, "failed to read %s node", fan_info->front_fan_prefix);
		return -1;
	}
	psu_speed = ret;
	usleep(11000);
	if(psu_speed < FAN_FAIL_RPM) {
		fan_info->front_failed++;
		if(fan_info->front_failed == 1)
			syslog(LOG_WARNING, "%s speed %d, less than %d detected", 
				fantray->name, psu_speed, FAN_FAIL_RPM);
		if(fan_info->front_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s speed %d, less than %d time out", 
				fantray->name, psu_speed, FAN_FAIL_RPM);
		if(fan_info->front_failed > FAN_FAIL_COUNT)
			fan_info->front_failed = FAN_FAIL_COUNT;
	} else if(speed == FAN_MAX && (psu_speed < (PSU_SPEED_MAX * (100 - slop) / 100))){
		fan_info->front_failed++;
		if(fan_info->front_failed == 1)
			syslog(LOG_WARNING, "%s speed %d, less than %d%% of max speed(%d) detected", 
				fantray->name, psu_speed, 100 - slop, speed);
		if(fan_info->front_failed == FAN_FAIL_COUNT)
			syslog(LOG_WARNING, "%s speed %d, less than %d%% of max speed(%d) time out", 
				fantray->name, psu_speed, 100 - slop, speed);
		if(fan_info->front_failed > FAN_FAIL_COUNT)
			fan_info->front_failed = FAN_FAIL_COUNT;
	} else {
		if(fan_info->front_failed)
			syslog(LOG_WARNING, "%s speed resumed normal", fantray->name);
		fan_info->front_failed = 0;
	}

	if(fan_info->front_failed >= FAN_FAIL_COUNT)
		return 0;

	return 1;

}

static int fancpld_watchdog_enable(void)
{
	int ret;
	char fullpath[PATH_CACHE_SIZE];
	
	snprintf(fullpath, PATH_CACHE_SIZE, "%s", FAN_WDT_ENABLE_SYSFS);
	ret = write_sysfs_int(fullpath, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set fan %s, value 1",
		FAN_WDT_ENABLE_SYSFS);
		return -1;
	}
	usleep(11000);
	snprintf(fullpath, PATH_CACHE_SIZE, "%s", FAN_WDT_TIME_SYSFS);
	ret = write_sysfs_int(fullpath, FAN_WDT_TIME);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set fan %s, value 1",
		FAN_WDT_TIME_SYSFS);
		return -1;
	}
	usleep(11000);

	return 0;
}

static int system_shutdown(const char *why)
{
	int ret;

	syslog(LOG_EMERG, "Shutting down:  %s", why);

	ret = write_sysfs_int(PSU1_SHUTDOWN_SYSFS, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set PSU1 shutdown");
		return -1;
	}
	ret = write_sysfs_int(PSU2_SHUTDOWN_SYSFS, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set PSU2 shutdown");
		return -1;
	}
	ret = write_sysfs_int(PSU3_SHUTDOWN_SYSFS, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set PSU2 shutdown");
		return -1;
	}
	ret = write_sysfs_int(PSU4_SHUTDOWN_SYSFS, 1);
	if(ret < 0) {
		syslog(LOG_ERR, "failed to set PSU2 shutdown");
		return -1;
	}

	stop_watchdog();

	sleep(2);
	exit(2);

	return 0;
}

const char *fan_path[] = {
	"/sys/bus/i2c/devices/36-0050/eeprom",
	"/sys/bus/i2c/devices/35-0050/eeprom",
	"/sys/bus/i2c/devices/34-0050/eeprom",
	"/sys/bus/i2c/devices/33-0050/eeprom",
	"/sys/bus/i2c/devices/32-0050/eeprom",
};
static int get_fan_direction(void)
{
// #ifdef FOR_F2B
// 	return FAN_DIR_F2B;
// #else
// 	return FAN_DIR_B2F;
// #endif
	char dir_str[64];
	int f2r_fan_cnt = 0;
	int r2f_fan_cnt = 0;
	FILE *fp;
	int i = 0;
	// return FAN_DIR_B2F;
	for(; i < TOTAL_FANS; i++)
	{
		char command[128];
		memset(command, 0, sizeof(command));
		if(i >= sizeof(fan_path)/sizeof(fan_path[0]))
			continue;
		// sprintf(command, "fruid-util fan%d | grep 'Product Part Number' 2>/dev/null", i+1);
		/* Compare fan eeprom date
		*  string "Product Part Number       : R1241-F9001-01"
		*  string "001" of "F9001" indicates direction: FAN_DIR_F2B
		*/
		sprintf(command, "hexdump -C %s | grep R1241-F9001; echo $?", fan_path[i]);
		fp = popen(command,"r");
		if (fp == NULL) {
			syslog(LOG_ERR, "failed to get fan%d direction", i+1);
			continue;
		}

		memset(dir_str, 0, sizeof(dir_str));
		/* Read the output a line at a time - output it. */
		while (fgets(dir_str, sizeof(dir_str), fp) != NULL) ;

		/* close */
		pclose(fp);

		// syslog(LOG_INFO, "get fan%d direction, [%s]", i+1, dir_str);
		if(!strncmp(dir_str, "0", 1)) {
			f2r_fan_cnt++;
			syslog(LOG_INFO, "get fan%d direction, [Front to rear]",i+1);			
		}
		else {
			r2f_fan_cnt++;
			syslog(LOG_INFO, "get fan%d direction: [Rear to front]", i+1);	
		}
	}

	if((f2r_fan_cnt == 0) && (r2f_fan_cnt == 0))
		syslog(LOG_ERR, "failed to get all fan direction, use default direction : Front to rear");

	if(f2r_fan_cnt >= r2f_fan_cnt) {
		syslog(LOG_INFO, "direction: [Front to rear]");
		return FAN_DIR_F2B;
	} else {
		syslog(LOG_INFO, "direction: [Rear to front]");
		return FAN_DIR_B2F;
	}
}

static int pid_ini_parser(struct board_info_stu_sysfs *info, FILE *fp)
{
	char *p;
	char buf[PID_FILE_LINE_MAX];
	struct sensor_info_sysfs *sensor;
	sensor = info->critical;

	while(fgets(buf, PID_FILE_LINE_MAX, fp) != NULL) {
		if(buf[0] == '#' || strlen(buf) <= 0 || buf[0] == '\r' || buf[0] == '\n')
			continue;

		p = strtok(buf, "=");
		while(p != NULL) {
			if(!strncmp(p, "PID_enable", strlen("PID_enable"))) {
				p = strtok(NULL, "=");
				if(p) {
					pid_using = atoi(p);
				} else {
					pid_using = 0;
				}
				return 0;
			} else if(!strncmp(p, "setpoint", strlen("setpoint"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->setpoint = atof(p);
			} else if(!strncmp(p, "P", strlen("P"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->p = atof(p);
			} else if(!strncmp(p, "I", strlen("I"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->i = atof(p);
			} else if(!strncmp(p, "D", strlen("D"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->d = atof(p);
			} else if(!strncmp(p, "min_output", strlen("min_output"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->min_output = atof(p);
				// syslog(LOG_DEBUG, "%s: setpoint=%f, P=%f, I=%f, D=%f, min_output=%f",
				// 	__func__, sensor->setpoint, sensor->p, sensor->i, sensor->d, sensor->min_output);
				return 0;
			} else if(!strncmp(p, "max_output", strlen("max_output"))) {
				p = strtok(NULL, "=");
				if(p)
					sensor->max_output = atof(p);
				// syslog(LOG_DEBUG, "%s: setpoint=%f, P=%f, I=%f, D=%f, max_output=%f",
				// 	__func__, sensor->setpoint, sensor->p, sensor->i, sensor->d, sensor->max_output);
				return 0;
			}
			p = strtok(NULL, "=");
		}
	}
	
	return 0;
}

static int load_pid_config(void)
{
	int i;
	FILE *fp;
	char buf[PID_FILE_LINE_MAX];
	char *p;
	int len;
	struct board_info_stu_sysfs *binfo = &board_info[0];

	fp = fopen(PID_CONFIG_PATH, "r");
	if(!fp) {
		pid_using = 0;
		syslog(LOG_NOTICE, "PID configure file does not find, using default PID params");
		return 0;
	}
	while(fgets(buf, PID_FILE_LINE_MAX, fp) != NULL) {
		len = strlen(buf);
		buf[len - 1] = '\0';
		if(buf[0] == '#' || strlen(buf) <= 0 || buf[0] == '\r' || buf[0] == '\n')
			continue;
		p = strtok(buf, "[");
		while(p != NULL) {
			if(!strncmp(p, "PID enable", strlen("PID enable"))) {
				pid_ini_parser(binfo, fp);
			} else {
				for(i = 0; i < BOARD_INFO_SIZE; i++) {
					binfo = &board_info[i];
					if(!strncmp(binfo->name, p, strlen(binfo->name))) {
						pid_ini_parser(binfo, fp);
						break;
					}
				}
			}
			p = strtok(NULL, "[");
		}
	}

	fclose(fp);
	return 0;
}

static int policy_init(void)
{
	int slope;

	direction = get_fan_direction();
	if(direction == FAN_DIR_B2F) {
		policy = &b2f_normal_policy;
	} else {
		policy = &f2b_normal_policy;
	}

	load_pid_config();
	if(pid_using == 0) {
		syslog(LOG_NOTICE, "PID configure: using default PID params");
		//return 0;
	}

	struct board_info_stu_sysfs *info;
	struct sensor_info_sysfs *critical;
	int i;
	for(i = 0; i < BOARD_INFO_SIZE; i++) {
		info = &board_info[i];
		if(info->slot_id != direction)
			continue;
		if(info->critical && (info->flag & PID_CTRL_BIT)) {
			critical = info->critical;
			syslog(LOG_INFO, "%s: setpoint=%f, p=%f, i=%f, d=%f", info->name, critical->setpoint, 
			critical->p, critical->i, critical->d);
		}
	}

	return 0;
}

int main(int argc, char **argv) {
	int critical_temp;
	int old_temp = -1;
	struct fantray_info_stu_sysfs *info;
	int fan_speed_temp = FAN_MEDIUM;
	int fan_speed = FAN_MEDIUM;
	int bad_reads = 0;
	int fan_failure = 0;
	int sub_failed = 0;
	int one_failed = 0; //recored one system fantray failed
	int old_speed = FAN_MEDIUM;
	int fan_bad[TOTAL_FANS + TOTAL_PSUS] = {0};
	int fan;
	unsigned log_count = 0; // How many times have we logged our temps?
	int prev_fans_bad = 0;
	int shutdown_delay = 0;
	int psu_pwm;
	int switch_pwm = 0;
	int pid_pwm = 0;
	int alarm = 0;
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
	int psu_old_temp = 0;
	int psu_raising_pwm;
	int psu_falling_pwm;
	int psu_fan_speed = FAN_MEDIUM;
#endif
	struct fantray_info_stu_sysfs *fantray;
	struct fan_info_stu_sysfs *fan_info;

	// Initialize path cache
	init_path_cache();

	// Start writing to syslog as early as possible for diag purposes.
	openlog("fand", LOG_CONS, LOG_DAEMON);

	daemon(1, 0);

	syslog(LOG_DEBUG, "Starting up;  system should have %d fans.", TOTAL_FANS);

	/* Start watchdog in manual mode */
	start_watchdog(0);

	/* Set watchdog to persistent mode so timer expiry will happen independent
	* of this process's liveliness. */
	set_persistent_watchdog(WATCHDOG_SET_PERSISTENT);

	fancpld_watchdog_enable();

	policy_init();

	sleep(5);  /* Give the fans time to come up to speed */

	while (1) {
		/* Read sensors */
		critical_temp = read_critical_max_temp();
		alarm_temp_update(&alarm);
		fan_speed_temp = 0;
		// if (critical_temp == BAD_TEMP) {
		// 	if(bad_reads++ >= ERROR_TEMP_MAX) {
		// 		if(critical_temp == BAD_TEMP) {
		// 			syslog(LOG_ERR, "Critical Temp read error!");
		// 		}
		// 		bad_reads = 0;
		// 	}
		// }

#if 0
		/* Protection heuristics */
		if(critical_temp > SYSTEM_LIMIT) {
			for (fan = 0; fan < TOTAL_FANS; fan++) {
				write_fan_speed(fan, fan_speed);
			}
			if(shutdown_delay++ > SHUTDOWN_DELAY_TIME)
				system_shutdown("Critical temp limit reached");
		}
#endif
		/*
		 * Calculate change needed -- we should eventually
		 * do something more sophisticated, like PID.
		 *
		 * We should use the intake temperature to adjust this
		 * as well.
		 */

		/* Other systems use a simpler built-in table to determine fan speed. */
#if 1
		policy->old_pwm = fan_speed;
		// fan_speed_temp = policy->calculate_pwm(critical_temp, old_temp);
		fan_speed_temp = calculate_line_pwm();
#ifdef DEBUG
		syslog(LOG_DEBUG, "[zmzhan]%s: line_speed=%d", __func__, fan_speed_temp);
#endif
#endif
#ifndef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
		psu_pwm = get_psu_pwm();
		// if(fan_speed_temp < psu_pwm)
		// 	fan_speed_temp = psu_pwm;
#endif
		if(1) {
			read_pid_max_temp();
			pid_pwm = calculate_pid_pwm(fan_speed);
			if(pid_pwm > fan_speed_temp)
				fan_speed_temp = pid_pwm;
		}
		fan_speed = fan_speed_temp;
#ifdef DEBUG
		syslog(LOG_DEBUG, "[zmzhan]%s: fan_speed=%d, pid_using=%d, pid_pwm=%d", 
			__func__, fan_speed, pid_using, pid_pwm);
#endif
		policy->pwm = fan_speed;
		old_temp = critical_temp;
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
		psu_raising_pwm = calculate_psu_raising_fan_pwm(critical_temp);
		psu_falling_pwm = calculate_psu_falling_fan_pwm(critical_temp);
		if(psu_old_temp <= critical_temp) {
			/*raising*/
			if(psu_raising_pwm >= psu_fan_speed) {
				psu_fan_speed = psu_raising_pwm;
			}
		} else {
			/*falling*/
			if(psu_falling_pwm <= psu_fan_speed ) {
				psu_fan_speed = psu_falling_pwm;
			}
		}
		psu_old_temp = critical_temp;
#endif

		/*
		 * Update fans only if there are no failed ones. If any fans failed
		 * earlier, all remaining fans should continue to run at max speed.
		 */
		if (fan_failure == 0) {
			if (log_count++ % REPORT_TEMP == 0) {
				syslog(LOG_NOTICE, "critical temp %d, fan speed %d%%",
					critical_temp, fan_speed * 100 / FAN_MAX);
				syslog(LOG_NOTICE, "Fan speed changing from %d%% to %d%%",
					old_speed * 100 / FAN_MAX, fan_speed * 100 / FAN_MAX);
			}
			for (fan = 0; fan < TOTAL_FANS; fan++) {
				if((alarm & HIGH_WARN_BIT) == 0) {
					write_fan_speed(fan, fan_speed);
				}
				else {
#ifdef DEBUG
					syslog(LOG_DEBUG, "[zmzhan] HIGH_WARN_BIT: %d",alarm & HIGH_WARN_BIT);
#endif
				}
			}
#ifdef CONFIG_PSU_FAN_CONTROL_INDEPENDENT
			write_psu_fan_speed(fan, psu_fan_speed);
#else
			if((alarm & HIGH_WARN_BIT) == 0)
				write_psu_fan_speed(fan, fan_speed);
#endif
		}

		
		/*
		 * Wait for some change.  Typical I2C temperature sensors
		 * only provide a new value every second and a half, so
		 * checking again more quickly than that is a waste.
		 *
		 * We also have to wait for the fan changes to take effect
		 * before measuring them.
		 */

		sleep(3);

	    /* Check fan RPMs */

	    for (fan = 0; fan < TOTAL_FANS; fan++) {
			/*
			* Make sure that we're within some percentage
			* of the requested speed.
			*/
			if (fan_speed_okay(fan, fan_speed, FAN_FAILURE_OFFSET)) {
				if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
					write_fan_led(fan, FAN_LED_GREEN);
					syslog(LOG_CRIT, "Fan %d has recovered", fan + 1);
				}
				fan_bad[fan] = 0;
			} else {
				fan_bad[fan]++;
			}
		}
		for(fan = TOTAL_FANS; fan < TOTAL_FANS + TOTAL_PSUS; fan++) {
			if (psu_speed_okay(fan, fan_speed, FAN_FAILURE_OFFSET)) {
				if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
					//write_fan_led(fan, FAN_LED_GREEN);
					syslog(LOG_CRIT, "PSU %d has recovered", fan - TOTAL_FANS + 1);
				}
				fan_bad[fan] = 0;
			} else {
				fan_bad[fan]++;
			}
		}

		fan_failure = 0;
		sub_failed = 0;
		one_failed = 0;
		for (fan = 0; fan < TOTAL_FANS + TOTAL_PSUS; fan++) {
			if (fan_bad[fan] >= FAN_FAILURE_THRESHOLD) {
				fantray = &fantray_info[fan];
				fan_info = &fantray->fan1;
				if(fan_info->front_failed >= FAN_FAIL_COUNT) {
					sub_failed++;
					one_failed++;
#ifdef DEBUG
					syslog(LOG_DEBUG, "[zmzhan]%s:fan[%d] front_failed=%d", __func__, fan, fan_info->front_failed);
#endif
				}
				if(fan_info->rear_failed >= FAN_FAIL_COUNT) {
					sub_failed++;
					one_failed++;
#ifdef DEBUG
					syslog(LOG_DEBUG, "[zmzhan]%s:fan[%d] rear_failed=%d", __func__, fan, fan_info->rear_failed);
#endif
				}
				if(fantray->failed > 0)
					fan_failure++;
				else if(fan < TOTAL_FANS && fantray->present == 0)
					fan_failure++;

				write_fan_led(fan, FAN_LED_RED);
			}
		}
#ifdef DEBUG
		syslog(LOG_DEBUG, "[zmzhan]%s: fan_failure=%d, sub_failed=%d", __func__, fan_failure, sub_failed);
#endif
		if(sub_failed >= 2) {
			fan_failure += sub_failed / 2;
		} else if(sub_failed == 1 && one_failed == 1) {
			if(direction == FAN_DIR_B2F) {
				policy = &b2f_one_fail_policy;
			} else {
				policy = &f2b_one_fail_policy;
			}
#ifdef DEBUG
			syslog(LOG_DEBUG, "[zmzhan]%s: Change the policy: policy=%p(fail: b2f:%p, f2b:%p)", __func__, policy, &b2f_one_fail_policy, &f2b_one_fail_policy);
#endif
		} else {
			if(one_failed == 0 && (policy == &b2f_one_fail_policy || policy == &f2b_one_fail_policy)) {
				if(direction == FAN_DIR_B2F) {
					policy = &b2f_normal_policy;
				} else {
					policy = &f2b_normal_policy;
				}
#ifdef DEBUG
				syslog(LOG_DEBUG, "[zmzhan]%s: Recovery policy: policy=%p(b2f:%p, f2b:%p)", __func__, policy, &b2f_normal_policy, &f2b_normal_policy);
#endif
			}
		}
		if (fan_failure > 0) {
			if (prev_fans_bad != fan_failure) {
				syslog(LOG_CRIT, "%d fans failed", fan_failure);
			}
			fan_speed = FAN_MAX;
			for (fan = 0; fan < TOTAL_FANS; fan++) {
				write_fan_speed(fan, fan_speed);
			}
			write_psu_fan_speed(fan, fan_speed);
			old_speed = fan_speed;
		} else if(prev_fans_bad != 0 && fan_failure == 0){
			old_speed = fan_speed;
		} else {
			old_speed = fan_speed;
		}
		/* Suppress multiple warnings for similar number of fan failures. */
		prev_fans_bad = fan_failure;

		/* if everything is fine, restart the watchdog countdown. If this process
		 * is terminated, the persistent watchdog setting will cause the system
		 * to reboot after the watchdog timeout. */
		kick_watchdog();
		usleep(11000);
	}

	return 0;
}

