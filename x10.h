/*
 * X10 definitions.
 * Copyright (C) 1999  Steven Brown
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * Steven Brown <swbrown@ucsd.edu>
 *
 * $Id: x10.h,v 1.4 1999/05/19 08:22:17 kefka Exp $
 */

#ifndef X10_H
#define X10_H

#include <time.h>
#include <unistd.h>

/* Magic number for X10 data structure */

#define X10_MAGIC 0x4F8A19E6

/* The maximum time to wait for an expected byte to be readable. */
#define X10_WAIT_READ_USEC_DELAY 5000000

/* The maximum time to wait to be able to write to the x10 hardware. */
#define X10_WAIT_WRITE_USEC_DELAY 5000000

/* Bitflags that can be attached to a time download. */
#define TIME_MONITOR_CLEAR 1
#define TIME_TIMER_PURGE 2
#define TIME_BATTERY_TIMER_CLEAR 4

/* Bitflags for the Header:Code byte. */
#define HEADER_DEFAULT 0x04
#define HEADER_EXTENDED 0x01
#define HEADER_FUNCTION 0x02

/* Types of hardware we know about. */
#define DEVICE_UNDEFINED 0
#define DEVICE_LAMP 1
#define DEVICE_APPLIANCE 2
#define DEVICE_MOTION_DETECTOR 3
#define DEVICE_SIGNAL 4
#define DEVICE_TRANSCEIVER 5

/* Housecode values. */
#define HOUSECODE_A 0x06
#define HOUSECODE_B 0x0e
#define HOUSECODE_C 0x02
#define HOUSECODE_D 0x0a
#define HOUSECODE_E 0x01
#define HOUSECODE_F 0x09
#define HOUSECODE_G 0x05
#define HOUSECODE_H 0x0d
#define HOUSECODE_I 0x07
#define HOUSECODE_J 0x0f
#define HOUSECODE_K 0x03
#define HOUSECODE_L 0x0b
#define HOUSECODE_M 0x00
#define HOUSECODE_N 0x08
#define HOUSECODE_O 0x04
#define HOUSECODE_P 0x0c

/* Devicecode values. */
#define DEVICECODE_1 0x06
#define DEVICECODE_2 0x0e
#define DEVICECODE_3 0x02
#define DEVICECODE_4 0x0a
#define DEVICECODE_5 0x01
#define DEVICECODE_6 0x09
#define DEVICECODE_7 0x05
#define DEVICECODE_8 0x0d
#define DEVICECODE_9 0x07
#define DEVICECODE_10 0x0f
#define DEVICECODE_11 0x03
#define DEVICECODE_12 0x0b
#define DEVICECODE_13 0x00
#define DEVICECODE_14 0x08
#define DEVICECODE_15 0x04
#define DEVICECODE_16 0x0c

/* Commands the x10 hardware can accept. */
#define COMMAND_ALL_UNITS_OFF 0x00
#define COMMAND_ALL_LIGHTS_ON 0x01
#define COMMAND_ON 0x02
#define COMMAND_OFF 0x03
#define COMMAND_DIM 0x04
#define COMMAND_BRIGHT 0x05
#define COMMAND_ALL_LIGHTS_OFF 0x06
#define COMMAND_EXTENDED_CODE 0x07
#define COMMAND_HAIL_REQUEST 0x08
#define COMMAND_HAIL_ACKNOWLEDGE 0x09
#define COMMAND_PRESET_DIM1 0x0a
#define COMMAND_PRESET_DIM2 0x0b
#define COMMAND_EXTENDED_DATA_TRANSFER 0x0c
#define COMMAND_STATUS_ON 0x0d
#define COMMAND_STATUS_OFF 0x0e
#define COMMAND_STATUS_REQUEST 0x0f

/* Typedefs. */
typedef struct x10 X10;

/* Structure to hold x10 info. */
struct x10 {
	unsigned magic;
	int fd;
	int housecode;
	int address_buffer_count;
	unsigned char address_buffer_housecode;
	char address_buffer[16];
};

/* Prototypes. */

X10 *x10_open(char *x10_tty_name);
int x10_write_message(X10 *x10, void *buf, size_t count);
void x10_read_event(X10 *x10);
int x10_letter_to_housecode(char houseletter, unsigned char *housecode);
int x10_number_to_devicecode(int devicenum, unsigned char *devicecode);

#endif
