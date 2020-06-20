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
//
// newly created clients are not automatically focused, though they are switched to properly
//
// the system for managing the shift key is totally redundant
// there is no need to grab the same already grabbed keys for mod-shift to work, since a boolean is set when shift
// pressed it will work without that
// just be sure to grab the shift key under modmask in the beginning and it will literally work exactly the same without
// the lag
//
//
// ideas:
// a keybinding which notifies date on the statusbar
// I don't personally like having date on my dsktop because I believe it limits intuition for time, but a temp
// notification is no different from checking phone so it'd be cool
//
// window swallowing support by launching the relevant client on selmon, switching to it, then switching back when it is
// selected and exits
//
// similar to ^, support for the previous client and monitor keybinding
//
//

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WHITE      0xffffff
#define BLACK      0x000000
#define RED        0xff0000
#define BLUE       0x0000ff
#define SKY_BLUE   0x1867b9
#define DARK_BLUE  0x0066ff
#define COL_NRM_FG WHITE    // normal foreground
#define COL_NRM_BG SKY_BLUE // normal background
#define COL_SEL_FG BLACK    // selected foreground
#define COL_SEL_BG WHITE    // selected background
// not implemented yet
#define COL_HID_FG BLUE  // hidden foreground
#define COL_HID_BG BLACK // hidden background

#define BAR_H                15
#define CHAR_H               10
#define CHAR_W               5
#define CELL_W               20
#define CELL_GAP             2
#define CLIENT_GROWTH_FACTOR 2
#define CLIENT_MAX           999

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
typedef enum GCType          GCType;
typedef enum SwitchDirection SwitchDirection;
typedef struct Monitor       Monitor;
typedef struct MonitorList   MonitorList;
typedef struct Client        Client;
typedef struct ClientList    ClientList;
typedef void (*EventHandler)(XEvent *);

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
    KEY_minus = 20,
    KEY_q     = 24,
    KEY_r     = 27,
    KEY_p     = 33,
    KEY_j     = 44,
    KEY_k     = 45,
    KEY_SHIFT = 50,
    KEY_c     = 54,
    KEY_MOD4  = 133,
};

enum GCType {
    NRM_FG,
    NRM_BG,
    SEL_FG,
    SEL_BG,
    HID_FG,
    HID_BG,
    GC_LAST_TYPE,
};

enum SwitchDirection {
    DOWN,
    UP,
};

typedef struct {
    KeyCode      code;
    unsigned int modifier;
} Key;

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
static Mon  open_mon(void);
static Mon  mon_using_cli(Cli);
static bool in_client_list(Window);
static void expand_clients(void);
static void append_client(Window);
static void try_remove_window(Window);
static void delete_client(Cli);
static void change_cli_for(Cli, Mon);
static void deselect_cli(Cli);
static void kill_client(Cli);
static void focus(Mon);
static void switch_client(SwitchDirection);
static void switch_monitor(SwitchDirection);
static void jump_cli(Cli);
static void jump_mon(Mon);
static void swap_clients(int, int);
static void select_mon(Mon);
static bool mon_in_range(Mon);
static bool cli_in_range(Cli);
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

static KeyCode keys[] = {
    KEY_MOD4,
};

static KeyCode mod_keys[] = {
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
    KEY_c,
    KEY_j,
    KEY_k,
    KEY_p,
    KEY_q,
    KEY_r,
    KEY_SHIFT,
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

static GC gcs[GC_LAST_TYPE];

static MonitorList monitors;
static ClientList  clients;
static Display *   dp;   // display pointer
static Screen *    sp;   // screen pointer to the struct
static int         sn;   // screen number number of display, better than calling funcs to get it every time
static Window      root; // parent of topmost windows
static int         selmon;
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
    if ((dp = XOpenDisplay(NULL)) == NULL) {
        fprintf(stderr, "failed to open display %s!\n", XDisplayString(dp));
        exit(1);
    }

    sp      = XDefaultScreenOfDisplay(dp);
    sn      = DefaultScreen(dp);
    root    = XDefaultRootWindow(dp);
    running = true;
    cursor  = XCreateFontCursor(dp, XC_left_ptr);

    XUngrabKey(dp, AnyKey, AnyModifier, root);
    for (int i = 0; i < LENGTH(keys); ++i) XGrabKey(dp, keys[i], AnyModifier, root, True, GrabModeAsync, GrabModeAsync);

    for (int i = 0; i < LENGTH(mod_keys); ++i)
        XGrabKey(dp, mod_keys[i], Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    init_gcs();
    init_clients();
    init_monitors();

    selmon = 0;

    XSetWindowAttributes root_attr
        = {.cursor     = cursor,
           .event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
                         /*KeyPressMask | KeyReleaseMask |*/ EnterWindowMask | LeaveWindowMask | FocusChangeMask};
    XChangeWindowAttributes(dp, root, CWEventMask | CWCursor, &root_attr);

    XSelectInput(dp, root, root_attr.event_mask);

    // | | ButtonPressMask | EnterWindowMask
    // | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;

    init_bars();
    draw_bars();
}

static void init_gcs(void) {
    const unsigned long value_mask = GCCapStyle | GCJoinStyle | GCForeground | GCBackground;

    XGCValues gc_vals = {
        .cap_style  = CapButt,
        .join_style = JoinBevel,
    };

    gc_vals.foreground = COL_NRM_FG;
    gc_vals.background = COL_NRM_FG;
    gcs[NRM_FG]        = XCreateGC(dp, root, value_mask, &gc_vals);

    gc_vals.foreground = COL_NRM_BG;
    gc_vals.background = COL_NRM_FG;
    gcs[NRM_BG]        = XCreateGC(dp, root, value_mask, &gc_vals);

    gc_vals.foreground = COL_SEL_FG;
    gc_vals.background = COL_SEL_BG;
    gcs[SEL_FG]        = XCreateGC(dp, root, value_mask, &gc_vals);

    gc_vals.foreground = COL_SEL_BG;
    gc_vals.background = COL_SEL_FG;
    gcs[SEL_BG]        = XCreateGC(dp, root, value_mask, &gc_vals);

    gc_vals.foreground = COL_HID_FG;
    gc_vals.background = COL_HID_BG;
    gcs[HID_FG]        = XCreateGC(dp, root, value_mask, &gc_vals);

    gc_vals.foreground = COL_HID_BG;
    gc_vals.background = COL_HID_FG;
    gcs[HID_BG]        = XCreateGC(dp, root, value_mask, &gc_vals);
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
                                                    COL_NRM_FG,
                                                    COL_NRM_BG);
        XMapWindow(dp, monitors.array[i].bar);
    }
}

