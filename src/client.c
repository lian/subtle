
 /**
	* subtle - window manager
	* Copyright (c) 2005-2008 Christoph Kappel
	*
	* See the COPYING file for the license in the latest tarball.
	*
	* $Id$
	**/

#include "subtle.h"

static int nclients = 0;
static Window *clients = NULL;

 /** subClientNew {{{
	* Create new client
	* @param[in] win Main window of the new client
	* @return Returns either a #SubClient on success or otherwise NULL
	**/

SubClient *
subClientNew(Window win)
{
	int i, n = 0;
	Window unnused;
	XWMHints *hints = NULL;
	XWindowAttributes attr;
	XSetWindowAttributes attrs;
	Atom *protos = NULL;
	SubClient *c = NULL;

	assert(win);
	
	c	= (SubClient *)subUtilAlloc(1, sizeof(SubClient));
	c->y 			= d->th;
	c->width	= DisplayWidth(d->disp, DefaultScreen(d->disp));
	c->height	= DisplayHeight(d->disp, DefaultScreen(d->disp)) - d->th;
	c->win		= win;

	XSelectInput(d->disp, c->win, PropertyChangeMask|StructureNotifyMask);
	XSetWindowBorderWidth(d->disp, c->win, 0);
	XReparentWindow(d->disp, c->win, c->frame, d->bw, d->th);
	XAddToSaveSet(d->disp, c->win);

	/* Create windows */
	attrs.border_pixel			= d->colors.border;
	attrs.background_pixel	= d->colors.norm;
	attrs.background_pixmap	= ParentRelative;
	attrs.save_under				= False;
	attrs.event_mask				= KeyPressMask|ButtonPressMask|EnterWindowMask|LeaveWindowMask|ExposureMask|
		VisibilityChangeMask|FocusChangeMask|SubstructureRedirectMask|SubstructureNotifyMask;

	c->frame			= SUBWINNEW(DefaultRootWindow(d->disp), c->x, c->y, c->width, c->height, 0, 
		CWBackPixmap|CWSaveUnder|CWEventMask);
	c->title			= SUBWINNEW(c->frame, 0, 0, c->width, d->th, 0, CWBackPixel);
	c->caption		= SUBWINNEW(c->frame, 0, 0, 1, d->th, 0, CWBackPixel);
	attrs.cursor	= d->cursors.horz;
	c->left				= SUBWINNEW(c->frame, 0, d->th, d->bw, c->height - d->th, 0, CWBackPixel|CWCursor);
	c->right			= SUBWINNEW(c->frame, c->width - d->bw, d->th, d->bw, c->height - d->th, 0, CWBackPixel|CWCursor);
	attrs.cursor	= d->cursors.vert;
	c->bottom			= SUBWINNEW(c->frame, 0, c->height - d->bw, c->width, d->bw, 0, CWBackPixel|CWCursor);

	/* Window attributes */
	XGetWindowAttributes(d->disp, c->win, &attr);
	c->cmap	= attr.colormap;

	/* EWMH: Client list and client list stacking */
	subClientFetchName(c);
	clients = (Window *)subUtilRealloc(clients, sizeof(Window) * (nclients + 1));
	clients[nclients++] = c->win;
	subEwmhSetWindows(DefaultRootWindow(d->disp), SUB_EWMH_NET_CLIENT_LIST, clients, nclients);
	subEwmhSetWindows(DefaultRootWindow(d->disp), SUB_EWMH_NET_CLIENT_LIST_STACKING, clients, nclients);

	/* Window manager hints */
	hints = XGetWMHints(d->disp, win);
	if(hints)
		{
			if(hints->flags & StateHint) subClientSetWMState(c, hints->initial_state);
			else subClientSetWMState(c, NormalState);
			if(hints->input) c->flags |= SUB_CLIENT_PREF_INPUT;
			if(hints->flags & XUrgencyHint) c->flags |= SUB_CLIENT_STATE_TRANS;
			XFree(hints);
		}
	
	/* Protocols */
	if(XGetWMProtocols(d->disp, c->win, &protos, &n))
		{
			for(i = 0; i < n; i++)
				{
					if(protos[i] == subEwmhFind(SUB_EWMH_WM_TAKE_FOCUS)) 		c->flags |= SUB_CLIENT_PREF_FOCUS;
					if(protos[i] == subEwmhFind(SUB_EWMH_WM_DELETE_WINDOW))	c->flags |= SUB_CLIENT_PREF_CLOSE;
				}
			XFree(protos);
		}

	/* Check for dialog windows etc. */
	XGetTransientForHint(d->disp, win, &unnused);
	if(unnused && !(c->flags & SUB_CLIENT_STATE_TRANS)) c->flags |= SUB_CLIENT_STATE_TRANS;

	return(c);
}
/* }}} */

 /** subClientDelete {{{
	* Send interested clients the close signal and delete it
	* @param[in] c A #SubClient
	**/

