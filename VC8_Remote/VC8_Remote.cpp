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
    * Build with: gcc -o vc8_remote vc8_remote.c -lSDL2
    * Call with: ./remote_vc8 <PiDP8I host>
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
#include <SDL2 / SDL.h>
#include <SDL2/SDL_thread.h>
#endif
#endif

#ifdef _WIN32
#include <winsock.h>
#pragma comment(lib,"wsock32.lib") //Winsock Library
#define close closesocket
#define errexit 1
#define perror(x) fprintf(stderr, "%s: failed\n", x)
#include <conio.h>
#endif

FILE _iob[] = { *stdin, *stdout, *stderr };

extern "C" FILE * __cdecl __iob_func(void)
{
    return _iob;
}

#if defined (main)                                  /* Required for SDL */
#undef main
#endif

#define WINDOW_WIDTH 512

void changemode(int);
short keyPressed(char);
short keyReleased(char);

short old_sr = 0;
short sr = 0;
SDL_Window* window = NULL;
SDL_Surface* windowSurface = NULL;
int sockfd;


void fade(SDL_Surface* windowSurface)
{
    int i;
    double sn;

    unsigned char* pixels = (unsigned char*)windowSurface->pixels;
    int surlen = (WINDOW_WIDTH * windowSurface->pitch) / 4;
    pixels += 1;
    for (i = 0; i < surlen; i++, pixels += 4)
        if (*pixels)
            *pixels -= 8;
    SDL_Delay(2);
}

void setpixel(SDL_Surface* surface, int ix, int iy, int color)
{
    Uint32* p;
    unsigned char* pixels = (unsigned char *)surface->pixels;
    ix &= 0x1ff;
    iy &= 0x1ff;
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

static int thr_fade(void *dummy)
{
    SDL_Event event;

    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("VC8 Display", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_WIDTH, SDL_WINDOW_SHOWN);
    windowSurface = SDL_GetWindowSurface(window);

    while (1)
    {
        fade(windowSurface);
        SDL_UpdateWindowSurface(window);
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
                SDL_DestroyWindow(window);
                return -1;
            }
    }
    return 0;
}


int main(int argc, char* argv[])
{

    int portno = 2222, n;
    struct sockaddr_in serv_addr;
    struct hostent* server;
    char buffer[256], ch=0;
    int k, i = 0;
    char coord[4];
    float x, y;
    SDL_Thread* sthrd;
    SDL_Event stop;

    if (argc != 2)
    {
        printf("Usage: vc8_remote <host>\r\n");
        exit(1);
    }

    changemode(1);	// used for kbhit()
    SDL_Init(SDL_INIT_VIDEO);

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
    memset((void*)&serv_addr, '\0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((void*)&serv_addr.sin_addr.s_addr, (void*)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        exit(1);
    }
    stop.type = SDL_QUIT;
    sthrd = SDL_CreateThread(thr_fade, "FadeThread", NULL);

    while (!windowSurface)
        SDL_Delay(10);

    do
    {
        {
            n = recv(sockfd, buffer, 5, MSG_PEEK);
            if (n < 0)
            {
                perror("ERROR receiving from socket");
                exit(1);
            }

            if (n >= 5)
            {
                n = recv(sockfd, buffer, 1, 0);
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
                        perror("ERROR reading from socket");
                        exit(1);
                    }
                    for (k = 0; k < 4; k++)
                        coord[k] = buffer[k] & 0x3f;
                    x = ((((coord[0] | (coord[1] << 6)) / 2) + 256) % 512) + (WINDOW_WIDTH - 512) / 2;
                    y = (512 - ((((coord[2] | (coord[3] << 6)) / 2) + 256) % 512)) + (WINDOW_WIDTH - 512) / 2;
                    k = WINDOW_WIDTH;
                    setpixel(windowSurface, x, y, 0xf800);
                    if (x < k) setpixel(windowSurface, x+1, y, 0xf800);
                    if (y < k) setpixel(windowSurface, x, y +1, 0xf800);
                    if (y < k && x < k) setpixel(windowSurface, x +1, y+1, 0xf800);


                    // -------------------------------------------------------
/*
                    Keyup and Keydown are really difficult to implement.
                    The original code depended upon key repeat. Not reliable.
*/
                    if (_kbhit())
                    {
#ifdef _WIN32
                        ch = _getch();
#else
                        ch = getchar();
#endif
                        //sr = keyPressed(ch);
/*
                        if (sr)
                        {
                            buffer[0] = 0; //(sr & 0xF);
                            buffer[1] = ((sr & 0xF00) >> 4) | (sr & 0xF);
                            // s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
                            n = write(sockfd, buffer, 2);
                        }
*/
                    }
                    // -------------------------------------------------------
                }
            }
        }
    } while (ch != 'x');
    changemode(0);	// used for kbhit()
    SDL_PushEvent(&stop);
    SDL_WaitThread(sthrd, NULL);
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

int kbhit(void)
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