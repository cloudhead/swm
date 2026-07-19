// clang-format off

/* Short names used to compose modifiers in the key and button tables. */
#define MOD   WLR_MODIFIER_LOGO
#define SHIFT WLR_MODIFIER_SHIFT
#define CTRL  WLR_MODIFIER_CTRL
#define ALT   WLR_MODIFIER_ALT

/* Create this many numbered workspaces. Valid values are 1 through 32. */
#define WSCOUNT (10)

/* Convert an 0xRRGGBBAA color to the floating-point format used by wlroots. */
#define RGBA(hex)                                                              \
    { ((hex >> 24) & 0xFF) / 255.0f,                                           \
      ((hex >> 16) & 0xFF) / 255.0f,                                           \
      ((hex >> 8)  & 0xFF) / 255.0f,                                           \
       (hex        & 0xFF) / 255.0f }

/* Focus follows the pointer when enabled. */
static const bool         sloppyfocus   = true;
/* Borders are measured in physical layout pixels. */
static const unsigned int borderwidth   = 1;
/* Fill uncovered parts of the desktop with this color. */
static const float        rootcolor[]   = RGBA(0x222222ff);
/* Draw this color around an unfocused window. */
static const float        bordercolor[] = RGBA(0x101010ff);
/* Draw this color around the focused window. */
static const float        focuscolor[]  = RGBA(0x404040ff);
/* Draw this color around a window requesting attention. */
static const float        urgentcolor[] = RGBA(0xb03060ff);
/* Place this translucent color behind matching layer surfaces. */
static const float        dimcolor[]    = RGBA(0x00000080);

/* This background hides other windows behind transparent fullscreen content,
 * as required by the XDG shell protocol. Set its alpha to zero to disable it. */
static const float fullscreen_bg[] = {
    0.0f, 0.0f, 0.0f, 1.0f
};

/* Report errors only. Use WLR_DEBUG for detailed wlroots diagnostics. */
static int log_level = WLR_ERROR;

/* Window rules are applied in order; later matches override earlier ones.
 * Use "*" as the application ID to match every window. Workspace and monitor
 * numbers start at zero, and -1 keeps the window's default placement. */
static const rule_t rules[] = {
    /* application ID         title    workspace  floating  monitor  borderless */
    { "org.telegram.desktop", nullptr,  -1, true,        -1,        true },
    {},
};

/* Layer rules match panels, launchers, and other desktop surfaces by namespace. */
static const layer_rule_t layerrules[] = {
    /* namespace  background dim color */
    { "launcher", dimcolor },
};

/* Layouts marked true participate in MOD+Space cycling. The max-stack layout
 * is selected separately with MOD+F. */
static const layout_t layouts[] = {
    /* arrangement function  cycle with MOD+Space */
    { master_left,        true },
    { master_top,         true },
    { master_right,       true },
    { master_bottom,      true },
    { max_stack,          false },
};

/* Display rules match by output name. A nullptr name is the required fallback
 * rule. The position (-1, -1) asks wlroots to place the display automatically;
 * other negative positions can break XWayland applications. */
static const monitor_rule_t monrules[] = {
    /* name        scale  rotation/reflection          x    y
     * Example for a high-density laptop display:
     { "eDP-1",    2,    WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
    { nullptr, 1.5, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1 },
    /* Keep at least one fallback rule. */
};

/* XKB keyboard layout shared by physical keyboards. */
static const struct xkb_rule_names xkb_rules = {
    /* XKB layout name. */
    .layout  = "us",
    /* Layout variant; altgr-intl provides international characters via AltGr. */
    .variant = "altgr-intl",
    /* Comma-separated XKB options, or nullptr for none. */
    .options = nullptr,
};

/* Repeat a held key this many times per second. */
static const int repeat_rate  = 40;
/* Wait this many milliseconds before a held key starts repeating. */
static const int repeat_delay = 180;

/* Pointer and touchpad behavior. Unsupported options are ignored per device. */
/* Tap the touchpad to produce a click. */
static const bool tap_to_click            = true;
/* Keep a tapped item grabbed while the finger moves. */
static const bool tap_and_drag            = true;
/* Keep dragging after the finger is briefly lifted and placed down again. */
static const bool drag_lock               = true;
/* Reverse the usual scrolling direction when enabled. */
static const bool natural_scrolling       = false;
/* Ignore touchpad input while typing. */
static const bool disable_while_typing    = true;
/* Swap the primary and secondary pointer buttons when enabled. */
static const bool left_handed             = false;
/* Press the left and right buttons together to produce a middle click. */
static const bool middle_button_emulation = false;