void
subClientDelete(SubClient *c)
{
	int i, j;

	assert(c);

	/* Update client list */
	for(i = 0; i < nclients; i++) 	
		if(clients[i] == c->win)
			{
				for(j = i; j < nclients - 1; j++) clients[j] = clients[j + 1];
				break; /* Leave loop */
			}
	clients = (Window *)subUtilRealloc(clients, sizeof(Window) * nclients);
	nclients--;

	/* EWMH: Client list and client list stacking */			
	subEwmhSetWindows(DefaultRootWindow(d->disp), SUB_EWMH_NET_CLIENT_LIST, clients, nclients);
	subEwmhSetWindows(DefaultRootWindow(d->disp), SUB_EWMH_NET_CLIENT_LIST_STACKING, clients, nclients);					

	if(!(c->flags & SUB_CLIENT_STATE_DEAD))
		{
			/* Honor window preferences */
			if(c->flags & SUB_CLIENT_PREF_CLOSE)
				{
					XEvent ev;

					ev.type									= ClientMessage;
					ev.xclient.window				= c->win;
					ev.xclient.message_type = subEwmhFind(SUB_EWMH_WM_PROTOCOLS);
					ev.xclient.format				= 32;
					ev.xclient.data.l[0]		= subEwmhFind(SUB_EWMH_WM_DELETE_WINDOW);
					ev.xclient.data.l[1]		= CurrentTime;

					XSendEvent(d->disp, c->win, False, NoEventMask, &ev);
				}
			else XKillClient(d->disp, c->win);
		}

	if(c->name) XFree(c->name);
	free(c);
}
/* }}} */

 /** subClientRender {{{
	* Render client and redraw titlebar and borders
	* @param[in] C A #SubClient
	**/

void
subClientRender(SubClient *c)
{
	unsigned long col = 0;

	assert(c);

	/* Check if client was meanwhile destroyed */
	if(c->flags & SUB_CLIENT_STATE_DEAD) return;

	col = d->focus && d->focus == c ? d->colors.focus : (c->flags & SUB_CLIENT_STATE_SHADE ? d->colors.cover : d->colors.norm);

	/* Update color */
	XSetWindowBackground(d->disp, c->title,		col);
	XSetWindowBackground(d->disp, c->left, 		col);
	XSetWindowBackground(d->disp, c->right, 	col);
	XSetWindowBackground(d->disp, c->bottom,	col);
	XSetWindowBackground(d->disp, c->caption, col);

	/* Clear windows */
	XClearWindow(d->disp, c->title);
	XClearWindow(d->disp, c->left);
	XClearWindow(d->disp, c->right);
	XClearWindow(d->disp, c->bottom);
	XClearWindow(d->disp, c->caption);

	/* Titlebar */
	XFillRectangle(d->disp, c->title, d->gcs.border, d->th + 1, 2, c->width - d->th - 4, d->th - 4);	
	XDrawString(d->disp, c->caption, d->gcs.font, 5, d->fy - 1, c->name, strlen(c->name));
}
/* }}} */

 /** subClientFocus {{{
	* Set or unset focus to client
	* @param[in] c A #SubClient
	**/

