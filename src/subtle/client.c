
 /**
  * @package subtle
  *
  * @file Client functions
  * @copyright (c) 2005-2010 Christoph Kappel <unexist@dorfelite.net>
  * @version $Id$
  *
  * This program can be distributed under the terms of the GNU GPL.
  * See the file COPYING.
  **/

#include <X11/Xatom.h>
#include "subtle.h"

/* Typedef {{{ */
typedef struct clientmwmhints_t
{
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long input_mode;
  unsigned long status;
} ClientMWMHints;
/* }}} */

/* ClientMask {{{ */
static void
ClientMask(SubClient *c)
{
  XDrawRectangle(subtle->dpy, ROOT, subtle->gcs.invert, c->geom.x - 1, c->geom.y - 1,
    c->geom.width + 1, c->geom.height + 1);
} /* }}} */

/* ClientSnap {{{ */
static void
ClientSnap(SubClient *c)
{
  SubScreen *s = NULL;

  assert(c);

  s = SCREEN(subtle->screens->data[c->screen]);

  /* Snap to screen edges */
  if(s->geom.x + subtle->snap > c->geom.x)
    c->geom.x = s->geom.x + BORDER(c);
  else if(c->geom.x > (s->geom.x + s->geom.width + BORDER(c) - c->geom.width - subtle->snap))
    c->geom.x = s->geom.x + s->geom.width - c->geom.width - BORDER(c);

  if(s->geom.y + subtle->snap > c->geom.y)
    c->geom.y = s->geom.y + BORDER(c);
  else if(c->geom.y > (s->geom.y + s->geom.height + BORDER(c) - c->geom.height - subtle->snap))
    c->geom.y = s->geom.y + s->geom.height - c->geom.height - BORDER(c);
} /* }}} */

/* ClientGravity {{{ */
int
ClientGravity(void)
{
  int grav = 5;
  SubClient *c = NULL;

  /* Default gravity */
  if(0 == subtle->gravity)
    {
      if((c = CLIENT(subSubtleFind(subtle->windows.focus, CLIENTID))))
        grav = c->gravity; ///< Copy gravity
    }
  else grav = subtle->gravity; ///< Set default

  return grav;
} /* }}} */

/* ClientCopy {{{ */
void
ClientCopy(SubClient *c,
  SubClient *k)
{
  /* Copy stick flag, tags and screens */
  c->flags  |= (k->flags & SUB_CLIENT_MODE_STICK);
  c->tags   |= k->tags;
  c->screen |= k->screen;
} /* }}} */

 /** subClientNew {{{
  * @brief Create new client
  * @param[in]  win  Client window
  * @return Returns a new #SubClient or \p NULL
  **/

SubClient *
subClientNew(Window win)
{
  int i, grav = 0, flags = 0;
  long vid = 0;
  XWindowAttributes attrs;
  XSetWindowAttributes sattrs;
  SubClient *c = NULL;

  assert(win);

  /* Create new client */
  c = CLIENT(subSharedMemoryAlloc(1, sizeof(SubClient)));
  c->gravities = (int *)subSharedMemoryAlloc(subtle->views->ndata, sizeof(int));
  c->flags     = SUB_TYPE_CLIENT;
  c->gravity   = -1; ///< Force update
  c->win       = win;

  /* Window attributes */
  XGetWindowAttributes(subtle->dpy, c->win, &attrs);
  c->cmap        = attrs.colormap;
  c->geom.x      = attrs.x;
  c->geom.y      = attrs.y;
  c->geom.width  = MAX(MINW, attrs.width);
  c->geom.height = MAX(MINH, attrs.height);

  /* Init gravities and screens */
  grav = ClientGravity();
  for(i = 0; i < subtle->views->ndata; i++)
    c->gravities[i] = grav;

   /* Fetch name, instance, class and role */
  subSharedPropertyClass(subtle->dpy, c->win, &c->instance, &c->klass);
  subSharedPropertyName(subtle->dpy, c->win, &c->name, c->klass);
  c->role = subSharedPropertyGet(subtle->dpy, c->win, XA_STRING,
    subEwmhGet(SUB_EWMH_WM_WINDOW_ROLE), NULL);

  /* X properties */
  sattrs.border_pixel = subtle->colors.bo_inactive;
  sattrs.event_mask   = EVENTMASK;
  XChangeWindowAttributes(subtle->dpy, c->win, CWBorderPixel|CWEventMask, &sattrs);
  XAddToSaveSet(subtle->dpy, c->win);
  XSaveContext(subtle->dpy, c->win, CLIENTID, (void *)c);

  /* Update client */
  subEwmhSetWMState(c->win, WithdrawnState);
  subClientSetProtocols(c);
  subClientSetStrut(c);
  subClientSetType(c, &flags);
  subClientSetTags(c, &flags);
  subClientSetSizeHints(c, &flags);
  subClientSetWMHints(c, &flags);
  subClientSetState(c, &flags);
  subClientSetTransient(c, &flags);

  /* Set border */
  subClientSetMWMHints(c);
  XSetWindowBorderWidth(subtle->dpy, c->win, BORDER(c));

  /* Update and handle according to flags */
  subClientToggle(c, (~c->flags & flags), False); ///< Toggle flags
  if(c->flags & SUB_CLIENT_TYPE_DIALOG) subClientCenter(c);

  /* EWMH: Gravity, screen and desktop */
  subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_GRAVITY, (long *)&subtle->gravity, 1);
  subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_SCREEN, (long *)&c->screen, 1);
  subEwmhSetCardinals(c->win, SUB_EWMH_NET_WM_DESKTOP, &vid, 1);

  subSharedLogDebug("new=client, name=%s, instance=%s, klass=%s, win=%#lx\n",
    c->name, c->instance, c->klass, win);

  return c;
} /* }}} */

 /** subClientConfigure {{{
  * @brief Send a configure request to client
  * @param[in]  c  A #SubClient
  **/

