/*
 * Handle the cm11a interface to the x10 hardware.
 * Portions Copyright (C) 2013 Stephen Rodgers
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
 */

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include "notify.h"
#include "x10.h"
#include "types.h"


static unsigned char houseCodes[16] =
{
	HOUSECODE_A,
	HOUSECODE_B,
	HOUSECODE_C,
	HOUSECODE_D,
	HOUSECODE_E,
	HOUSECODE_F,
	HOUSECODE_G,
	HOUSECODE_H,	
	HOUSECODE_I,
	HOUSECODE_J,
	HOUSECODE_K,
	HOUSECODE_L,
	HOUSECODE_M,
	HOUSECODE_N,
	HOUSECODE_O,
	HOUSECODE_P
};

static unsigned char deviceCodes[16] =
{
	DEVICECODE_1,
	DEVICECODE_2,
	DEVICECODE_3,
	DEVICECODE_4,
	DEVICECODE_5,
	DEVICECODE_6,	
	DEVICECODE_7,
	DEVICECODE_8,
	DEVICECODE_9,
	DEVICECODE_10,
	DEVICECODE_11,
	DEVICECODE_12,
	DEVICECODE_13,
	DEVICECODE_14,
	DEVICECODE_15,
	DEVICECODE_16
};

static const int addresscode2int[16] = 
	{13,5,3,11,15,7,1,9,14,6,4,12,16,8,2,10};
	
	
static const char housecode2letter[17] = "MECKOGAINFDLPHBJ";


/* 
 * Wait for the x10 hardware to provide us with some data.
 *
 * This function should only be called when we know the x10 should have sent
 * us something.  We don't wait long in here, if it isn't screwed up, it
 * should be sending quite quickly.  We return true if we got a byte and
 * false if we timed out waiting for one.
 */
 
static int x10_wait_read(X10 *x10) {
	fd_set read_fd_set;
	struct timeval tv;
	int retval;
	
	/* Wait for data to be readable. */
	for(;;) {
		
		/* Make the call to select to wait for reading. */
		FD_ZERO(&read_fd_set);
		FD_SET(x10->fd, &read_fd_set);
		tv.tv_sec = (X10_WAIT_READ_USEC_DELAY)/1000000u;
		tv.tv_usec = (X10_WAIT_READ_USEC_DELAY)%1000000u;
		retval=select(x10->fd+1, &read_fd_set, NULL, NULL, &tv);
		
		/* Did select error? */
		if(retval == -1) {
			
			/* If it's an EINTR, go try again. */
			if(errno == EINTR) {
				debug(DEBUG_EXPECTED, "Signal recieved in read select, restarting.");
				continue;
			}
			
			/* It was something weird. */
			fatal("Error in read select: %s", strerror(errno));
		}
		
		/* Was data available? */
		if(retval) {	
			
			/* We got some data, return ok. */
			return(1);
		}
		
		/* No data available. */
		else {
			
			/* We didn't get any data, this is a fail. */
			return(0);
		}
	}
}


/* 
 * Wait for the x10 hardware to be writable.
 */
static int x10_wait_write(X10 *x10) {
	fd_set write_fd_set;
	struct timeval tv;
	int retval;
	
	/* Wait for data to be writable. */
	for(;;) {
		
		/* Make the call to select to wait for writing. */
		FD_ZERO(&write_fd_set);
		FD_SET(x10->fd, &write_fd_set);
		tv.tv_sec = (X10_WAIT_WRITE_USEC_DELAY)/1000000u;
		tv.tv_usec = (X10_WAIT_WRITE_USEC_DELAY)%1000000u;
		retval=select(x10->fd+1, NULL, &write_fd_set, NULL, &tv);
		
		/* Did select error? */
		if(retval == -1) {
			
			/* If it's an EINTR, go try again. */
			if(errno == EINTR) {
				debug(DEBUG_EXPECTED, "Signal recieved in write select, restarting.");
				continue;
			}
			
			/* It was something weird. */
			fatal("Error in write select: %s", strerror(errno));
		}
		
		/* Can we write data? */
		if(retval) {	
			
			/* We can write some data, return ok. */
			return(1);
		}
		
		/* No data writable. */
		else {
			
			/* We can't write any data, this is a fail. */
			return(0);
		}
	}
}


/* 
 * Read data from the x10 hardware.
 *
 * Basically works like read(), but with a select-provided readable check
 * and timeout.
 * 
 * Returns the number of bytes read.  This might be less than what was given
 * if we ran out of time.
 */
