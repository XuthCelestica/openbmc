/*
 * Copyright 2018-present Facebook. All Rights Reserved.
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
 */

#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <errno.h>
#include "vbs.h"


/* Location in SRAM used for verified boot content/flags. */
#define AST_SRAM_VBS_BASE   0x1E720200

#define PAGE_SIZE              0x1000
#define PAGE_OFFSETED_OFF(addr, ps) ((ps-1)&addr)
#define PAGE_OFFSETED_BASE(addr, ps) (addr-PAGE_OFFSETED_OFF(addr, ps))

#define VBS_ERROR(type, val, string)[val] = string
const char *error_code_map[] = {
  VBS_ERROR_TABLE
};
#undef VBS_ERROR

struct vbs *vboot_status()
{
  static struct vbs vbs_cache = {0};
  static bool   vbs_cache_valid = false;
  int mem_fd;
  uint8_t *vboot_base;

  if (vbs_cache_valid) {
    return &vbs_cache;
  }

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    syslog(LOG_CRIT, "%s: Error opening /dev/mem (%s)\n", __func__, strerror(errno));
    return NULL;
  }
  vboot_base = (uint8_t *)mmap(NULL, PAGE_SIZE, 
      PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd,
      PAGE_OFFSETED_BASE(AST_SRAM_VBS_BASE, PAGE_SIZE));
  if (!vboot_base) {
    syslog(LOG_CRIT, "%s: Error mapping VERIFIED_BOOT_STRUCT_BASE (%s)\n", __func__, strerror(errno));
    close(mem_fd);
    return NULL;
  }
  memcpy(&vbs_cache, vboot_base + PAGE_OFFSETED_OFF(AST_SRAM_VBS_BASE, PAGE_SIZE),
      sizeof(vbs_cache));
  // TODO check CRC?
  vbs_cache_valid = true;
  munmap(vboot_base, PAGE_SIZE);
  close(mem_fd);
  return &vbs_cache;
}

const char *vboot_error(uint32_t error_code)
{
  if (error_code >= sizeof(error_code_map)/sizeof(error_code_map[0]) ||
        error_code_map[error_code] == NULL) {
    return "The error code is unknown";
  }
  return error_code_map[error_code];
}

const char *vboot_time(char *time_str, size_t buf_size, uint32_t t)
{
  struct tm tms;
  time_t tx = (time_t)t;
  if (!time_str || !buf_size) {
    return "invalid";
  }
  memset(time_str, 0, buf_size);
  if (t == 0 || t > 4000000000) {
    strcpy(time_str, "not set");
  } else {
    tms = *(localtime(&tx));
    // Sat Jun 17 20:17:06 2017
    strftime(time_str, buf_size - 1, "%c", &tms);
  }
  return time_str;
}