static void press_mod(void) {
    for (int i = 0; i < LENGTH(shift_keys); ++i)
        XGrabKey(dp, shift_keys[i], ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

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
            if (shift)
                jump_mon(e->xkey.keycode - KEY_1 + 1);
            else
                jump_cli(e->xkey.keycode - KEY_1 + 1);
            break;
        case KEY_0:
            if (shift)
                jump_mon(0);
            else
                jump_cli(0);
            break;
        case KEY_c: kill_client(MON_CLI(selmon)); break;
        case KEY_j:
            if (shift)
                switch_monitor(DOWN);
            else
                switch_client(DOWN);
            break;
        case KEY_k:
            if (shift)
                switch_monitor(UP);
            else
                switch_client(UP);
            break;
        case KEY_p: dmenu(); break;
        case KEY_q: end(); break;
        case KEY_r: restart(); break;
        case KEY_minus: change_cli_for(-1, selmon); break;
        case KEY_MOD4: press_mod(); break;
        case KEY_SHIFT: shift = true; break;
        default: break;
    }
}

static void release_mod(void) {
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

    Mon open;
    if (!in_client_list(request.window)) {
        append_client(request.window);

        // selected monitor free
        if (MON_CLI(selmon) == -1) {
            change_cli_for(clients.count - 1, selmon);
            /*
            set_cli(selmon, clients.count - 1);
            XMapWindow(dp, request.window);
            focus(selmon);
            */
        }
        // any monitor available
        else if ((open = open_mon()) != -1) {
            // needs fixing b/c redundant focus in select_mon
            select_mon(open);
            change_cli_for(clients.count - 1, selmon);
            /*
            set_cli(open, clients.count - 1);
            XMapWindow(dp, request.window);
            select_mon(open);
            */
        }
        // no monitor available
        else // switch to new client on selected monitor
            change_cli_for(clients.count - 1, selmon);
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

static void draw_bar_on(Mon mon) {
    if (!mon_in_range(mon)) return;
    Monitor *m   = MONITOR(mon);
    Window   bar = m->bar;
    char     num[12];
    int      len;
    GC       fg;
    GC       bg;
    int      x;

    fg  = gcs[selmon == mon ? SEL_FG : NRM_FG];
    bg  = gcs[selmon == mon ? SEL_BG : NRM_BG];
    len = sprintf(num, "Monitor %d", mon);

    draw_filled_rectangle(bar, bg, 0, 0, m->w, BAR_H);
    XDrawString(dp, bar, fg, m->w - len * 10, BAR_H / 2 + CHAR_H / 2, num, len);

    // starts at -1 to mark when no clients selected
    for (int i = -1; i < clients.count; ++i) {
        fg = gcs[m->cli == i ? SEL_FG : NRM_FG];
        bg = gcs[m->cli == i ? SEL_BG : NRM_BG];
        x  = (i + 1) * (CELL_W + CELL_GAP);

        draw_filled_rectangle(bar, bg, x, 0, CELL_W, BAR_H);

        len = sprintf(num, "%d", i);
        XDrawString(dp, bar, fg, x + (CELL_W / 2 - CHAR_W * len / 2), BAR_H / 2 + CHAR_H / 2, num, len);
    }
}

static void draw_filled_rectangle(Drawable d, GC gc, int x, int y, int w, int h) {
    for (int y_i = y; y_i < y + h; ++y_i) XDrawLine(dp, d, gc, x, y_i, x + w, y_i);
}

static bool in_client_list(Window w) {
    for (int cli = 0; cli < clients.count; cli++)
        if (CLI_WINDOW(cli) == w) return true;
    return false;
}

static Mon open_mon(void) {
    for (int mon = 0; mon < monitors.count; ++mon)
        if (MON_CLI(mon) == -1) return mon;
    return -1;
}

static void try_remove_window(Window w) {
    // bool used in case client not removed
    bool removed = false;

    for (int i = 0; i < clients.count;)
        if (clients.array[i].window == w) {
            delete_client(i);
            deselect_cli(i);
            removed = true;
        } else
            ++i;

    if (removed) draw_bars();
}

static void delete_client(Cli cli) {
    // range check
    if (cli < 0 || cli >= clients.count) return;

    --clients.count;

    // shift items back one, overwriting index
    for (Cli i = cli; i < clients.count; ++i) clients.array[i] = clients.array[i + 1];
}

// select client displayed on selected monitor; -1 to select none
static void change_cli_for(Cli cli, Mon mon) {
    if (mon == -1) return;

    // get current monitor before setting
    Mon prevmon = mon_using_cli(cli);

    // remove the window currently on the monitor if it exists
    if (MON_CLI(mon) != -1) {
        XUnmapWindow(dp, MON_WINDOW(mon));
        MON_CLI(mon) = -1;
    }

    if (cli != -1) {
        Monitor *m = MONITOR(mon);
        // resize cli to fit monitor
        XMoveResizeWindow(dp, CLI_WINDOW(cli), m->x, m->y + BAR_H, m->w, m->h - BAR_H);
    }

    MON_CLI(mon) = cli;

    // if client was in use
    if (prevmon != -1) {
        MON_CLI(prevmon) = -1;
        draw_bar_on(prevmon);
    } else if (cli != -1)
        XMapWindow(dp, MON_WINDOW(mon));

    focus(mon);
    draw_bar_on(mon);
}

static void deselect_cli(int index) {
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

static int wrap_cli(Cli cli) {
    Cli ret = cli;
    // negative wrapping
    while (ret < -1) ret += clients.count + 1;
    // positive wrapping
    while (ret >= clients.count) ret -= clients.count + 1;

    return ret;
}

static Mon wrap_mon(Mon mon) {
    Mon ret = mon;
    while (ret < 0) ret += monitors.count;
    while (ret >= monitors.count) ret -= monitors.count;
    return ret;
}

static void focus(Mon mon) {
    // const long ptr_event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    Window w = root;
    if (MON_CLI(mon) != -1) w = MON_WINDOW(mon);
    // XGrabPointer(dp, w, False, ptr_event_mask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XSetInputFocus(dp, w, RevertToPointerRoot, CurrentTime);
    printf("focus set to monitor %d\n", selmon);
}

static void switch_client(SwitchDirection direction) {
    fprintf(stderr, "Switching clients in direction %s\n", direction == DOWN ? "DOWN" : "UP");
    switch (direction) {
        case DOWN: change_cli_for(wrap_cli(MON_CLI(selmon) - 1), selmon); break;
        case UP: change_cli_for(wrap_cli(MON_CLI(selmon) + 1), selmon); break;
    }
}

static void jump_cli(Cli cli) { change_cli_for(cli_in_range(cli) ? cli : -1, selmon); }

static void jump_mon(Mon mon) { select_mon(mon_in_range(mon) ? mon : selmon); }

static void switch_monitor(SwitchDirection direction) {
    fprintf(stderr, "Switching monitors in direction %s\n", direction == DOWN ? "DOWN" : "UP");
    switch (direction) {
        case DOWN: select_mon(wrap_mon(selmon - 1)); break;
        case UP: select_mon(wrap_mon(selmon + 1)); break;
    }
}

static void select_mon(Mon mon) {
    Mon prevmon = selmon;

    Monitor *m = MONITOR(mon);

    move_cursor(m->x + m->w / 2, m->y + m->h / 2);
    focus(selmon = mon);

    draw_bar_on(prevmon);
    draw_bar_on(selmon);
}

static Mon mon_using_cli(Cli cli) {
    if (cli != -1)
        for (Mon mon = 0; mon < monitors.count; mon++)
            if (MON_CLI(mon) == cli) return mon;
    return -1;
}

static bool mon_in_range(Mon mon) { return mon >= 0 && mon < monitors.count; }

static bool cli_in_range(Cli cli) { return cli >= 0 && cli < clients.count; }

static void restart(void) {
    end();
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
    XQueryPointer(dp, root, &a, &cur_window, &cur_x, &cur_y, &b, &b, &c);

    int root_w;
    int root_h;
    XGetGeometry(dp, root, &a, &b, &b, &root_w, &root_h, &b, &b);

    // prevent cursor from being confined to a window
    if (XGrabPointer(dp, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime)
        != GrabSuccess) {
        fprintf(stderr, "failed to move cursor - couldn't grab\n");
        return;
    }

    // moves by offset to the point specified as arguments
    XWarpPointer(dp, root, root, 0, 0, root_w, root_h, x, y);

    XUngrabPointer(dp, CurrentTime);
}

#undef PRINT_XINERAMA_MONITOR