void
subClientConfigure(SubClient *c)
{
  XRectangle r = { 0 };
  XConfigureEvent ev;

  assert(c);
  DEAD(c);

  r = c->geom;

  if(c->flags & SUB_CLIENT_MODE_FULL) ///< Get fullscreen size of screen
    r = SCREEN(subArrayGet(subtle->screens, -1 != c->screen ? c->screen : 0))->base;

  /* Tell client new geometry */
  ev.type              = ConfigureNotify;
  ev.event             = c->win;
  ev.window            = c->win;
  ev.x                 = r.x;
  ev.y                 = r.y;
  ev.width             = r.width;
  ev.height            = r.height;
  ev.border_width      = BORDER(c);
  ev.above             = None;
  ev.override_redirect = False;

  XMoveResizeWindow(subtle->dpy, c->win, r.x, r.y, r.width, r.height);
  XSendEvent(subtle->dpy, c->win, False, StructureNotifyMask, (XEvent *)&ev);

  subSharedLogDebug("Configure: type=client, win=%#lx, name=%s, state=%c, x=%03d, y=%03d, width=%03d, height=%03d\n",
    c->win, c->klass, c->flags & SUB_CLIENT_MODE_FLOAT ? 'f' : c->flags & SUB_CLIENT_MODE_FULL ? 'u' : 'n',
    c->geom.x, c->geom.y, c->geom.width, c->geom.height);

  /* Hook: Create */
  subHookCall(SUB_HOOK_CLIENT_CONFIGURE, (void *)c);
} /* }}} */

 /** subClientDimension {{{
  * @brief Redimension clients
  * @param[in]  id  View id
  **/

void
subClientDimension(int id)
{
  int i, j;

  /* Update all clients */
  for(i = 0; i < subtle->clients->ndata; i++)
    {
      SubClient *c = CLIENT(subtle->clients->data[i]);

      /* Shift if necessary */
      for(j = id; -1 < id && j < subtle->views->ndata; j++)
        c->gravities[j] = c->gravities[j + 1];

      /* Resize array */
      c->gravities = (int *)subSharedMemoryRealloc((void *)c->gravities,
        subtle->views->ndata * sizeof(int));

      if(-1 == id) ///< Initialize
        c->gravities[subtle->views->ndata - 1] = ClientGravity();
    }
} /* }}} */

 /** subClientRender {{{
  * @brief Render client
  * @param[in]  c  A #SubClient
  **/

void
subClientRender(SubClient *c)
{
  DEAD(c);
  assert(c);

  /* Exclude desktop type windows */
  if(!(c->flags & SUB_CLIENT_TYPE_DESKTOP))
    {
      XSetWindowBorder(subtle->dpy, c->win, subtle->windows.focus == c->win ?
        subtle->colors.bo_active : subtle->colors.bo_inactive);
    }
} /* }}} */

 /** subClientCompare {{{
  * @brief Compare stacking level of clients
  * @param[in]  a  A #SubClient
  * @param[in]  b  A #SubClient
  * @return Returns the result of the comparison of both clients
  * @retval  -1  a is smaller
  * @retval  0   a and b are equal
  * @retval  1   a is greater
  **/

int
subClientCompare(const void *a,
  const void *b)
{
  int ret = 0;
  SubClient *c1 = *(SubClient **)a, *c2 = *(SubClient **)b;

  assert(a && b);

  /* Check flags */
  if((c1->flags | c2->flags) & SUB_CLIENT_MODE_FULL)
    {
      if(c1->flags & SUB_CLIENT_MODE_FULL) ret = 1;
      if(c2->flags & SUB_CLIENT_MODE_FULL) ret = -1;
    }
  else if((c1->flags | c2->flags) & SUB_CLIENT_TYPE_DESKTOP)
    {
      if(c1->flags & SUB_CLIENT_TYPE_DESKTOP) ret = -1;
      if(c2->flags & SUB_CLIENT_TYPE_DESKTOP) ret = 1;
    }

  return ret;
} /* }}} */

 /** subClientFocus {{{
  * @brief Set focus to client
  * @param[in]  c  A #SubClient
  **/

void
subClientFocus(SubClient *c)
{
  DEAD(c);
  assert(c);

  /* Remove urgent after getting focus */
  if(c->flags & SUB_CLIENT_MODE_URGENT)
    {
      c->flags            &= ~SUB_CLIENT_MODE_URGENT;
      subtle->urgent_tags &= ~c->tags;
    }

  /* Check client input focus type */
  if(!(c->flags & SUB_CLIENT_INPUT) && c->flags & SUB_CLIENT_FOCUS)
    {
      subEwmhMessage(c->win, c->win, SUB_EWMH_WM_PROTOCOLS,
        subEwmhGet(SUB_EWMH_WM_TAKE_FOCUS), CurrentTime, 0, 0, 0);
    }
  else XSetInputFocus(subtle->dpy, c->win, RevertToPointerRoot, CurrentTime);

  subSharedLogDebug("Focus: type=client, win=%#lx, current=%#lx, input=%d, focus=%d\n", c->win,
    subtle->windows.focus, !!(c->flags & SUB_CLIENT_INPUT), !!(c->flags & SUB_CLIENT_FOCUS));
} /* }}} */

 /** subClientWarp {{{
  * @brief Warp pointer to window center
  * @param[in]  c     A #SubClient
  * @param[in]  rise  Raise window
  **/

