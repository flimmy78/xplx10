/*
*    
*    Copyright (C) 2012  Stephen A. Rodgers
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*    
*
*
*/
/* Define these if not defined */

#ifndef VERSION
	#define VERSION "X.X.X"
#endif

#ifndef EMAIL
	#define EMAIL "hwstar@rodgers.sdcoxmail.com"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <xPL.h>
#include "types.h"
#include "notify.h"
#include "confread.h"
#include "x10.h"

#define MALLOC_ERROR	malloc_error(__FILE__,__LINE__)

#define SHORT_OPTIONS "c:d:f:hi:l:np:s:vy"

#define WS_SIZE 256

//#define DEF_PID_FILE		"/var/run/xplx10.pid"
#define DEF_PID_FILE		"./xplx10.pid"
//#define DEF_CONFIG_FILE		"/etc/xplx10.conf"
#define DEF_CONFIG_FILE		"./xplx10.conf"
#define DEF_INSTANCE_ID		"cm11a"

 
typedef struct cloverrides {
	unsigned pid_file : 1;
	unsigned instance_id : 1;
	unsigned log_path : 1;
	unsigned interface : 1;
	unsigned houseletter : 1;
	unsigned tty : 1;
} clOverride_t;


char *progName;
int debugLvl = 0; 

enum {CMD_SEL = 0, CMD_AUO, CMD_ALO, CMD_ALF, CMD_ON, CMD_OFF, CMD_DIM, CMD_BRI, CMD_EXT, CMD_HRQ, CMD_PD1, CMD_PD2, CMD_STS};

static const char *X10_commands[14] =
{
	"select",
	"all_units_off",
	"all_lights_on",
	"all_lights_off",
	"on",
	"off",
	"dim",
	"bright",
	"extended",
	"hail_req",
	"predim1",
	"predim2",
	"status",
	NULL
};


static Bool noBackground = FALSE;
static Bool dryRun = FALSE;

static clOverride_t clOverride = {0,0,0,0};

static xPL_ServicePtr xplx10Service = NULL;
static xPL_MessagePtr xplx10TriggerMessage = NULL;
static ConfigEntryPtr_t	configEntry = NULL;

static char configFile[WS_SIZE] = DEF_CONFIG_FILE;
static char interface[WS_SIZE] = "";
static char logPath[WS_SIZE] = "";
static char tty[WS_SIZE] = "";
static char instanceID[WS_SIZE] = DEF_INSTANCE_ID;
static char pidFile[WS_SIZE] = DEF_PID_FILE;
static X10 *myX10 = NULL;


/* Commandline options. */

static struct option longOptions[] = {
	{"config-file", 1, 0, 'c'},
	{"debug", 1, 0, 'd'},
	{"dry-run", 0, 0, 'y'},
	{"help", 0, 0, 'h'},
	{"interface", 1, 0, 'i'},	
	{"instance", 1, 0, 's'},
	{"tty", 1, 0, 'p'},	
	{"log", 1, 0, 'l'},
	{"no-background", 0, 0, 'n'},
	{"pid-file", 0, 0, 'f'},
	{"version", 0, 0, 'v'},
	{0, 0, 0, 0}
};

/* 
 * Allocate a memory block and zero it out
 */

static void *mallocz(size_t size)
{
	void *m = calloc(size, 1);
	return m;
}
 
/*
 * Malloc error handler
 */
 
static void malloc_error(String file, int line)
{
	fatal("Out of memory in file %s, at line %d");
}


/*
* Convert a string to an unsigned int with bounds checking
*/

static Bool str2uns(String s, unsigned *num, unsigned min, unsigned max)
{
		long val;
		if((!num) || (!s)){
			debug(DEBUG_UNEXPECTED, "NULL pointer passed to str2uns");
			return FALSE;
		}
		val = strtol(s, NULL, 0);
		if((val < min) || (val > max))
			return FALSE;
		*num = (unsigned) val;
		return TRUE;
}