static ssize_t x10_read(X10 *x10, void *buf, size_t count) {
	int bytes_read;
	ssize_t retval;
	
	/* 
	 * The x10 sends at maximum 8 data bytes (we don't count the size or
	 * function byte here), so we better not ask for more.
	 */
	if(count > 8) {
		
		/* 
		 * This can actually happen because of the cm11a getting
		 * confused or sending a poll.  We need to handle it
		 * gracefully.
		 */
		debug(DEBUG_EXPECTED, "Byte count too large in x10 read, '%i'.", count);
		return(0);
	}
	
	/* Read the request into the buffer. */
	for(bytes_read=0; bytes_read < count;) {
		
		/* Wait for data to be available. */
		if(!x10_wait_read(x10)) {
			debug(DEBUG_UNEXPECTED, "Gave up waiting for x10 to be readable.");
			return(bytes_read);
		}
		
		/* Get as much of it as we can.  Loop for the rest. */
		retval=read(x10->fd, (char *) buf + bytes_read, count - bytes_read);
		if(retval == -1) {
			fatal("Failure reading x10 buffer: %s", strerror(errno));
		}
		bytes_read += retval;
		debug(DEBUG_ACTION, "Read %i bytes, %i remaining.", retval, count - bytes_read);
	}
	
	/* We're all done. */
	return(bytes_read);
}


/* 
 * Write data to the x10 hardware.
 *
 * Basically works like write(), but with a select-provided writeable check
 * and timeout.
 * 
 * Returns the number of bytes written.  This might be less than what was
 * given if we ran out of time.
 */
static ssize_t x10_write(X10 *x10, void *buf, size_t count) {
	int bytes_written;
	ssize_t retval;
	
	/* Write the buffer to the x10 hardware. */
	for(bytes_written=0; bytes_written < count;) {
		
		/* Wait for data to be writeable. */
		if(!x10_wait_write(x10)) {
			debug(DEBUG_UNEXPECTED, "Gave up waiting for x10 to be writeable.");
			return(bytes_written);
		}
		
		/* Get as much of it as we can.  Loop for the rest. */
		retval=write(x10->fd, (char *) buf + bytes_written, count - bytes_written);
		if(retval == -1) {
			fatal("Failure writing x10 buffer.");
		}
		bytes_written += retval;
		debug(DEBUG_ACTION, "Wrote %i bytes, %i remaining.", retval, count - bytes_written);
	}
	
	/* We're all done. */
	return(bytes_written);
}


/* 
 * Build the time structure to send to the x10 hardware.
 *
 * Note that the download header, 0x9b, is not included here.  That should
 * be handled by the caller if needed.
 */
static void x10_build_time(char *buffer, time_t time, int house_code, int flags) {
	struct tm *tm;
	
	/* Break the time given down into day, year, etc.. */
	tm=localtime(&time);
	
	/* Byte zero is the number of seconds. */
	buffer[0]=(char) tm->tm_sec;
	
	/* Byte one is the minutes from 0 to 119. */
	buffer[1]=(char) tm->tm_min;
	if(tm->tm_hour % 2) buffer[1] += (char) 60;
	
	/* Byte two is the hours/2. */
	buffer[2]=(char) tm->tm_hour/2;
	
	/* Byte three and the first bit in four is the year day. */
	buffer[3]=(char) tm->tm_yday & 0xff;
	buffer[4]=(char) (tm->tm_yday >> 8) & 0x1;
	
	/* The top 7 bits of byte 4 are the day mask (SMTWTFS). */
	buffer[4] |= (char) (1 << (tm->tm_wday + 1));
	
	/* The top 4 of byte 5 is the house code. */
	buffer[5]=(char) house_code << 4;
	
	/* One bit is reserved and the lower three are flags. */
	buffer[5] |= (char) flags;

	return;
}

/* 
 * Handles a data poll request from the x10. 
 *
 * *** 
 * Need to do better checking that the event is valid.  Make sure things
 * that need at least one device are getting them.  Sometimes after not
 * being listened to for a while, the cm11a will send us a buffer with a
 * function like 'ON' in it but no addresses.
 */
 