void
subClientWarp(SubClient *c,
  int rise)
{
  DEAD(c);
  assert(c);

  if(rise) XRaiseWindow(subtle->dpy, c->win);
  XWarpPointer(subtle->dpy, None, ROOT, 0, 0, 0, 0,
    c->geom.x + c->geom.width / 2, c->geom.y + c->geom.height / 2);
} /* }}} */

 /** subClientDrag {{{
  * @brief Move and/or drag client
  * @param[in]  c     A #SubClient
  * @param[in]  mode  Drag/move mode
  **/

void
subClientDrag(SubClient *c,
  int mode)
{
  XEvent ev;
  Window win;
  unsigned int dummy = 0;
  int loop = True, left = False;
  int wx = 0, wy = 0, ww = 0, wh = 0, rx = 0, ry = 0, maxw = 0, maxh = 0;
  short *dirx = NULL, *diry = NULL;
  SubScreen *s = NULL;
  Cursor cursor;

  DEAD(c);
  assert(c);

  /* Init {{{ */
  XQueryPointer(subtle->dpy, c->win, &win, &win, &rx, &ry, &wx, &wy, &dummy);
  c->geom.x = rx - wx;
  c->geom.y = ry - wy;
  ww        = c->geom.width;
  wh        = c->geom.height;

  /* Fet max width/height */
  s    = SCREEN(subtle->screens->data[c->screen]);
  maxw = -1 == c->maxw ? s->geom.width - 2 * BORDER(c) : c->maxw;
  maxh = -1 == c->maxh ? s->geom.height - 2 * BORDER(c) : c->maxh;

  switch(mode)
    {
      case SUB_DRAG_MOVE:
        dirx   = &c->geom.x;
        diry   = &c->geom.y;
        cursor = subtle->cursors.move;
        break;
      case SUB_DRAG_RESIZE:
        dirx   = (short *)&c->geom.width;
        diry   = (short *)&c->geom.height;
        cursor = subtle->cursors.resize;
        left   = wx < c->geom.width / 2; ///< Resize from left
        break;
    } /* }}} */

  /* Prevent resizing of nonfloat windows */
  if(c->flags & SUB_CLIENT_MODE_NOFLOAT) return;

  /* Add grabs */
  XGrabPointer(subtle->dpy, c->win, True, GRABMASK, GrabModeAsync,
    GrabModeAsync, None, cursor, CurrentTime);
  XGrabKeyboard(subtle->dpy, ROOT, True, GrabModeAsync,
    GrabModeAsync, CurrentTime);
  XGrabServer(subtle->dpy);

  if(mode & (SUB_DRAG_MOVE|SUB_DRAG_RESIZE)) ClientMask(c);

  while(loop) ///< Event loop
    {
      XMaskEvent(subtle->dpy, DRAGMASK, &ev);
      switch(ev.type)
        {
          case EnterNotify:   win = ev.xcrossing.window; break; ///< Find destination window
          case ButtonRelease: loop = False;              break;
          case FocusIn:
          case FocusOut:                                 break; ///< Ignore focus changes
          case KeyPress: /* {{{ */
            if(mode & (SUB_DRAG_MOVE|SUB_DRAG_RESIZE))
              {
                ClientMask(c);

                /* Handle keys */
                switch(XKeycodeToKeysym(subtle->dpy, ev.xkey.keycode, 0))
                  {
                    case XK_Left:   *dirx -= subtle->step; break;
                    case XK_Right:  *dirx += subtle->step; break;
                    case XK_Up:     *diry -= subtle->step; break;
                    case XK_Down:   *diry += subtle->step; break;
                    case XK_Return:
                    case XK_Escape: loop = False;          break;
                    default: break;
                  }

                if(SUB_DRAG_MOVE == mode)
                  {
                    *dirx = MINMAX(*dirx, c->minw, maxw);
                    *diry = MINMAX(*diry, c->minh, maxh);
                  }

                subClientResize(c);
                ClientMask(c);
              }
            break; /* }}} */
          case MotionNotify: /* {{{ */
            if(mode & (SUB_DRAG_MOVE|SUB_DRAG_RESIZE))
              {
                if(XYINRECT(ev.xmotion.x_root,
                    ev.xmotion.y_root, s->geom)) ///< Check values
                  {
                    ClientMask(c);

                    /* Calculate selection rect */
                    switch(mode)
                      {
                        case SUB_DRAG_MOVE:
                          c->geom.x = (rx - wx) - (rx - ev.xmotion.x_root);
                          c->geom.y = (ry - wy) - (ry - ev.xmotion.y_root);

                          ClientSnap(c); ///< Snap border
                          break;
                        case SUB_DRAG_RESIZE:
                          if(left) ///< Drag left
                            {
                              /* Calculate width and x */
                              int check = ww + (rx - ev.xmotion.x_root);

                              c->geom.width  = MAX(check, c->minw);
                              c->geom.width -= (c->geom.width % c->incw);
                              c->geom.x      = (rx - wx) + ww - c->geom.width;
                            }
                          else c->geom.width = ww - (rx - ev.xmotion.x_root); ///< Drag right

                          c->geom.height = wh - (ry - ev.xmotion.y_root);
                          subClientResize(c);

                          break;
                      }

                    ClientMask(c);
                  }
              }
            break; /* }}} */
        }
    }

  ClientMask(c); ///< Erase mask

  if(c->flags & SUB_CLIENT_MODE_FLOAT) ///< Resize client
    {
      /* Subtract border */
      if(!(c->flags & SUB_CLIENT_BORDERLESS))
        {
          c->geom.x -= subtle->bw;
          c->geom.y -= subtle->bw;
        }

      subClientConfigure(c);
    }

  /* Remove grabs */
  XUngrabPointer(subtle->dpy, CurrentTime);
  XUngrabKeyboard(subtle->dpy, CurrentTime);
  XUngrabServer(subtle->dpy);
} /* }}} */

  /** subClientResize {{{
   * @brief Set client size according to client hints
   * @param[in]  c    A #SubClient
   **/

