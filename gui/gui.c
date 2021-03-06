/*
	Glidix GUI

	Copyright (c) 2014-2017, Madd Games.
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.
	
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <libddi.h>
#include <pthread.h>
#include <sys/glidix.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <signal.h>
#include <libgwm.h>
//#include <glidix/video.h>
#include <glidix/humin.h>

#define	GUI_WINDOW_BORDER				3
#define	GUI_CAPTION_HEIGHT				20

// US keyboard layout
static int keymap[128] =
{
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
	'9', '0', '-', '=', '\b',	/* Backspace */
	'\t',			/* Tab */
	'q', 'w', 'e', 'r',	/* 19 */
	't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
	GWM_KC_CTRL,			/* 29	 - Control */
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
	'\'', '`',	 GWM_KC_SHIFT,		/* Left shift */
	'\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
	'm', ',', '.', '/',	 GWM_KC_SHIFT,				/* Right shift */
	'*',
	0,	/* Alt */
	' ',	/* Space bar */
	0,	/* Caps lock */
	0,	/* 59 - F1 key ... > */
	0,	 0,	 0,	 0,	 0,	 0,	 0,	 0,
	0,	/* < ... F10 */
	0,	/* 69 - Num lock*/
	0,	/* Scroll Lock */
	0,	/* Home key */
	GWM_KC_UP,	/* Up Arrow */
	0,	/* Page Up */
	'-',
	GWM_KC_LEFT,	/* Left Arrow */
	0,
	GWM_KC_RIGHT,	/* Right Arrow */
	'+',
	0,	/* 79 - End key*/
	GWM_KC_DOWN,	/* Down Arrow */
	0,	/* Page Down */
	0,	/* Insert Key */
	0,	/* Delete Key */
	0,	 0,	 0,
	0,	/* F11 Key */
	0,	/* F12 Key */
	0,	/* All other keys are undefined */
};

// when shift is pressed
static int keymapShift[128] =
{
		0,	0, '!', '@', '#', '$', '%', '^', '&', '*',	/* 9 */
	'(', ')', '_', '+', '\b',	/* Backspace */
	'\t',			/* Tab */
	'Q', 'W', 'E', 'R',	/* 19 */
	'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',	/* Enter key */
		GWM_KC_CTRL,			/* 29	 - Control */
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',	/* 39 */
 '\"', '`',	 GWM_KC_SHIFT,		/* Left shift */
 '\\', 'Z', 'X', 'C', 'V', 'B', 'N',			/* 49 */
	'M', '<', '>', '?',	 GWM_KC_SHIFT,				/* Right shift */
	'*',
		0,	/* Alt */
	' ',	/* Space bar */
		0,	/* Caps lock */
		0,	/* 59 - F1 key ... > */
		0,	 0,	 0,	 0,	 0,	 0,	 0,	 0,
		0,	/* < ... F10 */
		0,	/* 69 - Num lock*/
		0,	/* Scroll Lock */
		0,	/* Home key */
		0,	/* Up Arrow */
		0,	/* Page Up */
	'-',
		0,	/* Left Arrow */
		0,
		0,	/* Right Arrow */
	'+',
		0,	/* 79 - End key*/
		0,	/* Down Arrow */
		0,	/* Page Down */
		0,	/* Insert Key */
		0,	/* Delete Key */
		0,	 0,	 0,
		0,	/* F11 Key */
		0,	/* F12 Key */
		0,	/* All other keys are undefined */
};

DDISurface *desktopBackground;
DDISurface *screen;
DDISurface *frontBuffer;
DDISurface *defWinIcon;
DDISurface *winButtons;
DDISurface *winCap;

unsigned int screenWidth, screenHeight;
int mouseX, mouseY;
pthread_mutex_t mouseLock;

pthread_t inputThread;
pthread_t ptrThread;
pthread_t redrawThread;
pthread_t msgThread;

DDIColor winBackColor = {0xDD, 0xDD, 0xDD, 0xFF};		// window background color
DDIColor winDecoColor = {0x00, 0xAA, 0x00, 0xB2};		// window decoration color
DDIColor winUnfocColor = {0x55, 0x55, 0x55, 0xB2};		// unfocused window decoration color
DDIColor winCaptionColor = {0xFF, 0xFF, 0xFF, 0xFF};		// window caption (text) color

DDISurface *winDeco;
DDISurface *winUnfoc;

DDIFont *captionFont;

typedef struct Window_
{
	struct Window_*				prev;
	struct Window_*				next;
	struct Window_*				children;
	struct Window_*				parent;
	GWMWindowParams				params;
	DDISurface*				clientArea[2];
	DDISurface*				icon;
	DDISurface*				titleBar;
	DDISurface*				display;
	int					titleBarDirty;
	int					displayDirty;
	int					frontBufferIndex;
	uint64_t				id;
	int					pid;
	int					fd;
	uint32_t				clientID[2];
	int					cursor;
} Window;

pthread_mutex_t windowLock;
Window* desktopWindows = NULL;

typedef struct
{
	DDISurface*				image;
	int					hotX, hotY;
	const char*				src;
} Cursor;

static Cursor cursors[GWM_CURSOR_COUNT] = {
	{NULL, 0, 0, "/usr/share/images/cursor.png"},
	{NULL, 7, 7, "/usr/share/images/txtcursor.png"}
};

/**
 * The currently focused window. All its ancestors are considered focused too.
 */
Window* focusedWindow = NULL;

/**
 * The window that is currently being moved around by the user; NULL means the user
 * isn't moving a window. movingOffX and movingOffY define how far form the corner of
 * the window the user has clicked.
 */
Window *movingWindow = NULL;
int movingOffX;
int movingOffY;

/**
 * The window being resized, the anchor point and the size it was at the anchor point.
 */
Window *resizingWindow = NULL;
int resizingAnchorX;
int resizingAnchorY;
int resizingAnchorWidth;
int resizingAnchorHeight;

/**
 * The window that the mouse is currently inside of. This is used to issues enter/motion/leave
 * events.
 */
Window *hoveringWindow = NULL;

/**
 * The window that is currently active (clicked on).
 */
Window *activeWindow = NULL;

/**
 * The "listening window", to which an event is reported when the desktop is updated. This is usually
 * the system bar.
 */
Window *listenWindow = NULL;

/**
 * The semaphore counting the number of redraws to be made.
 */
sem_t semRedraw;

/**
 * The file descriptor representing the message queue.
 */
int guiQueue = -1;

/**
 * GWM information.
 */
GWMInfo *gwminfo;

int mouseLeftDown = 0;

void PostDesktopUpdate();

int isWindowFocused(Window *win)
{
	Window *check = focusedWindow;
	while (check != NULL)
	{
		if (check == win)
		{
			return 1;
		};
		
		check = check->parent;
	};
	
	return 0;
};

void PaintWindows(Window *win, DDISurface *target);

