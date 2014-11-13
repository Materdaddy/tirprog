/*
 * DMX Tir Address application
 *
 * Author: Materdaddy (Mat Mrosko)
 *
 * Date: 2014/11/01
 *
 * Description:
 * 		Linux command-line application to change the DMX address of a
 * 		TIR commercial led flood light.
 *
 * Usage:
 * 		tirSetter [--serial <serial> --address <address> --device <device> --flashes <#>]
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <linux/serial.h>

int SerialOpen(char *deviceName, int *fd);
int SerialClose(int fd);
int SendData(int fd, char *channelData);
static bool verbose = false;

void usage(char *appname)
{
	fprintf(stderr, "Usage: %s [OPTION...]\n"
		   "\n"
		   "%s is a command line application to program the DMX address of a TIR flood.\n"
		   "\n"
		   "Options:\n"
		   "\t-s, --serial\t\tSerial number of the TIR you're programming.\n\n"
		   "\t-a, --address\t\tNew DMX address to set.\n\n"
		   "\t-d, --device\t\tDMX Device name (/dev/ttyUSB0 by default).\n\n"
		   "\t-f, --flashes\t\tNumber of flashes of DMX data, default is 5.\n\n"
		   "\t-v, --verbose\t\tPrint debug output\n\n"
		   "\n"
		   "If only address is given, we will not send the programming packet.  Instead we\n"
		   "set every 3rd channel (for red) except the address will be toggled green/off.\n"
		   ,
		appname,
		appname);
}

int main(int argc, char *argv[])
{
	int serialNumber = -1;
	int DMXAddress = -1;
	int flashes = 5;
	bool program = false;

	int c;
	char *deviceName = strdup("/dev/ttyUSB0");

	if (!deviceName)
	{
		fprintf(stderr, "ERROR: Failed to initialize device string!\n");
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		//int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] =
		{
			{"serial",				required_argument,	0, 's'},
			{"address",				required_argument,	0, 'a'},
			{"device",				required_argument,	0, 'd'},
			{"flashes",				required_argument,	0, 'f'},
			{"verbose",				no_argument,		0, 'v'},
			{"help",				no_argument,		0, 'h'},
			{0,						0,					0,  0 }
		};

		c = getopt_long(argc, argv, "s:a:d:f:vh",
		long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{
			case 's':
			{
				serialNumber = atoi(optarg);
				break;
			}
			case 'a':
			{
				DMXAddress = atoi(optarg);
				break;
			}
			case 'd':
			{
				free(deviceName);
				deviceName = NULL;
				deviceName = strdup(optarg);
				if ( !deviceName )
				{
					fprintf(stderr, "ERROR: Couldn't get device string!\n");
					exit(EXIT_FAILURE);
				}
				break;
			}
			case 'f':
			{
				flashes = atoi(optarg);
				break;
			}
			case 'v':
			{
				verbose = true;
				break;
			}
			case 'h':
			{
				usage(argv[0]);
				exit(EXIT_SUCCESS);
			}
			default:
			{
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
		}
	}

	if ( DMXAddress == -1 )
	{
		fprintf(stderr, "Error: no address given\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if ( serialNumber != -1 )
		program = true;

	int i;
	int fd = -1;
	int toggle = 1;

	uint8_t DMXBuffer[513];
	bzero(DMXBuffer, sizeof(DMXBuffer));

	DMXBuffer[0] = 0x37;
	DMXBuffer[1] = 0x07;
	DMXBuffer[2] = 'T';
	DMXBuffer[3] = 'I';
	DMXBuffer[4] = 'R';
	DMXBuffer[5] = 'L';
	DMXBuffer[6] = 'U';
	DMXBuffer[7] = 'M';
	DMXBuffer[8] = 'V';
	DMXBuffer[9] = '1';
	DMXBuffer[10] = '5';
	DMXBuffer[11] = 0x00;
	DMXBuffer[12] = 0x00;

	//14-16 Hex Serial Number
	DMXBuffer[13] = (serialNumber & 0x00FF0000)>>16;
	DMXBuffer[14] = (serialNumber & 0x0000FF00)>>8;
	DMXBuffer[15] = (serialNumber & 0x000000FF);

	//17-18 HEX DMX Start Channel
	DMXBuffer[16] = (DMXAddress & 0x0000FF00)>>8;
	DMXBuffer[17] = (DMXAddress & 0x000000FF);

	//19 Seems to vary depending on run
	DMXBuffer[18] = 0x18;

	//20-28 ?? Does not Change 00:ec:40:41:00:70:56:01:00
	DMXBuffer[20] = 0xEC;
	DMXBuffer[21] = 0x40;
	DMXBuffer[22] = 0x41;
	DMXBuffer[24] = 0x70;
	DMXBuffer[25] = 0x56;
	DMXBuffer[26] = 0x01;

		//29-36 "preset00"
	DMXBuffer[28] = 'p';
	DMXBuffer[29] = 'r';
	DMXBuffer[30] = 'e';
	DMXBuffer[31] = 's';
	DMXBuffer[32] = 'e';
	DMXBuffer[33] = 't';
	DMXBuffer[34] = '0';
	DMXBuffer[35] = '0';

	// BYTES 53/54 seeded assuming 0x18 for byte 19, base values of 0x46 and 0xCA based on the post here:
	// http://doityourselfchristmas.com/forums/showthread.php?21586-Initializer-Program-for-TIR-Systems-Destiny-CG-Lights-Commercial-LED-Spots&p=220032#post220032

	//  Checksum code from PIC spreadsheet:
	//
	//      e3=3080852      serial number
	//      e4=33           dmx address
	//      e72=3080000     base serial number
	//      e73=1           base dmx address
	//      e74=0x46        base checksum 1
	//      e75=0xCA        base checksum 2
	//
	//  dec2hex(mod((mod(mod((e3-e72)+(e4-e73),256)+hex2dec(e74),256))+(if((e3-e72)>191,if((e3-e72-192)>255,quotient((e3-e72-192),256)*1+2,2),0)+if((e4-e73)>=255,1,0)),256),2)
	//  dec2hex(mod((mod(mod((e3-e72)+(e4-e73),256)*2+hex2dec(e75),256)+(if((e3-e72)>191,if((e3-e72-192)>255,(quotient((e3-e72-192),256)*1+2),2),0)+if((e4-e73)>=255,1,0))*2),256),2)

	DMXBuffer[52] = (((((((serialNumber-3080000)+(DMXAddress-1))%256) +0x46)%256))+((((serialNumber-3080000)>191)?(((serialNumber-3080000-192)>255)?((serialNumber-3080000-192)/256)*1+2:2):0)+((DMXAddress-1)>=255?1:0))%256);
	DMXBuffer[53] = (((((((serialNumber-3080000)+(DMXAddress-1))%256) *2+0xCA)%256)+((((serialNumber-3080000)>191)?(((serialNumber-3080000-192)>255)?(((serialNumber-3080000-192)/256)*1+2):2):0)+(((DMXAddress-1)>=255)?1:0))*2)%256);

	SerialOpen(deviceName, &fd);
	if ( fd == -1 )
	{
		fprintf(stderr, "Failed to open serial device\n");
		return -1;
	}

	if (program)
	{
		if (verbose)
			printf("Sending TIR programming...\n");

		SendData(fd, DMXBuffer);

		// The first set of "flashes" seem to take the TIR out of
		// programming mode, so let's add one to get the expected
		// results when programming.
		flashes++;
	}

	bzero(DMXBuffer, sizeof(DMXBuffer));

	for ( i = 1; i < 512; i+=3 )
	{
		// Set all RED channels
		DMXBuffer[i] = 0xFF;
	}

	for (i = 0; i < flashes*2; ++i)
	{
		DMXBuffer[DMXAddress+0] = 0x00; //RED
		DMXBuffer[DMXAddress+1] = 0x00; //GREEN
		DMXBuffer[DMXAddress+2] = 0x00; //BLUE

		if ( toggle )
		{
			toggle = 0;
			DMXBuffer[DMXAddress+1] = 0xFF; //GREEN
		}
		else
			toggle = 1;

		SendData(fd, DMXBuffer);
		sleep(1);
	}

	SerialClose(fd);
	free(deviceName);
	deviceName = NULL;
	fd = -1;

	return 0;
}

int SerialOpen(char *deviceName, int *fd)
{
	struct termios tty;
	struct serial_struct ss;
	int CtrlFlag = TIOCM_RTS;

	if (verbose)
		printf("SerialOpen('%s')\n", deviceName);

	*fd = open(deviceName, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (*fd < 0)
		return -1;

	if (ioctl(*fd, TIOCEXCL) == -1)
	{
		fprintf(stderr, "Error setting port to exclusive mode\n");
		close(*fd);
		return -1;
	}

	if (tcgetattr(*fd, &tty) == -1)
	{
		fprintf(stderr, "Error getting port attributes\n");
		close(*fd);
		return -1;
	}

	if (cfsetspeed(&tty, B38400) == -1)
	{
		fprintf(stderr, "Error setting port speed\n");
		close(*fd);
		return -1;
	}

	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;
	tty.c_cflag &= ~PARENB;
	tty.c_cflag |= CSTOPB;

	cfmakeraw(&tty);

	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	if (tcsetattr(*fd, TCSANOW, &tty) == -1)
	{
		fprintf(stderr, "Error setting port attributes\n");
		close(*fd);
		return -1;
	}

	if (ioctl(*fd, TIOCGSERIAL, &ss) < 0)
	{
		fprintf(stderr, "Error getting serial settings: %s\n",
			strerror(errno));
		close(*fd);
		return -1;
	}

	ss.custom_divisor	= ss.baud_base / 250000;
	ss.flags			&= ~ASYNC_SPD_MASK;
	ss.flags			|= ASYNC_SPD_CUST;

	if (ioctl(*fd, TIOCSSERIAL, &ss) < 0)
	{
		fprintf(stderr, "Error setting custom baud rate\n");
		close(*fd);
		return -1;
	}

	if (*fd < 0)
	{
		fprintf(stderr, "Error %d opening %s: %s\n",
			errno, deviceName, strerror(errno));
		return -1;
	}

	if (ioctl(*fd, TIOCMBIC, &CtrlFlag ) < 0)
	{
		fprintf(stderr, "Error %d resetting RTS on %s: %s\n",
			errno, deviceName, strerror(errno));
		return -1;
	}

	if (verbose)
	{
		printf("\tdevice\t: %s\n", deviceName);
		printf("\tfd\t: %d\n", *fd);
	}

	return 0;
}

int SerialClose(int fd)
{
	if (verbose)
		printf("SerialClose(%d)\n", fd);

	if (fd < 0)
		return -1;

	tcflush(fd, TCOFLUSH);

	return close(fd);
}

int SendData(int fd, char *channelData)
{
	if (verbose)
		printf("SendData(%d, %p)\n", fd, channelData);

	// DMX512-A-2004 recommends 176us minimum
	if (ioctl(fd, TIOCSBRK) < 0)
	{
		fprintf(stderr, "Error %d setting break: %s\n",
			errno, strerror(errno));
	}
	usleep(200);
	if (ioctl(fd, TIOCCBRK) < 0)
	{
		fprintf(stderr, "Error %d setting break: %s\n",
			errno, strerror(errno));
	}

	// Then need to sleep a minimum of 8us
	usleep(20);

	write(fd, channelData, 513);
}