void
subClientResize(SubClient *c)
{
  SubScreen *s = NULL;

  DEAD(c);
  assert(c);

  s = SCREEN(subtle->screens->data[c->screen]);

  /* Check sizes */
  if(!(c->flags & SUB_CLIENT_MODE_NORESIZE) &&
      (subtle->flags & SUB_SUBTLE_RESIZE ||
      c->flags & (SUB_CLIENT_MODE_FLOAT|SUB_CLIENT_MODE_RESIZE)))
    {
      int maxw = -1 == c->maxw ? s->geom.width - 2 * BORDER(c) : c->maxw;
      int maxh = -1 == c->maxh ? s->geom.height - 2 * BORDER(c) : c->maxh;

      /* Limit width */
      if(c->geom.width < c->minw)  c->geom.width  = c->minw;
      if(c->geom.width > maxw)     c->geom.width  = maxw;

      /* Limit height */
      if(c->geom.height < c->minh) c->geom.height = c->minh;
      if(c->geom.height > maxh)    c->geom.height = maxh;

      /* Check incs */
      c->geom.width  -= WIDTH(c) % c->incw;
      c->geom.height -= HEIGHT(c) % c->inch;

      /* Check aspect ratios */
      if(c->minr && c->geom.height * c->minr > c->geom.width)
        c->geom.width = (int)(c->geom.height * c->minr);

      if(c->maxr && c->geom.height * c->maxr < c->geom.width)
        c->geom.width = (int)(c->geom.height * c->maxr);
    }

  /* Fit sizes */
  if(!(c->flags & SUB_CLIENT_TYPE_DESKTOP))
    subScreenFit(s, &c->geom);
} /* }}} */

  /** subClientCenter {{{
   * @brief Center client
   * @param[in]  c  A #SubClient
   **/

void
subClientCenter(SubClient *c)
{
  DEAD(c);
  assert(c);

  if(!(c->flags & (SUB_CLIENT_TYPE_DESKTOP|SUB_CLIENT_MODE_NORESIZE)))
    {
      SubScreen *s = SCREEN(subtle->screens->data[c->screen]);

      c->geom.x = s->geom.x +
        (s->geom.width - c->geom.width - 2 * BORDER(c)) / 2;
      c->geom.y = s->geom.y +
        (s->geom.height - c->geom.height - 2 * BORDER(c)) / 2;
    }
} /* }}} */

 /** subClientTag {{{
  * @brief Set tag properties to client
  * @param[in]     c      A #SubClient
  * @param[in]     tag    Tag id
  * @param[inout]  flags  Mode flags
  * @return Return changed flags
  **/

void
subClientTag(SubClient *c,
  int tag,
  int *flags)
{
  SubTag *t = NULL;

  assert(c);

  /* Update flags and tags */
  if(c && !(c->flags & SUB_CLIENT_DEAD) &&
      (t = TAG(subArrayGet(subtle->tags, tag))))
    {
      int i;

      /* Collect flags and tags */
      *flags   |= (t->flags & (TYPES_ALL|MODES_ALL));
      c->flags |= (t->flags & MODES_NONE);
      c->tags  |= (1L << (tag + 1));

      /* Set size and enable float */
      if(t->flags & SUB_TAG_GEOMETRY && !(c->flags & SUB_CLIENT_MODE_NOFLOAT))
        {
          *flags  |= (SUB_CLIENT_MODE_FLOAT|SUB_CLIENT_MODE_NORESIZE); ///< Disable size checks
          c->geom  = t->geometry;
        }

      /* Set gravity and screens for matching views */
      for(i = 0; i < subtle->views->ndata; i++)
        {
          SubView *v = VIEW(subtle->views->data[i]);

          /* Match only views with this tag */
          if(v->tags & (1L << (tag + 1)) || t->flags & SUB_CLIENT_MODE_STICK)
            if(t->flags & SUB_TAG_GRAVITY) c->gravities[i] = t->gravity;
        }
    }
} /* }}} */

 /** subClientSetTags {{{
  * @brief Set client tags
  * @param[in]     c      A #SubClient
  * @param[inout]  flags  Mode flags
  **/

void
subClientSetTags(SubClient *c,
  int *flags)
{
  int i, visible = 0;

  DEAD(c);
  assert(c);

  c->tags = 0; ///< Reset tags

  /* Check matching tags */
  for(i = 0; i < subtle->tags->ndata; i++)
    {
      /* Check if tag matches client */
      if(subTagMatch(TAG(subtle->tags->data[i]), c))
        subClientTag(c, i, flags);
    }

  /* Check if client is visible on at least one screen w/o stick */
  for(i = 0; i < subtle->views->ndata; i++)
    if(VIEW(subtle->views->data[i])->tags & c->tags)
      {
        visible++;
        break;
      }

  if(0 == visible) subClientTag(c, 0, flags); ///< Set default tag

  /* EWMH: Tags */
  subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_TAGS, (long *)&c->tags, 1);
} /* }}} */

  /** subClientSetGravity {{{
   * @brief Set client gravity for current view
   * @param[in]  c        A #SubClient
   * @param[in]  gravity  The gravity id
   * @param[in]  screen   The screen id
   * @param[in]  force    Force update
   **/