void PaintWindow(Window *win, DDISurface *target)
{
	if ((win->params.flags & GWM_WINDOW_HIDDEN) == 0)
	{
		DDISurface *display = win->display;
		if (win->displayDirty)
		{
			ddiOverlay(win->clientArea[win->frontBufferIndex], 0, 0, display, 0, 0, win->params.width, win->params.height);
			PaintWindows(win->children, display);
			win->displayDirty = 0;
		};
		
		if (win->parent == NULL)
		{
			if ((win->params.flags & GWM_WINDOW_NODECORATE) == 0)
			{
				DDISurface *borderSurface = winUnfoc;
				if (isWindowFocused(win))
				{
					borderSurface = winDeco;
				};
				
				ddiBlit(borderSurface, 0, 0, target, win->params.x, win->params.y+GUI_CAPTION_HEIGHT,
					win->params.width+2*GUI_WINDOW_BORDER,
					win->params.height+GUI_WINDOW_BORDER);

				// the caption graphic
				int yoff = 0;
				if (!isWindowFocused(win))
				{
					yoff = GUI_CAPTION_HEIGHT;
				};

				ddiBlit(winCap, 0, yoff, target, win->params.x, win->params.y,
					winCap->width/2, GUI_CAPTION_HEIGHT);
				int end = win->params.width + 2*GUI_WINDOW_BORDER - winCap->width/2;
				ddiBlit(winCap, winCap->width/2+1, yoff, target, win->params.x+end, win->params.y,
					winCap->width/2, GUI_CAPTION_HEIGHT);
				
				int xoff;
				for (xoff=winCap->width/2; xoff<end; xoff++)
				{
					ddiBlit(winCap, winCap->width/2, yoff, target, win->params.x+xoff, win->params.y,
						1, GUI_CAPTION_HEIGHT);
				};

				if (win->titleBarDirty)
				{
					win->titleBarDirty = 0;
					DDIColor col = {0, 0, 0, 0};
					ddiFillRect(win->titleBar, 0, 0, win->params.width+2*GUI_WINDOW_BORDER,
						GUI_CAPTION_HEIGHT, &col);

					int textX = 30;
					if (win->params.flags & GWM_WINDOW_NOICON)
					{
						textX = 12;
					};
					
					const char *penError;
					DDIPen *pen = ddiCreatePen(&screen->format, captionFont, textX, 15,
						win->params.width+2*GUI_WINDOW_BORDER, GUI_CAPTION_HEIGHT,
						0, 0, &penError);
					if (pen == NULL)
					{
						printf("cannot create pen: %s\n", penError);
					}
					else
					{
						ddiSetPenColor(pen, &winCaptionColor);
						ddiWritePen(pen, win->params.caption);
						ddiExecutePen2(pen, win->titleBar, DDI_POSITION_BASELINE);
						ddiDeletePen(pen);
					};
				};
				
				ddiBlit(win->titleBar, 0, 0, target, win->params.x, win->params.y,
					win->params.width+2*GUI_WINDOW_BORDER, GUI_CAPTION_HEIGHT);

				if ((win->params.flags & GWM_WINDOW_NOICON) == 0)
				{
					if (win->icon != NULL)
					{
						ddiBlit(win->icon, 0, 0, target, win->params.x+12, win->params.y+2, 16, 16);
					}
					else
					{
						ddiBlit(defWinIcon, 0, 0, target, win->params.x+12, win->params.y+2, 16, 16);
					};
				};
				
				int btnIndex = ((mouseX - (int)win->params.x) - ((int)win->params.width-70))/20;
				if ((mouseX >= (win->params.x + win->params.width + GUI_WINDOW_BORDER - 30)) && (mouseX < (win->params.x + win->params.width)))
				{
					btnIndex = 2;
				};
				if ((mouseY < (int)win->params.y) || (mouseY > (int)win->params.y+GUI_CAPTION_HEIGHT) || (mouseX < (win->params.x + win->params.width + GUI_WINDOW_BORDER - 70)))
				{
					btnIndex = -1;
				};
				
				if ((win->params.flags & GWM_WINDOW_NOSYSMENU) == 0)
				{
					int i;
					for (i=0; i<3; i++)
					{
						int yi = 0;
						if (i == btnIndex)
						{
							yi = 1;
							if (mouseLeftDown)
							{
								yi = 2;
							};
						};
						
						if (i == 1)
						{
							if ((win->params.flags & GWM_WINDOW_RESIZEABLE) == 0)
							{
								yi = 3;
							};
						};
						
						int width = 20;
						if (i == 2) width = 30;
						ddiBlit(winButtons, 20*i, 20*yi, target, win->params.x+(win->params.width-70+2*GUI_WINDOW_BORDER)+20*i, win->params.y, width, 20);
					};
				};
				
				ddiOverlay(display, 0, 0, target,
					win->params.x+GUI_WINDOW_BORDER,
					win->params.y+GUI_CAPTION_HEIGHT,
					win->params.width, win->params.height);
			}
			else
			{
				ddiBlit(display, 0, 0, target,
					win->params.x, win->params.y, win->params.width, win->params.height);
			};
		}
		else
		{
			ddiBlit(display, 0, 0, target,
				win->params.x, win->params.y, win->params.width, win->params.height);
		};
	};
};

void PaintWindows(Window *win, DDISurface *target)
{
	for (; win!=NULL; win=win->next)
	{
		PaintWindow(win, target);
	};
};

Window *GetWindowByIDFromList(Window *win, uint64_t id, int pid, int fd)
{
	for (; win!=NULL; win=win->next)
	{
		if ((win->id == id) && (win->pid == pid) && (win->fd == fd)) return win;
		Window *child = GetWindowByIDFromList(win->children, id, pid, fd);
		if (child != NULL) return child;
	};
	
	return NULL;
};

Window *GetWindowByID(uint64_t id, int pid, int fd)
{
	return GetWindowByIDFromList(desktopWindows, id, pid, fd);
};

void DeleteWindow(Window *win);

void DeleteWindowList(Window *win)
{
	for (; win!=NULL; win=win->next)
	{
		DeleteWindow(win);
	};
};

void DeleteWindow(Window *win)
{
	if (focusedWindow == win) focusedWindow = NULL;
	if (movingWindow == win) movingWindow = NULL;
	if (hoveringWindow == win) hoveringWindow = NULL;
	if (activeWindow == win) activeWindow = NULL;
	if (listenWindow == win) listenWindow = NULL;
	if (resizingWindow == win) resizingWindow = NULL;
	
	DeleteWindowList(win->children);
	
	// unlink from previous window
	if (win->parent == NULL)
	{
		PostDesktopUpdate();

		if (win->prev == NULL)
		{
			desktopWindows = win->next;
		}
		else
		{
			win->prev->next = win->next;
		};
	}
	else
	{
		if (win->prev == NULL)
		{
			win->parent->children = win->next;
		}
		else
		{
			win->prev->next = win->next;
		};
	};
	
	// unlink from next window
	if (win->next != NULL)
	{
		win->next->prev = win->prev;
	};
	
	if (win->icon != NULL) ddiDeleteSurface(win->icon);
	ddiDeleteSurface(win->clientArea[0]);
	ddiDeleteSurface(win->clientArea[1]);
	ddiDeleteSurface(win->titleBar);
	ddiDeleteSurface(win->display);
	free(win);
};

void ResizeWindow(Window *win, unsigned int width, unsigned int height)
{
	ddiDeleteSurface(win->clientArea[0]);
	ddiDeleteSurface(win->clientArea[1]);
	ddiDeleteSurface(win->titleBar);
	ddiDeleteSurface(win->display);
	
	win->params.width = width;
	win->params.height = height;

	win->clientArea[0] = ddiCreateSurface(&screen->format, win->params.width, win->params.height, NULL, DDI_SHARED);
	win->clientArea[1] = ddiCreateSurface(&screen->format, win->params.width, win->params.height, NULL, DDI_SHARED);
	win->clientID[0] = win->clientArea[0]->id;
	win->clientID[1] = win->clientArea[1]->id;
	
	win->frontBufferIndex = 0;
	win->display = ddiCreateSurface(&screen->format, win->params.width, win->params.height, NULL, 0);
	ddiFillRect(win->clientArea[0], 0, 0, win->params.width, win->params.height, &winBackColor);
	ddiFillRect(win->clientArea[1], 0, 0, win->params.width, win->params.height, &winBackColor);
	ddiFillRect(win->display, 0, 0, win->params.width, win->params.height, &winBackColor);
	win->displayDirty = 1;
	win->titleBar = ddiCreateSurface(&screen->format, win->params.width+2*GUI_WINDOW_BORDER, GUI_CAPTION_HEIGHT, NULL, 0);
	win->titleBarDirty = 1;
};

void DeleteWindowsOf(int pid, int fd)
{
	// we only need to scan the top-level windows because all windows
	// created by the application must be either top-level or children
	// of top-level windows created by it.
	Window *win;
	for (win=desktopWindows; win!=NULL; win=win->next)
	{
		if ((win->pid == pid) && (win->fd == fd))
		{
			DeleteWindow(win);
		};
	};
};