void
subClientFocus(SubClient *c)
{
	assert(c);

	if(c->flags & SUB_CLIENT_PREF_FOCUS && !(c->flags & SUB_CLIENT_STATE_SHADE))
		{
			XEvent ev;
  
			ev.type                 = ClientMessage;
			ev.xclient.window       = c->win;
			ev.xclient.message_type = subEwmhFind(SUB_EWMH_WM_PROTOCOLS);
			ev.xclient.format       = 32;
			ev.xclient.data.l[0]    = subEwmhFind(SUB_EWMH_WM_TAKE_FOCUS);
			ev.xclient.data.l[1]    = CurrentTime;
      
			XSendEvent(d->disp, c->win, False, NoEventMask, &ev);
      
			subUtilLogDebug("Focus: win=%#lx, input=%d, send=%d\n", c->win,      
				!!(c->flags & SUB_CLIENT_PREF_INPUT), !!(c->flags & SUB_CLIENT_PREF_FOCUS));     
		}   
	else
		{
			/* Remove focus from client */
			if(d->focus && d->focus != c) 
				{
					SubClient *f = d->focus;
					d->focus = NULL;
					if(f) subClientRender(f);
          
					subKeyUngrab(f);
				} 
        
			XSetInputFocus(d->disp, c->frame, RevertToNone, CurrentTime);
      
			d->focus = c;
			subClientRender(c);
			subEwmhSetWindows(DefaultRootWindow(d->disp), SUB_EWMH_NET_ACTIVE_WINDOW, &c->frame, 1);
			subKeyGrab(c);
		}
}
/* }}} */

 /** subClientResize {{{
  * Resize clients to the stored values
  * @param[in] c A #SubClient
  **/

void
subClientResize(SubClient *c)
{
	assert(c);

	XMoveResizeWindow(d->disp, c->frame, c->x, c->y, c->width, (c->flags & SUB_CLIENT_STATE_SHADE) ? d->th : c->height);

	if(c->flags & SUB_CLIENT_STATE_FULL) XMoveResizeWindow(d->disp, c->win, 0, 0, c->width, c->height);
	else if(!(c->flags & SUB_CLIENT_STATE_SHADE))
		{
			XMoveResizeWindow(d->disp, c->win, d->bw, d->th, c->width - 2 * d->bw, c->height - d->th - d->bw);
			XMoveResizeWindow(d->disp, c->right, c->width - d->bw, d->th, d->bw, c->height - d->th);
			XMoveResizeWindow(d->disp, c->bottom, 0, c->height - d->bw, c->width, d->bw);
		}
	else XMoveResizeWindow(d->disp, c->title, 0, 0, c->width, d->th);
	if(!(c->flags & SUB_CLIENT_STATE_SHADE)) subClientConfigure(c);
}
/* }}} */

 /** subClientMap {{{
	* Map client with all subwindows to screen
	* @param[in] c A #SubClient
	**/

void
subClientMap(SubClient *c)
{
	assert(c);

	XMapSubwindows(d->disp, c->frame);
	XMapWindow(d->disp, c->frame);
}
/* }}} */

 /** subClientUnmap {{{
	* Unmap client with all subClientUnmap from screen
	* @param[in] c A #SubClient
	**/

void
subClientUnmap(SubClient *c)
{
	assert(c);

	XUnmapSubwindows(d->disp, c->frame);
	XUnmapWindow(d->disp, c->frame);
}
/* }}} */