static void x10_poll(X10 *x10) {
	unsigned char x10_buffer[8];
	unsigned char command;
	unsigned char buffer_size;
	unsigned char function_byte;
	int i,j,pos;
	
	/* Acknowledge the x10's poll. */
	command=0xc3;
	if(x10_write(x10, &command, 1) != 1) {
		debug(DEBUG_UNEXPECTED, "Gave up waiting to write an acknowledgement.");
		return;
	}
	debug(DEBUG_STATUS, "Poll acknowledgement sent.");
	
	/* Get the size of the request. */
	if(x10_read(x10, &buffer_size, 1) != 1) {
		
		/* 
		 * Errors here are unexpected since if we didn't get the ack
		 * through, we should at least read another 'poll' byte
		 * here.
		 */
		debug(DEBUG_UNEXPECTED, "Gave up trying to read the buffer size.");
		return;
	}
	debug(DEBUG_STATUS, "Request size: %i.", buffer_size);
	
	/* Must have at least 2 bytes or it's just weird. */
	if(buffer_size < 2) {
		debug(DEBUG_UNEXPECTED, "Short request from x10.");
		return;
	}
	
	/* Read in the function byte. */
	if(x10_read(x10, &function_byte, 1) != 1) {
		debug(DEBUG_UNEXPECTED, "Could not read function byte.");
		return;
	}
	
	/* Read in the buffer from the x10. */
	if(x10_read(x10, &x10_buffer, buffer_size - 1) != buffer_size - 1) {
		debug(DEBUG_UNEXPECTED, "Gave up while reading the buffer.");
		return;
	}
	
	/* Print packet info to debug. */
	debug_hexdump(DEBUG_STATUS, x10_buffer, buffer_size - 1,"X10 packet size: %d, function mask: %02x\n Packet contents: ",
	buffer_size, function_byte);
	
	/* Decode the packet. */
	for(i=0; i < buffer_size - 1; i++) {
		
		/* Was this byte a function? */
		if(function_byte & 1) {
			
			/* 
			 * If the housecode doesn't match the addresses,
			 * flush the address buffer.
			 */
			if((x10_buffer[i] >> 4) != x10->address_buffer_housecode) {
				debug(DEBUG_EXPECTED, "Function housecode and address housecode mismatch.");
				x10->address_buffer_count = 0;
			}
	
			
			/* User handler installed? */
			if(x10->event_callback){
				/* Translate the data */
				//debug(DEBUG_ACTION,"Address count: %i\n", x10->address_buffer_count);
				/* Generate the address string */
				x10->address_string[0] = 0;		
				for(j = 0, pos = 0 ; j < x10->address_buffer_count; j++){
					if(j)
						x10->address_string[pos++] = ',';
				    //debug(DEBUG_ACTION,"Address code: %02X\n", x10->address_buffer[j]);
					pos += snprintf(x10->address_string + pos, 3, "%u", addresscode2int[x10->address_buffer[j] & 0x0F]);
				}
				//debug(DEBUG_ACTION,"Address string: %s\n", x10->address_string);
				//debug(DEBUG_ACTION,"House code: %02X\n", x10->address_buffer_housecode);	 
				/* Call the user event handler */
				(*x10->event_callback)(x10->address_string,
				housecode2letter[x10->address_buffer_housecode], x10_buffer[i] & 0x0F);
			}
			
			/* Flush the address buffer. */
			x10->address_buffer_count=0;
		}
		
		/* This was an address byte. */
		else {
			
			/* 
			 * If this is the first byte, set the address
			 * housecode.
			 */
			if(x10->address_buffer_count == 0) {
				x10->address_buffer_housecode=x10_buffer[i] >> 4;
			}
			
			/*
			 * If this address doesn't match the housecode of
			 * the addresses we were storing, we start over with
			 * this address.  This should match how the hardware
			 * handles things like A1,B2,function.
			 */
			else if(x10->address_buffer_housecode != x10_buffer[i] >> 4) {
				debug(DEBUG_UNEXPECTED, "Address buffer housecode mismatch.");
				
				x10->address_buffer_housecode=x10_buffer[i] >> 4;
				x10->address_buffer_count=0;
			}
			
			/* The address buffer better not be full. */
			else if(x10->address_buffer_count == 16) {
				debug(DEBUG_UNEXPECTED, "Address buffer overflow.");
				
				/* Just ignore this address. */
				continue;
			}
				
			/* Save it on the address buffer. */
			x10->address_buffer[x10->address_buffer_count++]=x10_buffer[i] & 0x0f;
		}
		
		/* Shift the function byte, we've handled this one. */
		function_byte=function_byte >> 1;
	}
}