void DeleteWindowByID(uint64_t id, int pid, int fd)
{
	Window *win = GetWindowByID(id, pid, fd);
	if (win != NULL) DeleteWindow(win);
};

Window* CreateWindow(uint64_t parentID, GWMWindowParams *pars, uint64_t myID, int pid, int fd, int painterPid)
{
	if (pars->x == GWM_POS_UNSPEC)
	{
		pars->x = rand() % (screenWidth - pars->width);
	};
	
	if (pars->y == GWM_POS_UNSPEC)
	{
		pars->y = rand() % (screenHeight - pars->height);
	};
	
	pars->caption[255] = 0;
	pars->iconName[255] = 0;
	if (myID == 0)
	{
		return NULL;
	};
	
	Window *parent = NULL;
	if (parentID != 0)
	{
		parent = GetWindowByID(parentID, pid, fd);
		if (parent == NULL)
		{
			return NULL;
		};
	};
	
	Window *win = (Window*) malloc(sizeof(Window));
	win->prev = NULL;
	win->next = NULL;
	win->children = NULL;
	win->parent = parent;
	win->cursor = GWM_CURSOR_NORMAL;
	memcpy(&win->params, pars, sizeof(GWMWindowParams));

	win->clientArea[0] = ddiCreateSurface(&screen->format, pars->width, pars->height, NULL, DDI_SHARED);
	win->clientArea[1] = ddiCreateSurface(&screen->format, pars->width, pars->height, NULL, DDI_SHARED);
	win->clientID[0] = win->clientArea[0]->id;
	win->clientID[1] = win->clientArea[1]->id;

	win->frontBufferIndex = 0;
	win->display = ddiCreateSurface(&screen->format, pars->width, pars->height, NULL, 0);
	ddiFillRect(win->clientArea[0], 0, 0, pars->width, pars->height, &winBackColor);
	ddiFillRect(win->clientArea[1], 0, 0, pars->width, pars->height, &winBackColor);
	ddiFillRect(win->display, 0, 0, pars->width, pars->height, &winBackColor);
	win->displayDirty = 1;
	win->titleBar = ddiCreateSurface(&screen->format, pars->width+2*GUI_WINDOW_BORDER, GUI_CAPTION_HEIGHT, NULL, 0);
	win->titleBarDirty = 1;
	win->icon = NULL;
	win->id = myID;
	win->pid = pid;
	win->fd = fd;

	if (parent == NULL)
	{
		PostDesktopUpdate();
		
		if (desktopWindows == NULL)
		{
			desktopWindows = win;
		}
		else
		{
			Window *last = desktopWindows;
			while (last->next != NULL) last = last->next;
			last->next = win;
			win->prev = last;
		};
	}
	else
	{
		if (parent->children == NULL)
		{
			parent->children = win;
		}
		else
		{
			Window *last = parent->children;
			while (last->next != NULL) last = last->next;
			last->next = win;
			win->prev = last;
		};
	};
	return win;
};

void PaintDesktop()
{
	ddiOverlay(desktopBackground, 0, 0, screen, 0, 0, screenWidth, screenHeight);
	
	pthread_mutex_lock(&windowLock);
	PaintWindows(desktopWindows, screen);
	pthread_mutex_unlock(&windowLock);

	pthread_mutex_lock(&mouseLock);
	int cursorIndex = 0;
	if (hoveringWindow != NULL)
	{
		cursorIndex = hoveringWindow->cursor;
	};
	ddiBlit(cursors[cursorIndex].image, 0, 0, screen, mouseX-cursors[cursorIndex].hotX, mouseY-cursors[cursorIndex].hotY, 16, 16);
	pthread_mutex_unlock(&mouseLock);
	
	ddiOverlay(screen, 0, 0, frontBuffer, 0, 0, screenWidth, screenHeight);
};

void GetWindowSize(Window *win, int *width, int *height)
{
	if (win->parent == NULL)
	{
		if ((win->params.flags & GWM_WINDOW_NODECORATE) == 0)
		{
			*width = win->params.width + 2 * GUI_WINDOW_BORDER;
			*height = win->params.height + 2 * GUI_WINDOW_BORDER + GUI_CAPTION_HEIGHT;
			return;
		};
	};
	
	*width = win->params.width;
	*height = win->params.height;
};

void GetClientOffset(Window *win, int *offX, int *offY)
{
	if (win->parent == NULL)
	{
		if ((win->params.flags & GWM_WINDOW_NODECORATE) == 0)
		{
			*offX = GUI_WINDOW_BORDER;
			*offY = GUI_CAPTION_HEIGHT;
			return;
		};
	};
	
	*offX = 0;
	*offY = 0;
};

Window* FindWindowFromListAt(Window *win, int x, int y)
{
	Window *result = NULL;
	for (; win!=NULL; win=win->next)
	{
		if ((win->params.flags & GWM_WINDOW_HIDDEN) == 0)
		{
			int width, height;
			GetWindowSize(win, &width, &height);
		
			int endX = win->params.x + width;
			int endY = win->params.y + height;
		
			int offX, offY;
			GetClientOffset(win, &offX, &offY);
		
			if ((x >= (int)win->params.x) && (x < endX) && (y >= (int)win->params.y) && (y < endY))
			{
				Window *child = FindWindowFromListAt(win->children, x-win->params.x-offX, y-win->params.y-offY);
				if (child == NULL) result = win;
				else result = child;
			};
		};
	};
	
	return result;
};

Window* FindWindowAt(int x, int y)
{
	return FindWindowFromListAt(desktopWindows, x, y);
};

void AbsoluteToRelativeCoords(Window *win, int inX, int inY, int *outX, int *outY)
{
	int offX, offY;
	GetClientOffset(win, &offX, &offY);
	
	int cornerX = win->params.x + offX;
	int cornerY = win->params.y + offY;
	
	int effectiveX = inX - cornerX;
	int effectiveY = inY - cornerY;
	
	if (win->parent == NULL)
	{
		*outX = effectiveX;
		*outY = effectiveY;
	}
	else
	{
		AbsoluteToRelativeCoords(win->parent, effectiveX, effectiveY, outX, outY);
	};
};

void RelativeToAbsoluteCoords(Window *win, int inX, int inY, int *outX, int *outY)
{
	if (win == NULL)
	{
		*outX = inX;
		*outY = inY;
	}
	else
	{
		int offX, offY;
		GetClientOffset(win, &offX, &offY);
	
		int startX, startY;
		RelativeToAbsoluteCoords(win->parent, win->params.x, win->params.y, &startX, &startY);
		
		*outX = startX + offX + inX;
		*outY = startY + offY + inY;
	};
};

void PostWindowEvent(Window *win, GWMEvent *event)
{
	GWMMessage msg;
	msg.event.type = GWM_MSG_EVENT;
	msg.event.seq = 0;
	memcpy(&msg.event.payload, event, sizeof(GWMEvent));
	_glidix_mqsend(guiQueue, win->pid, win->fd, &msg, sizeof(GWMMessage));
};

void PostDesktopUpdate()
{
	if (listenWindow != NULL)
	{
		GWMEvent ev;
		ev.type = GWM_EVENT_DESKTOP_UPDATE;
		ev.win = listenWindow->id;
		PostWindowEvent(listenWindow, &ev);
	};
};

void BringWindowToFront(Window *win)
{
	Window *toplevel = win;
	while (toplevel->parent != NULL) toplevel = toplevel->parent;
	
	// bring it to front
	if (toplevel->next != NULL)
	{
		if (toplevel->prev != NULL) toplevel->prev->next = toplevel->next;
		else
		{
			desktopWindows = toplevel->next;
		};
		if (toplevel->next != NULL) toplevel->next->prev = toplevel->prev;
	
		Window *last = toplevel->next;
		while (last->next != NULL) last = last->next;
		last->next = toplevel;
		toplevel->prev = last;
		toplevel->next = NULL;
	};
};