/* DrawMask {{{ */
static void
DrawMask(int type,
	SubClient *c,
	XRectangle *r)
{
	/* XXX: Put modifiers into a static table? */
	switch(type)
		{
			case SUB_CLIENT_DRAG_STATE_START: 
				XDrawRectangle(d->disp, DefaultRootWindow(d->disp), d->gcs.invert, r->x + 1, r->y + 1, 
					r->width - 3, (c->flags & SUB_CLIENT_STATE_SHADE) ? d->th - d->bw : r->height - d->bw);
				break;
			case SUB_CLIENT_DRAG_STATE_TOP:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, 5, c->width - 10, c->height * 0.5 - 5);
				break;
			case SUB_CLIENT_DRAG_STATE_BOTTOM:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, c->height * 0.5, c->width - 10, c->height * 0.5 - 5);
				break;
			case SUB_CLIENT_DRAG_STATE_LEFT:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, 5, c->width * 0.5 - 5, c->height - 10);
				break;
			case SUB_CLIENT_DRAG_STATE_RIGHT:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, c->width * 0.5, 5, c->width * 0.5 - 5, c->height - 10);
				break;
			case SUB_CLIENT_DRAG_STATE_SWAP:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, c->width * 0.35, c->height * 0.35, c->width * 0.3, c->height * 0.3);
				break;
			case SUB_CLIENT_DRAG_STATE_BEFORE:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, 5, c->width * 0.1 - 5, c->height - 10);
				break;
			case SUB_CLIENT_DRAG_STATE_AFTER:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, c->width * 0.9, 5, c->width * 0.1 - 5, c->height - 10);
				break;
			case SUB_CLIENT_DRAG_STATE_ABOVE:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, 5, c->width - 10, c->height * 0.1 - 5);
				break;
			case SUB_CLIENT_DRAG_STATE_BELOW:
				XFillRectangle(d->disp, c->frame, d->gcs.invert, 5, c->height * 0.9, c->width - 10, c->height * 0.1 - 5);
				break;
		}
}
/* }}} */

/* AdjustWeight {{{ */
static void
AdjustWeight(int mode,
	SubClient *c,
	XRectangle *r)
{
	if(c && c->tile)
		{
			if((c->tile->flags & SUB_TILE_TYPE_HORZ && (mode == SUB_CLIENT_DRAG_LEFT || mode == SUB_CLIENT_DRAG_RIGHT)) ||
				(c->tile->flags & SUB_TILE_TYPE_VERT && mode == SUB_CLIENT_DRAG_BOTTOM))
				{
					if(c->tile->flags & SUB_TILE_TYPE_HORZ) c->resized = r->width * 100 / SUBWINWIDTH(c->tile);
					else if(c->tile->flags & SUB_TILE_TYPE_VERT) c->resized = r->height * 100 / SUBWINHEIGHT(c->tile);

					if(c->resized >= 80) c->resized = 80;
					if(!(c->flags & SUB_CLIENT_STATE_RESIZE)) c->flags |= SUB_CLIENT_STATE_RESIZE;
					subTileConfigure(c->tile);
					return;
				}
/*			else if(((c->tile->flags & SUB_TILE_TYPE_VERT && (mode == SUB_CLIENT_DRAG_LEFT || mode == SUB_CLIENT_DRAG_RIGHT)) ||
				(c->tile->flags & SUB_TILE_TYPE_HORZ && mode == SUB_CLIENT_DRAG_BOTTOM)) && c->tile->tile)
				AdjustWeight(mode, c->tile->clients[0], r); */
		}
}
/* }}} */

 /** subClientDrag {{{
	* Move and/or drag client
	* @param[in] c A #SubClient
	* @param[in] mode Drag/move mode
	**/

