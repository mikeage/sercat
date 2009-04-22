/*
SerCat: Minimal serial port terminal emulation.
Copyright (C) 2008-9 Mike Miller (software@mikeage.net)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, version 2 only
(NOT including later versions)


This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

const char     *sercat_versionstring = "@(#) SerCat 0.1 by Mike Miller";

#include <stdio.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <windows.h>
#include <conio.h>

typedef struct _SERIAL_CFG {
	int             port;
	DWORD           baudRate;
	BYTE            parity;
	BYTE            byteSize;
	BYTE            stopBits;
	bool            timestamp;
	unsigned int    dtrControl;
	unsigned int    rtsControl;
} SERIAL_CFG;

static void     usage(_TCHAR * name);
static void     print_header(SERIAL_CFG cfg);
static void     print_message(int level, const char *fmt, ...);
static DWORD CALLBACK ListenThread(HANDLE hCom);
static int      Verbose = 1;

int main(int argc, char *argv[])
{
	HANDLE          hCom = INVALID_HANDLE_VALUE;
	HANDLE          hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE          hTmp;
	char            com_name[11] /* \\.\.COMXX plus a null */ ;
	DCB             dcb;
	SERIAL_CFG      cfg;
	char            cur_arg;
	DWORD           mask;
	DWORD           dwTmp;
	OVERLAPPED      overlap;
	int             option_index = 0;
	DWORD           lastError;

	/* Default parameters */
	cfg.port = 0;
	cfg.baudRate = CBR_115200;
	cfg.byteSize = 8;
	cfg.parity = NOPARITY;
	cfg.stopBits = ONESTOPBIT;
	cfg.dtrControl = DTR_CONTROL_ENABLE;
	cfg.rtsControl = RTS_CONTROL_HANDSHAKE;

#ifdef BREAK_ON_START
	__asm int 3     h;