/*
 ***********************************************************************************************************************************
 * Public Functions                                                                                                                *
 ***********************************************************************************************************************************
*/


/* 
 * Write a message to the x10 hardware.
 *
 * The data will be sent, a checksum from the x10 hardware will be expected,
 * a response to the checksum will be sent, and the x10 should signal us
 * ready.
 *
 * Sometimes the cm11a will kick into poll mode while we're trying to send
 * it a request then promptly ignore us until we do something about it.  To
 * handle this, if that looks like what is happening, we go deal with the
 * x10 then come back and try again.
 *
 * If it works, we return true, false otherwise.
 */
 
int x10_write_message(X10 *x10, void *buf, size_t count) {
	unsigned char checksum;
	unsigned char real_checksum;
	unsigned char temp;
	int i;
	int try_count;
	
	if(!x10 || x10->magic != X10_MAGIC){
		debug(DEBUG_UNEXPECTED, "Bogus X10 pointer passed to x10_write_message()");
		return 0;
	}
	
	if(!buf){
		debug(DEBUG_UNEXPECTED, "Null buffer pointer passed to x10_write_message()");
		return 0;
	}
	
	/* Try writing the message 5 times, then just fail. */
	for(try_count=1; try_count <= 5; try_count++) {
		
		/* Send the data. */
		if(x10_write(x10, buf, count) != count) {
			debug(DEBUG_UNEXPECTED, "Failed to send data on try %i.", try_count);
			continue;
		}
		
		/* Get the checksum byte from the x10 hardware. */
		if(x10_read(x10, &checksum, 1) != 1) {
			debug(DEBUG_UNEXPECTED, "Failed to get the checksum byte on try %i.", try_count);
			continue;
		}
		
		/* Calculate the checksum on the data.  This is a simple summation. */
		real_checksum=0;
		for(i=0; i < count; i++) {
			real_checksum=(real_checksum + ((char *) buf)[i]) & 0xff;
		}
		
		/* Make sure the checksums match. */
		if(checksum != real_checksum) {
			debug(DEBUG_EXPECTED, "Checksum mismatch (real: %02x, received: %02x) in write message on try %i.", real_checksum, checksum, try_count);
			
			/* Does this look like it was really a poll? */
			if(checksum == 0x5a) {
				debug(DEBUG_STATUS, "Probable poll start in checksum read, doing poll read.");
				
				/* Go service the x10, it probably needs some. */
				x10_poll(x10);
			}
			
			/* Retry sending. */
			continue;
		}
		
		/* Send a go-ahead to the x10 hardware. */
		temp = 0;
		if(x10_write(x10, &temp, 1) != 1) {
			debug(DEBUG_UNEXPECTED, "Failed to send go-ahead on try %i.", try_count);
			continue;
		}
			
		/* Get the ready byte from the x10 hardware. */
		if(x10_read(x10, &temp, 1) != 1) {
			debug(DEBUG_UNEXPECTED, "Failed to get the 'ready' byte on try %i.", try_count);
			continue;
		}
		
		/* It had better be 0x55, the 'ready' byte. */
		if(temp != 0x55) {
			debug(DEBUG_EXPECTED, "Expected ready byte, got %02x on try %i.", temp, try_count);
			
			/* Does this look like it was really a poll? */
			if(temp == 0x5a) {
				debug(DEBUG_STATUS, "Probable poll start in ready byte read, doing poll read.");
				
				/* Go service the x10, it probably needs some. */
				x10_poll(x10);
			}
			
			/* Retry sending. */
			continue;
		}
		
		/* We made it, return true. */
		return(1);
	}
		
	/* We gave up and failed. */
	return(0);
}

/* 
 * Open the x10 device. 
 *
 * Pass in the TTY name, and an optional callback function for read events.
 * If the callback isn't used, pass in NULL
 * 
 */
 