void
subClientDrag(SubClient *c,
	int mode)
{
	XEvent ev;
	Cursor cursor;
	Window win, unused;
	unsigned int mask;
	int wx = 0, wy = 0, rx = 0, ry = 0, state = SUB_CLIENT_DRAG_STATE_START, last_state = SUB_CLIENT_DRAG_STATE_START;
	XRectangle r;
	SubClient *c2 = NULL, *last_c = NULL;
	//SubTile *p = NULL;

	assert(c);
	
	/* Get window position on root window */
	XQueryPointer(d->disp, c->frame, &win, &win, &rx, &ry, &wx, &wy, &mask);
	r.x				= rx - wx;
	r.y				= ry - wy;
	r.width 	= c->width;
	r.height	= c->height;

	/* Select cursor */
	switch(mode)
		{
			case SUB_CLIENT_DRAG_LEFT:		
			case SUB_CLIENT_DRAG_RIGHT:		cursor = d->cursors.horz;		break;
			case SUB_CLIENT_DRAG_BOTTOM:	cursor = d->cursors.vert;		break;
			case SUB_CLIENT_DRAG_MOVE:		cursor = d->cursors.move;		break;
			default:											cursor = d->cursors.square;	break;
		}

	if(XGrabPointer(d->disp, c->frame, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, 
		GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime)) return;

	XGrabServer(d->disp);
	if(mode <= SUB_CLIENT_DRAG_MOVE) DrawMask(SUB_CLIENT_DRAG_STATE_START, c, &r);

	for(;;)
		{
			XMaskEvent(d->disp, PointerMotionMask|ButtonReleaseMask|EnterWindowMask, &ev);
			switch(ev.type)
				{
					/* Button release doesn't return our destination window */
					case EnterNotify: win = ev.xcrossing.window; break;
					case MotionNotify:
						if(mode <= SUB_CLIENT_DRAG_MOVE) 
							{
								DrawMask(SUB_CLIENT_DRAG_STATE_START, c, &r);
					
								/* Calculate dimensions of the selection rect */
								switch(mode)
									{
										case SUB_CLIENT_DRAG_LEFT: 	
											r.x			= (rx - wx) - (rx - ev.xmotion.x_root);	
											r.width = c->width + ((rx - wx ) - ev.xmotion.x_root);	
											break;
										case SUB_CLIENT_DRAG_RIGHT:	r.width		= c->width + (ev.xmotion.x_root - rx);	break;
										case SUB_CLIENT_DRAG_BOTTOM: r.height	= c->height + (ev.xmotion.y_root - ry);	break;
										case SUB_CLIENT_DRAG_MOVE:
											r.x	= (rx - wx) - (rx - ev.xmotion.x_root);
											r.y	= (ry - wy) - (ry - ev.xmotion.y_root);
											break;
									}	
								DrawMask(SUB_CLIENT_DRAG_STATE_START, c, &r);
							}
						else if(win != c->frame && mode == SUB_CLIENT_DRAG_SWAP)
							{
								if(!c2 || c2->frame != win) c2 = (SubClient *)subUtilFind(win, 1);
								if(c2)
									{
										XQueryPointer(d->disp, win, &unused, &unused, &rx, &ry, &wx, &wy, &mask);
										r.x = rx - wx;
										r.y = ry - wy;

										/* Change drag state */
										if(wx > c2->width * 0.35 && wx < c2->width * 0.65)
											{
												if(state != SUB_CLIENT_DRAG_STATE_TOP && wy > c2->height * 0.1 && wy < c2->height * 0.35)
													state = SUB_CLIENT_DRAG_STATE_TOP;
												else if(state != SUB_CLIENT_DRAG_STATE_BOTTOM && wy > c2->height * 0.65 && wy < c2->height * 0.9)
													state = SUB_CLIENT_DRAG_STATE_BOTTOM;
												else if(state != SUB_CLIENT_DRAG_STATE_SWAP && wy > c2->height * 0.35 && wy < c2->height * 0.65)
													state = SUB_CLIENT_DRAG_STATE_SWAP;
											}
										if(state != SUB_CLIENT_DRAG_STATE_ABOVE && wy < c2->height * 0.1) state = SUB_CLIENT_DRAG_STATE_ABOVE;
										else if(state != SUB_CLIENT_DRAG_STATE_BELOW && wy > c2->height * 0.9) state = SUB_CLIENT_DRAG_STATE_BELOW;
										if(wy > c2->height * 0.1 && wy < c2->height * 0.9)
											{
												if(state != SUB_CLIENT_DRAG_STATE_LEFT && wx > c2->width * 0.1 && wx < c2->width * 0.35)
													state = SUB_CLIENT_DRAG_STATE_LEFT;
												else if(state != SUB_CLIENT_DRAG_STATE_RIGHT && wx > c2->width * 0.65 && wx < c2->width * 0.9)
													state = SUB_CLIENT_DRAG_STATE_RIGHT;
												else if(state != SUB_CLIENT_DRAG_STATE_BEFORE && wx < c2->width * 0.1)
													state = SUB_CLIENT_DRAG_STATE_BEFORE;
												else if(state != SUB_CLIENT_DRAG_STATE_AFTER && wx > c2->width * 0.9)
													state = SUB_CLIENT_DRAG_STATE_AFTER;
											}

										if(last_state != state || last_c != c2) 
											{
												if(last_state != SUB_CLIENT_DRAG_STATE_START) DrawMask(last_state, last_c, &r);
												DrawMask(state, c2, &r);

												last_c		 = c2;
												last_state = state;
											}
										}
								}
						break;
					case ButtonRelease:
						if(win != c->frame && mode > SUB_CLIENT_DRAG_MOVE)
							{
								DrawMask(state, c2, &r); /* Erase mask */
#if 0 /* Drag actions {{{ */
								if(c && c2 && c->tile && c2->parent)
									{
										if(state == SUB_CLIENT_DRAG_STATE_TOP || state == SUB_CLIENT_DRAG_STATE_BOTTOM ||
											state == SUB_CLIENT_DRAG_STATE_LEFT || state == SUB_CLIENT_DRAG_STATE_RIGHT)
											{
												SubWin *t = subTileNew(state == SUB_CLIENT_DRAG_STATE_TOP || 
													state == SUB_CLIENT_DRAG_STATE_BOTTOM ? SUB_TILE_TYPE_VERT : SUB_TILE_TYPE_HORZ);
												p = c->tile;

												subWinReplace(w2, t);
												subWinUnlink(w);

												/* Check resizeded windows */
												if(c2->flags & SUB_CLIENT_STATE_RESIZE)
													{
														t->flags 		|= SUB_CLIENT_STATE_RESIZE;
														t->resized		= c2->resized;
														c2->flags		&= ~SUB_CLIENT_STATE_RESIZE;
														c2->resized	= 0;
													}

												subTileAdd(t, state == SUB_CLIENT_DRAG_STATE_TOP || state == SUB_CLIENT_DRAG_STATE_LEFT ? w : w2);
												subTileAdd(t, state == SUB_CLIENT_DRAG_STATE_TOP || state == SUB_CLIENT_DRAG_STATE_LEFT ? w2 : w);
												subWinMap(t);

												subTileConfigure(t->parent);
												subTileConfigure(p);
											}
										else if(state == SUB_CLIENT_DRAG_STATE_BEFORE || state == SUB_CLIENT_DRAG_STATE_AFTER ||
											state == SUB_CLIENT_DRAG_STATE_ABOVE || state == SUB_CLIENT_DRAG_STATE_BELOW)
											{
												p = c->tile;

												if((((state == SUB_CLIENT_DRAG_STATE_BEFORE || state == SUB_CLIENT_DRAG_STATE_AFTER) && 
													c2->parent->flags & SUB_TILE_TYPE_HORZ)) ||
													((state == SUB_CLIENT_DRAG_STATE_ABOVE || state == SUB_CLIENT_DRAG_STATE_BELOW) && 
													c2->parent->flags & SUB_TILE_TYPE_VERT))
													{
														subWinUnlink(w);

														if(state == SUB_CLIENT_DRAG_STATE_BEFORE || state == SUB_CLIENT_DRAG_STATE_ABOVE) 
															subWinPrepend(w2, w);
														else subWinAppend(w2, w);

														subTileConfigure(c->tile);
														if(c->tile != p) subTileConfigure(p); 
													}
												else if(c2->parent->parent && 
													(((state == SUB_CLIENT_DRAG_STATE_BEFORE || state == SUB_CLIENT_DRAG_STATE_AFTER) && 
													c2->parent->flags & SUB_TILE_TYPE_VERT) ||
													((state == SUB_CLIENT_DRAG_STATE_ABOVE || state == SUB_CLIENT_DRAG_STATE_BELOW) && 
													c2->parent->flags & SUB_TILE_TYPE_HORZ)))
													{
														subWinUnlink(w);

														if(state == SUB_CLIENT_DRAG_STATE_BEFORE || state == SUB_CLIENT_DRAG_STATE_ABOVE) 
															subWinPrepend(c2->parent, w);
														else subWinAppend(c2->parent, w);
														
														subTileConfigure(c->tile);
														subTileConfigure(p); 
													}
											}
										else if(state == SUB_CLIENT_DRAG_STATE_SWAP) subWinSwap(w, w2);
									}
#endif /* }}} */	
							}
						else /* Resize */
							{
								XDrawRectangle(d->disp, DefaultRootWindow(d->disp), d->gcs.invert, r.x + 1, r.y + 1, 
									r.width - 3, (c->flags & SUB_CLIENT_STATE_SHADE) ? d->th - 3 : r.height - 3);
					
								if(c->flags & SUB_CLIENT_STATE_FLOAT) 
									{
										c->x			= r.x;
										c->y			= r.y;
										c->width	= r.width;
										c->height	= r.height;

										subClientResize(c);
									}
								else if(c->tile && mode <= SUB_CLIENT_DRAG_BOTTOM) AdjustWeight(mode, c, &r);
							}

						XUngrabServer(d->disp);
						XUngrabPointer(d->disp, CurrentTime);
						return;
				}
		}
}
/* }}} */

 /** subClientToggle {{{
	* Toggle various states of client
	* @param[in] c A #SubClient
	**/