void MakeWindowFocused(Window *win)
{
	if (win == focusedWindow) return;
	
	PostDesktopUpdate();
	
	if (win != NULL)
	{
		GWMEvent ev;
		ev.type = GWM_EVENT_FOCUS_IN;
		ev.win = win->id;
		PostWindowEvent(win, &ev);
	};

	if (focusedWindow != NULL)
	{
		GWMEvent ev;
		ev.type = GWM_EVENT_FOCUS_OUT;
		ev.win = focusedWindow->id;
		PostWindowEvent(focusedWindow, &ev);
	};
	
	focusedWindow = win;
};

void onMouseLeft()
{
	mouseLeftDown = 1;
	int x, y;
	pthread_mutex_lock(&mouseLock);
	x = mouseX;
	y = mouseY;
	pthread_mutex_unlock(&mouseLock);
	
	pthread_mutex_lock(&windowLock);
	Window *oldFocus = focusedWindow;
	focusedWindow = FindWindowAt(x, y);
	
	if (oldFocus != focusedWindow)
	{
		PostDesktopUpdate();
		
		if (focusedWindow != NULL)
		{
			GWMEvent ev;
			ev.type = GWM_EVENT_FOCUS_IN;
			ev.win = focusedWindow->id;
			PostWindowEvent(focusedWindow, &ev);
		};

		if (oldFocus != NULL)
		{
			GWMEvent ev;
			ev.type = GWM_EVENT_FOCUS_OUT;
			ev.win = oldFocus->id;
			PostWindowEvent(oldFocus, &ev);
		};
	};
	
	if (focusedWindow != NULL)
	{
		Window *toplevel = focusedWindow;
		while (toplevel->parent != NULL) toplevel = toplevel->parent;
		
		// bring it to front
		if (toplevel->next != NULL)
		{
			if (toplevel->prev != NULL) toplevel->prev->next = toplevel->next;
			else
			{
				desktopWindows = toplevel->next;
			};
			if (toplevel->next != NULL) toplevel->next->prev = toplevel->prev;
		
			Window *last = toplevel->next;
			while (last->next != NULL) last = last->next;
			last->next = toplevel;
			toplevel->prev = last;
			toplevel->next = NULL;
		};
		
		if ((toplevel->params.flags & GWM_WINDOW_NODECORATE) == 0)
		{
			// if the user clicked outside of this window's client area, it means
			// they clicked on the decoration. so if they clicked the title bar,
			// we allow them to move the window.
			int offX = x - toplevel->params.x;
			int offY = y - toplevel->params.y;
		
			if (offY < GUI_CAPTION_HEIGHT)
			{
				if ((focusedWindow->params.flags & GWM_WINDOW_NODECORATE) == 0)
				{
					if (focusedWindow->parent == NULL)
					{
						movingWindow = focusedWindow;
						movingOffX = offX;
						movingOffY = offY;
					};
				};
			}
			else if ((offY >= (focusedWindow->params.height + GUI_CAPTION_HEIGHT + GUI_WINDOW_BORDER))
				&& ((offX < GUI_WINDOW_BORDER) || (offX >= (GUI_WINDOW_BORDER+focusedWindow->params.width))))
			{
				if ((focusedWindow->params.flags & GWM_WINDOW_NODECORATE) == 0)
				{
					if (focusedWindow->parent == NULL)
					{
						if (focusedWindow->params.flags & GWM_WINDOW_RESIZEABLE)
						{
							resizingWindow = focusedWindow;
							resizingAnchorX = x;
							resizingAnchorY = y;
							resizingAnchorWidth = (int) focusedWindow->params.width;
							resizingAnchorHeight = (int) focusedWindow->params.height;
						};
					};
				};
			};
		};
	};
	
	activeWindow = focusedWindow;
	pthread_mutex_unlock(&windowLock);
};

void onMouseLeftRelease()
{
	activeWindow = NULL;
	mouseLeftDown = 0;
	
	pthread_mutex_lock(&mouseLock);
	int x = mouseX;
	int y = mouseY;
	pthread_mutex_unlock(&mouseLock);
	
	pthread_mutex_lock(&windowLock);
	movingWindow = NULL;
	resizingWindow = NULL;
	Window *win = FindWindowAt(x, y);
	
	if (win != NULL)
	{
		if (win->parent == NULL)
		{
			if ((win->params.flags & (GWM_WINDOW_NODECORATE | GWM_WINDOW_NOSYSMENU)) == 0)
			{
				int btnIndex = ((mouseX - (int)win->params.x) - ((int)win->params.width-70))/20;
				if ((mouseX >= (win->params.x + win->params.width + GUI_WINDOW_BORDER - 30)) && (mouseX < (win->params.x + win->params.width)))
				{
					btnIndex = 2;
				};
				if ((y < (int)win->params.y) || (y > (int)win->params.y+GUI_CAPTION_HEIGHT) || (x < (win->params.x + win->params.width + GUI_WINDOW_BORDER - 70)))
				{
					btnIndex = -1;
				};
				
				if (btnIndex == 0)
				{
					// clicked the minimize button
					win->params.flags |= GWM_WINDOW_HIDDEN;
					MakeWindowFocused(NULL);
					PostDesktopUpdate();
					sem_post(&semRedraw);
				}
				else if (btnIndex == 1)
				{
					// maximize
					if (win->params.flags & GWM_WINDOW_RESIZEABLE)
					{
						// request resize and move
						GWMEvent event;
						event.type = GWM_EVENT_RESIZE_REQUEST;
						event.win = win->id;
						event.x = 0;
						event.y = 0;
						event.width = screenWidth - 2*GUI_WINDOW_BORDER;
						event.height = screenHeight - GUI_CAPTION_HEIGHT - 2*GUI_WINDOW_BORDER - 40;
						PostWindowEvent(win, &event);
					};
				}
				else if (btnIndex == 2)
				{
					// clicked the close button
					GWMEvent event;
					event.type = GWM_EVENT_CLOSE;
					event.win = win->id;
					PostWindowEvent(win, &event);
				};
			};
		};
	};
	
	pthread_mutex_unlock(&windowLock);
};

static int currentKeyMods = 0;
void DecodeScancode(GWMEvent *ev)
{
	if (ev->scancode < 0x80) ev->keycode = keymap[ev->scancode];
	else ev->keycode = 0;
	ev->keychar = 0;

	if (ev->scancode < 0x80)
	{
		int key = keymap[ev->scancode];
		if (currentKeyMods & GWM_KM_SHIFT) key = keymapShift[ev->scancode];

		if (key == GWM_KC_CTRL)
		{
			// ctrl
			if (ev->type == GWM_EVENT_DOWN)
			{
				currentKeyMods |= GWM_KM_CTRL;
			}
			else
			{
				currentKeyMods &= ~GWM_KM_CTRL;
			};
		}
		else if (key == GWM_KC_SHIFT)
		{
			// shift
			if (ev->type == GWM_EVENT_DOWN)
			{
				currentKeyMods |= GWM_KM_SHIFT;
			}
			else
			{
				currentKeyMods &= ~GWM_KM_SHIFT;
			};
		}
		else if ((key != 0) && ((currentKeyMods & GWM_KM_CTRL) == 0) && (key < 0x80))
		{
			ev->keychar = key;
		};
	};
	
	ev->keymod = currentKeyMods;
};