X10 *x10_open(const char *x10_tty_name, void (*event_callback)(const char *, const char, const unsigned)) {
	X10 *x10;
	struct termios termios;
	
	/* Allocate us a structure for the x10 info. */
	x10 = calloc(1, sizeof(X10));
	if(!x10) fatal("Out of memory.");
	
	/* 
	 * Open the x10 tty device.
	 */
	x10->fd=open(x10_tty_name, O_RDWR | O_NOCTTY | O_NDELAY);
	if(x10->fd == -1) {
		fatal("Could not open tty '%s'.",x10_tty_name);
	}
	
	
	/* Set the options on the port. */
	
	/* We don't want to block reads. */
	if(fcntl(x10->fd, F_SETFL, O_NONBLOCK) == -1) {
		fatal("Could not set x10 to non-blocking.");
	}
	
	/* Get the current tty settings. */
	if(tcgetattr(x10->fd, &termios) != 0) {
		fatal("Could not get tty attributes.");
	}
	
	/* Enable receiver. */
	termios.c_cflag |= CLOCAL | CREAD;
	
	/* Set to 8N1. */
	termios.c_cflag &= ~PARENB;
	termios.c_cflag &= ~CSTOPB;
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |=  CS8;
	
	/* Accept raw data. */
	termios.c_lflag &= ~(ICANON | ECHO | ISIG);
	termios.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONLRET | OFILL);
	termios.c_iflag &= ~(ICRNL | IXON | IXOFF | IMAXBEL);
	
	/* Return after 1 character available */
	termios.c_cc[VMIN]=1;
	
	/* Set the speed of the port. */
	if(cfsetospeed(&termios, B4800) != 0) {
		fatal("Could not set tty output speed.");
	}
	if(cfsetispeed(&termios, B4800) != 0) {
		fatal("Could not set tty input speed.");
	}
	
	/* Save our modified settings back to the tty. */
	if(tcsetattr(x10->fd, TCSANOW, &termios) != 0) {
		fatal("Could not set tty attributes.");
	}
	x10->event_callback = event_callback;
	x10->magic = X10_MAGIC;
	return(x10);
}




/* Handle reading and handling requests from the x10. */
void x10_read_event(X10 *x10) {
	unsigned char command;
	char buffer[16];
	
	if(!x10 || x10->magic != X10_MAGIC){
		debug(DEBUG_UNEXPECTED, "Bogus X10 pointer passed to x10_read_event()");
		return;
	}
	
	/* Read the byte sent by the x10 hardware. */
	if(x10_read(x10, &command, 1) != 1) {
		debug(DEBUG_UNEXPECTED, "Could not read command byte.");
		return;
	}
	
	/* Is this a data poll? */
	if(command == 0x5a) {
		debug(DEBUG_STATUS, "Received poll from x10.");
		x10_poll(x10);
	}
	
	/* Is this a power-fail time request poll? */
	else if(command == 0xa5) {
		debug(DEBUG_STATUS, "Received power-fail time request poll from x10.");
		
		/* Build a time response to send to the hardware. */
		buffer[0]=(char) 0x9b;
		x10_build_time(&buffer[1], time(NULL), x10->housecode, TIME_TIMER_PURGE);
		
		/* Send this response to the hardware. */
		if(x10_write_message(x10, &buffer, 7) != 0) {
			
			/* 
			 * This shouldn't fail, the cm11a blocks in this
			 * mode until it is answered.  The only way it
			 * should fail is if we are in this mode due to
			 * static and a poll came in to block us.
			 */
			debug(DEBUG_UNEXPECTED, "Timeout trying to send power-fail time request.");
			return;
		}
	}
	
	/* It was an unknown command (probably static or leftovers). */
	else {
		debug(DEBUG_UNEXPECTED, "Unknown command byte from x10: %02x.", command);
		return;
	}
	
	return;
}

/*
 * Translate letter housecode to binary house code
 */

int x10_letter_to_housecode(char houseletter, unsigned char *housecode)
{
	unsigned char l = toupper(houseletter);
	
	if((housecode) && (l >= 'A') && (l <= 'P')){
		l -= 'A';
		*housecode = houseCodes[l];
		return 0;
	}
	return 1;
		
}

/*
 * Translate integer device number to binary device code
 */

int x10_number_to_devicecode(int devicenum, unsigned char *devicecode)
{
	if((devicecode) && (devicenum >= 1) && (devicenum <= 16)){
		*devicecode = deviceCodes[devicenum - 1];
		return 0;
	}
	return 1;	
}


/*
 * Return file descriptor if open
 */
int x10_fd(X10 *x10)
{
	if((x10) && (x10->magic == X10_MAGIC))
		return x10->fd;
	else
		return -1;
}

/*
 * Close the X10 communications port
 */
 
int x10_close(X10 *x10)
{
	int res;
	if((x10) && (x10->magic == X10_MAGIC)){
		res = close(x10->fd);
		x10->magic = 0;
		free(x10);
		return res;		
	}
	else
		return 0;
}