void
subClientToggle(SubClient *c,
int type)
{
	XEvent event;

	assert(c);

	if(c->flags & type)
		{
			c->flags &= ~type;
			switch(type)
				{
					case SUB_CLIENT_STATE_SHADE:
						subClientSetWMState(c, NormalState);

						/* Map most of the windows */
						XMapWindow(d->disp, c->win);
						XMapWindow(d->disp, c->left);
						XMapWindow(d->disp, c->right);
						XMapWindow(d->disp, c->bottom);

						/* Resize frame */
						XMoveResizeWindow(d->disp, c->frame, c->x, c->y, c->width, c->height);
						break;
					case SUB_CLIENT_STATE_FLOAT: 
						//XReparentWindow(d->disp, c->frame, c->tile->frame, c->x, c->y);	
						break;
					case SUB_CLIENT_STATE_FULL:
						/* Map most of the windows */
						XMapWindow(d->disp, c->title);
						XMapWindow(d->disp, c->left);
						XMapWindow(d->disp, c->right);
						XMapWindow(d->disp, c->bottom);
						XMapWindow(d->disp, c->caption);

						subClientResize(c);
						subTileAdd(c->tile, c);
						subTileConfigure(c->tile);
						break;								
				}
		}
	else 
		{
			long supplied = 0;
			XSizeHints *hints = NULL;
			c->flags |= type;

			switch(type)
				{
					case SUB_CLIENT_STATE_SHADE:
						subClientSetWMState(c, WithdrawnState);

						/* Unmap most of the windows */
						XUnmapWindow(d->disp, c->win);
						XUnmapWindow(d->disp, c->left);
						XUnmapWindow(d->disp, c->right);
						XUnmapWindow(d->disp, c->bottom);

						XMoveResizeWindow(d->disp, c->frame, c->x, c->y, c->width, d->th);
						break;						
					case SUB_CLIENT_STATE_FLOAT:
						/* Respect the user/program preferences */
						hints = XAllocSizeHints();
						if(!hints) subUtilLogError("Can't alloc memory. Exhausted?\n");
						if(XGetWMNormalHints(d->disp, c->win, hints, &supplied))
							{
								if(hints->flags & (USPosition|PPosition))
									{
										c->x = hints->x + 2 * d->bw;
										c->y = hints->y + d->th + d->bw;
									}
								else if(hints->flags & PAspect)
									{
										c->x = (hints->min_aspect.x - hints->max_aspect.x) / 2;
										c->y = (hints->min_aspect.y - hints->max_aspect.y) / 2;
									}
								if(hints->flags & (USSize|PSize))
									{
										c->width	= hints->width + 2 * d->bw;
										c->height	= hints->height + d->th + d->bw;
									}
								else if(hints->flags & PBaseSize)
									{
										c->width	= hints->base_width + 2 * d->bw;
										c->height	= hints->base_height + d->th + d->bw;
									}
								else
									{
										/* Fallback for clients breaking the ICCCM (mostly Gtk+ stuff) */
										if(hints->base_width > 0 && hints->base_width <= DisplayWidth(d->disp, DefaultScreen(d->disp)) &&
											hints->base_height > 0 && hints->base_height <= DisplayHeight(d->disp, DefaultScreen(d->disp))) 
											{
												c->width	= hints->base_width + 2 * d->bw;
												c->height	= hints->base_width + d->th + d->bw;
												c->x			= (DisplayWidth(d->disp, DefaultScreen(d->disp)) - c->width) / 2;
												c->y			= (DisplayHeight(d->disp, DefaultScreen(d->disp)) - c->height) / 2;
											}
									}
							}

						subClientResize(c);
						XReparentWindow(d->disp, c->frame, DefaultRootWindow(d->disp), c->x, c->y);
						XRaiseWindow(d->disp, c->frame);

						XFree(hints);
						break;
					case SUB_CLIENT_STATE_FULL:
						/* Unmap some windows */
						XUnmapWindow(d->disp, c->title);
						XUnmapWindow(d->disp, c->left);
						XUnmapWindow(d->disp, c->right);
						XUnmapWindow(d->disp, c->bottom);								
						XUnmapWindow(d->disp, c->caption);

						/* Resize to display resolution */
						c->x			= 0;
						c->y			= 0;
						c->width	= DisplayWidth(d->disp, DefaultScreen(d->disp));
						c->height	= DisplayHeight(d->disp, DefaultScreen(d->disp));

						XReparentWindow(d->disp, c->frame, DefaultRootWindow(d->disp), 0, 0);
						subClientResize(c);
				}
		}
	XUngrabServer(d->disp);
	while(XCheckTypedEvent(d->disp, UnmapNotify, &event));
	if(type != SUB_CLIENT_STATE_FULL) subTileConfigure(c->tile);
}
/* }}} */

 /** subClientConfigure {{{
	* Send a configure request to client
	* @param[in] c A #SubClient
	**/