void onInputEvent(int ev, int scancode)
{
	pthread_mutex_lock(&windowLock);
	if (focusedWindow != NULL)
	{
		GWMEvent event;
		if (ev == HUMIN_EV_BUTTON_DOWN)
		{
			event.type = GWM_EVENT_DOWN;
		}
		else
		{
			event.type = GWM_EVENT_UP;
		};
		
		event.win = focusedWindow->id;
		event.scancode = scancode;
		
		pthread_mutex_lock(&mouseLock);
		int mx = mouseX;
		int my = mouseY;
		pthread_mutex_unlock(&mouseLock);
		
		AbsoluteToRelativeCoords(focusedWindow, mx, my, &event.x, &event.y);
		DecodeScancode(&event);
		PostWindowEvent(focusedWindow, &event);
	};
	
	pthread_mutex_unlock(&windowLock);
};

void onMouseMoved()
{
	int x, y;
	pthread_mutex_lock(&mouseLock);
	x = mouseX;
	y = mouseY;
	pthread_mutex_unlock(&mouseLock);
	
	pthread_mutex_lock(&windowLock);
	if (movingWindow != NULL)
	{
		int newX = x - movingOffX;
		int newY = y - movingOffY;
		
		movingWindow->params.x = newX;
		movingWindow->params.y = newY;
	};
	
	if (resizingWindow != NULL)
	{
		int deltaX = x - resizingAnchorX;
		int deltaY = y - resizingAnchorY;
		
		int newWidth = resizingAnchorWidth + deltaX;
		int newHeight = resizingAnchorHeight + deltaY;
		
		if (newWidth < 48)
		{
			newWidth = 48;
		};
		
		if (newHeight < 48)
		{
			newHeight = 48;
		};

		GWMEvent event;
		event.type = GWM_EVENT_RESIZE_REQUEST;
		event.win = resizingWindow->id;
		event.x = resizingWindow->params.x;
		event.y = resizingWindow->params.y;
		event.width = newWidth;
		event.height = newHeight;
		PostWindowEvent(resizingWindow, &event);
	};
	
	if (activeWindow != NULL)
	{
		GWMEvent ev;
		ev.type = GWM_EVENT_MOTION;
		ev.win = activeWindow->id;
		AbsoluteToRelativeCoords(activeWindow, x, y, &ev.x, &ev.y);
		PostWindowEvent(activeWindow, &ev);
	}
	else
	{
		Window *win = FindWindowAt(x, y);
		if (win != hoveringWindow)
		{
			if (hoveringWindow != NULL)
			{
				GWMEvent ev;
				ev.type = GWM_EVENT_LEAVE;
				ev.win = hoveringWindow->id;
				PostWindowEvent(hoveringWindow, &ev);
			};
		
			if (win != NULL)
			{
				GWMEvent ev;
				ev.type = GWM_EVENT_ENTER;
				ev.win = win->id;
				AbsoluteToRelativeCoords(win, x, y, &ev.x, &ev.y);
				PostWindowEvent(win, &ev);
			};
		
			hoveringWindow = win;
		}
		else if (hoveringWindow != NULL)
		{
			GWMEvent ev;
			ev.type = GWM_EVENT_MOTION;
			ev.win = hoveringWindow->id;
			AbsoluteToRelativeCoords(hoveringWindow, x, y, &ev.x, &ev.y);
			PostWindowEvent(hoveringWindow, &ev);
		};
	};
	
	pthread_mutex_unlock(&windowLock);
};

void ClipMouse()
{
	if (mouseX < 0) mouseX = 0;
	if (mouseX >= screenWidth) mouseX = screenWidth - 1;
	if (mouseY < 0) mouseY = 0;
	if (mouseY >= screenHeight) mouseY = screenHeight - 1;
};

void *inputThreadFunc(void *ignore)
{
	struct pollfd *fds = NULL;
	nfds_t nfds = 0;
	
	DIR *dirp = opendir("/dev");
	if (dirp == NULL)
	{
		fprintf(stderr, "failed to scan /dev: no input\n");
		return NULL;
	};
	
	struct dirent *ent;
	while ((ent = readdir(dirp)) != NULL)
	{
		if (memcmp(ent->d_name, "humin", 5) == 0)
		{
			char fullpath[256];
			sprintf(fullpath, "/dev/%s", ent->d_name);
			
			int fd = open(fullpath, O_RDWR);
			if (fd == -1)
			{
				fprintf(stderr, "failed to open %s: ignoring input device\n", fullpath);
				continue;
			};
			
			fds = (struct pollfd*) realloc(fds, sizeof(struct pollfd)*(nfds+1));
			fds[nfds].fd = fd;
			fds[nfds].events = POLLIN;
			
			nfds++;
			printf("opened device %s for input\n", fullpath);
		};
	};
	
	closedir(dirp);
	
	HuminEvent ev;
	while (1)
	{
		int screenDirty = 0;

		int count = poll(fds, nfds, -1);
		if (count == 0) continue;
		
		nfds_t i;
		for (i=0; i<nfds; i++)
		{
			if (fds[i].revents & POLLIN)
			{
				if (read(fds[i].fd, &ev, sizeof(HuminEvent)) < 0)
				{
					continue;
				};

				if (ev.type == HUMIN_EV_BUTTON_DOWN)
				{
					if ((ev.button.scancode >= 0x100) && (ev.button.scancode < 0x200))
					{
						// mouse
						if (ev.button.scancode == HUMIN_SC_MOUSE_LEFT)
						{
							onMouseLeft();
							screenDirty = 1;
						};
					};
			
					onInputEvent(HUMIN_EV_BUTTON_DOWN, ev.button.scancode);
				}
				else if (ev.type == HUMIN_EV_BUTTON_UP)
				{
					if ((ev.button.scancode >= 0x100) && (ev.button.scancode < 0x200))
					{
						// mouse
						if (ev.button.scancode == HUMIN_SC_MOUSE_LEFT)
						{
							onMouseLeftRelease();
							screenDirty = 1;
						};
					};
			
					onInputEvent(HUMIN_EV_BUTTON_UP, ev.button.scancode);
				};
			};
		};
		
		if (screenDirty)
		{
			sem_post(&semRedraw);
		};
	};
	
	return NULL;
};

void* ptrThreadFunc(void *ignore)
{
	struct pollfd *fds = NULL;
	nfds_t nfds = 0;
	
	DIR *dirp = opendir("/dev");
	if (dirp == NULL)
	{
		fprintf(stderr, "failed to scan /dev: no mouse movement\n");
		return NULL;
	};
	
	struct dirent *ent;
	while ((ent = readdir(dirp)) != NULL)
	{
		if (memcmp(ent->d_name, "ptr", 3) == 0)
		{
			char fullpath[256];
			sprintf(fullpath, "/dev/%s", ent->d_name);
			
			int fd = open(fullpath, O_RDWR);
			if (fd == -1)
			{
				fprintf(stderr, "failed to open %s: ignoring pointer device\n", fullpath);
				continue;
			};
			
			_glidix_ptrstate state;
			state.width = screenWidth;
			state.height = screenHeight;
			state.posX = screenWidth/2;
			state.posY = screenHeight/2;
			write(fd, &state, sizeof(_glidix_ptrstate));
			
			fds = (struct pollfd*) realloc(fds, sizeof(struct pollfd)*(nfds+1));
			fds[nfds].fd = fd;
			fds[nfds].events = POLLIN;
			
			nfds++;
			printf("opened device %s for mouse tracking\n", fullpath);
		};
	};
	
	closedir(dirp);
	
	_glidix_ptrstate state;
	while (1)
	{
		int count = poll(fds, nfds, -1);
		if (count == 0) continue;
		
		nfds_t i;
		for (i=0; i<nfds; i++)
		{
			if (fds[i].revents & POLLIN)
			{
				if (read(fds[i].fd, &state, sizeof(_glidix_ptrstate)) < 0)
				{
					continue;
				};
				
				pthread_mutex_lock(&mouseLock);
				mouseX = state.posX;
				mouseY = state.posY;
				pthread_mutex_unlock(&mouseLock);
				onMouseMoved();
			};
		};
		
		sem_post(&semRedraw);
	};
	
	return NULL;
};