/*
* Duplicate or split a string. 
*
* The string is copied, and the sep characters are replaced with nul's and a list pointers
* is built. 
* 
* If no sep characters are found, the string is just duped and returned.
*
* This function returns the number of arguments found.
*
* When the caller is finished with the list and the return value is non-zero he should free() the first entry.
* 
*
*/

static int dupOrSplitString(const String src, String *list, char sep, int limit)
{
		String p, q, srcCopy;
		int i;
		

		if((!src) || (!list) || (!limit))
			return 0;

		if(!(srcCopy = strdup(src)))
			MALLOC_ERROR;

		for(i = 0, q = srcCopy; (i < limit) && (p = strchr(q, sep)); i++, q = p + 1){
			*p = 0;
			list[i] = q;
		
		}

		list[i] = q;
		i++;

		return i;
}

/* 
 * Get the pid from a pidfile.  Returns the pid or -1 if it couldn't get the
 * pid (either not there, stale, or not accesible).
 */
static pid_t pid_read(char *filename) {
	FILE *file;
	pid_t pid;
	
	/* Get the pid from the file. */
	file=fopen(filename, "r");
	if(!file) {
		return(-1);
	}
	if(fscanf(file, "%d", &pid) != 1) {
		fclose(file);
		return(-1);
	}
	if(fclose(file) != 0) {
		return(-1);
	}
	
	/* Check that a process is running on this pid. */
	if(kill(pid, 0) != 0) {
		
		/* It might just be bad permissions, check to be sure. */
		if(errno == ESRCH) {
			return(-1);
		}
	}
	
	/* Return this pid. */
	return(pid);
}


/* 
 * Write the pid into a pid file.  Returns zero if it worked, non-zero
 * otherwise.
 */
static int pid_write(char *filename, pid_t pid) {
	FILE *file;
	
	/* Create the file. */
	file=fopen(filename, "w");
	if(!file) {
		return -1;
	}
	
	/* Write the pid into the file. */
	(void) fprintf(file, "%d\n", pid);
	if(ferror(file) != 0) {
		(void) fclose(file);
		return -1;
	}
	
	/* Close the file. */
	if(fclose(file) != 0) {
		return -1;
	}
	
	/* We finished ok. */
	return 0;
}

/*
* Change string to upper case
* Warning: String must be nul terminated.
*/

static String str2Upper(char *q)
{
	char *p;
	
	if(!q)
		return NULL;
			
	if(q){
		for (p = q; *p; ++p) *p = toupper(*p);
	}
	return q;
}

/*
* When the user hits ^C, logically shutdown
* (including telling the network the service is ending)
*/

static void shutdownHandler(int onSignal)
{
	xPL_setServiceEnabled(xplx10Service, FALSE);
	xPL_releaseService(xplx10Service);
	xPL_shutdown();
	/* Unlink the pid file if we can. */
	(void) unlink(pidFile);
	exit(0);
}


/*
 * Send X10 command
 */
 

static void sendX10Command(void *buf, size_t count)
{
	debug_hexdump(DEBUG_EXPECTED, buf, count, "X10 transmit packet: ");
	if(!dryRun){
		if(!x10_write_message(myX10, buf, count))
			debug(DEBUG_UNEXPECTED, "X10 transmission error");
	}
	else
		debug(DEBUG_EXPECTED, "X10 transmission disabled (dry-run)");		
}


/*
* Our Listener 
*/

/*
 * Process an xPL x10.basic command
 *
 */

