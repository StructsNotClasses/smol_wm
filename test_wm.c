// Small Manager of cLients Window Manager (SMOLWM)
//
// issues //////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// if I move to monitor 3 then create a new client, the new client is placed on monitor two and that on monitor three
// remains
//
// ideas ///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// extension support (custom fpointers most probably)
//
// a keybinding which notifies date on the statusbar
// I don't personally like having date on my desktop because I believe it limits intuition for time, but a temp
// notification is no different from checking phone so it'd be cool
//
// window swallowing support, probably with a keybind, that tells wm to switch to prevcli on selmon when the current
// client closes
//
// check at beginning for windows and take control of them
// would let restart avoid killing windows
//
// color for clients not displayed
//
// definitely needs a preview mode
// rather than what I'd thought of, preview mode could just be a way to jump to different monitors if the client were
// displayed there rather than moving it
// probably shouldn't actually select the monitor
// just do some dummy selection thing that draws a new color in the bar
//
// dependencies ////////////////////////////////////////////////////////////////////////////////////////////////////
// gcc, X11, Xinerama, dmenu
//
// keybindings /////////////////////////////////////////////////////////////////////////////////////////////////////
// mod4 is the windows key on most keyboards by the way
//
// mod4 + p: run dmenu, allowing you to create a new client
//
// mod4 + j: move one client down, wrapping if none selected
// mod4 + k: move one client up, wrapping if at the end of the list
//
// mod4 + shift + j: move one monitor left, wrapping if at the end
// mod4 + shift + k: move one monitor right, wrapping if at the end
//
// mod4 + tab: move to the previously selected client on the current monitor
// mod4 + shift + tab: move to the previously selected monitor
//
// mod4 + 1-9: move to client at index if it exists; will select none if not
// mod4 + shift + 1-9: move to the monitor at index if it exists
//
// mod4 + -: select none/deselect on current monitor
//
// mod4 + shift + -: select non/deselect on all monitors
//
// mod4 + c: close the selected client
//
// mod4 + space: cycle mode of current monitor
//
// mod4 + b: toggle visibility of bar on the current monitor
//
// mod4 + l: toggles dummy mode. Deselects all clients and locks input, displaying fake empty bar. Press again to return
// to selected clients.
//
// mod4 + r: restart, attempts to kill all clients then reloads the wm; will redetermine monitor locations.
// mod4 + shift + q: quit, attempts to kill all clients then exits xorg

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

#define MODE_SEL_ICON      "[]"
#define MODE_SEL_ICON_LEN  2
#define MODE_SAFE_ICON     "[[]]"
#define MODE_SAFE_ICON_LEN 4

#define BAR_H                15
#define CHAR_H               10
#define CHAR_W               5
#define CELL_W               20
#define CELL_GAP             2
#define CLIENT_GROWTH_FACTOR 2
#define CLIENT_MAX           999

#define PRINT_XINERAMA_MONITOR(mon) \
    fprintf(stderr, "screen %d: (%d, %d) %d by %d\n", mon.screen_number, mon.x_org, mon.y_org, mon.width, mon.height)

#define NONE (-1)

// could be the same macro, but this allows seperate refactoring
#define CLI_EXISTS(cli) (cli != NONE)
#define MON_EXISTS(mon) (mon != NONE)

// utility macros that make it possible to use indices, rather than pointers, throughout the codebase
#define CLI(client)      (client - clients.array)
#define MON(monitor)     (monitor - monitors.array)
#define CLIENT(cli)      (clients.array + cli)
#define MONITOR(mon)     (monitors.array + mon)
#define CLI_WINDOW(cli)  clients.array[cli].window
#define MON_CLI(mon)     monitors.array[mon].cli
#define MON_PREVCLI(mon) monitors.array[mon].prevcli
#define MON_WINDOW(mon)  CLI_WINDOW(MON_CLI(mon))

#define LENGTH(array) (sizeof(array) / sizeof(array[0]))

// Cli and Mon used to represent client and monitor indices respectively
// using Cli and Mon may seem pointless, but makes the intention of many statements much more clear
typedef int                  Cli;
typedef int                  Mon;
typedef enum GCType          GCType;
typedef enum SwitchDirection SwitchDirection;
typedef enum MonitorMode     MonitorMode;
typedef struct Monitor       Monitor;
typedef struct MonitorList   MonitorList;
typedef struct Client        Client;
typedef struct ClientList    ClientList;
typedef void (*EventHandler)(XEvent *);