void PostWindowDirty(Window *win)
{
	// only bother if we don't already know
	win->displayDirty = 1;
	if ((win->params.flags & GWM_WINDOW_HIDDEN) == 0)
	{
		if (win->parent == NULL)
		{
			sem_post(&semRedraw);
		}
		else
		{
			PostWindowDirty(win->parent);
		};
	};
};

void *msgThreadFunc(void *ignore)
{
	static char msgbuf[65536];
	
	while (1)
	{
		_glidix_msginfo info;
		ssize_t size = _glidix_mqrecv(guiQueue, &info, msgbuf, 65536);
		if (size == -1) continue;

		if (info.type == _GLIDIX_MQ_CONNECT)
		{
		}
		else if (info.type == _GLIDIX_MQ_INCOMING)
		{
			if (size < sizeof(GWMCommand))
			{
				continue;
			};
		
			GWMCommand *cmd = (GWMCommand*) msgbuf;
			
			if (cmd->cmd == GWM_CMD_CREATE_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = CreateWindow(cmd->createWindow.parent,
						&cmd->createWindow.pars, cmd->createWindow.id,
						info.pid, info.fd, cmd->createWindow.painterPid);
				
				GWMMessage msg;
				msg.createWindowResp.type = GWM_MSG_CREATE_WINDOW_RESP;
				msg.createWindowResp.seq = cmd->createWindow.seq;
				msg.createWindowResp.status = 0;
				if (win == NULL) msg.createWindowResp.status = 1;
				else
				{
					memcpy(&msg.createWindowResp.format, &screen->format, sizeof(DDIPixelFormat));
					msg.createWindowResp.clientID[0] = win->clientID[0];
					msg.createWindowResp.clientID[1] = win->clientID[1];
					msg.createWindowResp.width = win->params.width;
					msg.createWindowResp.height = win->params.height;
					
					if (cmd->createWindow.pars.flags & GWM_WINDOW_MKFOCUSED)
					{
						movingWindow = NULL;
						MakeWindowFocused(win);
					};
				};
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_POST_DIRTY)
			{
				//sem_post(&semRedraw);
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->postDirty.id, info.pid, info.fd);
				if (win != NULL) PostWindowDirty(win);
				win->frontBufferIndex ^= 1;
				GWMMessage msg;
				msg.postDirtyResp.type = GWM_MSG_POST_DIRTY_RESP;
				msg.postDirtyResp.seq = cmd->postDirty.seq;
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				pthread_mutex_unlock(&windowLock);
			}
			else if (cmd->cmd == GWM_CMD_DESTROY_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				DeleteWindowByID(cmd->destroyWindow.id, info.pid, info.fd);
				pthread_mutex_unlock(&windowLock);
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_CLEAR_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->clearWindow.id, info.pid, info.fd);
				if (win != NULL)
				{
					ddiFillRect(win->clientArea[win->frontBufferIndex^1], 0, 0, win->params.width, win->params.height, &winBackColor);
				};
				
				GWMMessage msg;
				msg.clearWindowResp.type = GWM_MSG_CLEAR_WINDOW_RESP;
				msg.clearWindowResp.seq = cmd->clearWindow.seq;
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_SCREEN_SIZE)
			{
				GWMMessage msg;
				msg.screenSizeResp.type = GWM_MSG_SCREEN_SIZE_RESP;
				msg.screenSizeResp.seq = cmd->screenSize.seq;
				msg.screenSizeResp.width = screen->width;
				msg.screenSizeResp.height = screen->height;
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_SET_FLAGS)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->setFlags.win, info.pid, info.fd);
				
				GWMMessage msg;
				msg.setFlagsResp.type = GWM_MSG_SET_FLAGS_RESP;
				msg.setFlagsResp.seq = cmd->setFlags.seq;
				
				if (win != NULL)
				{
					win->params.flags = cmd->setFlags.flags;
					if (win->params.flags & GWM_WINDOW_MKFOCUSED)
					{
						MakeWindowFocused(win);
					};
					if ((win->params.flags & GWM_WINDOW_HIDDEN) == 0)
					{
						if (win->parent == NULL) PostDesktopUpdate();
						PostWindowDirty(win);
					};
					msg.setFlagsResp.status = 0;
				}
				else
				{
					msg.setFlagsResp.status = -1;
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_SET_CURSOR)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->setCursor.win, info.pid, info.fd);
				
				GWMMessage msg;
				msg.setCursorResp.type = GWM_MSG_SET_CURSOR_RESP;
				msg.setCursorResp.seq = cmd->setCursor.seq;
				
				if ((win != NULL) && (cmd->setCursor.cursor >= 0) && (cmd->setCursor.cursor < GWM_CURSOR_COUNT))
				{
					win->cursor = cmd->setCursor.cursor;
					msg.setCursorResp.status = 0;
				}
				else
				{
					msg.setCursorResp.status = -1;
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_SET_ICON)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->setIcon.win, info.pid, info.fd);
				
				GWMMessage msg;
				msg.setIconResp.type = GWM_MSG_SET_ICON_RESP;
				msg.setIconResp.seq = cmd->setIcon.seq;
				
				if (win != NULL)
				{
					if (win->icon != NULL) ddiDeleteSurface(win->icon);
					win->icon = ddiCreateSurface(&screen->format, 16, 16, cmd->setIcon.data, 0);
					msg.setIconResp.status = 0;
				}
				else
				{
					msg.setIconResp.status = -1;
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_GET_FORMAT)
			{
				GWMMessage msg;
				msg.getFormatResp.type = GWM_MSG_GET_FORMAT_RESP;
				msg.getFormatResp.seq = cmd->getFormat.seq;
				memcpy(&msg.getFormatResp.format, &screen->format, sizeof(DDIPixelFormat));
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_GET_WINDOW_LIST)
			{
				pthread_mutex_lock(&windowLock);
				Window *win;
				
				GWMMessage msg;
				msg.getWindowListResp.type = GWM_MSG_GET_WINDOW_LIST_RESP;
				msg.getWindowListResp.seq = cmd->getWindowList.seq;
				msg.getWindowListResp.count = 0;
				
				for (win=desktopWindows; win!=NULL; win=win->next)
				{
					if (msg.getWindowListResp.count == 128) break;
					
					msg.getWindowListResp.wins[msg.getWindowListResp.count].id = win->id;
					msg.getWindowListResp.wins[msg.getWindowListResp.count].fd = win->fd;
					msg.getWindowListResp.wins[msg.getWindowListResp.count].pid = win->pid;
					msg.getWindowListResp.count++;
				};
				
				if (focusedWindow != NULL)
				{
					Window *foc = focusedWindow;
					while (foc->parent != NULL) foc = foc->parent;
					msg.getWindowListResp.focused.id = foc->id;
					msg.getWindowListResp.focused.fd = foc->fd;
					msg.getWindowListResp.focused.pid = foc->pid;
				}
				else
				{
					msg.getWindowListResp.focused.id = 0;
					msg.getWindowListResp.focused.fd = 0;
					msg.getWindowListResp.focused.pid = 0;
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_GET_WINDOW_PARAMS)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->getWindowParams.ref.id, cmd->getWindowParams.ref.pid, cmd->getWindowParams.ref.fd);
				
				GWMMessage msg;
				msg.getWindowParamsResp.type = GWM_MSG_GET_WINDOW_PARAMS_RESP;
				msg.getWindowParamsResp.seq = cmd->getWindowParams.seq;
				if (win == NULL)
				{
					msg.getWindowParamsResp.status = -1;
				}
				else
				{
					msg.getWindowParamsResp.status = 0;
					memcpy(&msg.getWindowParamsResp.params, &win->params, sizeof(GWMWindowParams));
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_TOGGLE_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->toggleWindow.ref.id, cmd->toggleWindow.ref.pid, cmd->toggleWindow.ref.fd);
				
				GWMMessage msg;
				msg.toggleWindowResp.type = GWM_MSG_TOGGLE_WINDOW_RESP;
				msg.toggleWindowResp.seq = cmd->toggleWindow.seq;
				if (win == NULL)
				{
					msg.toggleWindowResp.status = -1;
				}
				else
				{
					msg.toggleWindowResp.status = 0;
					
					win->params.flags ^= GWM_WINDOW_HIDDEN;
					if ((win->params.flags & GWM_WINDOW_HIDDEN) == 0)
					{
						MakeWindowFocused(win);
						BringWindowToFront(win);
					}
					else
					{
						if (focusedWindow == win)
						{
							MakeWindowFocused(NULL);
						};
					};
					
					PostWindowDirty(win);
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_SET_LISTEN_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				listenWindow = GetWindowByID(cmd->setListenWindow.win, info.pid, info.fd);
				pthread_mutex_unlock(&windowLock);
			}
			else if (cmd->cmd == GWM_CMD_RESIZE)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->resize.win, info.pid, info.fd);

				GWMMessage msg;
				msg.resizeResp.type = GWM_MSG_RESIZE_RESP;
				msg.resizeResp.seq = cmd->resize.seq;
				
				if (win != NULL)
				{
					ResizeWindow(win, cmd->resize.width, cmd->resize.height);
					msg.resizeResp.status = 0;
					msg.resizeResp.clientID[0] = win->clientID[0];
					msg.resizeResp.clientID[1] = win->clientID[1];
					msg.resizeResp.width = win->params.width;
					msg.resizeResp.height = win->params.height;
				}
				else
				{
					msg.resizeResp.status = -1;
				};
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_MOVE)
			{
				pthread_mutex_lock(&windowLock);
				Window *win = GetWindowByID(cmd->move.win, info.pid, info.fd);
				
				if (win != NULL)
				{
					win->params.x = cmd->move.x;
					win->params.y = cmd->move.y;
				};
				
				pthread_mutex_unlock(&windowLock);
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_REL_TO_ABS)
			{
				pthread_mutex_lock(&windowLock);
				
				Window *win = GetWindowByID(cmd->relToAbs.win, info.pid, info.fd);
				// win is allowed to be NULL!
				
				GWMMessage msg;
				msg.relToAbsResp.type = GWM_MSG_REL_TO_ABS_RESP;
				msg.relToAbsResp.seq = cmd->relToAbs.seq;
				RelativeToAbsoluteCoords(win, cmd->relToAbs.relX, cmd->relToAbs.relY,
					&msg.relToAbsResp.absX, &msg.relToAbsResp.absY);
				
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			}
			else if (cmd->cmd == GWM_CMD_REDRAW_SCREEN)
			{
				sem_post(&semRedraw);
			}
			else if (cmd->cmd == GWM_CMD_SCREENSHOT_WINDOW)
			{
				pthread_mutex_lock(&windowLock);
				Window *subject = GetWindowByID(cmd->screenshotWindow.ref.id, cmd->screenshotWindow.ref.pid, cmd->screenshotWindow.ref.fd);
				if (subject == NULL)
				{
					GWMMessage msg;
					msg.screenshotWindowResp.type = GWM_MSG_SCREENSHOT_WINDOW_RESP;
					msg.screenshotWindowResp.status = -1;
					msg.screenshotWindowResp.seq = cmd->screenshotWindow.seq;
					pthread_mutex_unlock(&windowLock);
					_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
					continue;
				};

				if (subject->parent != NULL)
				{
					GWMMessage msg;
					msg.screenshotWindowResp.type = GWM_MSG_SCREENSHOT_WINDOW_RESP;
					msg.screenshotWindowResp.status = -1;
					msg.screenshotWindowResp.seq = cmd->screenshotWindow.seq;
					pthread_mutex_unlock(&windowLock);
					_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
					continue;
				};

				if (subject->params.flags & GWM_WINDOW_HIDDEN)
				{
					GWMMessage msg;
					msg.screenshotWindowResp.type = GWM_MSG_SCREENSHOT_WINDOW_RESP;
					msg.screenshotWindowResp.status = -1;
					msg.screenshotWindowResp.seq = cmd->screenshotWindow.seq;
					pthread_mutex_unlock(&windowLock);
					_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
					continue;
				};

				GWMWindowParams pars;
				strcpy(pars.caption, "<screenshot>");
				strcpy(pars.iconName, "<screenshot>");
				pars.flags = GWM_WINDOW_HIDDEN | GWM_WINDOW_NOTASKBAR;
				
				if (subject->params.flags & GWM_WINDOW_NODECORATE)
				{
					pars.width = subject->params.width;
					pars.height = subject->params.height;
				}
				else
				{
					pars.width = subject->params.width + 2 * GUI_WINDOW_BORDER;
					pars.height = subject->params.height + GUI_WINDOW_BORDER + GUI_CAPTION_HEIGHT;
				};

				Window *win = CreateWindow(0,
						&pars, cmd->screenshotWindow.id,
						info.pid, info.fd, info.pid);
				
				GWMMessage msg;
				msg.screenshotWindowResp.type = GWM_MSG_SCREENSHOT_WINDOW_RESP;
				msg.screenshotWindowResp.seq = cmd->screenshotWindow.seq;
				msg.screenshotWindowResp.status = 0;
				if (win == NULL) msg.screenshotWindowResp.status = 1;
				else
				{
					memcpy(&msg.screenshotWindowResp.format, &screen->format, sizeof(DDIPixelFormat));
					msg.screenshotWindowResp.clientID[0] = win->clientID[0];
					msg.screenshotWindowResp.clientID[1] = win->clientID[1];
					msg.screenshotWindowResp.width = win->params.width;
					msg.screenshotWindowResp.height = win->params.height;
					
					DDIColor transp = {0, 0, 0, 0};
					ddiFillRect(win->clientArea[0], 0, 0,
						win->clientArea[0]->width, win->clientArea[0]->height,
						&transp);

					int x = subject->params.x;
					int y = subject->params.y;
					subject->params.x = 0;
					subject->params.y = 0;
					PaintWindow(subject, win->clientArea[0]);
					subject->params.x = x;
					subject->params.y = y;
				};
				pthread_mutex_unlock(&windowLock);
				_glidix_mqsend(guiQueue, info.pid, info.fd, &msg, sizeof(GWMMessage));
			};
		}
		else if (info.type == _GLIDIX_MQ_HANGUP)
		{
			pthread_mutex_lock(&windowLock);
			DeleteWindowsOf(info.pid, info.fd);
			pthread_mutex_unlock(&windowLock);
			sem_post(&semRedraw);
		};
	};
};