static void processX10BasicCommand(xPL_MessagePtr theMessage)
{
	int cmd,i,j,cnt,pktx;
	unsigned char hc;
	unsigned char x10_pkt[6];
	String addrList[16];
	const String command =  xPL_getMessageNamedValue(theMessage, "command");
	const String deviceList = xPL_getMessageNamedValue(theMessage, "device");
	const String houseList = xPL_getMessageNamedValue(theMessage, "house");
	const String level =  xPL_getMessageNamedValue(theMessage, "level");
	const String data1 = xPL_getMessageNamedValue(theMessage, "data1");
	const String data2 = xPL_getMessageNamedValue(theMessage, "data2");
	
	if(!theMessage){
		debug(DEBUG_UNEXPECTED, "No message passed in");
		return;
	}
		
	
	if(!command){
		debug(DEBUG_UNEXPECTED, "No command passed in");
		return;
	}
	
	if(!houseList){
		debug(DEBUG_UNEXPECTED, "House list not present");
		return;
	}
	
	if(strlen(houseList) != 1 || x10_letter_to_housecode(houseList[0], &hc)){
		debug(DEBUG_UNEXPECTED, "Bad house code %s", houseList);
		return;
	}	
	
	else{
		debug(DEBUG_ACTION, "Received command: %s", command);
		for(cmd = 0; X10_commands[cmd]; cmd++){
			if(!strcmp(command, X10_commands[cmd]))
				break;
		}
	    debug(DEBUG_ACTION, "Command index : %d", cmd);
		pktx = 0;
		x10_pkt[pktx++] = HEADER_DEFAULT | HEADER_FUNCTION;
		x10_pkt[pktx] = (hc << 4);
			
		switch(cmd){
			case CMD_SEL: /* Select */
				if(!deviceList){
					debug(DEBUG_UNEXPECTED, "Device list not present");
					return;
				}
				/* Split the address list */
				cnt = dupOrSplitString(deviceList, addrList, ',', 16);
				debug(DEBUG_ACTION, "Number of devices: %d", cnt);
			
				/* Must have at least 1 address */
				if(!cnt){
					debug(DEBUG_UNEXPECTED, "No devices specified");
					free(addrList[0]);
					return;
				}
				/* Send an X10 transmission for each address */	
				for(i = 0; pktx < 6 && i < cnt; i++){
					/* Build the address packet */
					pktx = 0;
					x10_pkt[pktx++] = HEADER_DEFAULT;
					if(x10_number_to_devicecode(atoi(addrList[i]),x10_pkt + pktx)){
						debug(DEBUG_UNEXPECTED,"Bad device code: %s. Skipped.",addrList[i]);
						continue;
					}
					/* Or in the house code bits */
					x10_pkt[pktx++] |= (hc << 4);	
					sendX10Command(x10_pkt, pktx);	
				}
				free(addrList[0]); /* Done with address list */						
				break;
				
			case CMD_AUO: /* All units off */
				x10_pkt[pktx++] = COMMAND_ALL_UNITS_OFF;
				sendX10Command(x10_pkt, pktx);	
				break;
				
			case CMD_ALO: /* All lights on */
			case CMD_ALF: /* All lights off */
				x10_pkt[pktx++] |= (cmd == CMD_ALO) ? COMMAND_ALL_LIGHTS_ON : COMMAND_ALL_LIGHTS_OFF;			
				sendX10Command(x10_pkt, pktx);
				break;
				
			case CMD_ON: /* On */
			case CMD_OFF: /* Off */
				x10_pkt[pktx++] |= (cmd == CMD_ON) ? COMMAND_ON : COMMAND_OFF;			
				sendX10Command(x10_pkt, pktx);
				break;
				
			case CMD_DIM: /* Dim */
			case CMD_BRI: /* Bright */
				if(!level){
					debug(DEBUG_UNEXPECTED, "No level n/v");
					return;
				}
				i = atoi(level);
				if((i < 0) || (i > 100)){
					debug(DEBUG_UNEXPECTED, "Dim/Bright level out of bounds");
					return;
				}
				x10_pkt[0] |= (i << 3);
				x10_pkt[pktx++] |= (cmd == CMD_DIM) ? COMMAND_DIM : COMMAND_BRIGHT;			
				sendX10Command(x10_pkt, pktx);
				break;
				
			case CMD_EXT: /* Extended */
				if(!data1 || !data2){
					debug(DEBUG_UNEXPECTED, "data1 or data2 n/v missing");
					return;
				}
				i = atoi(data1);
				j = atoi(data2);
				if((i < 0) || (i > 255)){
					debug(DEBUG_UNEXPECTED, "data1 out of bounds");
					return;
				}
				if((j < 0) || (j > 255)){
					debug(DEBUG_UNEXPECTED, "data2 out of bounds");
					return;
				}
				x10_pkt[0] |= HEADER_EXTENDED;
				x10_pkt[pktx++] |= COMMAND_EXTENDED_CODE;
				x10_pkt[pktx++] = (unsigned char) i; /* Data 1 */
				x10_pkt[pktx++] = (unsigned char) j; /* Data 2 */
				sendX10Command(x10_pkt, pktx);
				break;
				
			case CMD_HRQ: /* Hail Request */
				x10_pkt[pktx++] = COMMAND_HAIL_REQUEST;
				sendX10Command(x10_pkt, pktx);	
				break;
				
			case CMD_PD1: /* Pre dim 1 */
				x10_pkt[pktx++] = COMMAND_PRESET_DIM1;
				sendX10Command(x10_pkt, pktx);	
				break;
				
			case CMD_PD2: /* Pre dim 2 */
				x10_pkt[pktx++] = COMMAND_PRESET_DIM2;
				sendX10Command(x10_pkt, pktx);	
				break;
				
			case CMD_STS: /* Status */
				x10_pkt[pktx++] = COMMAND_STATUS_REQUEST;
				sendX10Command(x10_pkt, pktx);	
				break;
			
			default:
				debug(DEBUG_UNEXPECTED,"Bad command");
				break;	
		}
		/* Always send  a trigger message */
		xPL_clearMessageNamedValues(xplx10TriggerMessage);
		xPL_setMessageNamedValue(xplx10TriggerMessage, "command", command);
		xPL_setMessageNamedValue(xplx10TriggerMessage, "house", houseList);
		if(deviceList)
			xPL_setMessageNamedValue(xplx10TriggerMessage, "device", deviceList); 
		if(!xPL_sendMessage(xplx10TriggerMessage))
			debug(DEBUG_UNEXPECTED, "Command complete trigger message transmission failed");		
	}
}


