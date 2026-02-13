// VC8_Remote.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

/* vc8_remote.c

Copyright (c) 2022, Ian Schofield

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the author shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the author.

*/

/*
	This is a very simple SDL app that may be used as a remote VC8 display for Spacewars.
	It is intended to be used with the current (2022) build for the PiDP8I.
	This can be found at: https://tangentsoft.com/pidp8i/wiki?name=Home
	This app requires the SDL2.
	The network port is set to 2222 as per script 4 in the PiDP8I installation.
	The key controls are 1 2 3 4 for ship 1 and 9 0 - = for ship 2.
	Only the SDL window may be used to send keyboard commands.
	To exit the app, type 'x' into the calling window.
	The screen decay constant is set in fade(...). Please change if required. (usual range 1..5).
	*
	*
	* Build with: (Linux, MacOSX) gcc -o vc8_remote vc8_remote.cpp -lSDL2
	* Call with: ./vc8_remote <PiDP8I host> <-L>
	* the -L option will double the window size.
*/

#ifdef _WIN32
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // exit()
#include <SDL.h>
#else
#if defined (__linux__) || defined (VMS) || defined (__APPLE__)
#include <errno.h> // errno will not work on windows
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <termios.h>
#include <unistd.h>
int _kbhit(void);
#endif
#endif

#ifdef _WIN32
#include <winsock.h>
#pragma comment(lib,"wsock32.lib") //Winsock Library
#define close closesocket
#define errexit 1
#define perror(x) fprintf(stderr, "%s: failed\n", x)
#include <conio.h>
FILE _iob[] = { *stdin, *stdout, *stderr };

extern "C" FILE * __cdecl __iob_func(void)
{
	return _iob;
}
#endif



#if defined (main)                                  /* Required for SDL */
#undef main
#endif

#define WINDOW_WIDTH 512
#define MASK (WINDOW_WIDTH * winsize - 1)
void changemode(int);
short keyPressed(char);
short keyReleased(char);

short old_sr = 0;
short sr = 0;
int run_thr = 1;
SDL_Window* window = NULL;
SDL_Surface* windowSurface = NULL;
SDL_Renderer* rend;
SDL_Texture* tex;
int sockfd;
int winsize = 1;        // Default small window
OVERLAPPED osReader = { 0 }, wtReader = { 0 };


void fade(SDL_Surface* windowSurface)
{
	int i;

	unsigned char* pixels = (unsigned char*)windowSurface->pixels;
	int surlen = (WINDOW_WIDTH * windowSurface->pitch) / ((winsize == 1) ? 4 : 2);
	pixels += 1;
	for (i = 0; i < surlen; i++, pixels += 4)
		if (*pixels)
			*pixels -= 8;
	SDL_Delay(2);
}

void setpixel(SDL_Surface* surface, int ix, int iy, int color)
{
	Uint32* p;

	if (!surface)
		return;
	unsigned char* pixels = (unsigned char*)surface->pixels;

	ix &= MASK;
	iy &= MASK;
	p = (Uint32*)(pixels + (iy * surface->pitch) + (ix * sizeof(Uint32)));
	*p = color;

}

void sendSR()
{
	char buf[2];

	buf[0] = 0; //(sr & 0xF);
	buf[1] = ((sr & 0xF00) >> 4) | (sr & 0xF);
	send(sockfd, buf, 2, 0);
	// printf("SR:%o\r\n",sr);

}

void PrintCommState(DCB dcb)
{
	//  Print some of the DCB structure values
	printf("\nBaudRate = %d, ByteSize = %d, Parity = %d, StopBits = %d\n",
		dcb.BaudRate,
		dcb.ByteSize,
		dcb.Parity,
		dcb.StopBits);
}

void ReadSerial(HANDLE hComm, int nBytes, char* buffer) {
	DWORD bread,eventMask=0;

	while (nBytes) {
		while (true) {
			WaitCommEvent(hComm, &eventMask, NULL);
			if (EV_RXCHAR & eventMask)
				break;
		}
		ReadFile(hComm, buffer++, 1, &bread, NULL);
		nBytes--;
	}
}