enum Key {
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
    KEY_MINUS = 20,
    KEY_TAB   = 23,
    KEY_q     = 24,
    KEY_r     = 27,
    KEY_p     = 33,
    KEY_j     = 44,
    KEY_k     = 45,
    KEY_l     = 46,
    KEY_SHIFT = 50,
    KEY_c     = 54,
    KEY_b     = 56,
    KEY_SPACE = 65,
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

enum MonitorMode {
    MODE_SEL,
    MODE_SAFE,
    MODE_LAST, // used to determine number of modes
};

typedef struct {
    KeyCode      code;
    unsigned int modifier;
} Key;

struct Monitor {
    short       x;
    short       y;
    short       w;
    short       h;
    Cli         cli;
    Cli         prevcli;
    Window      bar;
    bool        bar_visible;
    MonitorMode mode;
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
static void create(XEvent *);
static void unmap(XEvent *);
static void focus_in(XEvent *);
static void enter(XEvent *);
static void destroy(XEvent *);
static void nothing(XEvent *);
static void dmenu(void);
static void toggle_lock(void);
static void fit_bar(Monitor *);
static void toggle_bar(Mon);
static void draw_bars(const ClientList *const);
static void draw_bar_on(Mon mon, const ClientList *const);
static void draw_filled_rectangle(Drawable, GC, int, int, int, int);
static Mon  open_mon(void);
static Mon  mon_using_cli(Cli);
static bool in_client_list(Window);
static void expand_clients(void);
static void append_client(Window);
static void try_remove_window(Window);
static Cli  prev_cli(void);
static void delete_cli(Cli);
static void clear_mon(Mon);
static void fit_cli_to_mon(Cli, Mon);
static void change_cli_for_all(Cli);
static void change_cli_for_mon(Cli, Mon);
static void deselect_cli(Cli);
static void kill_cli(Cli);
static void focus(Mon);
static void switch_client(SwitchDirection);
static void switch_monitor(SwitchDirection);
static void switch_mode(SwitchDirection);
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
static void replace_cli(Cli, Cli);

static EventHandler handle_event[LASTEvent] = {
    [FocusIn]     = focus_in,
    [EnterNotify] = enter,
    [KeyPress]    = key_down,
    [KeyRelease]  = key_up,
    [MapRequest]  = map,
    //[CreateNotify]  = create,
    [DestroyNotify] = destroy,
};

static KeyCode keys[] = {
    KEY_MOD4,
};

static KeyCode mod_keys[] = {
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,     KEY_0,
    KEY_c, KEY_j, KEY_k, KEY_l, KEY_p, KEY_q, KEY_r, KEY_b, KEY_SHIFT,
};

static GC          gcs[GC_LAST_TYPE];
static MonitorList monitors;
static ClientList  clients;
static Display *   dp;   // display pointer
static Screen *    sp;   // screen pointer to the struct
static int         sn;   // screen number number of display, better than calling funcs to get it every time
static Window      root; // parent of topmost windows
static Mon         prevmon;
static Mon         selmon;
static GC          black_gc;
static GC          red_gc;
static GC          blue_gc;
static bool        mod     = false; // mod4
static bool        shift   = false;
static bool        running = false;
static bool        locked  = false;
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
    XSetErrorHandler(error_handler);

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

    prevmon = selmon = 0;

    XSetWindowAttributes root_attr
        = {.cursor     = cursor,
           .event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
                         /*KeyPressMask | KeyReleaseMask |*/ EnterWindowMask | LeaveWindowMask | FocusChangeMask};
    XChangeWindowAttributes(dp, root, CWEventMask | CWCursor, &root_attr);

    XSelectInput(dp, root, root_attr.event_mask);

    // | | ButtonPressMask | EnterWindowMask
    // | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;

