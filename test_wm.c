// Shitty Manager of cLients Window Manager (SMOLWM)
//
// docs:
// a client is just a window, really. I thought they'd end up doing more but meh
// I use 'mon' to describe an index of a monitor, and 'cli' that of a client
//
// issues:
// it segfaults when closing for some reason
// it happens in XCloseDisplay I think
// doesn't happen if no windows created
// not that big a deal, since resources freed beforehand

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WHITE  0xffffff
#define BLACK  0x000000
#define RED    0xff0000
#define BLUE   0x0000ff
#define NRM_FG BLACK // normal foreground
#define NRM_BG WHITE // normal background
#define SEL_FG RED   // selected foreground
#define HID_FG BLUE  // hidden foreground
#define BAR_FG NRM_FG
#define BAR_BG NRM_BG

#define BAR_H                20
#define CELL_W               20
#define CELL_GAP             2
#define CLIENT_GROWTH_FACTOR 2
#define CLIENT_MAX           999

#define DISPLAY_STRING    ":0"
#define DP_MONITOR_STRING ":DP-0"

#define PRINT_XINERAMA_MONITOR(mon) \
    fprintf(stderr, "screen %d: (%d, %d) %d by %d\n", mon.screen_number, mon.x_org, mon.y_org, mon.width, mon.height)

// utility macros that make it possible to use indices, rather than pointers, throughout the codebase
#define CLI(client)     client - clients.array
#define MON(monitor)    monitor - monitors.array
#define CLIENT(cli)     clients.array + cli
#define MONITOR(mon)    monitors.array + mon
#define CLI_WINDOW(cli) clients.array[cli].window
#define MON_CLI(mon)    monitors.array[mon].cli
#define MON_WINDOW(mon) CLI_WINDOW(MON_CLI(mon))

#define LENGTH(array) (sizeof(array) / sizeof(array[0]))

typedef int                  Cli;
typedef int                  Mon;
typedef enum SwitchDirection SwitchDirection;
typedef struct Monitor       Monitor;
typedef struct MonitorList   MonitorList;
typedef struct Client        Client;
typedef struct ClientList    ClientList;
typedef void (*EventHandler)(XEvent *);

// fucking x keycodes
// why in the hell is j > q
enum {
    KEY_1     = 10,
    KEY_2     = 11,
    KEY_3     = 12,
    KEY_4     = 13,
    KEY_5     = 14,
    KEY_6     = 15,
    KEY_7     = 16,
    KEY_8     = 17,
    KEY_9     = 18,
    KEY_0     = 19,
    KEY_c     = 54,
    KEY_j     = 44,
    KEY_k     = 45,
    KEY_p     = 33,
    KEY_q     = 24,
    KEY_r     = 27,
    KEY_SHIFT = 50,
    KEY_MOD4  = 133,
};

typedef struct {
    KeyCode      code;
    unsigned int modifier;
} Key;

enum SwitchDirection {
    DOWN,
    UP,
};

struct Monitor {
    short  x;
    short  y;
    short  w;
    short  h;
    Cli    cli;
    Window bar;
};

struct MonitorList {
    Monitor *array;
    int      count;
};

struct Client {
    Window window;
};

struct ClientList {
    Client *array;
    int     count;
    int     capacity;
};

static void setup(void);
static void init_gcs(void);
static void init_clients(void);
static void init_monitors(void);
static void init_bars(void);
static void key_down(XEvent *);
static void key_up(XEvent *);
static void map(XEvent *);
static void focus_in(XEvent *);
static void enter(XEvent *);
static void destroy(XEvent *);
static void nothing(XEvent *);
static void dmenu(void);
static void draw_bars(void);
static void draw_bar_on(int mon);
static void draw_filled_rectangle(Drawable, GC, int, int, int, int);
static int  free_monitor(void);
static int  monitor_using_client(int index);
static bool in_client_list(Window);
static void expand_clients(void);
static void append_client(Window);
static void set_client(int, int);
static void try_remove_window(Window w);
static void delete_client(int);
static void unselect_client(int);
static void kill_client(int);
static void switch_client(SwitchDirection direction);
static void switch_monitor(SwitchDirection direction);
static void jump_cli(int);
static void jump_mon(int);
static void swap_clients(int, int);
static void select_mon(Mon);
static bool mon_in_range(int);
static bool cli_in_range(int);
static void restart(void);
static void quit(void);
static void end(void);
static void free_clients(void);
static int  dummy_error_handler(Display *, XErrorEvent *);
static int  error_handler(Display *, XErrorEvent *);
static void move_cursor(int, int);