/* Scroll with two fingers. Other choices are NO_SCROLL, EDGE, and
 * ON_BUTTON_DOWN. */
static const enum libinput_config_scroll_method scroll_method =
    LIBINPUT_CONFIG_SCROLL_2FG;

/* Divide the bottom of a clickpad into physical button areas. Other choices
 * are NONE and CLICKFINGER. */
static const enum libinput_config_click_method click_method =
    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* Keep pointer events enabled. They can instead be disabled always or only
 * while an external mouse is connected. */
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* Use adaptive pointer acceleration. FLAT disables the acceleration curve. */
static const enum libinput_config_accel_profile accel_profile =
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
/* Pointer speed ranges from -1.0 (slowest) to 1.0 (fastest); 0.0 is neutral. */
static const double accel_speed = 0.0;

/* Map one-, two-, and three-finger taps to left, right, and middle clicks. */
static const enum libinput_config_tap_button_map button_map =
    LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Commands are argument vectors and must end with nullptr. */
/* Terminal opened by MOD+Shift+Return. */
static const char *termcmd[] = { "foot", nullptr };
/* Application launcher opened by MOD+P. */
static const char *menucmd[] = { "fuzzel", nullptr };
/* Session locker started by MOD+X. */
static const char *lockcmd[] = { "swaylock", "-f", "-c", "000000", nullptr };

/* Run these commands once at startup. A nullptr separates commands, and a
 * second nullptr ends the list. Environment variables are expanded without
 * shell word splitting. */
static const char *const autostart[] = {
    "dbus-update-activation-environment", "--systemd",     "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", nullptr,
    "foot",                               "--login-shell",                                           nullptr,
    "waybar",                                                                                        nullptr,
    "swaybg",                             "-m", "tile", "-i", "$HOME/images/wallpaper.png",          nullptr,
    "sh",                                 "-c", "wl-paste --type text --watch cliphist store",       nullptr,
    "sh",                                 "-c", "wl-paste --type image --watch cliphist store",      nullptr,
    nullptr /* End the command list. */
};