#endif											 /* BREAK_ON_START */

	/* Parse Arguments */
	for (;;) {
		static struct option long_options[] = {
			{"baud", required_argument, 0, 'b'},
			{"dtr-on", no_argument, 0, 'd'},
			{"dtr-off", no_argument, 0, 'D'},
			{"dtr-handshake", no_argument, 0, 'h'},
			{"rts-on", no_argument, 0, 'r'},
			{"rts-off", no_argument, 0, 'R'},
			{"rts-handshake", no_argument, 0, 'H'},
			{"rts-toggle", no_argument, 0, 't'},
			{"parity", required_argument, 0, 'p'},
			{"verbose", no_argument, 0, 'v'},
			{"quiet", required_argument, 0, 'q'},
			{0, 0, 0, 0}
		};
		cur_arg = getopt_long(argc, argv, "b:p:vq", long_options, &option_index);

		if (cur_arg == -1)
			break;

		switch (cur_arg) {
		case 'v':
			Verbose++;
			break;
		case 'q':
			Verbose--;
			break;
		case 'b':
			cfg.baudRate = strtol(optarg, NULL, 0);
			break;
		case 'd':
			cfg.dtrControl = DTR_CONTROL_ENABLE;
			break;
		case 'D':
			cfg.dtrControl = DTR_CONTROL_DISABLE;
			break;
		case 'h':
			cfg.dtrControl = DTR_CONTROL_HANDSHAKE;
			break;
		case 'r':
			cfg.rtsControl = RTS_CONTROL_ENABLE;
			break;
		case 'R':
			cfg.rtsControl = RTS_CONTROL_DISABLE;
			break;
		case 'H':
			cfg.rtsControl = RTS_CONTROL_HANDSHAKE;
			break;
		case 't':
			cfg.rtsControl = RTS_CONTROL_TOGGLE;
			break;
		case 'p':
			char            p;
			char            s;
			char            bs;
			if (3 == sscanf(optarg, "%c%c%c", &bs, &p, &s)) {
				cfg.byteSize = (BYTE) (bs - '0');
				switch (p) {
				case 'N':
				case 'n':
					cfg.parity = (BYTE) NOPARITY;
					break;
				case 'E':
				case 'e':
					cfg.parity = (BYTE) EVENPARITY;
					break;
				case 'O':
				case 'o':
					cfg.parity = (BYTE) ODDPARITY;
					break;
				case 'M':
				case 'm':
					cfg.parity = (BYTE) MARKPARITY;
					break;
				case 'S':
				case 's':
					cfg.parity = (BYTE) SPACEPARITY;
					break;
				default:
					usage(argv[0]);
					exit(1);
				}
				switch (s) {
				case '1':
					cfg.stopBits = (BYTE) ONESTOPBIT;
					break;
				case '2':
					cfg.stopBits = (BYTE) TWOSTOPBITS;
					break;
				default:
					usage(argv[0]);
					exit(1);

				}
			} else {
				usage(argv[0]);
				exit(1);
			}
			break;
		default:
			usage(argv[0]);
			exit(1);
			break;
		}
	}

	if (argv[optind]) {
		cfg.port = strtol(argv[optind], NULL, 0);
	}

	/* Check that we have everything we need */
	if (cfg.port == 0) {
		usage(argv[0]);
		exit(1);
	}

	print_header(cfg);

	sprintf(com_name, "\\\\.\\COM%d", cfg.port);	 /* Actual string should be \\.\COM */

	print_message(1, "Opening port %d (as %s)\n", cfg.port, com_name);

	hCom = CreateFile(com_name, GENERIC_READ | GENERIC_WRITE, NULL, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (hCom == INVALID_HANDLE_VALUE) {
		print_message(0, "Error opening %s!\n", com_name);
		exit(1);
	}
	print_message(2, "handle=0x%02X\n", (INT_PTR) hCom);

	/* Configure the serial port */
	memset(&dcb, 0, sizeof (dcb));
	dcb.DCBlength = sizeof (dcb);

	GetCommState(hCom, &dcb);
	dcb.BaudRate = cfg.baudRate;
	dcb.ByteSize = cfg.byteSize;
	dcb.Parity = cfg.parity;
	dcb.StopBits = cfg.stopBits;

	dcb.fBinary = FALSE;
	dcb.fParity = TRUE;
	dcb.fDtrControl = cfg.dtrControl;
	dcb.fRtsControl = cfg.rtsControl;

	if (!SetCommState(hCom, &dcb)) {
		print_message(0, "Unable to set Comm Port config. Error was 0x%08X\n", (unsigned long) GetLastError());
		exit(1);
	}

	/* Configure timeouts */
	COMMTIMEOUTS    timeouts;
	/* No timeouts */
	timeouts.ReadIntervalTimeout = 2;
	timeouts.ReadTotalTimeoutMultiplier = 1;
	timeouts.ReadTotalTimeoutConstant = 1;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 0;

	if (!SetCommTimeouts(hCom, &timeouts)) {
		print_message(0, "Unable to set Comm Port timeouts. Error was 0x%08lX\n", (unsigned long) GetLastError());
		exit(1);
	}

	hTmp = CreateThread(NULL, 0, ListenThread, hCom, 0, &dwTmp);
	if (hTmp == INVALID_HANDLE_VALUE) {
		return 1;
	}
	CloseHandle(hTmp);

	memset(&overlap, 0, sizeof (overlap));

	overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (overlap.hEvent == INVALID_HANDLE_VALUE) {
		print_message(0, "Error: couldn't create event!\n");
		exit(1);
	}

	if (!SetCommMask(hCom, EV_RXCHAR)) {
		print_message(0, "Error: couldn't set comm mask!\n");
		exit(1);
	}

	for (;;) {
		if (!WaitCommEvent(hCom, &mask, &overlap)) {
			lastError = GetLastError();
			if (lastError == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(hCom, &overlap, &dwTmp, TRUE)) {
					print_message(0, "Couldn't get overlapped result!\n");
					exit(1);
				}
			} else {
				print_message(0, "WaitCommEvent failed!\n");
				exit(1);
			}
		}

		if (mask & EV_RXCHAR) {
			char            buf[16];
			DWORD           read;
			do {
				read = 0;
				if (!ReadFile(hCom, buf, sizeof (buf), &read, &overlap)) {
					if (GetLastError() == ERROR_IO_PENDING) {
						if (!GetOverlappedResult(hCom, &overlap, &read, TRUE)) {
							print_message(0, "Couldn't get overlapped result!\n");
							exit(1);
						}
					} else {
						print_message(0, "ReadFile failed!\n");
						exit(1);
					}
				}
				if (read) {
					WriteFile(hStdout, buf, read, &read, NULL);
				}
			} while (read);
		}
		mask = 0;
	}
	/* Never reach the end... */
}