void
subClientSetGravity(SubClient *c,
  int gravity,
  int screen,
  int force)
{
  DEAD(c);
  assert(c);

  /*< Check if update is required */
  if(force || c->gravity != gravity || c->screen != screen)
    {
      SubScreen *s = SCREEN(subArrayGet(subtle->screens, screen));

      if(c->flags & SUB_CLIENT_MODE_FLOAT)
        {
          if(-1 != screen && c->screen != screen)
            {
              SubScreen *s1 = NULL, *s2 = NULL;

              /* Get screens */
              s1 = SCREEN(subArrayGet(subtle->screens,
                -1 != c->screen ? c->screen : 0));
              s2 = SCREEN(subArrayGet(subtle->screens, screen));

              /* Update screen offsets */
              c->geom.x = c->geom.x - s1->geom.x + s2->geom.x;
              c->geom.y = c->geom.y - s1->geom.y + s2->geom.y;

              c->screen = screen;

              /* EWMH: Screen */
              subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_SCREEN,
                (long *)&c->screen, 1);
            }
        }
      /* Exclude desktop type windows */
      else if(!(c->flags & SUB_CLIENT_TYPE_DESKTOP))
        {
          SubGravity *g = GRAVITY(subtle->gravities->data[-1 != gravity ?
            gravity : c->gravity]);

          /* Calculate gravity size on screen */
          c->geom.width  = (s->geom.width * g->geom.width / 100);
          c->geom.height = (s->geom.height * g->geom.height / 100);
          c->geom.x      = s->geom.x + ((s->geom.width - c->geom.width) *
            g->geom.x / 100);
          c->geom.y      = s->geom.y + ((s->geom.height - c->geom.height) *
            g->geom.y / 100);

          /* Update border and gap */
          c->geom.x      += subtle->gap;
          c->geom.y      += subtle->gap;
          c->geom.width  -= (2 * BORDER(c) + 2 * subtle->gap);
          c->geom.height -= (2 * BORDER(c) + 2 * subtle->gap);

          /* Update client */
          if(-1 != screen)  c->screen = screen;
          if(-1 != gravity) c->gravity = c->gravities[s->vid] = gravity;

          subClientResize(c);

          /* EWMH: Gravity and screen */
          subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_GRAVITY,
            (long *)&c->gravity, 1);
          subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_SCREEN,
            (long *)&c->screen, 1);

          XSync(subtle->dpy, False); ///< Sync all changes

          /* Hook: Gravity */
          subHookCall(SUB_HOOK_CLIENT_GRAVITY, (void *)c);
        }
    }
} /* }}} */

  /** subClientSetStrut {{{
   * @brief Set client strut
   * @param[in]  c  A #SubClient
   **/

void
subClientSetStrut(SubClient *c)
{
  unsigned long size = 0;
  long *strut = NULL;

  DEAD(c);
  assert(c);

  /* Get strut property */
  if((strut = (long *)subSharedPropertyGet(subtle->dpy, c->win, XA_CARDINAL,
      subEwmhGet(SUB_EWMH_NET_WM_STRUT), &size)))
    {
      if(4 == size) ///< Only complete struts
        {
          subtle->strut.x      = MAX(subtle->strut.x,      strut[0]); ///< Left
          subtle->strut.y      = MAX(subtle->strut.y,      strut[1]); ///< Right
          subtle->strut.width  = MAX(subtle->strut.width,  strut[2]); ///< Top
          subtle->strut.height = MAX(subtle->strut.height, strut[3]); ///< Bottom

          /* Update screen and clients */
          subScreenResize();
          subScreenConfigure();

          subSharedLogDebug("Strut: x=%ld, y=%d, width=%d, height=%d\n", subtle->strut.x,
            subtle->strut.y, subtle->strut.width, subtle->strut.height);
        }

      XFree(strut);
    }
} /* }}} */

 /** subClientSetProtocols {{{
  * @brief Set client protocols
  * @param[in]  c  A #SubClient
  **/

void
subClientSetProtocols(SubClient *c)
{
  int i, n = 0;
  Atom *protos = NULL;

  assert(c);

  /* Window manager protocols */
  if(XGetWMProtocols(subtle->dpy, c->win, &protos, &n))
    {
      for(i = 0; i < n; i++)
        {
          switch(subEwmhFind(protos[i]))
            {
              case SUB_EWMH_WM_TAKE_FOCUS:    c->flags |= SUB_CLIENT_FOCUS; break;
              case SUB_EWMH_WM_DELETE_WINDOW: c->flags |= SUB_CLIENT_CLOSE; break;
              default: break;
            }
         }

      XFree(protos);
    }
} /* }}} */

  /** subClientSetSizeHints {{{
   * @brief Set client size hints
   * @param[in]  c      A #SubClient
   * @param[in]  flags  Mode flags
   **/