void
subClientConfigure(SubClient *c)
{
	XConfigureEvent ev;

	assert(c);

	ev.type								= ConfigureNotify;
	ev.event							= c->win;
	ev.window							= c->win;
	ev.x									= c->x;
	ev.y									= c->y;
	ev.width							= (c->flags & SUB_CLIENT_STATE_FULL) ? c->width	: SUBWINWIDTH(c);
	ev.height							= (c->flags & SUB_CLIENT_STATE_FULL) ? c->height : SUBWINHEIGHT(c);
	ev.above							= None;
	ev.border_width 			= 0;
	ev.override_redirect	= 0;

	XSendEvent(d->disp, c->win, False, StructureNotifyMask, (XEvent *)&ev);
}
/* }}} */

	/** subClientFetchName {{{
	 * Fetch client name and store it
	 * @param[in] c A #SubClient
	 **/

void
subClientFetchName(SubClient *c)
{
	int width;

	assert(c);

	if(c->name) XFree(c->name);
	XFetchName(d->disp, c->win, &c->name);
	if(!c->name) c->name = strdup("subtle");

	/* Check max length of the caption */
	width = (strlen(c->name) + 1) * d->fx + 3;
	if(width > c->width - d->th - 4) width = c->width - d->th - 14;
	XMoveResizeWindow(d->disp, c->caption, d->th, 0, width, d->th);

	subClientRender(c);
}
/* }}} */

 /** subClientSetWMState {{{
	* Set WM state for client
	* @param[in] w A #SubClient
	* @param[in] state New state for the client
	**/

void
subClientSetWMState(SubClient *c,
	long state)
{
	CARD32 data[2];
	data[0] = state;
	data[1] = None; /* No icons */

	assert(c);

	XChangeProperty(d->disp, c->win, subEwmhFind(SUB_EWMH_WM_STATE), subEwmhFind(SUB_EWMH_WM_STATE),
		32, PropModeReplace, (unsigned char *)data, 2);
}
/* }}} */

 /** subClientGetWMState {{{
	* Get WM state from client
	* @param[in] c A #SubClient
	* @return Returns the state of the client
	**/

long
subClientGetWMState(SubClient *c)
{
	Atom type;
	int format;
	unsigned long unused, bytes;
	long *data = NULL, state = WithdrawnState;

	assert(c);

	if(XGetWindowProperty(d->disp, c->win, subEwmhFind(SUB_EWMH_WM_STATE), 0L, 2L, False, 
			subEwmhFind(SUB_EWMH_WM_STATE), &type, &format, &bytes, &unused, (unsigned char **)&data) == Success && bytes)
		{
			state = *data;
			XFree(data);
		}
	return(state);
}
/* }}} */