static void usage(char *name)
{
	print_message(0, "Usage: %s <com_port> <arguments>\n", name);
	print_message(0, "\t-b\t--baud          Baud rate (default 115200)\n");
	print_message(0, "\t-p\t--parity        Parity (default 8N1)\n");
	print_message(0, "\t-v\t--verbose       Increase verbosity (always printed to stderr)\n");
	print_message(0, "\t-q\t--quiet         Decrease verbosity\n");
	print_message(0, "\t  \t--dtr-on        DTR Enabled (default)\n");
	print_message(0, "\t  \t--dtr-off       DTR Disabled\n");
	print_message(0, "\t  \t--dtr-handshake DTR Handshake\n");
	print_message(0, "\t  \t--rts-on        RTS Enabled\n");
	print_message(0, "\t  \t--rts-off       RTS Disabled\n");
	print_message(0, "\t  \t--rts-handshake RTS Handshake (default)\n");
	print_message(0, "\t  \t--rts-toggle    RTS Toggle\n");
	return;
}

static void print_header(SERIAL_CFG cfg)
{
	char            parity = '?';
	char            stopbits = '?';
	char            dtrControl[10];
	char            rtsControl[10];

	switch (cfg.parity) {
	case NOPARITY:
		parity = 'N';
		break;
	case EVENPARITY:
		parity = 'E';
		break;
	case ODDPARITY:
		parity = 'O';
		break;
	case MARKPARITY:
		parity = 'M';
		break;
	case SPACEPARITY:
		parity = 'S';
		break;
	}
	switch (cfg.stopBits) {
	case ONESTOPBIT:
		stopbits = '1';
		break;
	case TWOSTOPBITS:
		stopbits = '2';
		break;
	}
	switch (cfg.dtrControl) {
	case DTR_CONTROL_ENABLE:
		sprintf(dtrControl, "Enabled");
		break;
	case DTR_CONTROL_DISABLE:
		sprintf(dtrControl, "Disabled");
		break;
	case DTR_CONTROL_HANDSHAKE:
		sprintf(dtrControl, "Handshake");
		break;

	}
	switch (cfg.rtsControl) {
	case RTS_CONTROL_ENABLE:
		sprintf(rtsControl, "Enabled");
		break;
	case RTS_CONTROL_DISABLE:
		sprintf(rtsControl, "Disabled");
		break;
	case RTS_CONTROL_HANDSHAKE:
		sprintf(rtsControl, "Handshake");
		break;
	case RTS_CONTROL_TOGGLE:
		sprintf(rtsControl, "Toggle");
		break;

	}
	print_message(1, "%s\n", sercat_versionstring + 5);

	print_message(2, "Bitrate: %-6d  Parity: %d%c%c\n", (int) cfg.baudRate, cfg.byteSize, parity, stopbits);
	print_message(2, "DTR: %s\n", dtrControl);
	print_message(2, "RTS: %s\n", rtsControl);

	return;
}

static void print_message(int level, const char *fmt, ...)
{
	va_list         argp;
	va_start(argp, fmt);
	if (Verbose >= level) {
		vfprintf(stderr, fmt, argp);
	}
	va_end(argp);
}

static DWORD CALLBACK ListenThread(HANDLE hCom)
{
	OVERLAPPED      overlap;
	HANDLE          hStdin = GetStdHandle(STD_INPUT_HANDLE);

	memset(&overlap, 0, sizeof (overlap));
	overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (overlap.hEvent == INVALID_HANDLE_VALUE) {
		print_message(0, "Error: couldn't create event!\n");
		exit(1);
	}

	SetConsoleMode(hStdin, 0);
	for (;;) {
		char            buf[16];
		DWORD           read = 0;

		WaitForSingleObject(hStdin, INFINITE);

		if (!ReadConsole(hStdin, buf, sizeof (buf), &read, NULL)) {
			print_message(0, "Error: couldn't read from the console!\n");
			exit(1);
		}
		if (read) {
			if (!WriteFile(hCom, buf, read, &read, &overlap)) {
				if (GetLastError() == ERROR_IO_PENDING) {
					if (!GetOverlappedResult(hCom, &overlap, &read, TRUE)) {
						print_message(0, "Couldn't get overlapped result!\n");
						exit(1);
					}
				}
			} else {
				print_message(0, "ReadFile failed!\n");
				exit(1);
			}
		}
	}
}