void
subClientSetSizeHints(SubClient *c,
  int *flags)
{
  long supplied = 0;
  XSizeHints *size = NULL;
  SubScreen *s = NULL;

  DEAD(c);
  assert(c);

  if(!(size = XAllocSizeHints()))
    {
      subSharedLogError("Can't alloc memory. Exhausted?\n");
      abort();
    }

  s = SCREEN(subtle->screens->data[0]); ///< Assume first screen

  /* Default values {{{ */
  c->minw   = MINW;
  c->minh   = MINH;
  c->maxw   = -1;
  c->maxh   = -1;
  c->minr   = 0.0f;
  c->maxr   = 0.0f;
  c->incw   = 1;
  c->inch   = 1; /* }}} */

  /* Size hints - no idea why it's called normal hints */
  if(XGetWMNormalHints(subtle->dpy, c->win, size, &supplied))
    {
      if(size->flags & PMinSize) ///< Program min size
        {
          /* Limit min size to screen size if larger */
          if(size->min_width)
            c->minw = c->minw > s->geom.width ? s->geom.width :
              MIN(MINW, size->min_width);
          if(size->min_height)
            c->minh = c->minh > s->geom.height ? s->geom.height :
              MIN(MINH, size->min_height);
        }

      if(size->flags & PMaxSize) ///< Program max size
        {
          /* Limit max size to screen size if larger */
          if(size->max_width)
            c->maxw = size->max_width > s->geom.width ?
              s->geom.width : size->max_width;
          if(size->max_height)
            c->maxh = size->max_height > s->geom.height - subtle->th ?
              s->geom.height - subtle->th : size->max_height;
        }

      /* Floating on equal min and max sizes */
      if(size->flags & PMinSize && size->flags & PMaxSize)
        {
          if(size->min_width == size->max_width &&
              size->min_height == size->max_height &&
              !(c->flags & SUB_CLIENT_TYPE_DESKTOP))
            {
              *flags   |= SUB_CLIENT_MODE_FLOAT;
              c->flags |= SUB_CLIENT_TYPE_DIALOG;
            }
        }

      if(size->flags & PAspect) ///< Aspect
        {
          if(size->min_aspect.y)
            c->minr = (float)size->min_aspect.x / size->min_aspect.y;
          if(size->max_aspect.y)
            c->maxr = (float)size->max_aspect.x / size->max_aspect.y;
        }

      if(size->flags & PResizeInc) ///< Resize inc
        {
          if(size->width_inc)  c->incw = size->width_inc;
          if(size->height_inc) c->inch = size->height_inc;
        }

      /* Check for specific position */
      if(!(c->flags & SUB_CLIENT_MODE_NORESIZE) &&
          (subtle->flags & SUB_SUBTLE_RESIZE ||
          c->flags & (SUB_CLIENT_MODE_FLOAT|SUB_CLIENT_MODE_RESIZE)))
        {
          if(size->flags & (USSize|PSize)) ///< User/program size
            {
              c->geom.width  = size->width;
              c->geom.height = size->height;
            }

          if(size->flags & (USPosition|PPosition)) ///< User/program position
            {
              c->geom.x = size->x;
              c->geom.y = size->y;
            }

          /* Sanitize positions for stupid clients like GIMP */
          if(size->flags & (USSize|PSize|USPosition|PPosition))
            subClientResize(c);
        }
    }

  XFree(size);

  subSharedLogDebug("Normal hints: x=%d, y=%d, width=%d, height=%d, minw=%d, minh=%d, " \
    "maxw=%d, maxh=%d, minr=%f, maxr=%f\n",
    c->geom.x, c->geom.y, c->geom.width, c->geom.height, c->minw,
    c->minh, c->maxw, c->maxh, c->minr, c->maxr);
} /* }}} */

  /** subClientSetWMHints {{{
   * @brief Set client WM hints
   * @param[in]  c      A #SubClient
   * @param[in]  flags  Mode flags
   **/

void
subClientSetWMHints(SubClient *c,
  int *flags)
{
  XWMHints *hints = NULL;

  assert(c && flags);

  /* Window manager hints */
  if((hints = XGetWMHints(subtle->dpy, c->win)))
    {
      /* Handle urgency hint */
      if(!(c->flags & SUB_CLIENT_MODE_NOURGENT))
        {
          /* Set urgency and remove it after getting focus */
          if(hints->flags & XUrgencyHint)
            {
              *flags              |= SUB_CLIENT_MODE_URGENT;
              subtle->urgent_tags |= c->tags;
            }
        }

      /* Handle window group hint */
      if(hints->flags & WindowGroupHint)
        {
          SubClient *k = NULL;

          /* Copy tags and modes */
          if((k = CLIENT(subSubtleFind(hints->window_group, CLIENTID))))
            ClientCopy(c, k);
        }

      /* Handle input hint */
      if(hints->flags & InputHint && hints->input)
        c->flags |= SUB_CLIENT_INPUT;

      XFree(hints);
    }
} /* }}} */

  /** subClientSetMWMHints {{{
   * @brief Set client MWM hints
   * @param[in]  c  A #SubClient
   **/

void
subClientSetMWMHints(SubClient *c)
{
  unsigned long size = 0;
  ClientMWMHints *hints = NULL;

  assert(c);

  /* Window manager hints */
  if((hints = (ClientMWMHints *)subSharedPropertyGet(subtle->dpy, c->win,
      subEwmhGet(SUB_EWMH_MOTIF_WM_HINTS),
      subEwmhGet(SUB_EWMH_MOTIF_WM_HINTS), &size)))
    {
      /* Check if hints contain decoration flags */
      if(hints->flags & MWM_FLAG_DECORATIONS)
        {
          /* Check window border */
          if(!(hints->decorations & (MWM_DECOR_ALL|MWM_DECOR_BORDER)))
            c->flags |= SUB_CLIENT_BORDERLESS;
        }

      free(hints);
    }
} /* }}} */

  /** subClientSetState {{{
   * @brief Set client WM state
   * @param[in]  c      A #SubClient
   * @param[in]  flags  Mode flags
   **/