int thr_serial(void* dummy)
{
	HANDLE hComm;
	DWORD bread;
	DCB dcb;
	BOOL fSuccess,rd_wait=FALSE;
	const TCHAR* pcCommPort = TEXT("\\\\.\\COM19"); //  Most systems have a COM1 port
	char buffer[256];
	int k, i = 0;
	char coord[4];
	int x, y;
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 0;


	hComm = CreateFile(pcCommPort,GENERIC_READ | GENERIC_WRITE,0,0,OPEN_EXISTING, NULL,NULL);
	if (hComm == INVALID_HANDLE_VALUE) {
		printf("The Comport is closed or taken by other hardware/software!\n\r");
	}
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);

	//  Build on the current configuration by first retrieving all current
	//  settings.
	fSuccess = GetCommState(hComm, &dcb);

	if (!fSuccess)
	{
		//  Handle the error.
		printf("GetCommState failed with error %d.\n", GetLastError());
		return (2);
	}

	//  Fill in some DCB values and set the com state: 
	//  57,600 bps, 8 data bits, no parity, and 1 stop bit.
	dcb.BaudRate = CBR_256000;     //  baud rate
	dcb.ByteSize = 8;             //  data size, xmit and rcv
	dcb.Parity = NOPARITY;      //  parity bit
	dcb.StopBits = ONESTOPBIT;    //  stop bit
	dcb.fDtrControl = 0;
	dcb.fRtsControl = 0;

	fSuccess = SetCommState(hComm, &dcb);

	if (!fSuccess)
	{
		//  Handle the error.
		printf("SetCommState failed with error %d.\n", GetLastError());
		return (3);
	}
	SetCommTimeouts(hComm, &timeouts);
	osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	wtReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	SetCommMask(hComm, EV_RXCHAR);
	EscapeCommFunction(hComm, SETDTR);

	do {
		//ReadSerial(hComm, 5, buffer);
		if (true)
		{
			ReadSerial(hComm, 1, buffer);
			if (buffer[0] == 0)
				i++;
			else
				i = 0;

			if (i == 2)
			{
				i = 0;
				ReadSerial(hComm, 4, buffer);
				for (k = 0; k < 4; k++)
					coord[k] = buffer[k] & 0x3f;
				x = (coord[0] | (coord[1] << 6) + 512) % 1024;
				y = 1024 - ((coord[2] | (coord[3] << 6) + 512) % 1024);
				x /= (winsize == 1) ? 2 : 1;
				y /= (winsize == 1) ? 2 : 1;
				k = WINDOW_WIDTH * winsize;
				setpixel(windowSurface, x, y, 0xf800);
				setpixel(windowSurface, x + 1, y, 0xf800);
				setpixel(windowSurface, x, y + 1, 0xf800);
				setpixel(windowSurface, x + 1, y + 1, 0xf800);

				// -------------------------------------------------------
			}
		}
	} while (run_thr);
	return 0;
}

int thr_recv(void* dummy)
{
	char buffer[256];
	int k, i = 0, n;
	char coord[4];
	int x, y, errcode,err= EAGAIN;

	do
	{
		{
			n = recv(sockfd, buffer, 5, MSG_PEEK);
			if (n < 0 && errno)
			{
				errcode = errno;
				changemode(0);
				perror("ERROR receiving from socket");
				exit(1);
			}

			if (n >= 5)
			{
				n = recv(sockfd, buffer, 1, 0);
				if (n < 0)
				{
					changemode(0);
					perror("ERROR reading from socket");
					exit(1);
				}
				if (buffer[0] == 0)
					i++;
				else
					i = 0;

				if (i == 2)
				{
					i = 0;
					n = recv(sockfd, buffer, 4, 0);
					if (n < 0)
					{
						changemode(0);
						perror("ERROR reading from socket");
						exit(1);
					}
					for (k = 0; k < 4; k++)
						coord[k] = buffer[k] & 0x3f;
					x = (coord[0] | (coord[1] << 6) + 512) % 1024;
					y = 1024 - ((coord[2] | (coord[3] << 6) + 512) % 1024);
					x /= (winsize == 1) ? 2 : 1;
					y /= (winsize == 1) ? 2 : 1;
					k = WINDOW_WIDTH * winsize;
					setpixel(windowSurface, x, y, 0xf800);
					setpixel(windowSurface, x + 1, y, 0xf800);
					setpixel(windowSurface, x, y + 1, 0xf800);
					setpixel(windowSurface, x + 1, y + 1, 0xf800);

					// -------------------------------------------------------
				}
			}
		}
	} while (run_thr);   // Exit flag
	close(sockfd);
	return 0;
}