/*
 * Our xPL listener
 */


static void xPLListener(xPL_MessagePtr theMessage, xPL_ObjectPtr userValue)
{
	if(!xPL_isBroadcastMessage(theMessage)){ /* If not a broadcast message */
		if(xPL_MESSAGE_COMMAND == xPL_getMessageType(theMessage)){ /* If the message is a command */
			const String iID = xPL_getTargetInstanceID(theMessage);
			const String type = xPL_getSchemaType(theMessage);
			const String class = xPL_getSchemaClass(theMessage);
						
			if(!strcmp(instanceID, iID)){
				if(!strcmp(class,"x10")){
					if(!strcmp(type, "basic")){ /* Basic command schema */
						processX10BasicCommand(theMessage);			
					}
					else
						debug(DEBUG_UNEXPECTED, "Unsupported type: %s", type);
				}
				else
					debug(DEBUG_EXPECTED, "Unsupported class: %s", class);
			}
		}
	}
}



/*
* Our tick handler. 
* 
*/

static void tickHandler(int userVal, xPL_ObjectPtr obj)
{

}


/*
* Show help
*/

void showHelp(void)
{
	printf("'%s' is a daemon that XXXXXXXXXX\n", progName);
	printf("via XXXXXXXXXXX\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progName);
	printf("\n");
	printf("  -c, --config-file PATH  Set the path to the config file\n");
	printf("  -d, --debug LEVEL       Set the debug level, 0 is off, the\n");
	printf("                          compiled-in default is %d and the max\n", debugLvl);
	printf("                          level allowed is %d\n", DEBUG_MAX);
	printf("  -f, --pid-file PATH     Set new pid file path, default is: %s\n", pidFile);
	printf("  -h, --help              Shows this\n");
	printf("  -i, --interface NAME    Set the broadcast interface (e.g. eth0)\n");
	printf("  -l, --log  PATH         Path name to debug log file when daemonized\n");
	printf("  -n, --no-background     Do not fork into the background (useful for debugging)\n");
	printf("  -p, --tty               TTY port");
	printf("  -s, --instance ID       Set instance id. Default is %s", instanceID);
	printf("  -v, --version           Display program version\n");
	printf("  -y, --dry-run           Do not send X10 packets, just process commands\n");
	printf("\n");
 	printf("Report bugs to <%s>\n\n", EMAIL);
	return;

}


/*
* main
*/


int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	String p;

		

	/* Set the program name */
	progName=argv[0];

	/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, longOptions, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
				/* Was it a long option? */
			case 0:
				
				/* Hrmm, something we don't know about? */
				fatal("Unhandled long getopt option '%s'", longOptions[longindex].name);
			
				/* If it was an error, exit right here. */
			case '?':
				exit(1);
		
				/* Was it a config file switch? */
			case 'c':
				confreadStringCopy(configFile, optarg, WS_SIZE - 1);
				debug(DEBUG_ACTION,"New config file path is: %s", configFile);
				break;
				
				/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				debugLvl=atoi(optarg);
				if(debugLvl < 0 || debugLvl > DEBUG_MAX) {
					fatal("Invalid debug level");
				}

				break;

			/* Was it a pid file switch? */
			case 'f':
				confreadStringCopy(pidFile, optarg, WS_SIZE - 1);
				clOverride.pid_file = 1;
				debug(DEBUG_ACTION,"New pid file path is: %s", pidFile);
				break;
			
				/* Was it a help request? */
			case 'h':
				showHelp();
				exit(0);

				/* Specify interface to broadcast on */
			case 'i': 
				confreadStringCopy(interface, optarg, WS_SIZE -1);
				clOverride.interface = 1;
				break;

			case 'l':
				/* Override log path*/
				confreadStringCopy(logPath, optarg, WS_SIZE - 1);
				clOverride.log_path = 1;
				debug(DEBUG_ACTION,"New log path is: %s",
				logPath);

				break;

				/* Was it a no-backgrounding request? */
			case 'n':
				/* Mark that we shouldn't background. */
				noBackground = TRUE;
				break;
	
			
			case 'p': /* TTY ? */
				confreadStringCopy(tty, optarg, sizeof(tty));
				clOverride.tty = 1;
				break;	
					
			case 's':
				confreadStringCopy(instanceID, optarg, WS_SIZE);
				clOverride.instance_id = 1;
				debug(DEBUG_ACTION,"New instance ID is: %s", instanceID);
				break;


				/* Was it a version request? */
			case 'v':
				printf("Version: %s\n", VERSION);
				exit(0);
	
				/* Was it dry run request? */
			case 'y':
				/* Mark that we shouldn't send X10 commands */
				dryRun = TRUE;
				break;
			
				/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}
	}

	
	/* If there were any extra arguments, we should complain. */

	if(optind < argc) {
		fatal("Extra argument on commandline, '%s'", argv[optind]);
	}

	/* Attempt to read a config file */
	
	if(!(configEntry = confreadScan(configFile, NULL)))
		fatal("xplx10.conf file missing (required). Looked here: %s", configFile);

			
	/* Instance ID */
	if((!clOverride.instance_id) && (p = confreadValueBySectKey(configEntry, "general", "instance-id")))
		confreadStringCopy(instanceID, p, sizeof(instanceID));
		
	/* Interface */
	if((!clOverride.interface) && (p = confreadValueBySectKey(configEntry, "general", "interface")))
		confreadStringCopy(interface, p, sizeof(interface));
			
	/* pid file */
	if((!clOverride.pid_file) && (p = confreadValueBySectKey(configEntry, "general", "pid-file")))
		confreadStringCopy(pidFile, p, sizeof(pidFile));	
						
	/* log path */
	if((!clOverride.log_path) && (p = confreadValueBySectKey(configEntry, "general", "log-path")))
		confreadStringCopy(logPath, p, sizeof(logPath));
		
	/* tty */
	if((!clOverride.tty) && (p = confreadValueBySectKey(configEntry, "general", "tty")))
		confreadStringCopy(tty, p, sizeof(tty));			

	/* Turn on library debugging for level 5 */
	if(debugLvl >= 5)
		xPL_setDebugging(TRUE);

  	/* Make sure we are not already running (.pid file check). */
	if(pid_read(pidFile) != -1) {
		fatal("%s is already running", progName);
	}
	
	/* Fork into the background. */

	if(!noBackground) {
		int retval;
		debug(DEBUG_STATUS, "Forking into background");

    	/* 
		* If debugging is enabled, and we are daemonized, redirect the debug output to a log file if
    	* the path to the logfile is defined
		*/

		if((debugLvl) && (logPath[0]))                          
			notify_logpath(logPath);
			
	
		/* Fork and exit the parent */

		if((retval = fork())){
      			if(retval > 0)
				exit(0);  /* Exit parent */
			else
				fatal_with_reason(errno, "parent fork");
    		}



		/*
		* The child creates a new session leader
		* This divorces us from the controlling TTY
		*/

		if(setsid() == -1)
			fatal_with_reason(errno, "creating session leader with setsid");


		/*
		* Fork and exit the session leader, this prohibits
		* reattachment of a controlling TTY.
		*/

		if((retval = fork())){
			if(retval > 0)
        			exit(0); /* exit session leader */
			else
				fatal_with_reason(errno, "session leader fork");
		}

		/* 
		* Change to the root of all file systems to
		* prevent mount/unmount problems.
		*/

		if(chdir("/"))
			fatal_with_reason(errno, "chdir to /");

		/* set the desired umask bits */

		umask(022);
		
		/* Close STDIN, STDOUT, and STDERR */

		close(0);
		close(1);
		close(2);
		} 

	debug(DEBUG_STATUS,"Initializing xPL library");
	
	/* Set the xPL interface */
	xPL_setBroadcastInterface(interface);

	/* Start xPL up */
	if (!xPL_initialize(xPL_getParsedConnectionType())) {
		fatal("Unable to start xPL lib");
	}

	/* Initialize xplx10 service */

	/* Create a service and set our application version */
	xplx10Service = xPL_createService("hwstar", "xplx10", instanceID);
  	xPL_setServiceVersion(xplx10Service, VERSION);

  
	/*
	* Create trigger message object
	*/

	xplx10TriggerMessage = xPL_createBroadcastMessage(xplx10Service, xPL_MESSAGE_TRIGGER);
	xPL_setSchema(xplx10TriggerMessage, "x10", "basic");



  	/* Install signal traps for proper shutdown */
 	signal(SIGTERM, shutdownHandler);
 	signal(SIGINT, shutdownHandler);


	/* Add 1 second tick service */
	xPL_addTimeoutHandler(tickHandler, 1, NULL);

  	/* And a listener for all xPL messages */
  	xPL_addMessageListener(xPLListener, NULL);


 	/* Enable the service */
  	xPL_setServiceEnabled(xplx10Service, TRUE);

	if(pid_write(pidFile, getpid()) != 0) {
		debug(DEBUG_UNEXPECTED, "Could not write pid file '%s'.", pidFile);
	}

	debug(DEBUG_STATUS,"Initializing x10 communications on tty: %s", tty);
	
	if(!dryRun){
		myX10 = x10_open(tty);
		if(!myX10)
			fatal("Could not initialize X10 communications on tty: %s", tty);
	}

 	/** Main Loop **/

	for (;;) {
		/* Let XPL run forever */
		xPL_processMessages(-1);
  	}

	exit(1);
}