    init_bars();
    draw_bars(&clients);
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
        fprintf(stderr, "please use xinerama. quitting...\n");
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
        monitors.array[count] = (Monitor){
            .x           = screen_info[count].x_org,
            .y           = screen_info[count].y_org,
            .w           = screen_info[count].width,
            .h           = screen_info[count].height,
            .cli         = NONE,
            .prevcli     = NONE,
            .bar_visible = true,
            .mode        = MODE_SEL,
        };
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
            i = NONE;
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

static void key_down(XEvent *e) {
    if (e->xkey.keycode == KEY_l) toggle_lock();

    if (!locked) switch (e->xkey.keycode) {
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
            case KEY_b: toggle_bar(selmon); break;
            case KEY_c: kill_cli(MON_CLI(selmon)); break;
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
            case KEY_q:
                if (shift) end();
                break;
            case KEY_r: restart(); break;
            case KEY_MINUS:
                if (shift)
                    change_cli_for_all(NONE);
                else
                    change_cli_for_mon(NONE, selmon);
                break;
            case KEY_SPACE:
                if (shift)
                    switch_mode(DOWN);
                else
                    switch_mode(UP);
                break;
            case KEY_TAB:
                if (shift)
                    jump_mon(prevmon);
                else
                    change_cli_for_mon(MON_PREVCLI(selmon), selmon);
                break;
            case KEY_SHIFT: shift = true; break;
            default: break;
        }
}

static void key_up(XEvent *e) {
    switch (e->xkey.keycode) {
        case KEY_SHIFT: shift = false; break;
        default: break;
    }
}

static void new_window(Window w) {
    // is it a good idea to map any window that makes a request while in clientlist?
    Mon openmon = open_mon();
    if (CLI_EXISTS(MON_CLI(selmon)) && MON_EXISTS(openmon))
        // needs fixing b/c redundant focus in select_mon
        select_mon(openmon);

    append_client(w);

    if (!locked) {
        Cli newcli = clients.count - 1;
        change_cli_for_mon(newcli, selmon);

        draw_bars(&clients);
    }
}

// runs whenever a window is made visible, which is the cue to start managing it
// I don't see why a wm would manage invisible/unmapped windows anyway
static void map(XEvent *e) {
    XMapRequestEvent request = e->xmaprequest;

    if (request.parent != root || in_client_list(request.window))
        XMapWindow(dp, request.window);
    else
        new_window(request.window);
}

static bool is_monitor_bar(Window w) {
    for (Monitor *m = monitors.array; m < monitors.array + monitors.count; ++m)
        if (m->bar == w) return true;
    return false;
}

static void create(XEvent *e) {
    XCreateWindowEvent create_event = e->xcreatewindow;
    if (create_event.parent == root && !in_client_list(create_event.window) && !is_monitor_bar(create_event.window))
        new_window(create_event.window);
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

static void toggle_lock(void) {
    locked = !locked;

    if (locked) {
        change_cli_for_all(NONE);
        ClientList fake_list = {
            .array    = NULL,
            .count    = 0,
            .capacity = 0,
        };
        draw_bars(&fake_list);
    } else {
        for (Mon cur = 0; cur < monitors.count; ++cur) change_cli_for_mon(MON_PREVCLI(cur), cur);
        draw_bars(&clients);
    }
}

static void fit_bar(Monitor *m) { XMoveResizeWindow(dp, m->bar, m->x, m->y, m->w, BAR_H); }

static void toggle_bar(Mon mon) {
    Monitor *m     = MONITOR(mon);
    m->bar_visible = !m->bar_visible;

    if (m->bar_visible) {
        fit_bar(m);
        draw_bar_on(mon, &clients);
    } else
        XResizeWindow(dp, m->bar, 1, 1);
    fit_cli_to_mon(m->cli, mon);
}

static void append_client(Window w) {
    if (clients.count >= CLIENT_MAX) {
        fprintf(stderr,
                "Due to string drawing limitations, only %d simultaneous clients are supported at this time. Have a "
                "cheerio day!",
                CLIENT_MAX);
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

static void draw_bars(const ClientList *const clients) {
    for (Mon mon = 0; mon < monitors.count; ++mon) { draw_bar_on(mon, clients); }
}

static GC bar_fg_col(Mon mon) { return gcs[selmon == mon && !locked ? SEL_FG : NRM_FG]; }

static GC bar_bg_col(Mon mon) { return gcs[selmon == mon && !locked ? SEL_BG : NRM_BG]; }

static GC cli_fg_col(Monitor *m, Cli cli) { return gcs[m->cli == cli ? SEL_FG : NRM_FG]; }

static GC cli_bg_col(Monitor *m, Cli cli) { return gcs[m->cli == cli ? SEL_BG : NRM_BG]; }

static void draw_monitor_number(const Monitor *const m, const char *const str, int len) {
    XDrawString(dp, m->bar, bar_fg_col(MON(m)), m->w - len * 10, BAR_H / 2 + CHAR_H / 2, str, len);
}

static void draw_monitor_mode(Monitor *m, int offset) {
    switch (m->mode) {
        case MODE_SEL:
            XDrawString(dp,
                        m->bar,
                        bar_fg_col(MON(m)),
                        m->w - offset - MODE_SEL_ICON_LEN * 10,
                        BAR_H / 2 + CHAR_H / 2,
                        MODE_SEL_ICON,
                        MODE_SEL_ICON_LEN);
            break;
        case MODE_SAFE:
            XDrawString(dp,
                        m->bar,
                        bar_fg_col(MON(m)),
                        m->w - offset - MODE_SAFE_ICON_LEN * 10,
                        BAR_H / 2 + CHAR_H / 2,
                        MODE_SAFE_ICON,
                        MODE_SAFE_ICON_LEN);
            break;
    }
}

static void draw_bar_on(Mon mon, const ClientList *const clients) {
    if (!mon_in_range(mon)) return;

    Monitor *m = MONITOR(mon);
    if (!m->bar_visible) return;

    Window bar = m->bar;
    char   num[12];
    int    len;
    GC     fg;
    GC     bg;
    int    x;

    len = sprintf(num, "Monitor %d", mon);

    draw_filled_rectangle(bar, bar_bg_col(mon), 0, 0, m->w, BAR_H);
    draw_monitor_number(m, num, len);
    draw_monitor_mode(m, len * 10);

    // starts at -1 to mark when no clients selected
    // this has a big problem if the NONE constant is changed to something other than -1
    // needs to be fixed
    for (Cli cli = -1; cli < clients->count; ++cli) {
        x = (cli + 1) * (CELL_W + CELL_GAP);

        draw_filled_rectangle(bar, cli_bg_col(m, cli), x, 0, CELL_W, BAR_H);

        len = sprintf(num, "%d", cli);
        XDrawString(dp, bar, cli_fg_col(m, cli), x + (CELL_W / 2 - CHAR_W * len / 2), BAR_H / 2 + CHAR_H / 2, num, len);
    }
}

static void draw_filled_rectangle(Drawable d, GC gc, int x, int y, int w, int h) {
    // not sure whether this or nested loops w/XDrawPoint is worse, but meh
    for (int y_i = y; y_i < y + h; ++y_i) XDrawLine(dp, d, gc, x, y_i, x + w, y_i);
}

static bool in_client_list(Window w) {
    for (int cli = 0; cli < clients.count; cli++)
        if (CLI_WINDOW(cli) == w) return true;
    return false;
}

static Mon open_mon(void) {
    for (Mon mon = 0; mon < monitors.count; ++mon)
        if (!CLI_EXISTS(MON_CLI(mon))) return mon;
    return NONE;
}

static void try_remove_window(Window w) {
    bool removed = false;

    for (int i = 0; i < clients.count;)
        if (clients.array[i].window == w) {
            delete_cli(i);
            deselect_cli(i);
            removed = true;
        } else
            ++i;

    if (removed) draw_bars(&clients);
}

static Cli prev_cli(void) { return MONITOR(selmon)->prevcli; }

static void delete_cli(Cli cli) {
    // range check
    if (cli < 0 || cli >= clients.count) return;

    --clients.count;

    // shift items back one, overwriting cli
    for (Cli cur = cli; cur < clients.count; ++cur) {
        clients.array[cur] = clients.array[cur + 1];
        replace_cli(cur + 1, cur);
    }
}

static void clear_mon(Mon mon) {
    if (CLI_EXISTS(MON_CLI(mon))) {
        XUnmapWindow(dp, MON_WINDOW(mon));
        MON_CLI(mon) = NONE;
        draw_bar_on(mon, &clients);
    }
}

static void fit_cli_to_mon(Cli cli, Mon mon) {
    if (CLI_EXISTS(cli)) {
        Monitor *m = MONITOR(mon);
        if (m->bar_visible)
            XMoveResizeWindow(dp, CLI_WINDOW(cli), m->x, m->y + BAR_H, m->w, m->h - BAR_H);
        else
            XMoveResizeWindow(dp, CLI_WINDOW(cli), m->x, m->y, m->w, m->h);
    }
}

// this should probably just be changed to a function which hides all, but this currently will properly set prevcli
static void change_cli_for_all(Cli cli) {
    for (Mon cur = 0; cur < monitors.count; ++cur) change_cli_for_mon(cli, cur);
}

// select client displayed on selected monitor; -1 to select none
static void change_cli_for_mon(Cli cli, Mon mon) {
    if (!MON_EXISTS(mon)) return;

    MON_PREVCLI(mon)   = MON_CLI(mon);
    Mon prev_using_mon = mon_using_cli(cli);

    clear_mon(mon);
    fit_cli_to_mon(cli, mon);
    MON_CLI(mon) = cli;

    if (MON_EXISTS(prev_using_mon)) { // no need to map if was previously on a diff monitor
        MON_CLI(prev_using_mon) = NONE;
        draw_bar_on(prev_using_mon, &clients);
    } else if (CLI_EXISTS(cli))
        XMapWindow(dp, MON_WINDOW(mon));

    focus(mon);
    draw_bar_on(mon, &clients);
}

static void deselect_cli(Cli cli) {
    for (Mon curmon = 0; curmon < monitors.count; ++curmon) {
        if (MON_CLI(curmon) == cli) MON_CLI(curmon) = NONE;
        if (MON_PREVCLI(curmon) == cli) MON_PREVCLI(curmon) = NONE;
    }
}

// I need to learn client messages eventually, but this is good for now
// XKillClient destroys the client's resources so not the worst bad practice
static void kill_cli(Cli cli) {
    XGrabServer(dp);
    XSetErrorHandler(dummy_error_handler);
    XSetCloseDownMode(dp, DestroyAll);
    XKillClient(dp, CLI_WINDOW(cli));
    XSync(dp, False);
    XSetErrorHandler(error_handler);
    XUngrabServer(dp);

    deselect_cli(cli);
}

// this breaks if NONE != -1, just for future reference
static Cli wrap_cli(Cli cli) {
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
    Window w = root;
    if (MON_EXISTS(MON_CLI(mon))) w = MON_WINDOW(mon);
    XSetInputFocus(dp, w, RevertToPointerRoot, CurrentTime);
}

// moves one client up/down in list on selmon
static void switch_client(SwitchDirection direction) {
    switch (direction) {
        case DOWN: change_cli_for_mon(wrap_cli(MON_CLI(selmon) - 1), selmon); break;
        case UP: change_cli_for_mon(wrap_cli(MON_CLI(selmon) + 1), selmon); break;
    }
}

static void jump_cli(Cli cli) { change_cli_for_mon(cli_in_range(cli) ? cli : NONE, selmon); }

static void jump_mon(Mon mon) { select_mon(mon_in_range(mon) ? mon : selmon); }

static void switch_monitor(SwitchDirection direction) {
    fprintf(stderr, "Switching monitors in direction %s\n", direction == DOWN ? "DOWN" : "UP");
    switch (direction) {
        case DOWN: select_mon(wrap_mon(selmon - 1)); break;
        case UP: select_mon(wrap_mon(selmon + 1)); break;
    }
}

static MonitorMode wrap_mode(MonitorMode mode) { return abs(mode) % MODE_LAST; }

static void switch_mode(SwitchDirection direction) {
    Monitor *m = MONITOR(selmon);
    switch (direction) {
        case DOWN: m->mode = wrap_mode(m->mode - 1); break;
        case UP: m->mode = wrap_mode(m->mode + 1); break;
    }
    draw_bar_on(selmon, &clients);
}

static void select_mon(Mon mon) {
    prevmon = selmon;

    Monitor *m = MONITOR(mon);

    move_cursor(m->x + m->w / 2, m->y + m->h / 2);
    focus(selmon = mon);

    draw_bar_on(prevmon, &clients);
    draw_bar_on(selmon, &clients);
}

static Mon mon_using_cli(Cli cli) {
    if (CLI_EXISTS(cli))
        for (Mon mon = 0; mon < monitors.count; mon++)
            if (MON_CLI(mon) == cli) return mon;
    return NONE;
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
    for (Cli cli = 0; cli < clients.count; ++cli) kill_cli(cli);
    free(clients.array);
    clients.count = clients.capacity = 0;
}

static int dummy_error_handler(Display *d, XErrorEvent *e) { return 0; }

// got this from dwm btw, credit to them
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

static void replace_cli(Cli old, Cli new) {
    for (Mon mon = 0; mon < monitors.count; ++mon) {
        if (MON_CLI(mon) == old) MON_CLI(mon) = new;
        if (MON_PREVCLI(mon) == old) MON_PREVCLI(mon) = new;
    }
}