int main_loop()
{
	SDL_Event event;

	SDL_Init(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("VC8 Display", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH * winsize, WINDOW_WIDTH * winsize, SDL_WINDOW_SHOWN);
	windowSurface = SDL_CreateRGBSurface(0, WINDOW_WIDTH * winsize, WINDOW_WIDTH * winsize, 32, 0, 0, 0, 0);

	rend = SDL_GetRenderer(window);
	if (rend)
		SDL_DestroyRenderer(rend);
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
	rend = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!rend)
		printf("%s\r\n", SDL_GetError());
	tex = SDL_CreateTextureFromSurface(rend, windowSurface);
	if (!tex)
		printf("%s\r\n", SDL_GetError());

	if (!tex || !rend || !window || !windowSurface)
		exit(1);

	while (1)
	{
		fade(windowSurface);
		SDL_UpdateTexture(tex, NULL, windowSurface->pixels, windowSurface->pitch);
		SDL_RenderCopy(rend, tex, NULL, NULL);
		SDL_RenderPresent(rend);
		if (SDL_PollEvent(&event))
			switch (event.type)
			{
			case SDL_KEYDOWN:
				keyPressed(event.key.keysym.sym);
				if (!event.key.repeat)
					sendSR();
				break;
			case SDL_KEYUP:
				keyReleased(event.key.keysym.sym);
				sendSR();
				break;
			case SDL_QUIT:
				return -1;
			}
	}
	return 0;
}


int main(int argc, char* argv[])
{

	int portno = 2222;
	struct sockaddr_in serv_addr;
	struct hostent* server;
	SDL_Thread* sthrd;

	if (argc != 2 && argc != 3)
	{
		printf("Usage: vc8_remote <host> <-L>\r\n");
		exit(1);
	}

	changemode(1);	// used for kbhit()
	SDL_Init(SDL_INIT_VIDEO);

#ifdef USE_SERIAL

	sthrd = SDL_CreateThread(thr_serial, "ReceiveThread", NULL);

#else

#ifdef _WIN32
	static WSADATA winsockdata;
	WSAStartup(MAKEWORD(1, 1), &winsockdata);
#endif

	// create socket
	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
	{
		perror("ERROR opening socket");
		exit(1);
	}
	server = gethostbyname(argv[1]);
	if (server == NULL)
	{
		perror("ERROR no such host");
		exit(1);
	}
#if defined (__linux__)
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#endif
#ifdef _WIN32
	DWORD timeout = 1000;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#endif
	memset((void*)&serv_addr, '\0', sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy((void*)&serv_addr.sin_addr.s_addr, (void*)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(portno);
	if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR connecting");
		exit(1);
	}

	if (argc == 3)  // Any arg will do!
		winsize = 2;

	sthrd = SDL_CreateThread(thr_recv, "ReceiveThread", NULL);

#endif
	main_loop();

	run_thr = 0;	// Cause thread to exit;
	changemode(0);	// used for kbhit()
	SDL_WaitThread(sthrd, NULL);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return EXIT_SUCCESS;
}

short keyPressed(char key)
{
	//  old_sr = sr;
	switch (key)
	{
	case '1':
		sr |= 0x800;
		break;
	case '2':
		sr |= 0x400;
		break;
	case '3':
		sr |= 0x200;
		break;
	case '4':
		sr |= 0x100;
		break;
	case '5':
		sr |= 0x80;
		break;
	case '6':
		sr |= 0x40;
		break;
	case '7':
		sr |= 0x20;
		break;
	case '8':
		sr |= 0x10;
		break;
	case '9':
		sr |= 0x8;
		break;
	case '0':
		sr |= 0x4;
		break;
	case '-':
		sr |= 0x2;
		break;
	case '=':
		sr |= 0x1;
		break;
	case 'w':
		sr |= 0x600;
		break;
	case 'p':
		sr |= 0x6;
		break;
	default:
		break;
	}
	/*  if (old_sr != sr)
	  {
		s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
	  }
	*/
	return sr;
}

short keyReleased(char key)
{
	//  old_sr = sr;
	switch (key)
	{
	case '1':
		sr &= ~0x800;
		break;
	case '2':
		sr &= ~0x400;
		break;
	case '3':
		sr &= ~0x200;
		break;
	case '4':
		sr &= ~0x100;
		break;
	case '5':
		sr &= ~0x80;
		break;
	case '6':
		sr &= ~0x40;
		break;
	case '7':
		sr &= ~0x20;
		break;
	case '8':
		sr &= ~0x10;
		break;
	case '9':
		sr &= ~0x8;
		break;
	case '0':
		sr &= ~0x4;
		break;
	case '-':
		sr &= ~0x2;
		break;
	case '=':
		sr &= ~0x1;
		break;
	case 'w':
		sr &= ~0x600;
		break;
	case 'p':
		sr &= ~0x6;
		break;
	default:
		break;
	}
	/*  if (old_sr != sr)
	  {
		s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
	  }
	*/
	return sr;
}

#ifdef _WIN32
void changemode(int dir)
{}

#else
void changemode(int dir)
{
	static struct termios oldt, newt;

	if (dir == 1)
	{
		tcgetattr(STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	else
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int _kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);

	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);

}
#endif