void
subClientSetState(SubClient *c,
  int *flags)
{
  int i;
  unsigned long size = 0;
  Atom *states = NULL;

  assert(c);

  /* Window state */
  if((states = (Atom *)subSharedPropertyGet(subtle->dpy, c->win, XA_ATOM,
      subEwmhGet(SUB_EWMH_NET_WM_STATE), &size)))
    {
      for(i = 0; i < size; i++)
        {
          switch(subEwmhFind(states[i]))
            {
              case SUB_EWMH_NET_WM_STATE_FULLSCREEN: *flags |= SUB_CLIENT_MODE_FULL;   break;
              case SUB_EWMH_NET_WM_STATE_ABOVE:      *flags |= SUB_CLIENT_MODE_FLOAT;  break;
              case SUB_EWMH_NET_WM_STATE_STICKY:     *flags |= SUB_CLIENT_MODE_STICK;  break;
              case SUB_EWMH_NET_WM_STATE_MODAL:      *flags |= SUB_CLIENT_MODE_URGENT; break;
              default: break;
            }
        }

      XFree(states);
    }
} /* }}} */

 /** subClientSetTransient {{{
  * @brief Set client transient
  * @param[in]  c      A #SubClient
  * @param[in]  flags  Mode flags
  **/

void
subClientSetTransient(SubClient *c,
  int *flags)
{
  Window trans = None;

  assert(c && flags);

  /* Check for transient windows */
  if(XGetTransientForHint(subtle->dpy, c->win, &trans))
    {
      SubClient *k = NULL;

      /* Check if transient windows should be urgent */
      *flags |= subtle->flags & SUB_SUBTLE_URGENT ?
        SUB_CLIENT_MODE_FLOAT|SUB_CLIENT_MODE_URGENT : SUB_CLIENT_MODE_FLOAT;

      /* Find parent window */
      if((k = CLIENT(subSubtleFind(trans, CLIENTID)))) ClientCopy(c, k);
     }
} /* }}} */

 /** subClientSetType {{{
  * @brief Set client type
  * @param[in]  c      A #SubClient
  * @param[in]  flags  Mode flags
  **/

void
subClientSetType(SubClient *c,
  int *flags)
{
  int i;
  unsigned long size = 0;
  Atom *types = NULL;

  assert(c);

  /* Get window type */
  if((types = (Atom *)subSharedPropertyGet(subtle->dpy, c->win, XA_ATOM,
      subEwmhGet(SUB_EWMH_NET_WM_WINDOW_TYPE), &size)))
    {
      int id = 0;

      /* Set flags according to window types */
      for(i = 0; i < size; i++)
        {
          switch((id = subEwmhFind(types[i])))
            {
              case SUB_EWMH_NET_WM_WINDOW_TYPE_DOCK:
                c->flags |= SUB_CLIENT_TYPE_DOCK;
                break;
              case SUB_EWMH_NET_WM_WINDOW_TYPE_DESKTOP:
                c->flags |= SUB_CLIENT_TYPE_DESKTOP;
                break;
              case SUB_EWMH_NET_WM_WINDOW_TYPE_TOOLBAR:
                c->flags |= SUB_CLIENT_TYPE_TOOLBAR;
                break;
              case SUB_EWMH_NET_WM_WINDOW_TYPE_SPLASH:
                c->flags |= SUB_CLIENT_TYPE_SPLASH;
                break;
              case SUB_EWMH_NET_WM_WINDOW_TYPE_DIALOG:
                c->flags |= SUB_CLIENT_TYPE_DIALOG;
                *flags   |= SUB_CLIENT_MODE_FLOAT;
                break;
              default: break;
            }
        }

      XFree(types);
    }
} /* }}} */

 /** subClientToggle {{{
  * @brief Toggle various states of client
  * @param[in]  c        A #SubClient
  * @param[in]  type     Toggle type
  * @param[in]  gravity  Whether gravity should be set
  **/