int strStartsWith(const char *str, const char *prefix)
{
	if (strlen(str) < strlen(prefix))
	{
		return 0;
	};
	
	return memcmp(str, prefix, strlen(prefix)) == 0;
};

void onSig(int signo, siginfo_t *si, void *context)
{
	if (signo == SIGTERM)
	{
		_glidix_kopt(_GLIDIX_KOPT_GFXTERM, 1);
		exit(1);
	}
	else if (signo == SIGSEGV)
	{
		fprintf(stderr, "[GUI] ERROR! SIGSEGV caught!\n");
		exit(1);
	};
};

char dispdev[1024];
uint64_t requestRes;
char linebuf[1024];

int main(int argc, char *argv[])
{
	srand(time(NULL));

	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = onSig;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGTERM, &sa, NULL) != 0)
	{
		fprintf(stderr, "sigaction SIGTERM failed: %s\n", strerror(errno));
		return 1;
	};
	if (sigaction(SIGSEGV, &sa, NULL) != 0)
	{
		fprintf(stderr, "sigaction SIGSEGV failed: %s\n", strerror(errno));
		return 1;
	};
	
	if (geteuid() != 0)
	{
		fprintf(stderr, "you need to be root to start the window manager\n");
		return 1;
	};

	// make sure the clipboard and shared surface directories actually exist
	mkdir("/run/clipboard", 0777);
	mkdir("/run/shsurf", 0777);

	FILE *fp = fopen("/etc/gwm.conf", "r");
	if (fp == NULL)
	{
		fprintf(stderr, "could not open /etc/gwm.conf: %s\n", strerror(errno));
		return 1;
	};
	
	requestRes = DDI_RES_AUTO;
	dispdev[0] = 0;
	
	char *line;
	int lineno = 0;
	while ((line = fgets(linebuf, 1024, fp)) != NULL)
	{
		lineno++;
		
		char *endline = strchr(line, '\n');
		if (endline != NULL)
		{
			*endline = 0;
		};
		
		if (strlen(line) >= 1023)
		{
			fprintf(stderr, "/etc/gwm.conf:%d: buffer overflow\n", lineno);
			return 1;
		};
		
		if ((line[0] == 0) || (line[0] == '#'))
		{
			continue;
		}
		else
		{
			char *cmd = strtok(line, " \t");
			if (cmd == NULL)
			{
				continue;
			};
			
			if (strcmp(cmd, "display") == 0)
			{
				char *name = strtok(NULL, " \t");
				if (name == NULL)
				{
					fprintf(stderr, "/etc/gwm.conf:%d: 'display' needs a parameter\n", lineno);
					return 1;
				};
				
				strcpy(dispdev, name);
			}
			else if (strcmp(cmd, "resolution") == 0)
			{
				char *res = strtok(NULL, " \t");
				if (res == NULL)
				{
					fprintf(stderr, "/etc/gwm.conf:%d: 'resolution' needs a parameter\n", lineno);
					return 1;
				};
				
				uint64_t reqWidth, reqHeight;
				if (strcmp(res, "auto") == 0)
				{
					requestRes = DDI_RES_AUTO;
				}
				else if (strcmp(res, "safe") == 0)
				{
					requestRes = DDI_RES_SAFE;
				}
				else if (sscanf(res, "%lux%lu", &reqWidth, &reqHeight) == 2)
				{
					requestRes = DDI_RES_SPECIFIC(reqWidth, reqHeight);
				}
				else
				{
					fprintf(stderr, "/etc/gwm.conf:%d: invalid resolution: %s\n", lineno, res);
					return 1;
				};
			}
			else
			{
				fprintf(stderr, "/etc/gwm.conf:%d: invalid directive: %s\n", lineno, cmd);
				return 1;
			};
		};
	};
	fclose(fp);
	
	if (dispdev[0] == 0)
	{
		fprintf(stderr, "/etc/gwm.conf: no display device specified!\n");
		return 1;
	};

	if (ddiInit(dispdev, O_RDWR) != 0)
	{
		fprintf(stderr, "ddiInit: %s: %s\n", dispdev, strerror(errno));
		return 1;
	};

	const char *fontError;
	captionFont = ddiLoadFont("DejaVu Sans", 12, DDI_STYLE_BOLD, &fontError);
	if (captionFont == NULL)
	{
		fprintf(stderr, "Failed to load caption font: %s\n", fontError);
		return 1;
	};
	
	pthread_mutex_init(&windowLock, NULL);
	pthread_mutex_init(&mouseLock, NULL);
	sem_init(&semRedraw, 0, 0);
	_glidix_kopt(_GLIDIX_KOPT_GFXTERM, 0);
	
	frontBuffer = ddiSetVideoMode(requestRes);
	screenWidth = frontBuffer->width;
	screenHeight = frontBuffer->height;
	
	screen = ddiCreateSurface(&frontBuffer->format, screenWidth, screenHeight, NULL, 0);
	
	DDIColor backgroundColor = {0, 0, 0x77, 0xFF};
	desktopBackground = ddiCreateSurface(&screen->format, screenWidth, screenHeight, NULL, DDI_SHARED);
	ddiFillRect(desktopBackground, 0, 0, screenWidth, screenHeight, &backgroundColor);

	// initialize mouse cursor
	DDIColor mouseColor = {0xEE, 0xEE, 0xEE, 0xFF};

	int i;
	for (i=0; i<GWM_CURSOR_COUNT; i++)
	{
		cursors[i].image = ddiLoadAndConvertPNG(&screen->format, cursors[i].src, NULL);
		if (cursors[i].image == NULL)
		{
			cursors[i].image = ddiCreateSurface(&screen->format, 16, 16, NULL, 0);
			ddiFillRect(cursors[i].image, 0, 0, 16, 16, &mouseColor);
		};
	};
	
	mouseX = screenWidth / 2 - 8;
	mouseY = screenHeight / 2 - 8;
	
	// system images
	defWinIcon = ddiLoadAndConvertPNG(&screen->format, "/usr/share/images/defwinicon.png", NULL);
	if (defWinIcon == NULL)
	{
		defWinIcon = ddiCreateSurface(&screen->format, 16, 16, NULL, 0);
		ddiFillRect(defWinIcon, 0, 0, 16, 16, &mouseColor);
	};
	
	winButtons = ddiLoadAndConvertPNG(&screen->format, "/usr/share/images/winbuttons.png", NULL);
	if (winButtons == NULL)
	{
		winButtons = ddiCreateSurface(&screen->format, 48, 64, NULL, 0);
		ddiFillRect(winButtons, 0, 0, 48, 64, &mouseColor);
	};

	winCap = ddiLoadAndConvertPNG(&screen->format, "/usr/share/images/wincap.png", NULL);
	if (winCap == NULL)
	{
		winCap = ddiCreateSurface(&screen->format, 3, GUI_CAPTION_HEIGHT, NULL, 0);
		ddiFillRect(winCap, 0, 0, 3, GUI_CAPTION_HEIGHT, &winDecoColor);
	};
	
	// for window borders
	winDeco = ddiCreateSurface(&screen->format, screenWidth, screenHeight, NULL, 0);
	ddiFillRect(winDeco, 0, 0, screenWidth, screenHeight, &winDecoColor);
	winUnfoc = ddiCreateSurface(&screen->format, screenWidth, screenHeight, NULL, 0);
	ddiFillRect(winUnfoc, 0, 0, screenWidth, screenHeight, &winUnfocColor);

	// message queue
	guiQueue = _glidix_mqserver();
	if (guiQueue == -1)
	{
		fprintf(stderr, "Failed to open GUI server!\n");
		return 1;
	};

	// GWM information
	int fd = open("/run/gwminfo", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
	{
		fprintf(stderr, "Failed to open /run/gwminfo! %s\n", strerror(errno));
		return 1;
	};
	
	ftruncate(fd, sizeof(GWMInfo));
	gwminfo = (GWMInfo*) mmap(NULL, sizeof(GWMInfo), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	gwminfo->pid = getpid();
	gwminfo->fd = guiQueue;
	gwminfo->backgroundID = desktopBackground->id;

	// threads
	if (pthread_create(&inputThread, NULL, inputThreadFunc, NULL) != 0)
	{
		fprintf(stderr, "failed to create input thread!\n");
		return 1;
	};

	if (pthread_create(&ptrThread, NULL, ptrThreadFunc, NULL) != 0)
	{
		fprintf(stderr, "failed to create pointer device thread!\n");
		return 1;
	};
	
	if (pthread_create(&msgThread, NULL, msgThreadFunc, NULL) != 0)
	{
		fprintf(stderr, "failed to create server thread!\n");
		return 1;
	};
	
	pid_t child = fork();
	if (child == -1)
	{
		perror("fork");
		return 1;
	};
	
	if (child == 0)
	{
		execl("/usr/bin/gui-init", "gui-init", NULL);
		return 1;
	}
	else
	{
		waitpid(child, NULL, WNOHANG | WDETACH);
	};

	redrawThread = pthread_self();
	while (1)
	{
		PaintDesktop();
		
		while (sem_wait(&semRedraw) != 0);
		
		// ignore multiple simultaneous redraw requests
		int val;
		sem_getvalue(&semRedraw, &val);
		while ((val--) > 0)
		{
			sem_wait(&semRedraw);
		};
	};

	return 0;
};