static EventHandler handle_event[LASTEvent] = {
    [FocusIn]       = focus_in,
    [EnterNotify]   = enter,
    [KeyPress]      = key_down,
    [KeyRelease]    = key_up,
    [MapRequest]    = map,
    [DestroyNotify] = destroy,
};

static Key keys[] = {
    {KEY_1, Mod4Mask},
    {KEY_2, Mod4Mask},
    {KEY_3, Mod4Mask},
    {KEY_4, Mod4Mask},
    {KEY_5, Mod4Mask},
    {KEY_6, Mod4Mask},
    {KEY_7, Mod4Mask},
    {KEY_8, Mod4Mask},
    {KEY_9, Mod4Mask},
    {KEY_0, Mod4Mask},
    {KEY_c, Mod4Mask},
    {KEY_j, Mod4Mask},
    {KEY_k, Mod4Mask},
    {KEY_p, Mod4Mask},
    {KEY_q, Mod4Mask},
    {KEY_r, Mod4Mask},
    {KEY_SHIFT, Mod4Mask},
    {KEY_MOD4, AnyModifier},
};

static KeyCode shift_keys[] = {
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_j,
    KEY_k,
    KEY_p,
    KEY_q,
};

static MonitorList monitors;
static ClientList  clients;
static Display *   dp;   // display pointer
static Screen *    sp;   // screen pointer to the struct
static int         sn;   // screen number number of display, better than calling funcs to get it every time
static Window      root; // parent of topmost windows
static int         selmon;
static GC          white_gc;
static GC          black_gc;
static GC          red_gc;
static GC          blue_gc;
static bool        mod     = false; // mod4
static bool        shift   = false;
static bool        running = false;
static Cursor      cursor;

int main() {
    setup();

    XEvent e;
    while (!XNextEvent(dp, &e) && running)
        if (handle_event[e.type]) handle_event[e.type](&e);

    end();
    return 0;
}

static void setup(void) {
    // enables restarting
    if (!running) {
        if ((dp = XOpenDisplay(NULL)) == NULL) {
            fprintf(stderr, "failed to open display %s!\n", XDisplayString(dp));
            exit(1);
        }
    }

    sp      = XDefaultScreenOfDisplay(dp);
    sn      = DefaultScreen(dp);
    root    = XDefaultRootWindow(dp);
    running = true;
    cursor  = XCreateFontCursor(dp, XC_left_ptr);

    XUngrabKey(dp, AnyKey, AnyModifier, root);
    for (int i = 0; i < LENGTH(keys); ++i)
        XGrabKey(dp, keys[i].code, keys[i].modifier, root, True, GrabModeAsync, GrabModeAsync);

    init_gcs();
    init_clients();
    init_monitors();

    selmon = 0;

    XSetWindowAttributes root_attr = {.cursor     = cursor,
                                      .event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                                                    ButtonPressMask | KeyPressMask | KeyReleaseMask | EnterWindowMask |
                                                    LeaveWindowMask | FocusChangeMask};
    XChangeWindowAttributes(dp, root, CWEventMask | CWCursor, &root_attr);

    XSelectInput(dp, root, root_attr.event_mask);

    // | | ButtonPressMask | EnterWindowMask
    // | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;

    init_bars();
    draw_bars();
}