static const key_t keys[] = {
    /* Each row maps a modifier and key to an action and its optional argument. */
    /* modifier        key                  function           argument */
    { MOD,             XKB_KEY_p,           spawn,             { .v = menucmd } },
    { MOD | SHIFT,     XKB_KEY_Return,      spawn,             { .v = termcmd } },
    { MOD,             XKB_KEY_j,           focus_stack,       { .i = +1 } },
    { MOD,             XKB_KEY_Tab,         view,              { .i = -1 } },
    { MOD,             XKB_KEY_k,           focus_stack,       { .i = -1 } },
    { MOD,             XKB_KEY_h,           stack_config,      { .i = MASTER_SHRINK } },
    { MOD,             XKB_KEY_l,           stack_config,      { .i = MASTER_GROW } },
    { MOD,             XKB_KEY_comma,       stack_config,      { .i = MASTER_ADD } },
    { MOD,             XKB_KEY_period,      stack_config,      { .i = MASTER_DELETE } },
    { MOD | SHIFT,     XKB_KEY_less,        stack_config,      { .i = STACK_INCREMENT } },
    { MOD | SHIFT,     XKB_KEY_greater,     stack_config,      { .i = STACK_DECREMENT } },
    { MOD | SHIFT,     XKB_KEY_space,       stack_config,      { .i = STACK_RESET } },
    { MOD | SHIFT,     XKB_KEY_bar,         stack_config,      { .i = LAYOUT_FLIP } },
    { MOD | SHIFT,     XKB_KEY_j,           swap_client,       { .i = +1 } },
    { MOD | SHIFT,     XKB_KEY_k,           swap_client,       { .i = -1 } },
    { MOD,             XKB_KEY_Return,      zoom,              {} },
    { MOD,             XKB_KEY_m,           focus_main,        {} },
    { MOD,             XKB_KEY_u,           focus_urgent,      {} },
    { MOD,             XKB_KEY_r,           raise_client,      {} },
    { MOD,             XKB_KEY_a,           view,              { .i = -1 } },
    { MOD,             XKB_KEY_space,       cycle_layout,      {} },
    { MOD,             XKB_KEY_t,           toggle_floating,   {} },
    { MOD,             XKB_KEY_f,           toggle_max_stack,  {} },
    { MOD | SHIFT,     XKB_KEY_f,           toggle_fullscreen, {} },
    { MOD,             XKB_KEY_x,           spawn,             { .v = lockcmd } },
    { MOD | SHIFT,     XKB_KEY_C,           kill_client,       {} },
    /* Left and Right skip empty workspaces; Up and Down include them. Holding
     * Shift moves the focused window while cycling. */
    { MOD,             XKB_KEY_Right,       cycle_workspace,   { .i = WORKSPACE_NEXT } },
    { MOD,             XKB_KEY_Left,        cycle_workspace,   { .i = WORKSPACE_PREVIOUS } },
    { MOD,             XKB_KEY_Up,          cycle_workspace,   { .i = WORKSPACE_NEXT_ALL } },
    { MOD,             XKB_KEY_Down,        cycle_workspace,   { .i = WORKSPACE_PREVIOUS_ALL } },
    { MOD | SHIFT,     XKB_KEY_Up,          cycle_workspace,   { .i = WORKSPACE_NEXT_MOVE } },
    { MOD | SHIFT,     XKB_KEY_Down,        cycle_workspace,   { .i = WORKSPACE_PREVIOUS_MOVE } },
    /* Shift selects a neighboring display; Control moves the focused window. */
    { MOD | SHIFT,     XKB_KEY_Left,        focus_monitor,     { .i = WLR_DIRECTION_LEFT } },
    { MOD | SHIFT,     XKB_KEY_Right,       focus_monitor,     { .i = WLR_DIRECTION_RIGHT } },
    { MOD | CTRL,      XKB_KEY_Left,        tag_monitor,       { .i = WLR_DIRECTION_LEFT } },
    { MOD | CTRL,      XKB_KEY_Right,       tag_monitor,       { .i = WLR_DIRECTION_RIGHT } },
    { MOD,             XKB_KEY_1,           view,              { .i = 0 } },
    { MOD | SHIFT,     XKB_KEY_exclam,      tag,               { .i = 0 } },
    { MOD,             XKB_KEY_2,           view,              { .i = 1 } },
    { MOD | SHIFT,     XKB_KEY_at,          tag,               { .i = 1 } },
    { MOD,             XKB_KEY_3,           view,              { .i = 2 } },
    { MOD | SHIFT,     XKB_KEY_numbersign,  tag,               { .i = 2 } },
    { MOD,             XKB_KEY_4,           view,              { .i = 3 } },
    { MOD | SHIFT,     XKB_KEY_dollar,      tag,               { .i = 3 } },
    { MOD,             XKB_KEY_5,           view,              { .i = 4 } },
    { MOD | SHIFT,     XKB_KEY_percent,     tag,               { .i = 4 } },
    { MOD,             XKB_KEY_6,           view,              { .i = 5 } },
    { MOD | SHIFT,     XKB_KEY_asciicircum, tag,               { .i = 5 } },
    { MOD,             XKB_KEY_7,           view,              { .i = 6 } },
    { MOD | SHIFT,     XKB_KEY_ampersand,   tag,               { .i = 6 } },
    { MOD,             XKB_KEY_8,           view,              { .i = 7 } },
    { MOD | SHIFT,     XKB_KEY_asterisk,    tag,               { .i = 7 } },
    { MOD,             XKB_KEY_9,           view,              { .i = 8 } },
    { MOD | SHIFT,     XKB_KEY_parenleft,   tag,               { .i = 8 } },
    { MOD,             XKB_KEY_0,           view,              { .i = 9 } },
    { MOD | SHIFT,     XKB_KEY_parenright,  tag,               { .i = 9 } },
    { MOD | SHIFT,     XKB_KEY_q,           quit,              {} },

    /* Preserve the traditional Ctrl+Alt+Backspace shortcut for quitting. */
    { CTRL | ALT,      XKB_KEY_Terminate_Server, quit,      {} },
    /* Ctrl+Alt+Fn switches to another virtual terminal. */
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_1,  change_vt, { .u = 1 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_2,  change_vt, { .u = 2 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_3,  change_vt, { .u = 3 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_4,  change_vt, { .u = 4 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_5,  change_vt, { .u = 5 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_6,  change_vt, { .u = 6 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_7,  change_vt, { .u = 7 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_8,  change_vt, { .u = 8 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_9,  change_vt, { .u = 9 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_10, change_vt, { .u = 10 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_11, change_vt, { .u = 11 } },
    { CTRL | ALT,      XKB_KEY_XF86Switch_VT_12, change_vt, { .u = 12 } },
};

static const button_t buttons[] = {
    /* Hold MOD while clicking a window to move, toggle, or resize it. */
    /* Drag with the primary button to move the window. */
    { MOD, BTN_LEFT, move_resize, { .u = CURSOR_MOVE } },
    /* Click the middle button to toggle floating. */
    { MOD, BTN_MIDDLE, toggle_floating, {} },
    /* Drag with the secondary button to resize from the nearest corner. */
    { MOD, BTN_RIGHT, move_resize, { .u = CURSOR_RESIZE } },
};
// clang-format on