void
subClientToggle(SubClient *c,
  int type,
  int gravity)
{
  int flags = 0;

  DEAD(c);
  assert(c);

  /* Remove flags */
  if(type & SUB_CLIENT_MODE_FULL   && c->flags & SUB_CLIENT_MODE_NOFULL)
    type &= ~SUB_CLIENT_MODE_FULL;
  if(type & SUB_CLIENT_MODE_FLOAT  && c->flags & SUB_CLIENT_MODE_NOFLOAT)
    type &= ~SUB_CLIENT_MODE_FLOAT;
  if(type & SUB_CLIENT_MODE_STICK  && c->flags & SUB_CLIENT_MODE_NOSTICK)
    type &= ~SUB_CLIENT_MODE_STICK;
  if(type & SUB_CLIENT_MODE_URGENT && c->flags & SUB_CLIENT_MODE_NOURGENT)
    type &= ~SUB_CLIENT_MODE_URGENT;

  if(c->flags & type) ///< Unset flags
    {
      c->flags &= ~type;

      /* Client modes */
      if(type & SUB_CLIENT_MODE_FULL && !(c->flags & SUB_CLIENT_BORDERLESS))
        XSetWindowBorderWidth(subtle->dpy, c->win, subtle->bw);

      if(type & SUB_CLIENT_MODE_FLOAT)
        c->gravity = -1; ///< Updating gravity

      if(type & SUB_CLIENT_MODE_STICK)
        subScreenConfigure();
    }
  else ///< Set flags
    {
      c->flags |= type;

      /* Client modes */
      if(type & SUB_CLIENT_MODE_STICK)
        {
          if(gravity) ///< Check if gravity should be set
            {
              int i;

              /* Set gravity and screen for other views */
              for(i = 0; i < subtle->views->ndata; i++)
                {
                  SubView *v = VIEW(subtle->views->data[i]);

                  if(!(v->tags & c->tags)) ///< Check manually
                    if(-1 != c->gravity) c->gravities[i] = c->gravity;
                }
            }
        }

      if(type & SUB_CLIENT_MODE_FLOAT) subClientResize(c); ///< Sanitize

      if(type & SUB_CLIENT_MODE_FULL)
        {
          XSetWindowBorderWidth(subtle->dpy, c->win, 0);
          subClientResize(c); ///< Sanitize
        }

      if(type & (SUB_EWMH_NET_WM_WINDOW_TYPE_DOCK|SUB_CLIENT_TYPE_DESKTOP))
        {
          /* Special treatment */
          if(type & SUB_CLIENT_TYPE_DESKTOP)
            {
              SubScreen *s = SCREEN(subtle->screens->data[c->screen]);

              c->geom = s->base;

              /* Add panel heights without struts */
              if(s->flags & SUB_SCREEN_PANEL1)
                {
                  c->geom.y      += subtle->th;
                  c->geom.height -= subtle->th;
                }

              if(s->flags & SUB_SCREEN_PANEL2)
                c->geom.height -= subtle->th;

              XLowerWindow(subtle->dpy, c->win);
            }
          else XRaiseWindow(subtle->dpy, c->win);

          XSetWindowBorderWidth(subtle->dpy, c->win, 0);
        }
    }

  subClientConfigure(c);

  /* Sort for keeping stacking order */
  if(type & SUB_CLIENT_MODE_FULL) subArraySort(subtle->clients, subClientCompare);

  /* Translate flags */
  if(c->flags & SUB_CLIENT_MODE_FULL)  flags |= SUB_EWMH_FULL;
  if(c->flags & SUB_CLIENT_MODE_FLOAT) flags |= SUB_EWMH_FLOAT;
  if(c->flags & SUB_CLIENT_MODE_STICK) flags |= SUB_EWMH_STICK;

  /* EWMH: Flags */
  subEwmhSetCardinals(c->win, SUB_EWMH_SUBTLE_WINDOW_FLAGS, (long *)&flags, 1);

  /* Update panel */
  if(type & (SUB_CLIENT_MODE_FULL|SUB_CLIENT_MODE_FLOAT|SUB_CLIENT_MODE_STICK)
      && subtle->windows.focus == c->win)
    {
      subScreenUpdate();
      subScreenRender();
    }
} /* }}} */

 /** subClientPublish {{{
  * @brief Publish clients
  **/

void
subClientPublish(void)
{
  int i;
  Window *wins = (Window *)subSharedMemoryAlloc(subtle->clients->ndata,
    sizeof(Window));

  for(i = 0; i < subtle->clients->ndata; i++)
    wins[i] = CLIENT(subtle->clients->data[i])->win;

  /* EWMH: Client list and client list stacking */
  subEwmhSetWindows(ROOT, SUB_EWMH_NET_CLIENT_LIST, wins,
    subtle->clients->ndata);
  subEwmhSetWindows(ROOT, SUB_EWMH_NET_CLIENT_LIST_STACKING, wins,
    subtle->clients->ndata);

  XSync(subtle->dpy, False); ///< Sync all changes

  subSharedLogDebug("publish=client, clients=%d\n", subtle->clients->ndata);

  free(wins);
} /* }}} */

 /** subClientKill {{{
  * @brief Send interested clients the  signal and/or kill it
  * @param[in]  c        A #SubClient
  * @param[in]  destroy  Destroy window
  **/

void
subClientKill(SubClient *c,
  int destroy)
{
  assert(c);

  /* Hook: Kill */
  subHookCall(SUB_HOOK_CLIENT_KILL, (void *)c);

  /* Remove highlight of urgent client */
  if(c->flags & SUB_CLIENT_MODE_URGENT)
    subtle->urgent_tags &= ~c->tags;

  /* Ignore further events and delete context */
  XSelectInput(subtle->dpy, c->win, NoEventMask);
  XDeleteContext(subtle->dpy, c->win, CLIENTID);
  XUnmapWindow(subtle->dpy, c->win);

  /* Destroy window */
  if(destroy && !(c->flags & SUB_CLIENT_DEAD))
    {
      if(c->flags & SUB_CLIENT_CLOSE) ///< Honor window preferences
        {
          subEwmhMessage(c->win, c->win, SUB_EWMH_WM_PROTOCOLS,
            subEwmhGet(SUB_EWMH_WM_DELETE_WINDOW), CurrentTime, 0, 0, 0);
        }
      else XKillClient(subtle->dpy, c->win);
    }

  if(c->gravities) free(c->gravities);
  if(c->name)      free(c->name);
  if(c->instance)  free(c->instance);
  if(c->klass)     free(c->klass);
  if(c->role)      free(c->role);
  free(c);

  /* Hook: Tile */
  subHookCall(SUB_HOOK_TILE, NULL);

  subSharedLogDebug("kill=client\n");
} /* }}} */

// vim:ts=2:bs=2:sw=2:et:fdm=marker