static void init_gcs(void) {
    const unsigned long value_mask = GCCapStyle | GCJoinStyle | GCForeground | GCBackground;

    XGCValues white_values;
    white_values.cap_style  = CapButt;
    white_values.join_style = JoinBevel;
    white_values.foreground = WHITE;
    white_values.background = BLACK;

    white_gc = XCreateGC(dp, root, value_mask, &white_values);

    XGCValues black_values;
    black_values.cap_style  = CapButt;
    black_values.join_style = JoinBevel;
    black_values.foreground = BLACK;
    black_values.background = WHITE;

    black_gc = XCreateGC(dp, root, value_mask, &black_values);

    XGCValues red_values;
    red_values.cap_style  = CapButt;
    red_values.join_style = JoinBevel;
    red_values.foreground = RED;
    red_values.background = BLACK;

    red_gc = XCreateGC(dp, root, value_mask, &red_values);

    XGCValues blue_values;
    blue_values.cap_style  = CapButt;
    blue_values.join_style = JoinBevel;
    blue_values.foreground = BLUE;
    blue_values.background = BLACK;

    blue_gc = XCreateGC(dp, root, value_mask, &blue_values);
}

static void init_clients(void) {
    clients.capacity = 8;
    clients.array    = malloc(sizeof(Client) * clients.capacity);
    clients.count    = 0;
}

static void init_monitors(void) {
    if (!XineramaIsActive(dp)) {
        fprintf(stderr, "please use xinerama, I'm too mentally disabled to add support. quitting...\n");
        exit(1);
    }

    XineramaScreenInfo *screen_info = XineramaQueryScreens(dp, &monitors.count);

    if (monitors.count == 0) {
        fprintf(stderr, "no monitors detected, exiting...");
        exit(1);
    }

    monitors.array = malloc(sizeof(Monitor) * monitors.count);

    // move monitors into array
    for (int count = 0; count < monitors.count; ++count)
        monitors.array[count] = (Monitor){.x   = screen_info[count].x_org,
                                          .y   = screen_info[count].y_org,
                                          .w   = screen_info[count].width,
                                          .h   = screen_info[count].height,
                                          .cli = -1};
    // sort array by x vals
    // not the cleanest algo; but come on, it runs once
    for (int i = 0; i < monitors.count - 1; ++i) {
        // if unordered
        if (monitors.array[i].x > monitors.array[i + 1].x) {
            // swap
            Monitor tmp           = monitors.array[i];
            monitors.array[i]     = monitors.array[i + 1];
            monitors.array[i + 1] = tmp;

            // start over
            i = -1;
        }
    }

    XFree(screen_info);
}

static void init_bars(void) {
    for (int i = 0; i < monitors.count; ++i) {
        monitors.array[i].bar = XCreateSimpleWindow(dp,
                                                    root,
                                                    monitors.array[i].x,
                                                    monitors.array[i].y,
                                                    monitors.array[i].w,
                                                    BAR_H,
                                                    0,
                                                    BAR_FG,
                                                    BAR_BG);
        XMapWindow(dp, monitors.array[i].bar);
    }
}

