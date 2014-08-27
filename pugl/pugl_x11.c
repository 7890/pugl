/*
  Copyright 2012-2014 David Robillard <http://drobilla.net>
  Copyright 2013 Robin Gareus <robin@gareus.org>
  Copyright 2011-2012 Ben Loftis, Harrison Consoles

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/**
   @file pugl_x11.c X11 Pugl Implementation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef PUGL_HAVE_GL
#include <GL/gl.h>
#include <GL/glx.h>
#endif

#ifdef PUGL_HAVE_CAIRO
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#endif

#include "pugl_internal.h"

struct PuglInternalsImpl {
	Display*   display;
	int        screen;
	Window     win;
#ifdef PUGL_HAVE_CAIRO
	cairo_t*   cr;
#endif
#ifdef PUGL_HAVE_GL
	GLXContext ctx;
	Bool       doubleBuffered;
#endif
};

PuglInternals*
puglInitInternals()
{
	return (PuglInternals*)calloc(1, sizeof(PuglInternals));
}

int
puglCreateWindow(PuglView* view, const char* title)
{
	PuglInternals* impl = view->impl;

	impl->display = XOpenDisplay(0);
	impl->screen  = DefaultScreen(impl->display);

	XVisualInfo* vi = NULL;

#ifdef PUGL_HAVE_GL
	if (view->ctx_type == PUGL_GL) {
		// Try to create double-buffered visual
		int double_attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER,
		                       GLX_RED_SIZE,   4,
		                       GLX_GREEN_SIZE, 4,
		                       GLX_BLUE_SIZE,  4,
		                       GLX_DEPTH_SIZE, 16,
		                       None };
		vi = glXChooseVisual(impl->display, impl->screen, double_attrs);
		if (!vi) {
			// Failed, create single-buffered visual
			int single_attrs[] = { GLX_RGBA,
			                       GLX_RED_SIZE,   4,
			                       GLX_GREEN_SIZE, 4,
			                       GLX_BLUE_SIZE,  4,
			                       GLX_DEPTH_SIZE, 16,
			                       None };
			vi = glXChooseVisual(impl->display, impl->screen, single_attrs);
			impl->doubleBuffered = False;
		} else {
			impl->doubleBuffered = True;
		}
		
		impl->ctx = glXCreateContext(impl->display, vi, 0, GL_TRUE);
		PUGL_LOGF("GLX depth %d, %s-buffered, %s\n",
		          vi->depth,
		          impl->doubleBuffered ? "double" : "single",
		          (glXIsDirect(impl->display, impl->ctx)
		           ? "direct (set LIBGL_ALWAYS_INDIRECT=1 to disable)"
		           : "indirect"));
	}
#endif
#ifdef PUGL_HAVE_CAIRO
	if (view->ctx_type == PUGL_CAIRO) {
		XVisualInfo pat;
		int         n;
		pat.screen = impl->screen;
		vi         = XGetVisualInfo(impl->display, VisualScreenMask, &pat, &n);
	}
#endif

	Window xParent = view->parent
		? (Window)view->parent
		: RootWindow(impl->display, impl->screen);

	Colormap cmap = XCreateColormap(
		impl->display, xParent, vi->visual, AllocNone);

	XSetWindowAttributes attr;
	memset(&attr, 0, sizeof(XSetWindowAttributes));
	attr.background_pixel = BlackPixel(impl->display, impl->screen);
	attr.border_pixel     = BlackPixel(impl->display, impl->screen);
	attr.colormap         = cmap;
	attr.event_mask       = ExposureMask | KeyPressMask | KeyReleaseMask
		| ButtonPressMask | ButtonReleaseMask
#ifdef PUGL_GRAB_FOCUS
		| EnterWindowMask
#endif
		| PointerMotionMask | StructureNotifyMask;

	impl->win = XCreateWindow(
		impl->display, xParent,
		0, 0, view->width, view->height, 0, vi->depth, InputOutput, vi->visual,
		CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &attr);

#ifdef PUGL_HAVE_CAIRO
	if (view->ctx_type == PUGL_CAIRO) {
		cairo_surface_t* surface = cairo_xlib_surface_create(
			impl->display, impl->win, vi->visual, view->width, view->height);
		if (!(impl->cr = cairo_create(surface))) {
			fprintf(stderr, "failed to create cairo context\n");
		}
	}
#endif

	XSizeHints sizeHints;
	memset(&sizeHints, 0, sizeof(sizeHints));
	if (!view->resizable) {
		sizeHints.flags      = PMinSize|PMaxSize;
		sizeHints.min_width  = view->width;
		sizeHints.min_height = view->height;
		sizeHints.max_width  = view->width;
		sizeHints.max_height = view->height;
		XSetNormalHints(impl->display, impl->win, &sizeHints);
	}

	if (title) {
		XStoreName(impl->display, impl->win, title);
	}

	if (!view->parent) {
		Atom wmDelete = XInternAtom(impl->display, "WM_DELETE_WINDOW", True);
		XSetWMProtocols(impl->display, impl->win, &wmDelete, 1);
	}

	XFree(vi);

	return 0;
}

void
puglShowWindow(PuglView* view)
{
	PuglInternals* impl = view->impl;

	XMapRaised(impl->display, impl->win);
}

void
puglHideWindow(PuglView* view)
{
	PuglInternals* impl = view->impl;

	XUnmapWindow(impl->display, impl->win);
}

void
puglDestroy(PuglView* view)
{
	if (!view) {
		return;
	}

#ifdef PUGL_HAVE_GL
	if (view->ctx_type == PUGL_GL) {
		glXDestroyContext(view->impl->display, view->impl->ctx);
	}
#endif

	XDestroyWindow(view->impl->display, view->impl->win);
	XCloseDisplay(view->impl->display);
	free(view->impl);
	free(view);
}

static void
puglReshape(PuglView* view, int width, int height)
{
#ifdef PUGL_HAVE_GL
	if (view->ctx_type == PUGL_GL) {
		glXMakeCurrent(view->impl->display, view->impl->win, view->impl->ctx);
	}
#endif

	if (view->reshapeFunc) {
		view->reshapeFunc(view, width, height);
	} else {
		puglDefaultReshape(view, width, height);
	}

	view->width  = width;
	view->height = height;
}

static void
puglDisplay(PuglView* view)
{
#ifdef PUGL_HAVE_GL
	if (view->ctx_type == PUGL_GL) {
		glXMakeCurrent(view->impl->display, view->impl->win, view->impl->ctx);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadIdentity();
	}
#endif

	if (view->displayFunc) {
		view->displayFunc(view);
	}
	view->redisplay = false;

#ifdef PUGL_HAVE_GL
	if (view->ctx_type == PUGL_GL) {
		glFlush();
		if (view->impl->doubleBuffered) {
			glXSwapBuffers(view->impl->display, view->impl->win);
		}
	}
#endif
}

static PuglKey
keySymToSpecial(KeySym sym)
{
	switch (sym) {
	case XK_F1:        return PUGL_KEY_F1;
	case XK_F2:        return PUGL_KEY_F2;
	case XK_F3:        return PUGL_KEY_F3;
	case XK_F4:        return PUGL_KEY_F4;
	case XK_F5:        return PUGL_KEY_F5;
	case XK_F6:        return PUGL_KEY_F6;
	case XK_F7:        return PUGL_KEY_F7;
	case XK_F8:        return PUGL_KEY_F8;
	case XK_F9:        return PUGL_KEY_F9;
	case XK_F10:       return PUGL_KEY_F10;
	case XK_F11:       return PUGL_KEY_F11;
	case XK_F12:       return PUGL_KEY_F12;
	case XK_Left:      return PUGL_KEY_LEFT;
	case XK_Up:        return PUGL_KEY_UP;
	case XK_Right:     return PUGL_KEY_RIGHT;
	case XK_Down:      return PUGL_KEY_DOWN;
	case XK_Page_Up:   return PUGL_KEY_PAGE_UP;
	case XK_Page_Down: return PUGL_KEY_PAGE_DOWN;
	case XK_Home:      return PUGL_KEY_HOME;
	case XK_End:       return PUGL_KEY_END;
	case XK_Insert:    return PUGL_KEY_INSERT;
	case XK_Shift_L:   return PUGL_KEY_SHIFT;
	case XK_Shift_R:   return PUGL_KEY_SHIFT;
	case XK_Control_L: return PUGL_KEY_CTRL;
	case XK_Control_R: return PUGL_KEY_CTRL;
	case XK_Alt_L:     return PUGL_KEY_ALT;
	case XK_Alt_R:     return PUGL_KEY_ALT;
	case XK_Super_L:   return PUGL_KEY_SUPER;
	case XK_Super_R:   return PUGL_KEY_SUPER;
	}
	return (PuglKey)0;
}

static void
setModifiers(PuglView* view, unsigned xstate, unsigned xtime)
{
	view->event_timestamp_ms = xtime;

	view->mods = 0;
	view->mods |= (xstate & ShiftMask)   ? PUGL_MOD_SHIFT  : 0;
	view->mods |= (xstate & ControlMask) ? PUGL_MOD_CTRL   : 0;
	view->mods |= (xstate & Mod1Mask)    ? PUGL_MOD_ALT    : 0;
	view->mods |= (xstate & Mod4Mask)    ? PUGL_MOD_SUPER  : 0;
}

static void
dispatchKey(PuglView* view, XEvent* event, bool press)
{
	KeySym    sym;
	char      str[5];
	const int n = XLookupString(&event->xkey, str, 4, &sym, NULL);
	if (n == 0) {
		return;
	} else if (n > 1) {
		fprintf(stderr, "warning: Unsupported multi-byte key %X\n", (int)sym);
		return;
	}

	const PuglKey special = keySymToSpecial(sym);
	if (special && view->specialFunc) {
		view->specialFunc(view, press, special);
	} else if (!special && view->keyboardFunc) {
		view->keyboardFunc(view, press, str[0]);
	}
}

PuglStatus
puglProcessEvents(PuglView* view)
{
	XEvent event;
	while (XPending(view->impl->display) > 0) {
		XNextEvent(view->impl->display, &event);
		switch (event.type) {
		case MapNotify:
			puglReshape(view, view->width, view->height);
			break;
		case ConfigureNotify:
			if ((event.xconfigure.width != view->width) ||
			    (event.xconfigure.height != view->height)) {
				puglReshape(view,
				            event.xconfigure.width,
				            event.xconfigure.height);
			}
			break;
		case Expose:
			if (event.xexpose.count != 0) {
				break;
			}
			puglDisplay(view);
			break;
		case MotionNotify:
			setModifiers(view, event.xmotion.state, event.xmotion.time);
			if (view->motionFunc) {
				view->motionFunc(view, event.xmotion.x, event.xmotion.y);
			}
			break;
		case ButtonPress:
			setModifiers(view, event.xbutton.state, event.xbutton.time);
			if (event.xbutton.button >= 4 && event.xbutton.button <= 7) {
				if (view->scrollFunc) {
					float dx = 0, dy = 0;
					switch (event.xbutton.button) {
					case 4: dy =  1.0f; break;
					case 5: dy = -1.0f; break;
					case 6: dx = -1.0f; break;
					case 7: dx =  1.0f; break;
					}
					view->scrollFunc(view,
					                 event.xbutton.x, event.xbutton.y,
					                 dx, dy);
				}
				break;
			}
			// nobreak
		case ButtonRelease:
			setModifiers(view, event.xbutton.state, event.xbutton.time);
			if (view->mouseFunc &&
			    (event.xbutton.button < 4 || event.xbutton.button > 7)) {
				view->mouseFunc(view,
				                event.xbutton.button, event.type == ButtonPress,
				                event.xbutton.x, event.xbutton.y);
			}
			break;
		case KeyPress:
			setModifiers(view, event.xkey.state, event.xkey.time);
			dispatchKey(view, &event, true);
			break;
		case KeyRelease:
			setModifiers(view, event.xkey.state, event.xkey.time);
			if (view->ignoreKeyRepeat &&
			    XEventsQueued(view->impl->display, QueuedAfterReading)) {
				XEvent next;
				XPeekEvent(view->impl->display, &next);
				if (next.type == KeyPress &&
				    next.xkey.time == event.xkey.time &&
				    next.xkey.keycode == event.xkey.keycode) {
					XNextEvent(view->impl->display, &event);
					break;
				}
			}
			dispatchKey(view, &event, false);
			break;
		case ClientMessage: {
			char* type = XGetAtomName(view->impl->display,
			                          event.xclient.message_type);
			if (!strcmp(type, "WM_PROTOCOLS")) {
				if (view->closeFunc) {
					view->closeFunc(view);
				}
			}
			XFree(type);
		} break;
#ifdef PUGL_GRAB_FOCUS
		case EnterNotify:
			XSetInputFocus(view->impl->display,
			               view->impl->win,
			               RevertToPointerRoot,
			               CurrentTime);
			break;
#endif
		default:
			break;
		}
	}

	if (view->redisplay) {
		puglDisplay(view);
	}

	return PUGL_SUCCESS;
}

void
puglPostRedisplay(PuglView* view)
{
	view->redisplay = true;
}

PuglNativeWindow
puglGetNativeWindow(PuglView* view)
{
	return view->impl->win;
}

void*
puglGetContext(PuglView* view)
{
#ifdef PUGL_HAVE_CAIRO
	if (view->ctx_type == PUGL_CAIRO) {
		return view->impl->cr;
	}
#endif
	return NULL;
}