static void press_mod(void) {
    mod = true;
    for (int i = 0; i < LENGTH(shift_keys); ++i)
        XGrabKey(dp, shift_keys[i], ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

// storing mod and shift shouldn't be needed anymore since i learned to use grabkeys, but I'll fix that later
static void key_down(XEvent *e) {
    switch (e->xkey.keycode) {
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
            if (mod && shift) jump_mon(e->xkey.keycode - KEY_1 + 1);
            else if (mod)
                jump_cli(e->xkey.keycode - KEY_1 + 1);
            break;
        case KEY_0:
            if (mod && shift) jump_mon(0);
            else if (mod)
                jump_cli(0);
            break;
        case KEY_c:
            if (mod) kill_client(MON_CLI(selmon));
            break;
        case KEY_j:
            if (mod && shift) switch_monitor(DOWN);
            else if (mod)
                switch_client(DOWN);
            break;
        case KEY_k:
            if (mod && shift) switch_monitor(UP);
            else if (mod)
                switch_client(UP);
            break;
        case KEY_p:
            if (mod) dmenu();
            break;
        case KEY_q:
            if (mod) end();
            break;
        case KEY_r:
            if (mod) restart();
            break;
        case KEY_MOD4: press_mod(); break;
        case KEY_SHIFT: shift = true; break;
        default: break;
    }
}

static void release_mod(void) {
    mod = false;
    // keys will no longer be captured by wm when shift is pressed
    for (int i = 0; i < LENGTH(shift_keys); ++i) XUngrabKey(dp, shift_keys[i], ShiftMask, root);
}

static void key_up(XEvent *e) {
    switch (e->xkey.keycode) {
        case KEY_MOD4: release_mod(); break;
        case KEY_SHIFT: shift = false; break;
        default: break;
    }
}

static void map(XEvent *e) {
    XMapRequestEvent request = e->xmaprequest;

    if (request.parent != root) {
        XMapWindow(dp, request.window);
        return;
    }

    int open_mon;
    if (!in_client_list(request.window)) {
        append_client(request.window);

        // selected monitor free
        if (MON_CLI(selmon) == -1) {
            set_client(selmon, clients.count - 1);
            XMapWindow(dp, request.window);
        }
        // any monitor available
        else if ((open_mon = free_monitor()) != -1) {
            set_client(open_mon, clients.count - 1);
            XMapWindow(dp, request.window);
            select_mon(open_mon);
        }
    }

    draw_bars();
}

static void destroy(XEvent *e) {
    XDestroyWindowEvent *destroy_event = &e->xdestroywindow;

    Window parent = destroy_event->event;
    Window target = destroy_event->window;
    if (parent == root) try_remove_window(target);
}

static void focus_in(XEvent *e) { printf("changing focus\n"); }

static void enter(XEvent *e) { printf("Entering a window\n"); }

static void nothing(XEvent *e) {}

static void dmenu(void) {
    // this is basically dwm code
    if (fork() == 0) {
        if (dp) close(ConnectionNumber(dp));
        setsid();
        char *cmd    = "dmenu_run";
        char *arg[2] = {"dmenu_run", NULL};
        execvp(cmd, arg);
        fprintf(stderr, "failed to run 'dmenu_run'\n");
    }
}

static void append_client(Window w) {
    if (clients.count >= CLIENT_MAX) {
        fprintf(
            stderr,
            "Due to string drawing limitations, only 999 simultaneous clients are supported at this time. Bye bye.");
        exit(1);
    }

    if (clients.count >= clients.capacity) expand_clients();
    clients.array[clients.count++] = (Client){w};
}

static void expand_clients(void) {
    clients.capacity *= CLIENT_GROWTH_FACTOR;
    Client *new_array = malloc(sizeof(Client) * clients.capacity);

    // copy old to new array
    for (int i = 0; i < clients.count; ++i) new_array[i] = clients.array[i];

    free(clients.array);
    clients.array = new_array;
}

static void draw_bars(void) {
    for (int i = 0; i < monitors.count; ++i) { draw_bar_on(i); }
}

static void draw_bar_on(int mon) {
    if (!mon_in_range(mon)) return;
    Monitor *m = MONITOR(mon);

    Window bar = m->bar;

    if (selmon == mon) draw_filled_rectangle(bar, red_gc, 0, 0, m->w, BAR_H);
    else
        draw_filled_rectangle(bar, white_gc, 0, 0, m->w, BAR_H);

    // todo: add check that client count under 999
    char num[12];
    int  len;
    // starts at -1 to mark when no clients selected
    for (int i = -1; i < clients.count; ++i) {
        draw_filled_rectangle(bar, m->cli == i ? red_gc : black_gc, (i + 1) * (CELL_W + CELL_GAP), 0, CELL_W, BAR_H);
        len = sprintf(num, "%d", i);
        if (len > 0)
            XDrawString(dp, bar, white_gc, (i + 1) * (CELL_W + CELL_GAP) + CELL_W / 2.25, BAR_H / 1.5, num, len);
    }
    len = sprintf(num, "Monitor %d", mon);
    if (len > 0) XDrawString(dp, bar, blue_gc, m->w - len * 10, BAR_H / 1.5, num, len);
}

static void draw_filled_rectangle(Drawable d, GC gc, int x, int y, int w, int h) {
    // todo: use DrawLine instead of DrawPoint
    for (int x_i = x; x_i < x + w; ++x_i)
        for (int y_i = y; y_i < y + h; ++y_i) XDrawPoint(dp, d, gc, x_i, y_i);
}

static bool in_client_list(Window w) {
    for (int i = 0; i < clients.count; i++)
        if (clients.array[i].window == w) return true;
    return false;
}

static int free_monitor(void) {
    for (int i = 0; i < monitors.count; ++i)
        if (MON_CLI(i) == -1) return i;
    return -1;
}

static void set_client(int mon, int cl) {
    if (mon == -1) return;
    if (cl != -1) {
        // resize client window to fit monitor
        Monitor *m = monitors.array + mon;
        XMoveResizeWindow(dp, CLI_WINDOW(cl), m->x, m->y + BAR_H, m->w, m->h - BAR_H);
    }
    MON_CLI(mon) = cl;
}

static void try_remove_window(Window w) {
    // bool used in case client not removed
    bool removed = false;

    for (int i = 0; i < clients.count;)
        if (clients.array[i].window == w) {
            delete_client(i);
            unselect_client(i);
            removed = true;
        } else
            ++i;

    if (removed) draw_bars();
}

static void delete_client(int index) {
    // range check
    if (index < 0 || index >= clients.count) return;

    // shift items back one, overwriting index
    for (int i = index; i < clients.count - 1; ++i) clients.array[i] = clients.array[i + 1];

    --clients.count;
}

static void unselect_client(int index) {
    for (int cur_mon = 0; cur_mon < monitors.count; ++cur_mon)
        if (MON_CLI(cur_mon) == index) MON_CLI(cur_mon) = -1;
}

// I need to learn client messages eventually, but this is good for now
// XKillClient basically destroys the client's resources so works
static void kill_client(int cli) {
    XGrabServer(dp);
    XSetErrorHandler(dummy_error_handler);
    XSetCloseDownMode(dp, DestroyAll);
    XKillClient(dp, CLI_WINDOW(cli));
    XSync(dp, False);
    XSetErrorHandler(error_handler);
    XUngrabServer(dp);
}

static int wrap_client_index(int index) {
    int ret_index = index;
    // negative wrapping
    while (ret_index < -1) ret_index += clients.count + 1;
    // positive wrapping
    while (ret_index > clients.count - 1) ret_index -= clients.count + 1;

    return ret_index;
}

static void focus(int mon) {
    const long ptr_event_mask =
        ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | PointerMotionMask;
    Window w = root;
    if (MON_CLI(mon) != -1) w = MON_WINDOW(mon);
    XGrabPointer(dp, w, True, ptr_event_mask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XSetInputFocus(dp, w, RevertToPointerRoot, CurrentTime);
    printf("focus set to monitor %d\n", selmon);
}

// select client displayed on selected monitor; -1 to select none
static void select_client(int cli) {
    // get current monitor before setting
    int prevmon = monitor_using_client(cli);

    // remove the window currently on the monitor if it exists
    if (MON_CLI(selmon) != -1) {
        XUnmapWindow(dp, MON_WINDOW(selmon));
        MON_CLI(selmon) = -1;
    }

    set_client(selmon, cli);

    // if client was in use
    if (prevmon != -1) {
        MON_CLI(prevmon) = -1;
        draw_bar_on(prevmon);
    } else
        XMapWindow(dp, MON_WINDOW(selmon));

    focus(selmon);
    draw_bar_on(selmon);
}

static void switch_client(SwitchDirection direction) {
    int new_index = -1;
    switch (direction) {
        case DOWN: new_index = wrap_client_index(MON_CLI(selmon) - 1); break;
        case UP: new_index = wrap_client_index(MON_CLI(selmon) + 1); break;
        default: return;
    }

    select_client(new_index);
}

static void jump_cli(Cli cli) { select_client(cli_in_range(cli) ? cli : -1); }

static void jump_mon(Mon mon) { select_mon(mon_in_range(mon) ? mon : selmon); }

// Please refactor me!
static void switch_monitor(SwitchDirection direction) {
    Monitor *newmon;
    int      prevmon = selmon;
    switch (direction) {
        case DOWN:
            newmon = selmon <= 0 ? monitors.array + (selmon = monitors.count - 1) : monitors.array + --selmon;
            break;
        case UP:
            newmon = selmon >= monitors.count - 1 ? monitors.array + (selmon = 0) : monitors.array + ++selmon;
            break;
        default: return;
    }
    select_mon(MON(newmon));
    draw_bar_on(prevmon);
    draw_bar_on(newmon - monitors.array);
}

static void select_mon(Mon mon) {
    Monitor *m = MONITOR(mon);

    move_cursor(m->x + m->w / 2, m->y + m->h / 2);
    focus(selmon = mon);
}

static int monitor_using_client(int index) {
    for (int i = 0; i < monitors.count; i++)
        if (MON_CLI(i) == index) return i;
    return -1;
}

static void swap_clients(int from_mon, int to_mon) {
    if (to_mon < 0 || to_mon >= monitors.count) return;

    if (from_mon < 0 || from_mon >= monitors.count) return;

    if (MON_CLI(to_mon) != -1) XUnmapWindow(dp, MON_CLI(to_mon));

    set_client(to_mon, MON_CLI(from_mon));

    MON_CLI(from_mon) = -1;
}

static bool mon_in_range(Mon mon) { return mon >= 0 && mon < monitors.count; }

static bool cli_in_range(Cli cli) { return cli >= 0 && cli < clients.count; }

static void restart(void) {
    free(monitors.array);
    free_clients();
    setup();
}

static void quit(void) {
    end();
    exit(1);
}

static void end(void) {
    free(monitors.array);
    free_clients();
    XSync(dp, False);
    XSetInputFocus(dp, PointerRoot, RevertToPointerRoot, CurrentTime);
    XCloseDisplay(dp);
    running = false;
}

static void free_clients(void) {
    for (int i = 0; i < clients.count; ++i) XDestroyWindow(dp, clients.array[i].window);
    free(clients.array);
    clients.count = clients.capacity = 0;
}

static int dummy_error_handler(Display *d, XErrorEvent *e) { return 0; }

// basically dwm code
static int error_handler(Display *d, XErrorEvent *ee) {
    if (ee->error_code == BadWindow) return 0;
    fprintf(stderr, "fatal error: request code=%d, error code=%d\n", ee->request_code, ee->error_code);
    return 1;
}

static void move_cursor(int x, int y) {
    // trash variables that have to be provided to query funcs
    // short for readability
    Window       a;
    int          b;
    unsigned int c;
    //

    int    cur_x;
    int    cur_y;
    Window cur_window;
    int    root_w;
    int    root_h;
    XQueryPointer(dp, root, &a, &cur_window, &cur_x, &cur_y, &b, &b, &c);
    XGetGeometry(dp, root, &a, &b, &b, &root_w, &root_h, &b, &b);

    // prevent cursor from being confined to a window
    int result =
        XGrabPointer(dp, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);

    if (result != GrabSuccess) {
        fprintf(stderr, "failed to move cursor - couldn't grab\n");
        return;
    }

    Monitor *m = MONITOR(selmon);
    // moves by offset to the point specified as arguments
    XWarpPointer(dp, root, root, 0, 0, root_w, root_h, x, y);

    XUngrabPointer(dp, CurrentTime);
}

#undef PRINT_XINERAMA_MONITOR
