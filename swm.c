/*
 * swm.c
 * Simple Wayland window manager.
 */
#include <errno.h>
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-workspace-v1-protocol.h"
#include "swm-workspace-v1-protocol.h"
#include "swm.h"
#include "util.h"
#include "xdg-shell-protocol.h"

/* Constants and helper macros. */
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->ws && (C)->ws->mon == (M))
#define SLICE                   (32) /* Number of steps across the master area. */
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define MAX_CLIENTS             256
#define MAX_MONITORS            16
#define MAX_LAYER_SURFACES      256
#define MAX_KEYBOARD_GROUPS     32
#define MAX_POINTER_CONSTRAINTS 256
#define MAX_TEXT_INPUTS         128
#define MAX_INPUT_POPUPS        64
#define MAX_SESSION_LOCKS       4
#define MAX_PENDING_SPAWNS      256
#define MAX_WINDOW_STATES       512
#define MAX_WINDOW_STATE_FIELD  256
#define MAX_WINDOW_STATE_LINE   1024
#define MAX_STATE_PATH          4096
#define MAX_WORKSPACE_TITLE     256
#define MAX_WS_ID               16
#define MAX_STATIC_LISTENERS    512
#define MAX_WS_MANAGERS         64
#define MAX_WS_HANDLES          (MAX_WS_MANAGERS * WSCOUNT)
#define MAX_AUTOSTART           64
#define MAX_COMMAND_SIZE        1024
#define MAX_COMMAND_ARGS        64
#define LISTEN_STATIC(E, H)     listen_static((E), (H))

/* Cursor interaction state. */
enum { CURSOR_NORMAL, CURSOR_PRESSED, CURSOR_MOVE, CURSOR_RESIZE };

/* Master-stack configuration commands. */
enum {
    MASTER_SHRINK,
    MASTER_GROW,
    MASTER_ADD,
    MASTER_DELETE,
    STACK_INCREMENT,
    STACK_DECREMENT,
    STACK_RESET,
    LAYOUT_FLIP
};

/* Workspace cycling operations. */
enum {
    WORKSPACE_NEXT,
    WORKSPACE_PREVIOUS,
    WORKSPACE_NEXT_ALL,
    WORKSPACE_PREVIOUS_ALL,
    WORKSPACE_NEXT_MOVE,
    WORKSPACE_PREVIOUS_MOVE
};

/* Client surface implementations. */
enum { XDG_SHELL, LAYER_SHELL, X11 };

/* Window layers, ordered from back to front. */
enum {
    LAYER_BACKGROUND,
    LAYER_BOTTOM,
    LAYER_TILE,
    LAYER_FLOAT,
    LAYER_TOP,
    LAYER_FULLSCREEN,
    LAYER_OVERLAY,
    LAYER_BLOCK,
    NUM_LAYERS
};

/* Command argument value. */
typedef union {
    int         i;
    uint32_t    u;
    float       f;
    const void *v;
} arg_t;

/* Pointer button binding. */
typedef struct {
    unsigned int mod;
    unsigned int button;
    void (*func)(const arg_t *);
    const arg_t arg;
} button_t;

/* Display and its window-manager state. */
typedef struct monitor_t monitor_t;
/* Global workspace state. */
typedef struct workspace_t workspace_t;

/* Managed XDG or Xwayland client. */
typedef struct {
    /* This must remain first so generic surface code can read it. */
    unsigned int type; /* XDG_SHELL or X11. */

    monitor_t             *mon;
    workspace_t           *ws;
    struct wlr_scene_tree *scene;
    struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
    struct wlr_scene_tree *scene_surface;
    struct wl_list         link;
    struct wl_list         flink;
    struct wlr_box         geom;          /* Position within the layout, including borders. */
    struct wlr_box         prev;          /* Previous position and size, including borders. */
    struct wlr_box         maxstack_prev; /* Floating position and size before max layout. */
    struct wlr_box         pending_geom;  /* Size and position requested by the latest resize. */
    struct wlr_box         bounds;        /* Only the width and height are used. */
    union {
        struct wlr_xdg_surface      *xdg;
        struct wlr_xwayland_surface *xwayland;
    } surface;
    struct wlr_xdg_toplevel_decoration_v1     *decoration;
    struct wl_listener                         commit;
    struct wl_listener                         map;
    struct wl_listener                         maximize;
    struct wl_listener                         unmap;
    struct wl_listener                         destroy;
    struct wl_listener                         set_title;
    struct wl_listener                         set_appid;
    struct wl_listener                         fullscreen;
    struct wl_listener                         set_decoration_mode;
    struct wl_listener                         destroy_decoration;
    struct wlr_xdg_dialog_v1                  *dialog;
    struct wl_listener                         dialog_destroy;
    struct wl_listener                         dialog_modal;
    struct wl_listener                         set_parent;
    struct wl_listener                         activate;
    struct wl_listener                         associate;
    struct wl_listener                         dissociate;
    struct wl_listener                         configure;
    struct wl_listener                         set_hints;
    struct wlr_foreign_toplevel_handle_v1     *ftl;
    struct wlr_ext_foreign_toplevel_handle_v1 *extftl;
    monitor_t                                 *ftl_monitor;
    struct wl_listener                         ftl_activate;
    struct wl_listener                         ftl_close;
    struct wl_listener                         ftl_fullscreen;
    unsigned int                               bw;
    bool                                       is_floating, is_urgent, is_fullscreen, is_borderless;
    bool                                       is_max_stacked;
    int                                        persist_float;
    int                                        pending_map;
    uint32_t                                   resize_edges;
    uint32_t                                   resize; /* ID of a resize awaiting confirmation. */
} client_t;

/* Keyboard binding. */
typedef struct {
    uint32_t     mod;
    xkb_keysym_t keysym;
    void (*func)(const arg_t *);
    const arg_t arg;
} key_t;

/* Physical or virtual keyboard group. */
typedef struct {
    struct wlr_keyboard_group *wlr_group;
    bool                       is_virtual;
    int                        nsyms;
    const xkb_keysym_t        *keysyms; /* Valid only when nsyms is nonzero. */
    uint32_t                   mods;    /* Valid only when nsyms is nonzero. */
    struct wl_event_source    *key_repeat_source;
    struct wl_listener         modifiers;
    struct wl_listener         key;
    struct wl_listener         destroy;
} keyboard_group_t;

/* Layer-shell surface. */
typedef struct {
    /* This must remain first so generic surface code can read it. */
    unsigned int type; /* Always LAYER_SHELL. */

    monitor_t                         *mon;
    struct wlr_scene_tree             *scene;
    struct wlr_scene_rect             *dim;
    struct wlr_scene_tree             *popups;
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct wl_list                     link;
    int                                mapped;
    struct wlr_layer_surface_v1       *layer_surface;
    struct wl_listener                 destroy;
    struct wl_listener                 unmap;
    struct wl_listener                 surface_commit;
} layer_surface_t;

/* Layer-shell matching rule. */
typedef struct {
    const char  *namespace;
    const float *dim;
} layer_rule_t;

/* Workspace layout implementation. */
typedef struct {
    void (*arrange)(monitor_t *);
    bool cycle;
} layout_t;

struct monitor_t {
    struct wl_list           link;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wlr_scene_rect   *fullscreen_bg; /* Hides other layers behind fullscreen windows. */
    struct wl_listener       frame;
    struct wl_listener       destroy;
    struct wl_listener       request_state;
    struct wl_listener       output_bind;
    struct wl_listener       destroy_lock_surface;
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_box                      m;         /* Full display area within the layout. */
    struct wlr_box                      w;         /* Area available for regular windows. */
    struct wl_list                      layers[4]; /* Layer surfaces on this display. */
    workspace_t                        *ws;        /* Workspace shown on this display. */
    workspace_t *previous_workspace;               /* Workspace shown before the current one. */
    int          gamma_lut_changed;
    int          asleep;
};

/* Each workspace keeps separate settings for vertical and horizontal layouts. */
/* Master-stack layout parameters. */
typedef struct {
    int msize;  /* Master area width or height, in 1/SLICE steps. */
    int mwin;   /* Maximum number of windows in the master area. */
    int stacks; /* Stack columns, or rows in a horizontal layout. */
} stack_state_t;

/* A workspace owns its windows and layout settings, and can appear on only one display. */
struct workspace_t {
    char            name[4];
    char            title[MAX_WORKSPACE_TITLE]; /* Ephemeral human-readable label. */
    uint32_t        color; /* Ephemeral color encoded as 0xRRGGBBAA; zero means unset. */
    int             idx;
    monitor_t      *mon; /* Display showing this workspace, or nullptr if hidden. */
    const layout_t *lt;
    const layout_t *prevlt;
    stack_state_t   v, h;
    struct wl_list  handles; /* Protocol handles that publish this workspace. */
};

/* Each connected workspace client gets one manager and one group for all displays. */
/* Bound ext-workspace manager. */
typedef struct {
    struct wl_resource *resource; /* Client's workspace manager. */
    struct wl_resource *group;    /* Client's group, or nullptr after it disconnects. */
    workspace_t        *pending;  /* Workspace to activate when the client commits. */
    struct wl_list      link;     /* Entry in ws_managers. */
} workspace_manager_t;

/* Exported ext-workspace handle. */
typedef struct {
    struct wl_resource  *resource;
    workspace_t         *ws;
    workspace_manager_t *mgr;  /* nullptr after the manager is destroyed. */
    struct wl_list       link; /* Entry in workspace_t.handles. */
} workspace_handle_t;

/* Bound client for swm's ephemeral workspace metadata protocol. */
typedef struct {
    struct wl_resource *resource;
    struct wl_list      link;
} metadata_manager_t;

/* Spawn awaiting workspace assignment. */
typedef struct {
    pid_t          pid;
    workspace_t   *ws;
    struct wl_list link;
} pending_spawn_t;

/* Persisted client geometry. */
typedef struct {
    char           appid[MAX_WINDOW_STATE_FIELD];
    char           title[MAX_WINDOW_STATE_FIELD];
    struct wlr_box geom;
    struct wl_list link;
} window_state_t;

/* Connects applications to an input method such as fcitx5. */
/* Text-input relay state. */
typedef struct {
    struct wlr_text_input_v3 *ti;
    struct wl_listener        enable;
    struct wl_listener        commit;
    struct wl_listener        disable;
    struct wl_listener        destroy;
} text_input_t;

/* Input-method popup state. */
typedef struct {
    struct wlr_input_popup_surface_v2 *popup;
    struct wlr_scene_tree             *scene;
    struct wl_list                     link;
    struct wl_listener                 map;
    struct wl_listener                 unmap;
    struct wl_listener                 commit;
    struct wl_listener                 destroy;
} input_popup_t;

/* Output configuration rule. */
typedef struct {
    const char              *name;
    float                    scale;
    enum wl_output_transform rr;
    int                      x, y;
} monitor_rule_t;

/* Active pointer constraint. */
typedef struct {
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener                destroy;
} pointer_constraint_t;

/* Client matching rule. */
typedef struct {
    const char *id;
    const char *title;
    int         ws; /* Workspace number starting at zero; -1 keeps the current one. */
    bool        is_floating;
    int         monitor;
    bool        borderless;
} rule_t;

/* Session lock state. */
typedef struct {
    struct wlr_scene_tree      *scene;
    struct wlr_session_lock_v1 *lock;
    struct wl_listener          new_surface;
    struct wl_listener          unlock;
    struct wl_listener          destroy;
} session_lock_t;

/* Listener allocated from the static pool. */
typedef struct {
    struct wl_listener listener;
} static_listener_t;

/* Function declarations. */
static void         apply_bounds(client_t *c, struct wlr_box *bbox);
static void         apply_rules(client_t *c, workspace_t *defaultws);
static unsigned int client_border_width(client_t *c);
static void         arrange(monitor_t *m);
static void         arrange_layer(
    monitor_t *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive
);
static void              arrange_layers(monitor_t *m);
static void              autostart_exec(void);
static void              axis_notify(struct wl_listener *listener, void *data);
static void              button_press(struct wl_listener *listener, void *data);
static void              change_vt(const arg_t *arg);
static void              check_idle_inhibitor(struct wlr_surface *exclude);
static void              cleanup(void);
static void              cleanup_monitor(struct wl_listener *listener, void *data);
static void              cleanup_listeners(void);
static void              close_monitor(monitor_t *m);
static void              layer_surface_commit_notify(struct wl_listener *listener, void *data);
static void              commit_notify(struct wl_listener *listener, void *data);
static void              popup_commit(struct wl_listener *listener, void *data);
static void              create_decoration(struct wl_listener *listener, void *data);
static void              create_idle_inhibitor(struct wl_listener *listener, void *data);
static void              create_keyboard(struct wlr_keyboard *keyboard);
static keyboard_group_t *create_keyboard_group(bool is_virtual);
static void              create_layer_surface(struct wl_listener *listener, void *data);
static void              create_lock_surface(struct wl_listener *listener, void *data);
static void              create_monitor(struct wl_listener *listener, void *data);
static void              create_notify(struct wl_listener *listener, void *data);
static void              create_pointer(struct wlr_pointer *pointer);
static void              create_pointer_constraint(struct wl_listener *listener, void *data);
static void              create_popup(struct wl_listener *listener, void *data);
static void              cursor_constrain(struct wlr_pointer_constraint_v1 *constraint);
static void              cursor_frame(struct wl_listener *listener, void *data);
static void              cursor_warp_to_hint(void);
static void              destroy_decoration(struct wl_listener *listener, void *data);
static void              destroy_drag_icon(struct wl_listener *listener, void *data);
static void              destroy_idle_inhibitor(struct wl_listener *listener, void *data);
static void              layer_surface_destroy_notify(struct wl_listener *listener, void *data);
static void              destroy_lock(session_lock_t *lock, int unlocked);
static void              destroy_lock_surface(struct wl_listener *listener, void *data);
static void              destroy_notify(struct wl_listener *listener, void *data);
static void              destroy_pointer_constraint(struct wl_listener *listener, void *data);
static void              destroy_session_lock(struct wl_listener *listener, void *data);
static void              destroy_keyboard_group(struct wl_listener *listener, void *data);
static monitor_t        *direction_to_monitor(enum wlr_direction dir);
static void              cycle_layout(const arg_t *arg);
static void              cycle_workspace(const arg_t *arg);
static void              focus_client(client_t *c, int lift);
static client_t         *focus_close(client_t *c);
static void              focus_main(const arg_t *arg);
static void              focus_urgent(const arg_t *arg);
static void              ftl_activate_notify(struct wl_listener *listener, void *data);
static void              ftl_capture_request_notify(struct wl_listener *listener, void *data);
static void              ftl_close_notify(struct wl_listener *listener, void *data);
static void              ftl_fullscreen_notify(struct wl_listener *listener, void *data);
static void              ftl_sync(client_t *c);
static void              input_method_commit_notify(struct wl_listener *listener, void *data);
static void              input_method_create_notify(struct wl_listener *listener, void *data);
static void              input_method_destroy_notify(struct wl_listener *listener, void *data);
static struct wlr_text_input_v3 *input_method_focused_text_input(void);
static void      input_method_grab_keyboard(struct wl_listener *listener, void *data);
static void      input_method_new_popup(struct wl_listener *listener, void *data);
static void      input_method_send_state(struct wlr_text_input_v3 *ti);
static void      input_method_set_focus(struct wlr_surface *surface);
static void      position_input_popups(void);
static void      input_popup_commit(struct wl_listener *listener, void *data);
static void      input_popup_destroy(struct wl_listener *listener, void *data);
static void      input_popup_map(struct wl_listener *listener, void *data);
static bool      input_popup_position(input_popup_t *p);
static void      input_popup_unmap(struct wl_listener *listener, void *data);
static void      text_input_create_notify(struct wl_listener *listener, void *data);
static void      text_input_commit_notify(struct wl_listener *listener, void *data);
static void      text_input_destroy_notify(struct wl_listener *listener, void *data);
static void      text_input_disable_notify(struct wl_listener *listener, void *data);
static void      text_input_enable_notify(struct wl_listener *listener, void *data);
static void      focus_monitor(const arg_t *arg);
static void      focus_stack(const arg_t *arg);
static client_t *focus_top(monitor_t *m);
static bool      client_is_blocked(client_t *c);
static void      fullscreen_notify(struct wl_listener *listener, void *data);
static void      gpu_reset(struct wl_listener *listener, void *data);
static int       handle_signal(int signo, void *data);
static void      master_bottom(monitor_t *m);
static void      master_left(monitor_t *m);
static void      master_right(monitor_t *m);
static void      master_top(monitor_t *m);
static void      input_device(struct wl_listener *listener, void *data);
static int       key_binding(uint32_t mods, xkb_keysym_t sym, bool repeat);
static void      new_shortcuts_inhibitor(struct wl_listener *listener, void *data);
static bool      shortcuts_inhibited(void);
static void      key_press(struct wl_listener *listener, void *data);
static void      key_press_modifiers(struct wl_listener *listener, void *data);
static void      load_window_states(void);
static int       key_repeat(void *data);
static void      kill_client(const arg_t *arg);
static void      lock_session(struct wl_listener *listener, void *data);
static void      listen_static(struct wl_signal *signal, wl_notify_func_t notify);
static void      listener_release(struct wl_listener *listener);
static void      map_notify(struct wl_listener *listener, void *data);
static void      maximize_notify(struct wl_listener *listener, void *data);
static void      max_stack(monitor_t *m);
static void      motion_absolute(struct wl_listener *listener, void *data);
static void      motion_notify(
    uint32_t                 time,
    struct wlr_input_device *device,
    double                   sx,
    double                   sy,
    double                   sx_unaccel,
    double                   sy_unaccel,
    bool                     refocus
);
static void motion_relative(struct wl_listener *listener, void *data);
static void gesture_swipe_begin(struct wl_listener *listener, void *data);
static void gesture_swipe_update(struct wl_listener *listener, void *data);
static void gesture_swipe_end(struct wl_listener *listener, void *data);
static void gesture_pinch_begin(struct wl_listener *listener, void *data);
static void gesture_pinch_update(struct wl_listener *listener, void *data);
static void gesture_pinch_end(struct wl_listener *listener, void *data);
static void gesture_hold_begin(struct wl_listener *listener, void *data);
static void gesture_hold_end(struct wl_listener *listener, void *data);
static void move_resize(const arg_t *arg);
static void output_manager_apply(struct wl_listener *listener, void *data);
static void output_manager_apply_or_test(struct wlr_output_configuration_v1 *config, bool test);
static void output_manager_test(struct wl_listener *listener, void *data);
static void pointer_focus(
    client_t *c, struct wlr_surface *surface, double sx, double sy, uint32_t time, bool refocus
);
static void         print_status(void);
static void         publish_windows(const char *runtime);
static void         prepare_child(void);
static void         power_manager_set_mode(struct wl_listener *listener, void *data);
static void         quit(const arg_t *arg);
static void         raise_client(const arg_t *arg);
static void         render_monitor(struct wl_listener *listener, void *data);
static void         request_decoration_mode(struct wl_listener *listener, void *data);
static void         request_start_drag(struct wl_listener *listener, void *data);
static void         remember_client(client_t *c);
static void         forget_client(client_t *c);
static void         save_window_states(void);
static void         request_monitor_state(struct wl_listener *listener, void *data);
static void         resize(client_t *c, struct wlr_box geo, bool interact);
static void         resize_apply(client_t *c);
static void         restore_client(client_t *c);
static void         run(char *startup_cmd);
static void         set_cursor(struct wl_listener *listener, void *data);
static void         set_cursor_shape(struct wl_listener *listener, void *data);
static void         set_floating(client_t *c, bool floating);
static void         set_fullscreen(client_t *c, bool fullscreen);
static void         set_monitor(client_t *c, monitor_t *m, workspace_t *ws);
static void         set_workspace(client_t *c, workspace_t *ws);
static void         update_shortcuts_inhibitors(struct wlr_surface *surface);
static client_t    *client_main(client_t *c);
static bool         clients_related(client_t *a, client_t *b);
static void         assign_workspace(workspace_t *ws, monitor_t *m);
static workspace_t *free_workspace(void);
static void         view_workspace(workspace_t *ws, monitor_t *m);
static void         workspace_broadcast(void);
static void         workspace_metadata_bind(
    struct wl_client *client, void *data, uint32_t version, uint32_t id
);
static void workspace_metadata_broadcast(void);
static void workspace_handle_create(workspace_manager_t *mgr, workspace_t *ws);
static void workspace_manager_bind(
    struct wl_client *client, void *data, uint32_t version, uint32_t id
);
static void       workspace_output_bind(struct wl_listener *listener, void *data);
static uint32_t   workspace_state(workspace_t *ws);
static void       set_primary_selection(struct wl_listener *listener, void *data);
static void       set_selection(struct wl_listener *listener, void *data);
static void       setup(void);
static void       spawn(const arg_t *arg);
static void       expand_argv(const char *const *argv, char **expanded, char *storage);
static void       swap_client(const arg_t *arg);
static void       stack_config(const arg_t *arg);
static void       stack_master(monitor_t *m, int rot, int flip);
static void       start_drag(struct wl_listener *listener, void *data);
static void       tag(const arg_t *arg);
static void       tag_monitor(const arg_t *arg);
static void       toggle_floating(const arg_t *arg);
static void       toggle_fullscreen(const arg_t *arg);
static void       toggle_max_stack(const arg_t *arg);
static void       unlock_session(struct wl_listener *listener, void *data);
static void       layer_surface_unmap_notify(struct wl_listener *listener, void *data);
static void       unmap_notify(struct wl_listener *listener, void *data);
static void       update_monitors(struct wl_listener *listener, void *data);
static void       update_app_id(struct wl_listener *listener, void *data);
static void       update_title(struct wl_listener *listener, void *data);
static void       urgent(struct wl_listener *listener, void *data);
static void       ring_system_bell(struct wl_listener *listener, void *data);
static void       mark_urgent(client_t *c);
static void       create_dialog(struct wl_listener *listener, void *data);
static void       dialog_changed(struct wl_listener *listener, void *data);
static void       dialog_destroyed(struct wl_listener *listener, void *data);
static void       view(const arg_t *arg);
static void       virtual_keyboard(struct wl_listener *listener, void *data);
static void       virtual_pointer(struct wl_listener *listener, void *data);
static monitor_t *point_to_monitor(double x, double y);
static void       point_to_node(
    double               x,
    double               y,
    struct wlr_surface **psurface,
    client_t           **pc,
    layer_surface_t    **pl,
    double              *nx,
    double              *ny
);
static void zoom(const arg_t *arg);

/* Global state. */
static pid_t                   child_pid = -1;
static size_t                  autostart_len;
static bool                    locked;
static void                   *exclusive_focus;
static struct wl_display      *dpy;
static struct wl_event_loop   *event_loop;
static struct wl_event_source *signal_sources[3];
static sigset_t                original_signal_mask;
static struct wlr_backend     *backend;
static struct wlr_scene       *scene;
static struct wlr_scene_tree  *layers[NUM_LAYERS];
static struct wlr_scene_tree  *drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LAYER_BACKGROUND, LAYER_BOTTOM, LAYER_TOP, LAYER_OVERLAY };
static struct wlr_renderer   *drw;
static struct wlr_allocator  *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session    *session;

static struct wlr_xdg_shell                             *xdg_shell;
static struct wlr_xdg_wm_dialog_v1                      *xdg_dialog_mgr;
static struct wlr_xdg_system_bell_v1                    *system_bell;
static struct wlr_xdg_activation_v1                     *activation;
static struct wlr_xdg_decoration_manager_v1             *xdg_decoration_mgr;
static struct wl_list                                    clients; /* tiling order */
static struct wl_list                                    fstack;  /* focus order */
static struct wlr_idle_notifier_v1                      *idle_notifier;
static struct wlr_idle_inhibit_manager_v1               *idle_inhibit_mgr;
static struct wlr_layer_shell_v1                        *layer_shell;
static struct wlr_output_manager_v1                     *output_mgr;
static struct wlr_virtual_keyboard_manager_v1           *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1            *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1               *cursor_shape_mgr;
static struct wlr_output_power_manager_v1               *power_mgr;
static struct wlr_foreign_toplevel_manager_v1           *ftl_mgr;
static struct wlr_keyboard_shortcuts_inhibit_manager_v1 *kb_inhibit_mgr;
static struct wlr_input_method_manager_v2               *im_mgr;
static struct wlr_text_input_manager_v3                 *ti_mgr;
static struct wlr_input_method_v2                       *input_method; /* at most one */
static struct wl_listener im_commit    = { .notify = input_method_commit_notify };
static struct wl_listener im_destroy   = { .notify = input_method_destroy_notify };
static struct wl_listener im_grab_kb   = { .notify = input_method_grab_keyboard };
static struct wl_listener im_new_popup = { .notify = input_method_new_popup };
static struct wlr_ext_foreign_toplevel_list_v1                         *ext_ftl_list;
static struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *ext_ftl_capture_mgr;
static struct wl_listener ext_ftl_capture_request = { .notify = ftl_capture_request_notify };

static struct wlr_pointer_constraints_v1      *pointer_constraints;
static struct wlr_pointer_gestures_v1         *pointer_gestures;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1       *active_constraint;

static struct wlr_cursor          *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect              *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect              *locked_bg;
static struct wlr_session_lock_v1         *cur_lock;

static struct wlr_seat  *seat;
static keyboard_group_t *kb_group;
static unsigned int      cursor_mode;
static client_t         *grabc;
static int               grabcx, grabcy; /* Pointer position within the grabbed window. */
static int               grabx, graby;   /* Pointer position within the display layout. */
static uint32_t          grabedges;
static struct wlr_box    grabgeom;

static struct wlr_output_layout *output_layout;
static struct wlr_box            sgeom;
static struct wl_list            mons;
static struct wl_list            ws_managers;       /* workspace_manager_t.link */
static struct wl_list            metadata_managers; /* metadata_manager_t.link */
static struct wl_list            pending_spawns;    /* pending_spawn_t.link */
static struct wl_list            window_states;     /* window_state_t.link */
static struct wl_list            input_popups;      /* input_popup_t.link */
static int                       virtual_keyboards;
static monitor_t                *selmon;
static bool                      arranging_pointer_focus;

/* Event handlers shared for the lifetime of the compositor. */
static struct wl_listener cursor_axis            = { .notify = axis_notify };
static struct wl_listener cursor_button          = { .notify = button_press };
static struct wl_listener cursor_frame_listener  = { .notify = cursor_frame };
static struct wl_listener cursor_motion          = { .notify = motion_relative };
static struct wl_listener cursor_motion_absolute = { .notify = motion_absolute };
static struct wl_listener cursor_swipe_begin     = { .notify = gesture_swipe_begin };
static struct wl_listener cursor_swipe_update    = { .notify = gesture_swipe_update };
static struct wl_listener cursor_swipe_end       = { .notify = gesture_swipe_end };
static struct wl_listener cursor_pinch_begin     = { .notify = gesture_pinch_begin };
static struct wl_listener cursor_pinch_update    = { .notify = gesture_pinch_update };
static struct wl_listener cursor_pinch_end       = { .notify = gesture_pinch_end };
static struct wl_listener cursor_hold_begin      = { .notify = gesture_hold_begin };
static struct wl_listener cursor_hold_end        = { .notify = gesture_hold_end };
static struct wl_listener gpu_reset_listener     = { .notify = gpu_reset };
static struct wl_listener layout_change          = { .notify = update_monitors };
static struct wl_listener new_idle_inhibitor     = { .notify = create_idle_inhibitor };
static struct wl_listener new_input_device       = { .notify = input_device };
static struct wl_listener new_input_method       = { .notify = input_method_create_notify };
static struct wl_listener new_virtual_keyboard   = { .notify = virtual_keyboard };
static struct wl_listener new_virtual_pointer    = { .notify = virtual_pointer };
static struct wl_listener new_pointer_constraint = { .notify = create_pointer_constraint };
static struct wl_listener new_shortcuts_inhibitor_listener = { .notify = new_shortcuts_inhibitor };
static struct wl_listener new_output                       = { .notify = create_monitor };
static struct wl_listener new_xdg_toplevel                 = { .notify = create_notify };
static struct wl_listener new_text_input                   = { .notify = text_input_create_notify };
static struct wl_listener new_xdg_popup                    = { .notify = create_popup };
static struct wl_listener new_xdg_decoration               = { .notify = create_decoration };
static struct wl_listener new_xdg_dialog                   = { .notify = create_dialog };
static struct wl_listener new_layer_surface                = { .notify = create_layer_surface };
static struct wl_listener output_mgr_apply                 = { .notify = output_manager_apply };
static struct wl_listener output_mgr_test                  = { .notify = output_manager_test };
static struct wl_listener output_power_mgr_set_mode        = { .notify = power_manager_set_mode };
static struct wl_listener request_activate                 = { .notify = urgent };
static struct wl_listener system_bell_ring                 = { .notify = ring_system_bell };
static struct wl_listener request_cursor                   = { .notify = set_cursor };
static struct wl_listener request_set_psel                 = { .notify = set_primary_selection };
static struct wl_listener request_set_sel                  = { .notify = set_selection };
static struct wl_listener request_set_cursor_shape         = { .notify = set_cursor_shape };
static struct wl_listener request_start_drag_listener      = { .notify = request_start_drag };
static struct wl_listener start_drag_listener              = { .notify = start_drag };
static struct wl_listener new_session_lock                 = { .notify = lock_session };

static void                 activate_x11(struct wl_listener *listener, void *data);
static void                 associate_x11(struct wl_listener *listener, void *data);
static void                 configure_x11(struct wl_listener *listener, void *data);
static void                 create_notify_x11(struct wl_listener *listener, void *data);
static void                 dissociate_x11(struct wl_listener *listener, void *data);
static void                 set_hints(struct wl_listener *listener, void *data);
static void                 xwayland_ready(struct wl_listener *listener, void *data);
static struct wl_listener   new_xwayland_surface    = { .notify = create_notify_x11 };
static struct wl_listener   xwayland_ready_listener = { .notify = xwayland_ready };
static struct wlr_xwayland *xwayland;
static bool                 xwayland_listeners_registered;

/* Load configuration after declaring the types and state it may use. */
#include "config.h"

static_assert(WSCOUNT > 0 && WSCOUNT <= 32, "WSCOUNT must be between 1 and 32");

static workspace_t workspaces[WSCOUNT];

static pid_t                           autostart_pids[MAX_AUTOSTART];
static struct wlr_backend_output_state output_state_pool[MAX_MONITORS];

/* Fixed-capacity object pools. */
typedef struct {
    void       *items;
    bool       *used;
    size_t      capacity;
    size_t      item_size;
    const char *name;
} pool_t;

static void *pool_take(pool_t *pool) {
    size_t i;

    for (i = 0; i < pool->capacity; i++) {
        if (pool->used[i])
            continue;
        pool->used[i] = true;
        return (char *)pool->items + i * pool->item_size;
    }
    fprintf(stderr, "swm: %s pool exhausted (limit %zu)\n", pool->name, pool->capacity);
    return nullptr;
}

static void pool_release(pool_t *pool, void *item) {
    size_t index = ((char *)item - (char *)pool->items) / pool->item_size;

    pool->used[index] = false;
    memset(item, 0, pool->item_size);
}

static client_t             client_items[MAX_CLIENTS];
static monitor_t            monitor_items[MAX_MONITORS];
static layer_surface_t      layer_surface_items[MAX_LAYER_SURFACES];
static keyboard_group_t     keyboard_group_items[MAX_KEYBOARD_GROUPS];
static pointer_constraint_t pointer_constraint_items[MAX_POINTER_CONSTRAINTS];
static text_input_t         text_input_items[MAX_TEXT_INPUTS];
static input_popup_t        input_popup_items[MAX_INPUT_POPUPS];
static session_lock_t       session_lock_items[MAX_SESSION_LOCKS];
static pending_spawn_t      pending_spawn_items[MAX_PENDING_SPAWNS];
static window_state_t       window_state_items[MAX_WINDOW_STATES];
static static_listener_t    static_listener_items[MAX_STATIC_LISTENERS];
static workspace_manager_t  workspace_manager_items[MAX_WS_MANAGERS];
static workspace_handle_t   workspace_handle_items[MAX_WS_HANDLES];
static metadata_manager_t   metadata_manager_items[MAX_WS_MANAGERS];

static bool client_used[MAX_CLIENTS], monitor_used[MAX_MONITORS];
static bool layer_surface_used[MAX_LAYER_SURFACES], keyboard_group_used[MAX_KEYBOARD_GROUPS];
static bool pointer_constraint_used[MAX_POINTER_CONSTRAINTS], text_input_used[MAX_TEXT_INPUTS];
static bool input_popup_used[MAX_INPUT_POPUPS], session_lock_used[MAX_SESSION_LOCKS];
static bool pending_spawn_used[MAX_PENDING_SPAWNS], window_state_used[MAX_WINDOW_STATES];
static bool static_listener_used[MAX_STATIC_LISTENERS], workspace_manager_used[MAX_WS_MANAGERS];
static bool workspace_handle_used[MAX_WS_HANDLES], metadata_manager_used[MAX_WS_MANAGERS];

static pool_t client_pool = {
    client_items, client_used, LENGTH(client_items), sizeof *client_items, "client"
};
static pool_t monitor_pool = {
    monitor_items, monitor_used, LENGTH(monitor_items), sizeof *monitor_items, "monitor"
};
static pool_t layer_surface_pool      = { layer_surface_items,
                                          layer_surface_used,
                                          LENGTH(layer_surface_items),
                                          sizeof *layer_surface_items,
                                          "layer_surface" };
static pool_t keyboard_group_pool     = { keyboard_group_items,
                                          keyboard_group_used,
                                          LENGTH(keyboard_group_items),
                                          sizeof *keyboard_group_items,
                                          "keyboard_group" };
static pool_t pointer_constraint_pool = { pointer_constraint_items,
                                          pointer_constraint_used,
                                          LENGTH(pointer_constraint_items),
                                          sizeof *pointer_constraint_items,
                                          "pointer_constraint" };
static pool_t text_input_pool         = { text_input_items,
                                          text_input_used,
                                          LENGTH(text_input_items),
                                          sizeof *text_input_items,
                                          "text_input" };
static pool_t input_popup_pool        = { input_popup_items,
                                          input_popup_used,
                                          LENGTH(input_popup_items),
                                          sizeof *input_popup_items,
                                          "input_popup" };
static pool_t session_lock_pool       = { session_lock_items,
                                          session_lock_used,
                                          LENGTH(session_lock_items),
                                          sizeof *session_lock_items,
                                          "session_lock" };
static pool_t pending_spawn_pool      = { pending_spawn_items,
                                          pending_spawn_used,
                                          LENGTH(pending_spawn_items),
                                          sizeof *pending_spawn_items,
                                          "pending_spawn" };
static pool_t window_state_pool       = { window_state_items,
                                          window_state_used,
                                          LENGTH(window_state_items),
                                          sizeof *window_state_items,
                                          "window_state" };
static pool_t static_listener_pool    = { static_listener_items,
                                          static_listener_used,
                                          LENGTH(static_listener_items),
                                          sizeof *static_listener_items,
                                          "static_listener" };
static pool_t workspace_manager_pool  = { workspace_manager_items,
                                          workspace_manager_used,
                                          LENGTH(workspace_manager_items),
                                          sizeof *workspace_manager_items,
                                          "workspace_manager" };
static pool_t workspace_handle_pool   = { workspace_handle_items,
                                          workspace_handle_used,
                                          LENGTH(workspace_handle_items),
                                          sizeof *workspace_handle_items,
                                          "workspace_handle" };
static pool_t metadata_manager_pool   = { metadata_manager_items,
                                          metadata_manager_used,
                                          LENGTH(metadata_manager_items),
                                          sizeof *metadata_manager_items,
                                          "metadata_manager" };

/* Backend-specific client operations. */
/* Client helpers shared by the XDG shell and Xwayland implementations. These
 * are inline so unused helpers are omitted for builds that disable a backend. */

/* Return whether a client uses Xwayland. */
static inline bool client_is_x11(client_t *c) {
    return c->type == X11;
}

/* Return the underlying wlroots surface. */
static inline struct wlr_surface *client_surface(client_t *c) {
    if (client_is_x11(c))
        return c->surface.xwayland->surface;
    return c->surface.xdg->surface;
}

/* Resolve a surface to its owning toplevel. */
static inline int toplevel_from_wlr_surface(
    struct wlr_surface *s, client_t **pc, layer_surface_t **pl
) {
    struct wlr_xdg_surface      *xdg_surface, *tmp_xdg_surface;
    struct wlr_surface          *root_surface;
    struct wlr_layer_surface_v1 *layer_surface;
    client_t                    *c    = nullptr;
    layer_surface_t             *l    = nullptr;
    int                          type = -1;
    struct wlr_xwayland_surface *xsurface;

    if (!s)
        return -1;
    root_surface = wlr_surface_get_root_surface(s);

    if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(root_surface))) {
        c    = xsurface->data;
        type = c ? (int)c->type : -1;
        goto end;
    }
    if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root_surface))) {
        l    = layer_surface->data;
        type = l ? LAYER_SHELL : -1;
        goto end;
    }
    xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);

    while (xdg_surface) {
        tmp_xdg_surface = nullptr;

        switch (xdg_surface->role) {
        case WLR_XDG_SURFACE_ROLE_POPUP:

            if (!xdg_surface->popup || !xdg_surface->popup->parent)
                return -1;

            tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);

            if (!tmp_xdg_surface)
                return toplevel_from_wlr_surface(xdg_surface->popup->parent, pc, pl);

            xdg_surface = tmp_xdg_surface;
            break;
        case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
            c    = xdg_surface->data;
            type = c ? (int)c->type : -1;
            goto end;
        case WLR_XDG_SURFACE_ROLE_NONE:
            return -1;
        }
    }
end:

    if (pl)
        *pl = l;
    if (pc)
        *pc = c;
    return type;
}

/* Client operations. */
static inline void client_activate_surface(struct wlr_surface *s, bool activated) {
    struct wlr_xdg_toplevel     *toplevel;
    struct wlr_xwayland_surface *xsurface;

    if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(s))) {
        wlr_xwayland_surface_activate(xsurface, activated);
        return;
    }
    if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(s)))
        wlr_xdg_toplevel_set_activated(toplevel, activated);
}

/* Set the XDG configure bounds for a client. */
static inline uint32_t client_set_bounds(client_t *c, int32_t width, int32_t height) {
    if (client_is_x11(c))
        return 0;

    if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >=
            XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
        width >= 0 && height >= 0 && (c->bounds.width != width || c->bounds.height != height)) {
        c->bounds.width  = width;
        c->bounds.height = height;
        return wlr_xdg_toplevel_set_bounds(c->surface.xdg->toplevel, width, height);
    }
    return 0;
}

/* Return the client application identifier. */
static inline const char *client_get_appid(client_t *c) {
    if (client_is_x11(c))
        return c->surface.xwayland->class ? c->surface.xwayland->class : "broken";
    return c->surface.xdg->toplevel->app_id ? c->surface.xdg->toplevel->app_id : "broken";
}

/* Return the client surface clipping rectangle. */
static inline void client_get_clip(client_t *c, struct wlr_box *clip) {
    *clip = (struct wlr_box){
        .x      = 0,
        .y      = 0,
        .width  = c->geom.width - c->bw,
        .height = c->geom.height - c->bw,
    };

    if (client_is_x11(c))
        return;

    clip->x = c->surface.xdg->geometry.x;
    clip->y = c->surface.xdg->geometry.y;
}

/* Return the client surface geometry. */
static inline void client_get_geometry(client_t *c, struct wlr_box *geom) {
    if (client_is_x11(c)) {
        geom->x      = c->surface.xwayland->x;
        geom->y      = c->surface.xwayland->y;
        geom->width  = c->surface.xwayland->width;
        geom->height = c->surface.xwayland->height;
        return;
    }
    *geom = c->surface.xdg->geometry;
}

/* Return the parent client, if any. */
static inline client_t *client_get_parent(client_t *c) {
    client_t *p = nullptr;

    if (client_is_x11(c)) {
        if (c->surface.xwayland->parent)
            toplevel_from_wlr_surface(c->surface.xwayland->parent->surface, &p, nullptr);
        return p;
    }
    if (c->surface.xdg->toplevel->parent)
        toplevel_from_wlr_surface(c->surface.xdg->toplevel->parent->base->surface, &p, nullptr);
    return p;
}

/* Return the client title. */
static inline const char *client_get_title(client_t *c) {
    if (client_is_x11(c))
        return c->surface.xwayland->title ? c->surface.xwayland->title : "broken";
    return c->surface.xdg->toplevel->title ? c->surface.xdg->toplevel->title : "broken";
}

/* Return whether the client should float by type. */
static inline bool client_is_float_type(client_t *c) {
    struct wlr_xdg_toplevel      *toplevel;
    struct wlr_xdg_toplevel_state state;

    if (client_is_x11(c)) {
        struct wlr_xwayland_surface *surface    = c->surface.xwayland;
        xcb_size_hints_t            *size_hints = surface->size_hints;

        if (surface->modal)
            return true;

        if (wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG) ||
            wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH) ||
            wlr_xwayland_surface_has_window_type(
                surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR
            ) ||
            wlr_xwayland_surface_has_window_type(
                surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY
            )) {
            return true;
        }
        return size_hints && size_hints->min_width > 0 && size_hints->min_height > 0 &&
               (size_hints->max_width == size_hints->min_width ||
                size_hints->max_height == size_hints->min_height);
    }
    toplevel = c->surface.xdg->toplevel;
    state    = toplevel->current;
    return toplevel->parent ||
           (state.min_width != 0 && state.min_height != 0 &&
            (state.min_width == state.max_width || state.min_height == state.max_height));
}

/* Return whether the client is rendered on an output. */
static inline bool client_is_rendered_on_mon(client_t *c, monitor_t *m) {
    /* This is needed for when you don't want to check formal assignment,
     * but rather actual displaying of the pixels.
     * Usually VISIBLEON suffices and is also faster. */
    struct wlr_surface_output *s;
    int                        unused_lx, unused_ly;

    if (!wlr_scene_node_coords(&c->scene->node, &unused_lx, &unused_ly))
        return false;
    wl_list_for_each(
        s, &client_surface(c)->current_outputs, link
    ) if (s->output == m->wlr_output) return true;
    return false;
}

/* Return whether the client process is stopped. */
static inline bool client_is_stopped(client_t *c) {
    int       pid;
    siginfo_t in = {};

    if (client_is_x11(c))
        return false;

    wl_client_get_credentials(c->surface.xdg->client->client, &pid, nullptr, nullptr);

    if (waitid(P_PID, pid, &in, WNOHANG | WCONTINUED | WSTOPPED | WNOWAIT) < 0) {
        /* This process is not our child process, while is very unlikely that
         * it is stopped, in order to do not skip frames, assume that it is. */
        if (errno == ECHILD)
            return true;
    } else if (in.si_pid) {
        if (in.si_code == CLD_STOPPED || in.si_code == CLD_TRAPPED)
            return true;

        if (in.si_code == CLD_CONTINUED)
            return false;
    }
    return false;
}

/* Return whether the client bypasses window management. */
static inline bool client_is_unmanaged(client_t *c) {
    if (client_is_x11(c))
        return c->surface.xwayland->override_redirect;
    return false;
}

/* Return whether the client supports the requested pointer operation. */
static inline bool client_allows_move_resize(client_t *c, uint32_t mode) {
    return !client_is_unmanaged(c) && (!c->is_fullscreen || mode == CURSOR_RESIZE);
}

/* Send keyboard focus to a client surface. */
static inline void client_notify_enter(struct wlr_surface *s, struct wlr_keyboard *kb) {
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, s, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, s, nullptr, 0, nullptr);

    /* Send text-input enter only after wl_keyboard.enter has been queued. */
    input_method_set_focus(s);
}

/* Request that a client close. */
static inline void client_send_close(client_t *c) {
    if (client_is_x11(c)) {
        wlr_xwayland_surface_close(c->surface.xwayland);
        return;
    }
    wlr_xdg_toplevel_send_close(c->surface.xdg->toplevel);
}

/* Set the color of every managed client border while its scene exists. */
static inline void client_set_border_color(client_t *c, const float color[static 4]) {
    int i;

    if (!c->scene || client_is_unmanaged(c))
        return;

    for (i = 0; i < 4; i++)
        wlr_scene_rect_set_color(c->border[i], color);
}

/* Notify a client of its fullscreen state. */
static inline void client_set_fullscreen(client_t *c, bool fullscreen) {
    if (client_is_x11(c)) {
        wlr_xwayland_surface_set_fullscreen(c->surface.xwayland, fullscreen);
        return;
    }
    wlr_xdg_toplevel_set_fullscreen(c->surface.xdg->toplevel, fullscreen);
}

/* Set the preferred scale for a client surface. */
static inline void client_set_scale(struct wlr_surface *s, float scale) {
    wlr_fractional_scale_v1_notify_scale(s, scale);
    wlr_surface_set_preferred_buffer_scale(s, (int32_t)ceilf(scale));
}

/* Configure the client surface size. */
static inline uint32_t client_set_size(client_t *c, uint32_t width, uint32_t height) {
    if (client_is_x11(c)) {
        wlr_xwayland_surface_configure(
            c->surface.xwayland, c->geom.x + c->bw, c->geom.y + c->bw, width, height
        );
        return 0;
    }
    if ((int32_t)width == c->surface.xdg->toplevel->current.width &&
        (int32_t)height == c->surface.xdg->toplevel->current.height)
        return 0;
    return wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, (int32_t)width, (int32_t)height);
}

/* Notify a client of an interactive resize. */
static inline void client_set_resizing(client_t *c, bool resizing) {
    if (client_is_x11(c))
        return;
    wlr_xdg_toplevel_set_resizing(c->surface.xdg->toplevel, resizing);
}

/* Notify a client of its tiled edges. */
static inline void client_set_tiled(client_t *c, uint32_t edges) {
    if (client_is_x11(c)) {
        wlr_xwayland_surface_set_maximized(
            c->surface.xwayland, edges != WLR_EDGE_NONE, edges != WLR_EDGE_NONE
        );
        return;
    }
    if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >=
        XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION) {
        wlr_xdg_toplevel_set_tiled(c->surface.xdg->toplevel, edges);
    } else {
        wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel, edges != WLR_EDGE_NONE);
    }
}

/* Notify a client of its suspended state. */
static inline void client_set_suspended(client_t *c, bool suspended) {
    if (client_is_x11(c))
        return;

    wlr_xdg_toplevel_set_suspended(c->surface.xdg->toplevel, suspended);
}

/* Return whether an unmanaged client requests focus. */
static inline bool client_wants_focus(client_t *c) {
    return client_is_unmanaged(c) &&
           wlr_xwayland_surface_override_redirect_wants_focus(c->surface.xwayland) &&
           wlr_xwayland_surface_icccm_input_model(c->surface.xwayland) !=
               WLR_ICCCM_INPUT_MODEL_NONE;
}

/* Return whether a client requests fullscreen. */
static inline bool client_wants_fullscreen(client_t *c) {
    if (client_is_x11(c))
        return c->surface.xwayland->fullscreen;
    return c->surface.xdg->toplevel->requested.fullscreen;
}

/* Keep these values independent of wlroots so this module has no I/O or
 * compositor dependencies. They are the stable values from wayland-util.h. */
#define EDGE_TOP  1
#define EDGE_LEFT 4

/* Clamp a box to the minimum size and output bounds. */
void swm_box_apply_bounds(struct swm_box *box, const struct swm_box *bounds, unsigned int border) {
    int minimum = 1 + 2 * (int)border;

    if (box->width < minimum)
        box->width = minimum;

    if (box->height < minimum)
        box->height = minimum;

    if (box->x >= bounds->x + bounds->width)
        box->x = bounds->x + bounds->width - box->width;

    if (box->y >= bounds->y + bounds->height)
        box->y = bounds->y + bounds->height - box->height;

    if (box->x + box->width <= bounds->x)
        box->x = bounds->x;

    if (box->y + box->height <= bounds->y)
        box->y = bounds->y;
}

/* Resize a box from the selected edges. */
struct swm_box swm_box_resize(
    const struct swm_box *box, int dx, int dy, unsigned int edges, unsigned int border
) {
    int            minimum = 1 + 2 * (int)border;
    struct swm_box result  = *box;
    int            width   = box->width + ((edges & EDGE_LEFT) ? -dx : dx);
    int            height  = box->height + ((edges & EDGE_TOP) ? -dy : dy);

    result.width  = width < minimum ? minimum : width;
    result.height = height < minimum ? minimum : height;

    if (edges & EDGE_LEFT)
        result.x = box->x + box->width - result.width;

    if (edges & EDGE_TOP)
        result.y = box->y + box->height - result.height;

    return result;
}

/* Reconcile pending geometry with a committed size. */
void swm_box_reconcile_commit(
    struct swm_box *box, int width, int height, unsigned int border, unsigned int edges
) {
    int outer_width, outer_height;

    if (width <= 0 || height <= 0)
        return;

    outer_width  = width + 2 * (int)border;
    outer_height = height + 2 * (int)border;

    if (edges & EDGE_LEFT)
        box->x += box->width - outer_width;

    if (edges & EDGE_TOP)
        box->y += box->height - outer_height;

    box->width  = outer_width;
    box->height = outer_height;
}

/* Position an input popup inside an output. */
struct swm_box swm_popup_position(
    const struct swm_box *client,
    unsigned int          border,
    const struct swm_box *cursor_rect,
    int                   width,
    int                   height,
    const struct swm_box *output
) {
    struct swm_box popup = {
        .x      = client->x + (int)border + cursor_rect->x,
        .y      = client->y + (int)border + cursor_rect->y + cursor_rect->height,
        .width  = width,
        .height = height,
    };
    if (!output)
        return popup;

    if (popup.x + width > output->x + output->width)
        popup.x = output->x + output->width - width;

    if (popup.x < output->x)
        popup.x = output->x;

    if (popup.y + height > output->y + output->height)
        popup.y = client->y + (int)border + cursor_rect->y - height;

    if (popup.y < output->y)
        popup.y = output->y;

    return popup;
}

/* Compute exported workspace state flags. */
unsigned int swm_workspace_state(bool active, bool occupied, bool urgent) {
    unsigned int state = 0;

    if (active)
        state |= SWM_WORKSPACE_ACTIVE;
    if (urgent)
        state |= SWM_WORKSPACE_URGENT;
    if (!active && !occupied)
        state |= SWM_WORKSPACE_HIDDEN;

    return state;
}

/* Find the next eligible workspace. */
int swm_workspace_next(
    int         current,
    int         count,
    int         direction,
    int         allow_empty,
    const bool *visible_elsewhere,
    const bool *occupied
) {
    int step, index;

    if (count <= 1 || (direction != -1 && direction != 1) || current < 0 || current >= count)
        return -1;

    for (step = 1; step < count; step++) {
        index = (current + direction * step + count) % count;

        if (visible_elsewhere[index])
            continue;

        if (!allow_empty && !occupied[index])
            continue;
        return index;
    }
    return -1;
}

/* Apply a master-stack configuration command. */
bool swm_stack_configure(struct swm_stack_state *state, int command, int side, int *new_side) {
    if (!state || !new_side || side < SWM_MASTER_LEFT || side > SWM_MASTER_BOTTOM)
        return false;

    *new_side = side;

    switch (command) {
    case SWM_MASTER_SHRINK:
        if (state->msize > 1)
            state->msize--;
        break;
    case SWM_MASTER_GROW:
        if (state->msize < SWM_SLICE - 1)
            state->msize++;
        break;
    case SWM_MASTER_ADD:
        state->mwin++;
        break;
    case SWM_MASTER_DEL:
        if (state->mwin > 0)
            state->mwin--;
        break;
    case SWM_STACK_INC:
        state->stacks++;
        break;
    case SWM_STACK_DEC:
        if (state->stacks > 1)
            state->stacks--;
        break;
    case SWM_STACK_RESET:
        state->msize  = SWM_SLICE / 2;
        state->mwin   = 1;
        state->stacks = 1;

        if (side == SWM_MASTER_RIGHT)
            *new_side = SWM_MASTER_LEFT;
        else if (side == SWM_MASTER_BOTTOM)
            *new_side = SWM_MASTER_TOP;
        break;
    case SWM_FLIP_LAYOUT:
        *new_side = side == SWM_MASTER_LEFT    ? SWM_MASTER_RIGHT
                    : side == SWM_MASTER_RIGHT ? SWM_MASTER_LEFT
                    : side == SWM_MASTER_TOP   ? SWM_MASTER_BOTTOM
                                               : SWM_MASTER_TOP;
        break;
    default:
        return false;
    }
    return true;
}

/* Return whether a rule matches a client identity. */
bool swm_rule_matches(
    const char *rule_id, const char *rule_title, const char *appid, const char *title
) {
    appid = appid ? appid : "";
    title = title ? title : "";
    if (!rule_id && !rule_title)
        return false;

    return (!rule_id || !strcmp(rule_id, "*") || strstr(appid, rule_id)) &&
           (!rule_title || strstr(title, rule_title));
}

/* Replace state-file delimiters in a field. */
void swm_sanitize_field(char *dst, size_t size, const char *src, const char *fallback) {
    size_t i;

    if (!size)
        return;

    if (!src || !*src)
        src = fallback ? fallback : "";

    for (i = 0; src[i] && i + 1 < size; i++)
        dst[i] = src[i] == '\t' || src[i] == '\n' || src[i] == '\r' ? ' ' : src[i];

    dst[i] = '\0';
}

/* Parse one persisted window-state record. */
bool swm_parse_window_state(
    const char     *line,
    char           *appid,
    size_t          appid_size,
    char           *title,
    size_t          title_size,
    struct swm_box *geometry
) {
    char parsed_appid[256], parsed_title[256];
    int  fields;

    if (!line || !appid || !appid_size || !title || !title_size || !geometry)
        return false;

    fields = sscanf(
        line,
        "%255[^\t]\t%255[^\t]\t%d\t%d\t%d\t%d",
        parsed_appid,
        parsed_title,
        &geometry->x,
        &geometry->y,
        &geometry->width,
        &geometry->height
    );

    if (fields != 6) {
        fields = sscanf(
            line,
            "%255[^\t]\t%d\t%d\t%d\t%d",
            parsed_appid,
            &geometry->x,
            &geometry->y,
            &geometry->width,
            &geometry->height
        );
        if (fields != 5)
            return false;

        parsed_title[0] = '\0';
    }
    swm_sanitize_field(appid, appid_size, parsed_appid, "broken");
    swm_sanitize_field(title, title_size, parsed_title, nullptr);

    return true;
}

/* Compute the effective client border width. */
unsigned int swm_border_width(
    unsigned int configured,
    bool         fullscreen,
    bool         borderless,
    bool         x11,
    bool         unmanaged,
    bool         client_side
) {
    if (fullscreen || borderless || (x11 && unmanaged) || client_side)
        return 0;
    return configured;
}

/* Find the next enabled layout. */
int swm_next_layout(int current, int count, const bool *cycle) {
    int step, index;

    if (!cycle || count <= 0 || current < 0 || current >= count)
        return -1;

    for (step = 1; step <= count; step++) {
        index = (current + step) % count;

        if (cycle[index])
            return index;
    }
    return -1;
}

/* Arrange clients into master and stack areas. */
size_t swm_stack_layout(
    const struct swm_box         *area,
    const struct swm_stack_state *state,
    int                           rotate,
    int                           flip,
    size_t                        count,
    struct swm_box               *boxes
) {
    struct swm_box root = *area, master, stack, column, cell;
    int            n    = (int)count, mwin, sn, stacks, slice;
    int            j, ci, cw, extra, rows, base, remainder, offset;
    size_t         i;

    if (!count)
        return 0;

    if (rotate) {
        int t       = root.x;
        root.x      = root.y;
        root.y      = t;
        t           = root.width;
        root.width  = root.height;
        root.height = t;
    }
    slice = root.width / SWM_SLICE;
    mwin  = MIN(state->mwin, n);

    if (mwin < 0)
        mwin = 0;
    sn     = n - mwin;
    stacks = MIN(state->stacks, sn);

    if (stacks < 0)
        stacks = 0;

    master = stack = root;

    if (stacks && mwin) {
        master.width = slice * state->msize;

        if (master.width < 0)
            master.width = 0;

        if (master.width > root.width)
            master.width = root.width;
        stack.width -= master.width;

        if (flip)
            master.x += stack.width;
        else
            stack.x += master.width;
    }
    j      = 0;
    ci     = -1;
    rows   = mwin;
    column = master;
    cw     = stacks ? stack.width / stacks : 0;
    extra  = stacks ? stack.width % stacks : 0;

    for (i = 0; i < count; i++) {
        while (j >= rows) {
            if (stacks <= 0)
                return i;
            j = 0;
            ci++;
            rows         = sn / stacks + (stacks - ci <= sn % stacks ? 1 : 0);
            column       = stack;
            column.width = cw + (ci == stacks - 1 ? extra : 0);

            if (flip)
                column.x = stack.x + stack.width - (ci + 1) * cw - (ci == stacks - 1 ? extra : 0);
            else
                column.x = stack.x + ci * cw;
        }
        base        = column.height / rows;
        remainder   = column.height % rows;
        offset      = j * base + MIN(j, remainder);
        cell.x      = column.x;
        cell.y      = column.y + offset;
        cell.width  = column.width;
        cell.height = base + (j < remainder ? 1 : 0);

        if (rotate) {
            int t       = cell.x;
            cell.x      = cell.y;
            cell.y      = t;
            t           = cell.width;
            cell.width  = cell.height;
            cell.height = t;
        }
        boxes[i] = cell;
        j++;
    }
    return count;
}

/* Function implementations. */
/* Clamp client geometry to the supplied bounds. */
void apply_bounds(client_t *c, struct wlr_box *bbox) {
    swm_box_apply_bounds((struct swm_box *)&c->geom, (const struct swm_box *)bbox, c->bw);
}

/* Return the border width appropriate for a window's type and state. */
unsigned int client_border_width(client_t *c) {
    bool x11 = false, unmanaged = false;
    bool client_side = c->decoration && c->decoration->requested_mode ==
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    x11              = client_is_x11(c);
    unmanaged        = x11 && client_is_unmanaged(c);
    return swm_border_width(
        borderwidth, c->is_fullscreen, c->is_borderless, x11, unmanaged, client_side
    );
}

/* Apply matching rules to choose a window's workspace, display, and style. */
void apply_rules(client_t *c, workspace_t *defaultws) {
    /* Later matching rules override earlier ones. */
    const char   *appid, *title;
    workspace_t  *ws = defaultws;
    int           i;
    const rule_t *r;
    monitor_t    *mon = defaultws && defaultws->mon ? defaultws->mon : selmon, *m;

    appid = client_get_appid(c);
    title = client_get_title(c);

    for (r = rules; r < END(rules); r++) {
        if (swm_rule_matches(r->id, r->title, appid, title)) {
            c->is_floating   = r->is_floating;
            c->is_borderless = r->borderless;

            if (r->ws >= 0 && r->ws < WSCOUNT)
                ws = &workspaces[r->ws];
            i = 0;
            wl_list_for_each(m, &mons, link) {
                if (r->monitor == i++)
                    mon = m;
            }
        }
    }
    c->is_floating |= client_is_float_type(c);
    set_monitor(c, mon, ws);
}

/* Update the visibility, layer, size, and focus of windows on a display. */
void arrange(monitor_t *m) {
    client_t *c;
    client_t *focus;
    int       max;

    if (!m->wlr_output->enabled)
        return;

    focus = focus_top(m);
    max   = m->ws && m->ws->lt->arrange == max_stack;
    wl_list_for_each(c, &clients, link) {
        if (c->mon == m) {
            int visible;

            if (c->is_max_stacked && (!c->ws || c->ws->lt->arrange != max_stack)) {
                c->is_max_stacked = false;
                resize(c, c->maxstack_prev, 0);
            }
            visible = !c->pending_map && VISIBLEON(c, m) &&
                      (!max || c->is_fullscreen || clients_related(c, focus));
            wlr_scene_node_set_enabled(&c->scene->node, visible);
            client_set_suspended(c, !visible);
        }
    }
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, focus && focus->is_fullscreen);

    /* In a floating layout, keep tiled and floating windows in one layer so
     * their normal stacking order is preserved. */
    wl_list_for_each(c, &clients, link) {
        if (c->mon != m || !m->ws || c->scene->node.parent == layers[LAYER_FULLSCREEN])
            continue;

        wlr_scene_node_reparent(
            &c->scene->node,
            (!m->ws->lt->arrange && c->is_floating)  ? layers[LAYER_TILE]
            : (m->ws->lt->arrange && c->is_floating) ? layers[LAYER_FLOAT]
                                                     : c->scene->node.parent
        );
    }
    if (m->ws && m->ws->lt->arrange)
        m->ws->lt->arrange(m);
    /* Recheck pointer focus after windows move. Avoid repeating this if the
     * focus change itself rearranges the max layout. */
    if (!arranging_pointer_focus) {
        arranging_pointer_focus = true;
        motion_notify(0, nullptr, 0, 0, 0, 0, 1);
        arranging_pointer_focus = false;
    }
    check_idle_inhibitor(nullptr);
}

/* Position the surfaces in one desktop layer and reserve any requested space. */
void arrange_layer(monitor_t *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive) {
    layer_surface_t *l;
    struct wlr_box   full_area = m->m;

    wl_list_for_each(l, list, link) {
        struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

        if (!layer_surface->initialized)
            continue;

        if (exclusive != (layer_surface->current.exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);

        if (l->dim) {
            wlr_scene_node_set_position(&l->dim->node, full_area.x, full_area.y);
            wlr_scene_rect_set_size(l->dim, full_area.width, full_area.height);
        }
    }
}

/* Arrange desktop layers and give keyboard focus to the highest eligible surface. */
void arrange_layers(monitor_t *m) {
    int              i;
    struct wlr_box   usable_area = m->m;
    layer_surface_t *l;
    uint32_t         layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };

    if (!m->wlr_output->enabled)
        return;

    /* Reserve space for panels from the top layer down. */
    for (i = 3; i >= 0; i--)
        arrange_layer(m, &m->layers[i], &usable_area, 1);

    if (!wlr_box_equal(&usable_area, &m->w)) {
        m->w = usable_area;
        arrange(m);
    }
    /* Place overlays that do not reserve space, from the top layer down. */
    for (i = 3; i >= 0; i--)
        arrange_layer(m, &m->layers[i], &usable_area, 0);

    /* Give keyboard focus to the highest layer that requests it. */
    for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
            if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
                continue;
            /* Deactivate the focused client. */
            focus_client(nullptr, 0);
            exclusive_focus = l;
            client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
            return;
        }
    }
}

/* Expand $NAME variables into caller-owned stack storage. */
void expand_argv(const char *const *argv, char **expanded, char *storage) {
    size_t i, remaining = MAX_COMMAND_SIZE, size;

    for (i = 0; argv[i]; i++) {
        if (i + 1 >= MAX_COMMAND_ARGS)
            die("command has too many arguments");
        expanded[i] = storage;
        if (!(size = env_expand(storage, remaining, argv[i])))
            die("expanded command exceeds %d bytes", MAX_COMMAND_SIZE);
        storage   += size;
        remaining -= size;
    }
    expanded[i] = nullptr;
}

/* Start every command in the configured autostart list. */
void autostart_exec(void) {
    /* Run each configured startup command. A nullptr separates commands, and
     * a second nullptr ends the list. */
    const char *const *p;
    size_t             i = 0;

    for (p = autostart; *p && autostart_len < MAX_AUTOSTART; autostart_len++, p++)

        while (*++p)
            ;

    if (*p)
        fprintf(stderr, "swm: autostart limit reached (%d)\n", MAX_AUTOSTART);

    for (p = autostart; *p && i < autostart_len; i++, p++) {
        if ((autostart_pids[i] = fork()) == 0) {
            char *argv[MAX_COMMAND_ARGS], storage[MAX_COMMAND_SIZE];

            expand_argv(p, argv, storage);

            prepare_child();
            setsid();
            execvp(argv[0], argv);
            die("autostart: execvp %s:", argv[0]);
        }
        while (*++p)
            ;
    }
}

/* Forward a scroll event to the application under the pointer. */
void axis_notify(struct wl_listener *listener, void *data) {
    /* A mouse wheel or touchpad produced a scrolling event. */
    struct wlr_pointer_axis_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* TODO: Allow scroll events to trigger compositor bindings. */
    /* Forward the scroll event to the application under the pointer. */
    wlr_seat_pointer_notify_axis(
        seat,
        event->time_msec,
        event->orientation,
        event->delta,
        event->delta_discrete,
        event->source,
        event->relative_direction
    );
}

/* Focus, move, or resize a window in response to a pointer button. */
void button_press(struct wl_listener *listener, void *data) {
    struct wlr_pointer_button_event *event = data;
    struct wlr_keyboard             *keyboard;
    uint32_t                         mods;
    client_t                        *c;
    const button_t                  *b;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    switch (event->state) {
    case WL_POINTER_BUTTON_STATE_PRESSED:
        cursor_mode = CURSOR_PRESSED;
        selmon      = point_to_monitor(cursor->x, cursor->y);

        if (locked)
            break;

        /* Focus a window when a button is pressed over it. */
        point_to_node(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);

        if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
            focus_client(c, 1);

        keyboard = wlr_seat_get_keyboard(seat);
        mods     = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

        for (b = buttons; b < END(buttons); b++) {
            if (CLEANMASK(mods) == CLEANMASK(b->mod) && event->button == b->button && b->func) {
                b->func(&b->arg);
                return;
            }
        }
        break;
    case WL_POINTER_BUTTON_STATE_RELEASED:
        /* Releasing any button ends an interactive move or resize. */
        /* TODO: Restore the cursor requested by the window under the pointer. */
        if (!locked && cursor_mode != CURSOR_NORMAL && cursor_mode != CURSOR_PRESSED) {
            if (cursor_mode == CURSOR_RESIZE)
                client_set_resizing(grabc, 0);
            wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
            cursor_mode = CURSOR_NORMAL;
            /* Move the window to the workspace on its new display. The
             * cursor can sit outside every output (on a layout edge or in a
             * gap); keep the window on its current display in that case. */
            selmon = point_to_monitor(cursor->x, cursor->y);
            set_monitor(grabc, selmon ? selmon : grabc->mon, 0);
            remember_client(grabc);
            grabc = nullptr;
            return;
        }
        cursor_mode = CURSOR_NORMAL;
        break;
    }
    /* Forward buttons that were not handled here to the application under the pointer. */
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
}

/* Switch to the requested virtual terminal. */
void change_vt(const arg_t *arg) {
    wlr_session_change_vt(session, arg->u);
}

/* Prevent idling while a visible application requests it. */
void check_idle_inhibitor(struct wlr_surface *exclude) {
    int                           inhibited = 0, unused_lx, unused_ly;
    struct wlr_idle_inhibitor_v1 *inhibitor;

    wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
        struct wlr_surface    *surface = wlr_surface_get_root_surface(inhibitor->surface);
        struct wlr_scene_tree *tree    = surface->data;

        if (exclude != surface && tree &&
            wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)) {
            inhibited = 1;
            break;
        }
    }
    wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

/* Release compositor resources in dependency order before exiting. */
void cleanup(void) {
    size_t i;

    cleanup_listeners();
    /* Destroy windows before the protocol objects that listen to their
     * surfaces, so popup cleanup cannot call an expired listener. */
    wl_display_destroy_clients(dpy);

    if (xwayland) {
        wlr_xwayland_destroy(xwayland);
        xwayland = nullptr;
    }
    if (child_pid > 0) {
        kill(-child_pid, SIGTERM);
        waitpid(child_pid, nullptr, WNOHANG);
    }
    for (i = 0; i < autostart_len; i++) {
        if (autostart_pids[i] > 0) {
            kill(-autostart_pids[i], SIGTERM);
            waitpid(autostart_pids[i], nullptr, WNOHANG);
        }
    }
    for (i = 0; i < LENGTH(signal_sources); i++)
        wl_event_source_remove(signal_sources[i]);
    wlr_xcursor_manager_destroy(cursor_mgr);

    destroy_keyboard_group(&kb_group->destroy, nullptr);

    /* Destroy this explicitly because wlroots may otherwise retain a pointer
     * to the seat after the seat is gone. */
    wlr_backend_destroy(backend);

    wl_display_destroy(dpy);
    /* Destroy the scene after the display, once every display is gone. */
    wlr_scene_node_destroy(&scene->tree.node);
}

/* Remove a disconnected display and release everything attached to it. */
void cleanup_monitor(struct wl_listener *listener, void *data) {
    monitor_t       *m = wl_container_of(listener, m, destroy);
    layer_surface_t *l, *tmp;
    size_t           i;

    /* The layer lists belong to the display and disappear with it. */
    for (i = 0; i < LENGTH(m->layers); i++) {
        wl_list_for_each_safe(l, tmp, &m->layers[i], link)
            wlr_layer_surface_v1_destroy(l->layer_surface);
    }
    wl_list_remove(&m->destroy.link);
    wl_list_remove(&m->frame.link);
    wl_list_remove(&m->link);
    wl_list_remove(&m->request_state.link);
    wl_list_remove(&m->output_bind.link);

    if (m->lock_surface)
        destroy_lock_surface(&m->destroy_lock_surface, nullptr);
    m->wlr_output->data = nullptr;
    wlr_output_layout_remove(output_layout, m->wlr_output);
    wlr_scene_output_destroy(m->scene_output);

    close_monitor(m);
    wlr_scene_node_destroy(&m->fullscreen_bg->node);
    pool_release(&monitor_pool, m);
}

/* Detach global event listeners before their event sources are destroyed. */
void cleanup_listeners(void) {
    wl_list_remove(&cursor_axis.link);
    wl_list_remove(&cursor_button.link);
    wl_list_remove(&cursor_frame_listener.link);
    wl_list_remove(&cursor_motion.link);
    wl_list_remove(&cursor_motion_absolute.link);
    wl_list_remove(&cursor_swipe_begin.link);
    wl_list_remove(&cursor_swipe_update.link);
    wl_list_remove(&cursor_swipe_end.link);
    wl_list_remove(&cursor_pinch_begin.link);
    wl_list_remove(&cursor_pinch_update.link);
    wl_list_remove(&cursor_pinch_end.link);
    wl_list_remove(&cursor_hold_begin.link);
    wl_list_remove(&cursor_hold_end.link);
    wl_list_remove(&gpu_reset_listener.link);
    wl_list_remove(&new_idle_inhibitor.link);
    wl_list_remove(&layout_change.link);
    wl_list_remove(&new_input_device.link);
    wl_list_remove(&new_input_method.link);
    wl_list_remove(&new_virtual_keyboard.link);
    wl_list_remove(&new_virtual_pointer.link);
    wl_list_remove(&new_pointer_constraint.link);
    wl_list_remove(&new_shortcuts_inhibitor_listener.link);
    wl_list_remove(&new_output.link);
    wl_list_remove(&new_xdg_toplevel.link);
    wl_list_remove(&new_text_input.link);
    wl_list_remove(&new_xdg_decoration.link);
    wl_list_remove(&new_xdg_dialog.link);
    wl_list_remove(&new_xdg_popup.link);
    wl_list_remove(&new_layer_surface.link);
    wl_list_remove(&ext_ftl_capture_request.link);
    wl_list_remove(&output_mgr_apply.link);
    wl_list_remove(&output_mgr_test.link);
    wl_list_remove(&output_power_mgr_set_mode.link);
    wl_list_remove(&request_activate.link);
    wl_list_remove(&system_bell_ring.link);
    wl_list_remove(&request_cursor.link);
    wl_list_remove(&request_set_psel.link);
    wl_list_remove(&request_set_sel.link);
    wl_list_remove(&request_set_cursor_shape.link);
    wl_list_remove(&request_start_drag_listener.link);
    wl_list_remove(&start_drag_listener.link);
    wl_list_remove(&new_session_lock.link);

    if (xwayland_listeners_registered) {
        wl_list_remove(&new_xwayland_surface.link);
        wl_list_remove(&xwayland_ready_listener.link);
        xwayland_listeners_registered = false;
    }
}

/* Hide a display's workspace and move its windows to a valid display. */
void close_monitor(monitor_t *m) {
    /* If the selected display closes, select another one. Its workspace
     * becomes hidden, but its windows remain there. */
    client_t      *c;
    monitor_t     *candidate;
    workspace_t   *ws;
    struct wlr_box geom;

    if (m == selmon || (selmon && !selmon->wlr_output->enabled)) {
        selmon = nullptr;
        wl_list_for_each(candidate, &mons, link) {
            if (candidate != m && candidate->wlr_output->enabled) {
                selmon = candidate;
                break;
            }
        }
    }
    if (m->ws)
        m->ws->mon = nullptr;
    m->ws = m->previous_workspace = nullptr;

    for (ws = workspaces; ws < END(workspaces); ws++) {
        if (ws->mon == m)
            ws->mon = nullptr;
    }
    /* Keep each window tied to a valid display; its workspace decides visibility. */
    wl_list_for_each(c, &clients, link) {
        if (c->mon == m) {
            geom = c->geom;

            if (c->is_floating && selmon) {
                geom.x = selmon->m.x + c->geom.x - m->m.x;
                geom.y = selmon->m.y + c->geom.y - m->m.y;
            }
            c->mon = selmon;

            if (selmon)
                resize(c, c->is_fullscreen ? selmon->m : geom, 0);
        }
        if (c->ftl_monitor == m) {
            if (c->ftl && !m->wlr_output->enabled)
                wlr_foreign_toplevel_handle_v1_output_leave(c->ftl, m->wlr_output);
            c->ftl_monitor = nullptr;
        }
    }
    focus_client(focus_top(selmon), 1);
    print_status();
}

/* Apply a panel or background surface's newly committed state. */
void layer_surface_commit_notify(struct wl_listener *listener, void *data) {
    layer_surface_t                  *l             = wl_container_of(listener, l, surface_commit);
    struct wlr_layer_surface_v1      *layer_surface = l->layer_surface;
    struct wlr_scene_tree            *scene_layer = layers[layermap[layer_surface->current.layer]];
    struct wlr_layer_surface_v1_state old_state;

    if (l->layer_surface->initial_commit) {
        client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

        /* Arrange the surface using the state it is about to commit. */
        old_state                 = l->layer_surface->current;
        l->layer_surface->current = l->layer_surface->pending;
        arrange_layers(l->mon);
        l->layer_surface->current = old_state;
        return;
    }
    if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
        return;
    l->mapped = layer_surface->surface->mapped;

    if (l->dim)
        wlr_scene_node_set_enabled(&l->dim->node, l->mapped);

    if (scene_layer != l->scene->node.parent) {
        wlr_scene_node_reparent(&l->scene->node, scene_layer);

        if (l->dim) {
            wlr_scene_node_reparent(&l->dim->node, scene_layer);
            wlr_scene_node_place_below(&l->dim->node, &l->scene->node);
        }
        wl_list_remove(&l->link);
        wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
        wlr_scene_node_reparent(
            &l->popups->node,
            (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LAYER_TOP]
                                                                          : scene_layer)
        );
    }
    arrange_layers(l->mon);
}

/* Apply a Wayland window's initial state or confirmed resize. */
void commit_notify(struct wl_listener *listener, void *data) {
    client_t      *c = wl_container_of(listener, c, commit);
    struct wlr_box geom;

    if (c->surface.xdg->initial_commit) {
        /* Pick a display early so the application receives the right scale.
         * A title-based rule may move it once its title is available. */
        apply_rules(c, nullptr);

        if (c->mon) {
            client_set_scale(client_surface(c), c->mon->wlr_output->scale);
        }
        set_monitor(c, nullptr, 0); /* Reapply matching rules when the window maps. */

        wlr_xdg_toplevel_set_wm_capabilities(
            c->surface.xdg->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
        );

        if (c->decoration)
            request_decoration_mode(&c->set_decoration_mode, c->decoration);
        wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
        return;
    }
    /* Some applications, especially terminals, round requested sizes. Once
     * they confirm the resize, use the size they actually chose. */
    if (!c->is_fullscreen && c->resize) {
        client_get_geometry(c, &geom);
        swm_box_reconcile_commit(
            (struct swm_box *)&c->geom, geom.width, geom.height, c->bw, c->resize_edges
        );
    }
    if (c->resize && c->mon) {
        uint32_t resize_serial = c->resize;
        int      latest        = resize_serial <= c->surface.xdg->current.configure_serial;

        apply_bounds(c, &c->mon->w);
        resize_apply(c);

        if (latest)
            c->resize = 0;
        else
            c->geom = c->pending_geom;
    } else if (c->resize) {
        /* The client lost its display with a resize in flight; settle the
         * resize once a display is available again. */
    } else {
        resize(c, c->geom, c->is_floating && !c->is_fullscreen);
    }
    if (!c->resize)
        c->resize_edges = WLR_EDGE_NONE;
}

/* Create and constrain a popup after its initial state is committed. */
void popup_commit(struct wl_listener *listener, void *data) {
    struct wlr_surface   *surface = data;
    struct wlr_xdg_popup *popup   = wlr_xdg_popup_try_from_wlr_surface(surface);
    layer_surface_t      *l       = nullptr;
    client_t             *c       = nullptr;
    struct wlr_box        box;
    int                   type = -1;

    if (!popup) {
        wl_list_remove(&listener->link);
        listener_release(listener);
        return;
    }
    if (!popup->base->initial_commit)
        return;

    type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);

    if ((type == LAYER_SHELL && (!l || !l->mon)) || (type != LAYER_SHELL && (!c || !c->mon)) ||
        !popup->parent || !popup->parent->data)
        goto destroy_popup;
    popup->base->surface->data = wlr_scene_xdg_surface_create(popup->parent->data, popup->base);

    if (!popup->base->surface->data)
        goto destroy_popup;
    box    = type == LAYER_SHELL ? l->mon->m : c->mon->w;
    box.x -= (type == LAYER_SHELL ? l->scene->node.x : c->geom.x);
    box.y -= (type == LAYER_SHELL ? l->scene->node.y : c->geom.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    wl_list_remove(&listener->link);
    listener_release(listener);
    return;

destroy_popup:
    wl_list_remove(&listener->link);
    listener_release(listener);
    wlr_xdg_popup_destroy(popup);
}

/* Attach an event listener from the fixed-capacity listener pool. */
void listen_static(struct wl_signal *signal, wl_notify_func_t notify) {
    static_listener_t *slot = pool_take(&static_listener_pool);

    if (!slot)
        return;
    slot->listener.notify = notify;
    wl_signal_add(signal, &slot->listener);
}

/* Release a listener allocated from the static pool. */
void listener_release(struct wl_listener *listener) {
    static_listener_t *slot = wl_container_of(listener, slot, listener);
    pool_release(&static_listener_pool, slot);
}

/* Track a window's requested border-decoration mode. */
void create_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    client_t                              *c    = deco->toplevel->base->data;

    if (!c)
        return;
    c->decoration = deco;

    LISTEN(&deco->events.request_mode, &c->set_decoration_mode, request_decoration_mode);
    LISTEN(&deco->events.destroy, &c->destroy_decoration, destroy_decoration);

    request_decoration_mode(&c->set_decoration_mode, deco);
}

/* Track a new request to keep the session awake. */
void create_idle_inhibitor(struct wl_listener *listener, void *data) {
    struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;

    LISTEN_STATIC(&idle_inhibitor->events.destroy, destroy_idle_inhibitor);

    check_idle_inhibitor(nullptr);
}

/* Add a physical keyboard to the shared keyboard group. */
void create_keyboard(struct wlr_keyboard *keyboard) {
    /* Use the keyboard group's key mapping. */
    wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

    /* Add the keyboard to the shared group. */
    wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

/* Create a keyboard group with the configured key map and repeat settings. */
keyboard_group_t *create_keyboard_group(bool is_virtual) {
    keyboard_group_t   *group = pool_take(&keyboard_group_pool);
    struct xkb_context *context;
    struct xkb_keymap  *keymap;

    if (!group)
        return nullptr;
    group->wlr_group = wlr_keyboard_group_create();

    if (!group->wlr_group) {
        pool_release(&keyboard_group_pool, group);
        return nullptr;
    }
    group->wlr_group->data = group;
    group->is_virtual      = is_virtual;

    /* Prepare an XKB keymap and assign it to the keyboard group. */
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS)))
        die("failed to compile keymap");

    wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

    /* Listen for key and modifier changes. */
    LISTEN(&group->wlr_group->keyboard.events.key, &group->key, key_press);
    LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, key_press_modifiers);

    group->key_repeat_source = wl_event_loop_add_timer(event_loop, key_repeat, group);

    /* Wayland exposes one active keyboard per seat, so combine physical
     * keyboards into one group. */
    if (!is_virtual)
        wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    return group;
}

/* Add a new panel, background, or overlay surface to its display. */
void create_layer_surface(struct wl_listener *listener, void *data) {
    struct wlr_layer_surface_v1 *layer_surface = data;
    layer_surface_t             *l;
    const layer_rule_t          *r;
    monitor_t                   *m;
    struct wlr_surface          *surface     = layer_surface->surface;
    struct wlr_scene_tree       *scene_layer = layers[layermap[layer_surface->pending.layer]];

    if (!layer_surface->output &&
        !(layer_surface->output = selmon ? selmon->wlr_output : nullptr)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    if (!(m = layer_surface->output->data)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    if (!(l = pool_take(&layer_surface_pool))) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    l->type          = LAYER_SHELL;
    l->layer_surface = layer_surface;
    l->mon           = m;
    l->scene_layer   = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);

    if (!l->scene_layer) {
        pool_release(&layer_surface_pool, l);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    l->scene = l->scene_layer->tree;

    for (r = layerrules; r < END(layerrules); r++) {
        if (strcmp(layer_surface->namespace, r->namespace))
            continue;
        l->dim = wlr_scene_rect_create(scene_layer, 0, 0, r->dim);

        if (!l->dim) {
            wlr_scene_node_destroy(&l->scene->node);
            pool_release(&layer_surface_pool, l);
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
        wlr_scene_node_place_below(&l->dim->node, &l->scene->node);
        wlr_scene_node_set_enabled(&l->dim->node, 0);
        break;
    }
    l->popups = wlr_scene_tree_create(
        layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LAYER_TOP]
                                                                     : scene_layer
    );

    if (!l->popups) {
        if (l->dim)
            wlr_scene_node_destroy(&l->dim->node);
        wlr_scene_node_destroy(&l->scene->node);
        pool_release(&layer_surface_pool, l);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    layer_surface->data = l;
    surface->data       = l->popups;
    l->scene->node.data = l->popups->node.data = l;
    LISTEN(&surface->events.commit, &l->surface_commit, layer_surface_commit_notify);
    LISTEN(&surface->events.unmap, &l->unmap, layer_surface_unmap_notify);
    LISTEN(&layer_surface->events.destroy, &l->destroy, layer_surface_destroy_notify);

    wl_list_insert(&l->mon->layers[layer_surface->pending.layer], &l->link);
    wlr_surface_send_enter(surface, layer_surface->output);
}

/* Cover a display with a new session-lock surface. */
void create_lock_surface(struct wl_listener *listener, void *data) {
    session_lock_t                     *lock         = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    monitor_t             *m = lock_surface->output ? lock_surface->output->data : nullptr;
    struct wlr_scene_tree *scene_tree;

    if (!m) {
        wlr_session_lock_v1_destroy(lock->lock);
        return;
    }
    scene_tree = lock_surface->surface->data =
        wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);

    if (!scene_tree) {
        wlr_session_lock_v1_destroy(lock->lock);
        return;
    }
    m->lock_surface = lock_surface;

    wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
    wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

    LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroy_lock_surface);

    if (m == selmon)
        client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

/* Configure and publish a newly connected display. */
void create_monitor(struct wl_listener *listener, void *data) {
    /* The backend found a new display. */
    struct wlr_output      *wlr_output = data;
    const monitor_rule_t   *r;
    size_t                  i;
    struct wlr_output_state state;
    monitor_t              *m;

    if (!wlr_output_init_render(wlr_output, alloc, drw))
        return;

    if (!(m = pool_take(&monitor_pool))) {
        fprintf(stderr, "swm: ignoring output %s: monitor limit reached\n", wlr_output->name);
        return;
    }
    wlr_output->data = m;
    m->wlr_output    = wlr_output;

    for (i = 0; i < LENGTH(m->layers); i++)
        wl_list_init(&m->layers[i]);

    wlr_output_state_init(&state);
    /* Initialize the display from its matching configuration rule. */
    for (r = monrules; r < END(monrules); r++) {
        if (!r->name || strstr(wlr_output->name, r->name)) {
            m->m.x = r->x;
            m->m.y = r->y;
            wlr_output_state_set_scale(&state, r->scale);
            wlr_output_state_set_transform(&state, r->rr);
            break;
        }
    }
    /* Show the lowest-numbered hidden workspace on this output; client
     * monitor pointers are synced in update_monitors() once the output has its
     * final geometry. */
    m->ws = m->previous_workspace = free_workspace();

    if (m->ws)
        m->ws->mon = m;

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. We just pick the
     * monitor's preferred mode; a more sophisticated compositor would let
     * the user configure it. */
    wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

    /* Listen for display events. */
    LISTEN(&wlr_output->events.frame, &m->frame, render_monitor);
    LISTEN(&wlr_output->events.destroy, &m->destroy, cleanup_monitor);
    LISTEN(&wlr_output->events.request_state, &m->request_state, request_monitor_state);
    LISTEN(&wlr_output->events.bind, &m->output_bind, workspace_output_bind);

    wlr_output_state_set_enabled(&state, 1);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    wl_list_insert(&mons, &m->link);
    print_status();

    /* Hide unrelated content behind a transparent fullscreen window, as the
     * XDG shell protocol requires. */
    /* update_monitors() will set the final size and position. */
    m->fullscreen_bg = wlr_scene_rect_create(layers[LAYER_FULLSCREEN], 0, 0, fullscreen_bg);

    if (!m->fullscreen_bg)
        die("failed to create monitor fullscreen background");
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

    /* Add the display at its configured position and publish its properties
     * to applications. */
    m->scene_output = wlr_scene_output_create(scene, wlr_output);

    if (!m->scene_output)
        die("failed to create scene output");

    if (m->m.x == -1 && m->m.y == -1)
        wlr_output_layout_add_auto(output_layout, wlr_output);
    else
        wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

/* Allocate state and listeners for a new Wayland window. */
void create_notify(struct wl_listener *listener, void *data) {
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    struct wlr_xdg_toplevel *toplevel = data;
    client_t                *c        = nullptr;

    /* Allocate window state for this surface. */
    if (!(c = pool_take(&client_pool))) {
        wlr_xdg_toplevel_send_close(toplevel);
        return;
    }
    toplevel->base->data = c;
    c->surface.xdg       = toplevel->base;
    c->bw                = borderwidth;

    LISTEN(&toplevel->base->surface->events.commit, &c->commit, commit_notify);
    LISTEN(&toplevel->base->surface->events.map, &c->map, map_notify);
    LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmap_notify);
    LISTEN(&toplevel->events.destroy, &c->destroy, destroy_notify);
    LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreen_notify);
    LISTEN(&toplevel->events.request_maximize, &c->maximize, maximize_notify);
    LISTEN(&toplevel->events.set_title, &c->set_title, update_title);
    LISTEN(&toplevel->events.set_app_id, &c->set_appid, update_app_id);
    LISTEN(&toplevel->events.set_parent, &c->set_parent, dialog_changed);
}

/* Track modal state for a newly created XDG dialog. */
void create_dialog(struct wl_listener *listener, void *data) {
    struct wlr_xdg_dialog_v1 *dialog = data;
    client_t                 *c      = dialog->xdg_toplevel->base->data;

    if (!c)
        return;
    c->dialog = dialog;
    LISTEN(&dialog->events.set_modal, &c->dialog_modal, dialog_changed);
    LISTEN(&dialog->events.destroy, &c->dialog_destroy, dialog_destroyed);
    dialog_changed(&c->dialog_modal, nullptr);
}

/* Re-evaluate focus and pointer targets after dialog relationship changes. */
void dialog_changed(struct wl_listener *listener, void *data) {
    client_t *focused = nullptr;

    toplevel_from_wlr_surface(seat->keyboard_state.focused_surface, &focused, nullptr);
    if (focused && client_is_blocked(focused))
        focus_client(focus_top(focused->mon), 1);
    motion_notify(0, nullptr, 0, 0, 0, 0, 1);
}

/* Stop tracking a destroyed dialog and immediately restore parent interaction. */
void dialog_destroyed(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, dialog_destroy);

    wl_list_remove(&c->dialog_modal.link);
    wl_list_remove(&c->dialog_destroy.link);
    c->dialog = nullptr;
    dialog_changed(nullptr, nullptr);
}

/* Attach a pointer device and apply its configured input settings. */
void create_pointer(struct wlr_pointer *pointer) {
    struct libinput_device *device;

    if (wlr_input_device_is_libinput(&pointer->base) &&
        (device = wlr_libinput_get_device_handle(&pointer->base))) {

        if (libinput_device_config_tap_get_finger_count(device)) {
            libinput_device_config_tap_set_enabled(device, tap_to_click);
            libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
            libinput_device_config_tap_set_button_map(device, button_map);
        }
        if (libinput_device_config_scroll_has_natural_scroll(device))
            libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

        if (libinput_device_config_dwt_is_available(device))
            libinput_device_config_dwt_set_enabled(device, disable_while_typing);

        if (libinput_device_config_left_handed_is_available(device))
            libinput_device_config_left_handed_set(device, left_handed);

        if (libinput_device_config_middle_emulation_is_available(device))
            libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method(device, scroll_method);

        if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method(device, click_method);

        if (libinput_device_config_send_events_get_modes(device))
            libinput_device_config_send_events_set_mode(device, send_events_mode);

        if (libinput_device_config_accel_is_available(device)) {
            libinput_device_config_accel_set_profile(device, accel_profile);
            libinput_device_config_accel_set_speed(device, accel_speed);
        }
    }
    wlr_cursor_attach_input_device(cursor, &pointer->base);
}

/* Track an application's request to confine or lock the pointer. */
void create_pointer_constraint(struct wl_listener *listener, void *data) {
    pointer_constraint_t *pointer_constraint = pool_take(&pointer_constraint_pool);

    if (!pointer_constraint)
        return;
    pointer_constraint->constraint = data;
    LISTEN(
        &pointer_constraint->constraint->events.destroy,
        &pointer_constraint->destroy,
        destroy_pointer_constraint
    );

    if (pointer_constraint->constraint->surface == seat->pointer_state.focused_surface)
        cursor_constrain(pointer_constraint->constraint);
}

/* Wait for a new popup's initial commit before adding it to the scene. */
void create_popup(struct wl_listener *listener, void *data) {
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup *popup = data;

    LISTEN_STATIC(&popup->base->surface->events.commit, popup_commit);
}

/* Activate the pointer constraint belonging to the focused surface. */
void cursor_constrain(struct wlr_pointer_constraint_v1 *constraint) {
    if (active_constraint == constraint)
        return;

    if (active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(active_constraint);

    active_constraint = constraint;

    if (constraint)
        wlr_pointer_constraint_v1_send_activated(constraint);
}

/* Finish a group of pointer events for the focused application. */
void cursor_frame(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits a frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(seat);
}

/* Move the pointer to an application's requested position when a constraint ends. */
void cursor_warp_to_hint(void) {
    client_t *c  = nullptr;
    double    sx = active_constraint->current.cursor_hint.x;
    double    sy = active_constraint->current.cursor_hint.y;

    toplevel_from_wlr_surface(active_constraint->surface, &c, nullptr);

    if (c && active_constraint->current.cursor_hint.enabled) {
        wlr_cursor_warp(cursor, nullptr, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
        wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
    }
}

/* Stop tracking a window's decoration request after it is destroyed. */
void destroy_decoration(struct wl_listener *listener, void *data) {
    client_t    *c     = wl_container_of(listener, c, destroy_decoration);
    unsigned int oldbw = c->bw;

    wl_list_remove(&c->destroy_decoration.link);
    wl_list_remove(&c->set_decoration_mode.link);
    c->decoration = nullptr;
    c->bw         = client_border_width(c);

    if (oldbw != c->bw && client_surface(c)->mapped && c->scene)
        resize(c, c->geom, 0);
}

/* Remove a drag icon and restore pointer focus beneath it. */
void destroy_drag_icon(struct wl_listener *listener, void *data) {
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    focus_client(focus_top(selmon), 1);
    motion_notify(0, nullptr, 0, 0, 0, 0, 1);
    wl_list_remove(&listener->link);
    listener_release(listener);
}

/* Remove an idle request and recalculate whether the session may sleep. */
void destroy_idle_inhibitor(struct wl_listener *listener, void *data) {
    /* `data` is the wlr_surface of the idle inhibitor being destroyed,
     * and is still in the manager's list at this point. */
    check_idle_inhibitor(wlr_surface_get_root_surface(data));
    wl_list_remove(&listener->link);
    listener_release(listener);
}

/* Remove a destroyed panel, background, or overlay from its display. */
void layer_surface_destroy_notify(struct wl_listener *listener, void *data) {
    layer_surface_t *l = wl_container_of(listener, l, destroy);

    wl_list_remove(&l->link);
    wl_list_remove(&l->destroy.link);
    wl_list_remove(&l->unmap.link);
    wl_list_remove(&l->surface_commit.link);
    wlr_scene_node_destroy(&l->popups->node);

    if (l->dim)
        wlr_scene_node_destroy(&l->dim->node);
    pool_release(&layer_surface_pool, l);
}

/* Handle a session locker that exits without unlocking cleanly. */
void destroy_lock(session_lock_t *lock, int unlock) {
    wlr_seat_keyboard_notify_clear_focus(seat);
    input_method_set_focus(nullptr);
    locked = !unlock;

    if (unlock) {
        wlr_scene_node_set_enabled(&locked_bg->node, 0);
        focus_client(focus_top(selmon), 0);
        motion_notify(0, nullptr, 0, 0, 0, 0, 1);
    }
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);

    wlr_scene_node_destroy(&lock->scene->node);
    cur_lock = nullptr;
    pool_release(&session_lock_pool, lock);
}

/* Remove a display's session-lock surface and restore its background. */
void destroy_lock_surface(struct wl_listener *listener, void *data) {
    monitor_t                          *m = wl_container_of(listener, m, destroy_lock_surface);
    struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

    m->lock_surface = nullptr;
    wl_list_remove(&m->destroy_lock_surface.link);

    if (lock_surface->surface != seat->keyboard_state.focused_surface)
        return;

    if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
        surface = wl_container_of(cur_lock->surfaces.next, surface, link);
        client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
    } else if (!locked) {
        focus_client(focus_top(selmon), 1);
    } else {
        wlr_seat_keyboard_clear_focus(seat);
        input_method_set_focus(nullptr);
    }
}

/* Release all state associated with a destroyed Wayland window. */
void destroy_notify(struct wl_listener *listener, void *data) {
    /* Called when the xdg_toplevel is destroyed. */
    client_t *c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->set_appid.link);
    wl_list_remove(&c->fullscreen.link);

    if (c->type != XDG_SHELL) {
        wl_list_remove(&c->activate.link);
        wl_list_remove(&c->associate.link);
        wl_list_remove(&c->configure.link);
        wl_list_remove(&c->dissociate.link);
        wl_list_remove(&c->set_hints.link);
    } else {
        wl_list_remove(&c->commit.link);
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
        wl_list_remove(&c->maximize.link);
        wl_list_remove(&c->set_parent.link);

        if (c->dialog) {
            wl_list_remove(&c->dialog_modal.link);
            wl_list_remove(&c->dialog_destroy.link);
            c->dialog = nullptr;
        }

        if (c->decoration) {
            wl_list_remove(&c->destroy_decoration.link);
            wl_list_remove(&c->set_decoration_mode.link);
            c->decoration = nullptr;
        }
    }
    pool_release(&client_pool, c);
}

/* Remove a pointer constraint and restore unrestricted movement. */
void destroy_pointer_constraint(struct wl_listener *listener, void *data) {
    pointer_constraint_t *pointer_constraint =
        wl_container_of(listener, pointer_constraint, destroy);

    if (active_constraint == pointer_constraint->constraint) {
        cursor_warp_to_hint();
        active_constraint = nullptr;
    }
    wl_list_remove(&pointer_constraint->destroy.link);
    pool_release(&pointer_constraint_pool, pointer_constraint);
}

/* Release a completed session lock and its remaining surfaces. */
void destroy_session_lock(struct wl_listener *listener, void *data) {
    session_lock_t *lock = wl_container_of(listener, lock, destroy);
    destroy_lock(lock, 0);
}

/* Remove a keyboard group, its timer, and its event listeners. */
void destroy_keyboard_group(struct wl_listener *listener, void *data) {
    keyboard_group_t *group       = wl_container_of(listener, group, destroy);
    int               was_current = wlr_seat_get_keyboard(seat) == &group->wlr_group->keyboard;
    bool              is_main     = group == kb_group;

    if (group->is_virtual)
        virtual_keyboards--;
    wl_event_source_remove(group->key_repeat_source);
    wl_list_remove(&group->key.link);
    wl_list_remove(&group->modifiers.link);
    wl_list_remove(&group->destroy.link);
    wlr_keyboard_group_destroy(group->wlr_group);
    pool_release(&keyboard_group_pool, group);

    if (is_main) {
        kb_group = nullptr;
        return;
    }
    if (was_current)
        wlr_seat_set_keyboard(seat, &kb_group->wlr_group->keyboard);
    wlr_seat_set_capabilities(
        seat,
        WL_SEAT_CAPABILITY_POINTER |
            (!wl_list_empty(&kb_group->wlr_group->devices) || virtual_keyboards
                 ? WL_SEAT_CAPABILITY_KEYBOARD
                 : 0)
    );
}

/* Return the nearest enabled display in the requested direction. */
monitor_t *direction_to_monitor(enum wlr_direction dir) {
    struct wlr_output *next;

    if (!selmon)
        return nullptr;

    if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
        return selmon;

    if ((next = wlr_output_layout_adjacent_output(
             output_layout, dir, selmon->wlr_output, selmon->m.x, selmon->m.y
         )))
        return next->data;

    if ((next = wlr_output_layout_farthest_output(
             output_layout,
             dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
             selmon->wlr_output,
             selmon->m.x,
             selmon->m.y
         )))
        return next->data;

    return selmon;
}

/* Transfer keyboard focus to a window and update stacking and border state. */
void focus_client(client_t *c, int lift) {
    struct wlr_surface *old = seat->keyboard_state.focused_surface;
    int                 unused_lx, unused_ly, old_client_type;
    client_t           *old_c = nullptr;
    layer_surface_t    *old_l = nullptr;

    if (locked)
        return;

    if (c && client_is_blocked(c))
        c = focus_top(c->mon);

    /* Raise the window when requested. */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDG_SHELL) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }
    /* Put the new window first in the focus history and select its display. */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&fstack, &c->flink);
        selmon       = c->mon;
        c->is_urgent = false;

        if (c->mon && c->mon->ws && c->mon->ws->lt->arrange == max_stack)
            arrange(c->mon);

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation. */
        if (!exclusive_focus && !seat->drag)
            client_set_border_color(c, focuscolor);
    }
    /* Deactivate the old window when focus changes. */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in fstack to render its border with
         * focuscolor and focus it after the overlay is closed. */
        if (old_client_type == LAYER_SHELL &&
            wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) &&
            old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
            return;
            /* Don't deactivate old client if the new one wants focus, as this
             * avoids breaking applications such as winecfg. */
        } else if (
            old_c && old_c->scene && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))
        ) {
            client_set_border_color(old_c, bordercolor);
            client_activate_surface(old, 0);
        }
    }
    print_status();

    if (!c) {
        /* Clear keyboard focus when no window replaces the old one. */
        wlr_seat_keyboard_notify_clear_focus(seat);
        input_method_set_focus(nullptr);
        return;
    }
    /* Update the cursor image for the focused surface. */
    motion_notify(0, nullptr, 0, 0, 0, 0, 0);
    /* Give keyboard focus to the window's main surface. */
    client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));
    /* Mark the new window as active. */
    client_activate_surface(client_surface(c), 1);
}

/* Return the main client in a related client group. */
client_t *client_main(client_t *c) {
    client_t *p;

    while (c && (p = client_get_parent(c)) && p != c)
        c = p;
    return c;
}

/* Return whether a mapped modal XDG child blocks this direct parent. */
bool client_is_blocked(client_t *c) {
    client_t *child;

    if (!c || client_is_x11(c))
        return false;

    wl_list_for_each(child, &clients, link) {
        if (!client_is_x11(child) && child->dialog && child->dialog->modal &&
            client_surface(child)->mapped &&
            child->surface.xdg->toplevel->parent == c->surface.xdg->toplevel)
            return true;
    }
    return false;
}

/* Return whether two clients belong to one group. */
bool clients_related(client_t *a, client_t *b) {
    return a && b && client_main(a) == client_main(b);
}

/* Focus the nearest related window when the current one closes. */
client_t *focus_close(client_t *c) {
    client_t       *p;
    struct wl_list *node;

    if (!c || !c->mon)
        return nullptr;

    if ((p = client_get_parent(c)) && VISIBLEON(p, c->mon) && !client_is_blocked(p))
        return p;

    for (node = c->link.prev; node != &c->link; node = node->prev) {
        if (node == &clients)
            continue;
        p = wl_container_of(node, p, link);

        if (VISIBLEON(p, c->mon) && !client_is_blocked(p) && !clients_related(p, c))
            return p;
    }
    return nullptr;
}

/* Select the next enabled display and focus its top window. */
void focus_monitor(const arg_t *arg) {
    int i = 0, nmons = wl_list_length(&mons);

    if (selmon && nmons) {
        do /* Skip disabled displays. */
            selmon = direction_to_monitor(arg->i);
        while (!selmon->wlr_output->enabled && i++ < nmons);
    }
    focus_client(focus_top(selmon), 1);
}

/* Move focus forward or backward through tiled windows. */
void focus_stack(const arg_t *arg) {
    /* Focus the next or previous tiled window on the selected display. */
    client_t       *c = nullptr, *sel = focus_top(selmon);
    struct wl_list *node;

    if (!sel)
        return;

    for (node = arg->i > 0 ? sel->link.next : sel->link.prev; node != &sel->link;
         node = arg->i > 0 ? node->next : node->prev) {
        if (node == &clients)
            continue;
        c = wl_container_of(node, c, link);

        if (VISIBLEON(c, selmon) && !client_is_blocked(c) && !client_get_parent(c)) {
            bool fullscreen = sel->is_fullscreen;

            if (fullscreen)
                set_fullscreen(sel, 0);
            focus_client(c, 1);

            if (fullscreen)
                set_fullscreen(c, 1);
            return;
        }
    }
}

/* Toggle focus between the master window and the previous window. */
void focus_main(const arg_t *arg) {
    /* focus the main (first tiled) window; if it
     * already has focus, return focus to the previously focused window. */
    client_t *c, *sel = focus_top(selmon);

    wl_list_for_each(c, &clients, link) {
        if (VISIBLEON(c, selmon) && !client_is_blocked(c) && !c->is_floating) {
            if (c != sel) {
                focus_client(c, 1);
            } else {
                client_t *p;
                wl_list_for_each(p, &fstack, flink) {
                    if (p != sel && VISIBLEON(p, selmon) && !client_is_blocked(p)) {
                        focus_client(p, 1);
                        break;
                    }
                }
            }
            return;
        }
    }
}

/* Show and focus the most recently urgent window. */
void focus_urgent(const arg_t *arg) {
    /* focus the most recently urgent window, switching to its
     * workspace if necessary. */
    client_t *c;

    wl_list_for_each(c, &fstack, flink) {
        if (c->is_urgent && c->ws) {
            if (c->ws->mon != selmon)
                view_workspace(c->ws, selmon);
            focus_client(c, 1);
            return;
        }
    }
}

/* Return the most recently focused visible window. */
client_t *focus_top(monitor_t *m) {
    client_t *c;
    wl_list_for_each(c, &fstack, flink) {
        if (VISIBLEON(c, m) && !client_is_blocked(c))
            return c;
    }
    return nullptr;
}

/* Show and focus a window requested by a taskbar or switcher. */
void ftl_activate_notify(struct wl_listener *listener, void *data) {
    /* A taskbar requested this window; show its workspace and focus it. */
    client_t *c = wl_container_of(listener, c, ftl_activate);

    if (c->ws && c->ws->mon != selmon)
        view_workspace(c->ws, selmon);
    focus_client(c, 1);
}

/* Create a scene-backed capture source for a published window. */
void ftl_capture_request_notify(struct wl_listener *listener, void *data) {
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
    struct wlr_ext_image_capture_source_v1                                  *source;
    client_t *c = request->toplevel_handle->data;

    if (!c || !c->scene || !client_surface(c)->mapped)
        return;
    source = wlr_ext_image_capture_source_v1_create_with_scene_node(
        &c->scene->node, event_loop, alloc, drw
    );

    if (source)
        wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, source);
}

/* Forward a taskbar's close request to the selected window. */
void ftl_close_notify(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, ftl_close);

    client_send_close(c);
}

/* Apply a taskbar's fullscreen request to a window. */
void ftl_fullscreen_notify(struct wl_listener *listener, void *data) {
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    client_t *c = wl_container_of(listener, c, ftl_fullscreen);

    set_fullscreen(c, event->fullscreen);
}

/* Publish a window's title, application ID, display, and state. */
void ftl_sync(client_t *c) {
    /* Publish this window's current state to taskbars and switchers. */
    const char *title, *appid;

    if (!c->ftl)
        return;
    title = client_get_title(c);
    appid = client_get_appid(c);
    wlr_foreign_toplevel_handle_v1_set_title(c->ftl, title ? title : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(c->ftl, appid ? appid : "");

    if (c->ftl_monitor != c->mon) {
        if (c->ftl_monitor)
            wlr_foreign_toplevel_handle_v1_output_leave(c->ftl, c->ftl_monitor->wlr_output);

        if (c->mon)
            wlr_foreign_toplevel_handle_v1_output_enter(c->ftl, c->mon->wlr_output);
        c->ftl_monitor = c->mon;
    }
    wlr_foreign_toplevel_handle_v1_set_activated(c->ftl, c == focus_top(selmon));
    wlr_foreign_toplevel_handle_v1_set_fullscreen(c->ftl, c->is_fullscreen);

    if (c->extftl) {
        struct wlr_ext_foreign_toplevel_handle_v1_state state = {
            .app_id = appid,
            .title  = title,
        };
        wlr_ext_foreign_toplevel_handle_v1_update_state(c->extftl, &state);
    }
}

/* Apply an application's requested fullscreen state. */
void fullscreen_notify(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, fullscreen);
    set_fullscreen(c, client_wants_fullscreen(c));
}

/* Return the enabled text input belonging to the focused application. */
struct wlr_text_input_v3 *input_method_focused_text_input(void) {
    /* Find the enabled text input for the focused application. */
    struct wlr_text_input_v3 *ti;
    struct wlr_surface       *surface = seat->keyboard_state.focused_surface;

    if (!surface)
        return nullptr;
    wl_list_for_each(ti, &ti_mgr->text_inputs, link) {
        if (ti->focused_surface == surface && ti->current_enabled)
            return ti;
    }
    return nullptr;
}

/* Send the focused application's text-input state to the input method. */
void input_method_send_state(struct wlr_text_input_v3 *ti) {
    /* Send the application's text-input state to the input method. */
    if (!input_method)
        return;

    if (ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
        wlr_input_method_v2_send_surrounding_text(
            input_method,
            ti->current.surrounding.text,
            ti->current.surrounding.cursor,
            ti->current.surrounding.anchor
        );
        wlr_input_method_v2_send_text_change_cause(input_method, ti->current.text_change_cause);
    }
    if (ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(
            input_method, ti->current.content_type.hint, ti->current.content_type.purpose
        );
    wlr_input_method_v2_send_done(input_method);
}

/* Update text-input and input-method state after keyboard focus changes. */
void input_method_set_focus(struct wlr_surface *surface) {
    /* Tell text-input clients when keyboard focus moves, and deactivate the
     * input method if its application lost focus. */
    struct wlr_text_input_v3 *ti;

    wl_list_for_each(ti, &ti_mgr->text_inputs, link) {
        if (ti->focused_surface) {
            if (ti->focused_surface == surface)
                continue;

            if (input_method && ti->current_enabled) {
                wlr_input_method_v2_send_deactivate(input_method);
                wlr_input_method_v2_send_done(input_method);
            }
            wlr_text_input_v3_send_leave(ti);
        }
        if (surface &&
            wl_resource_get_client(ti->resource) == wl_resource_get_client(surface->resource))
            wlr_text_input_v3_send_enter(ti, surface);
    }
    update_shortcuts_inhibitors(surface);
    position_input_popups();
}

/* Forward text and edits committed by the input method to the application. */
void input_method_commit_notify(struct wl_listener *listener, void *data) {
    /* Send completed input-method text and edits to the application. */
    struct wlr_input_method_v2 *im = input_method;
    struct wlr_text_input_v3   *ti = input_method_focused_text_input();

    if (!im || !ti)
        return;

    if (im->current.preedit.text)
        wlr_text_input_v3_send_preedit_string(
            ti,
            im->current.preedit.text,
            im->current.preedit.cursor_begin,
            im->current.preedit.cursor_end
        );

    if (im->current.commit_text)
        wlr_text_input_v3_send_commit_string(ti, im->current.commit_text);

    if (im->current.delete.before_length || im->current.delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(
            ti, im->current.delete.before_length, im->current.delete.after_length
        );
    wlr_text_input_v3_send_done(ti);
}

/* Connect a newly available input method to the focused text input. */
void input_method_create_notify(struct wl_listener *listener, void *data) {
    struct wlr_input_method_v2 *im = data;
    struct wlr_text_input_v3   *ti;

    if (input_method) {
        /* Only one input method can serve a seat. */
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    input_method = im;
    wl_signal_add(&im->events.commit, &im_commit);
    wl_signal_add(&im->events.destroy, &im_destroy);
    wl_signal_add(&im->events.grab_keyboard, &im_grab_kb);
    wl_signal_add(&im->events.new_popup_surface, &im_new_popup);

    if ((ti = input_method_focused_text_input())) {
        wlr_input_method_v2_send_activate(input_method);
        input_method_send_state(ti);
    }
}

/* Disconnect the active input method and remove its listeners. */
void input_method_destroy_notify(struct wl_listener *listener, void *data) {
    wl_list_remove(&im_commit.link);
    wl_list_remove(&im_destroy.link);
    wl_list_remove(&im_grab_kb.link);
    wl_list_remove(&im_new_popup.link);
    input_method = nullptr;
}

/* Give the input method access to the active keyboard. */
void input_method_grab_keyboard(struct wl_listener *listener, void *data) {
    struct wlr_input_method_keyboard_grab_v2 *grab = data;

    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, &kb_group->wlr_group->keyboard);
}

/* Add a new input-method popup above regular windows. */
void input_method_new_popup(struct wl_listener *listener, void *data) {
    struct wlr_input_popup_surface_v2 *popup = data;
    input_popup_t                     *p     = pool_take(&input_popup_pool);

    if (!p)
        return;

    p->popup = popup;
    p->scene = wlr_scene_subsurface_tree_create(layers[LAYER_OVERLAY], popup->surface);

    if (!p->scene) {
        pool_release(&input_popup_pool, p);
        return;
    }
    wlr_scene_node_set_enabled(&p->scene->node, 0);
    wl_list_insert(&input_popups, &p->link);

    LISTEN(&popup->surface->events.map, &p->map, input_popup_map);
    LISTEN(&popup->surface->events.unmap, &p->unmap, input_popup_unmap);
    LISTEN(&popup->surface->events.commit, &p->commit, input_popup_commit);
    LISTEN(&popup->events.destroy, &p->destroy, input_popup_destroy);
}

/* Position an input-method popup beside the focused text cursor. */
bool input_popup_position(input_popup_t *p) {
    /* Place the input-method popup below the application's text cursor. */
    struct wlr_text_input_v3 *ti = input_method_focused_text_input();
    struct wlr_box            rect, output;
    client_t                 *c = nullptr;
    struct swm_box            position;
    int                       width, height;

    if (!ti)
        return false;
    toplevel_from_wlr_surface(ti->focused_surface, &c, nullptr);

    if (!c)
        return false;
    rect = ti->current.cursor_rectangle;

    if (!(ti->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE))
        rect = (struct wlr_box){};
    width  = p->popup->surface->current.width;
    height = p->popup->surface->current.height;

    if (c->mon)
        output = c->mon->m;
    position = swm_popup_position(
        (const struct swm_box *)&c->geom,
        c->bw,
        (const struct swm_box *)&rect,
        width,
        height,
        c->mon ? (const struct swm_box *)&output : nullptr
    );
    wlr_scene_node_set_position(&p->scene->node, position.x, position.y);
    wlr_input_popup_surface_v2_send_text_input_rectangle(
        p->popup, &(struct wlr_box){ .y = -rect.height, .width = rect.width, .height = rect.height }
    );
    return true;
}

/* Reposition and show all mapped input-method popups. */
void position_input_popups(void) {
    input_popup_t *p;

    wl_list_for_each(p, &input_popups, link) wlr_scene_node_set_enabled(
        &p->scene->node, p->popup->surface->mapped && input_popup_position(p)
    );
}

/* Position and show a newly mapped input-method popup. */
void input_popup_map(struct wl_listener *listener, void *data) {
    input_popup_t *p = wl_container_of(listener, p, map);

    wlr_scene_node_set_enabled(&p->scene->node, input_popup_position(p));
}

/* Hide an input-method popup when it is unmapped. */
void input_popup_unmap(struct wl_listener *listener, void *data) {
    input_popup_t *p = wl_container_of(listener, p, unmap);

    wlr_scene_node_set_enabled(&p->scene->node, 0);
}

/* Reposition an input-method popup after its content changes. */
void input_popup_commit(struct wl_listener *listener, void *data) {
    input_popup_t *p = wl_container_of(listener, p, commit);

    wlr_scene_node_set_enabled(
        &p->scene->node, p->popup->surface->mapped && input_popup_position(p)
    );
}

/* Remove a destroyed input-method popup and release its state. */
void input_popup_destroy(struct wl_listener *listener, void *data) {
    input_popup_t *p = wl_container_of(listener, p, destroy);

    wl_list_remove(&p->link);
    wl_list_remove(&p->map.link);
    wl_list_remove(&p->unmap.link);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    wlr_scene_node_destroy(&p->scene->node);
    pool_release(&input_popup_pool, p);
}

/* Track a new application's text-input connection. */
void text_input_create_notify(struct wl_listener *listener, void *data) {
    struct wlr_text_input_v3 *wlr_ti  = data;
    struct wlr_surface       *surface = seat->keyboard_state.focused_surface;
    text_input_t             *ti      = pool_take(&text_input_pool);

    if (!ti)
        return;

    ti->ti = wlr_ti;
    LISTEN(&wlr_ti->events.enable, &ti->enable, text_input_enable_notify);
    LISTEN(&wlr_ti->events.commit, &ti->commit, text_input_commit_notify);
    LISTEN(&wlr_ti->events.disable, &ti->disable, text_input_disable_notify);
    LISTEN(&wlr_ti->events.destroy, &ti->destroy, text_input_destroy_notify);

    if (surface &&
        wl_resource_get_client(wlr_ti->resource) == wl_resource_get_client(surface->resource))
        wlr_text_input_v3_send_enter(wlr_ti, surface);
}

/* Activate the input method for an enabled text field. */
void text_input_enable_notify(struct wl_listener *listener, void *data) {
    text_input_t *ti = wl_container_of(listener, ti, enable);

    if (!input_method || ti->ti != input_method_focused_text_input())
        return;
    wlr_input_method_v2_send_activate(input_method);
    input_method_send_state(ti->ti);
}

/* Forward an application's updated text-field state to the input method. */
void text_input_commit_notify(struct wl_listener *listener, void *data) {
    text_input_t *ti = wl_container_of(listener, ti, commit);

    if (!input_method || ti->ti != input_method_focused_text_input())
        return;
    input_method_send_state(ti->ti);
    position_input_popups();
}

/* Deactivate the input method when an application leaves its text field. */
void text_input_disable_notify(struct wl_listener *listener, void *data) {
    text_input_t *ti = wl_container_of(listener, ti, disable);

    if (!input_method || ti->ti->focused_surface != seat->keyboard_state.focused_surface)
        return;
    wlr_input_method_v2_send_deactivate(input_method);
    wlr_input_method_v2_send_done(input_method);
}

/* Disconnect and release a destroyed text-input connection. */
void text_input_destroy_notify(struct wl_listener *listener, void *data) {
    text_input_t *ti = wl_container_of(listener, ti, destroy);

    if (input_method && ti->ti->current_enabled &&
        ti->ti->focused_surface == seat->keyboard_state.focused_surface) {
        wlr_input_method_v2_send_deactivate(input_method);
        wlr_input_method_v2_send_done(input_method);
    }
    wl_list_remove(&ti->enable.link);
    wl_list_remove(&ti->commit.link);
    wl_list_remove(&ti->disable.link);
    wl_list_remove(&ti->destroy.link);
    pool_release(&text_input_pool, ti);
}

/* Recover from a GPU reset. */
void gpu_reset(struct wl_listener *listener, void *data) {
    struct wlr_renderer  *old_drw   = drw;
    struct wlr_allocator *old_alloc = alloc;
    struct monitor_t     *m;

    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't recreate renderer");

    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't recreate allocator");

    wl_list_remove(&gpu_reset_listener.link);
    wl_signal_add(&drw->events.lost, &gpu_reset_listener);

    wlr_compositor_set_renderer(compositor, drw);

    wl_list_for_each(m, &mons, link) {
        wlr_output_init_render(m->wlr_output, alloc, drw);
    }
    wlr_allocator_destroy(old_alloc);
    wlr_renderer_destroy(old_drw);
}

/* Reap child processes or begin shutdown when a queued signal arrives. */
int handle_signal(int signo, void *data) {
    if (signo == SIGCHLD) {
        pid_t            pid;
        pending_spawn_t *ps;
        struct wl_list  *node, *next;

        while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
            size_t i;

            if (pid == child_pid)
                child_pid = -1;

            for (i = 0; i < autostart_len; i++)

                if (autostart_pids[i] == pid)
                    autostart_pids[i] = -1;

            for (node = pending_spawns.next; node != &pending_spawns; node = next) {
                next = node->next;
                ps   = wl_container_of(node, ps, link);

                if (ps->pid != pid)
                    continue;
                wl_list_remove(&ps->link);
                pool_release(&pending_spawn_pool, ps);
            }
        }
    } else if (signo == SIGINT || signo == SIGTERM)
        quit(nullptr);
    return 0;
}

/* Add a newly connected keyboard or pointer and update seat capabilities. */
void input_device(struct wl_listener *listener, void *data) {
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct wlr_input_device *device = data;
    uint32_t                 caps;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        create_keyboard(wlr_keyboard_from_input_device(device));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        create_pointer(wlr_pointer_from_input_device(device));
        break;
    default:
        /* TODO: Handle other input device types. */
        break;
    }
    /* Advertise the input devices available to applications. swm always has
     * a cursor, even when no physical pointing device is connected. */
    /* TODO: Check whether swm truly needs to always advertise a pointer. */
    caps = WL_SEAT_CAPABILITY_POINTER;

    if (!wl_list_empty(&kb_group->wlr_group->devices) || virtual_keyboards)
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat, caps);
}

/* Run the matching window-manager shortcut and report whether it may repeat. */
int key_binding(uint32_t mods, xkb_keysym_t sym, bool repeat) {
    /* Handle window-manager shortcuts before applications receive the key. */
    const key_t *k;
    int          inhibited = shortcuts_inhibited();

    for (k = keys; k < END(keys); k++) {
        if (CLEANMASK(mods) == CLEANMASK(k->mod) &&
            xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym) && k->func) {
            int repeatable = k->func == focus_stack || k->func == swap_client ||
                             k->func == stack_config || k->func == cycle_workspace;

            if (repeat && !repeatable)
                continue;
            /* Keep core window-management controls available even when a
             * fullscreen client inhibits application-facing shortcuts. */
            if (inhibited && k->func != change_vt && k->func != quit && k->func != focus_stack &&
                k->func != toggle_max_stack && k->func != toggle_fullscreen)
                continue;
            k->func(&k->arg);
            return repeatable ? 2 : 1;
        }
    }
    return 0;
}

/* Handle shortcuts and forward unhandled key events to the focused application. */
void key_press(struct wl_listener *listener, void *data) {
    int i;

    /* This event is raised when a key is pressed or released. */
    keyboard_group_t              *group = wl_container_of(listener, group, key);
    struct wlr_keyboard_key_event *event = data;

    /* Convert the input key code to the numbering used by XKB. */
    uint32_t keycode = event->keycode + 8;

    /* Resolve the key to one or more symbols using the active key map. */
    const xkb_keysym_t *syms;
    int      nsyms   = xkb_state_key_get_syms(group->wlr_group->keyboard.xkb_state, keycode, &syms);
    int      handled = 0;
    uint32_t mods    = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    /* Handle shortcuts only on key press and while the session is unlocked. */
    if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (i = 0; i < nsyms; i++)
            handled |= key_binding(mods, syms[i], 0);
    }
    if ((handled & 2) && group->wlr_group->keyboard.repeat_info.delay > 0) {
        group->mods    = mods;
        group->keysyms = syms;
        group->nsyms   = nsyms;
        wl_event_source_timer_update(
            group->key_repeat_source, group->wlr_group->keyboard.repeat_info.delay
        );
    } else {
        group->nsyms = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
    }
    if (handled)
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Forward unhandled keys to the focused application. */
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

/* Forward a keyboard's updated modifier state to the focused application. */
void key_press_modifiers(struct wl_listener *listener, void *data) {
    /* A modifier such as Shift or Alt changed. */
    keyboard_group_t *group = wl_container_of(listener, group, modifiers);

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Forward the new modifier state to the focused application. */
    wlr_seat_keyboard_notify_modifiers(seat, &group->wlr_group->keyboard.modifiers);
}

/* Repeat the active shortcut at the keyboard's configured rate. */
int key_repeat(void *data) {
    keyboard_group_t *group = data;
    int               i;

    if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
        return 0;

    wl_event_source_timer_update(
        group->key_repeat_source, 1000 / group->wlr_group->keyboard.repeat_info.rate
    );

    for (i = 0; i < group->nsyms; i++)
        key_binding(group->mods, group->keysyms[i], 1);

    return 0;
}

/* Ask the focused application to close its window. */
void kill_client(const arg_t *arg) {
    client_t *sel = focus_top(selmon);

    if (sel)
        client_send_close(sel);
}

/* Accept a session lock and prevent applications from receiving input. */
void lock_session(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_v1 *session_lock = data;
    session_lock_t             *lock;

    if (cur_lock) {
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    if (!(lock = pool_take(&session_lock_pool))) {
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    lock->scene = wlr_scene_tree_create(layers[LAYER_BLOCK]);

    if (!lock->scene) {
        pool_release(&session_lock_pool, lock);
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    session_lock->data = lock;
    wlr_scene_node_set_enabled(&locked_bg->node, 1);
    focus_client(nullptr, 0);

    cur_lock = lock->lock = session_lock;
    locked                = true;

    LISTEN(&session_lock->events.new_surface, &lock->new_surface, create_lock_surface);
    LISTEN(&session_lock->events.destroy, &lock->destroy, destroy_session_lock);
    LISTEN(&session_lock->events.unlock, &lock->unlock, unlock_session);

    wlr_session_lock_v1_send_locked(session_lock);
}

/* Build the path used to persist floating window geometry. */
static const char *window_state_path(void) {
    static char path[MAX_STATE_PATH];
    const char *base = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");

    if (base && *base)
        snprintf(path, sizeof(path), "%s/swm/windows", base);
    else
        snprintf(path, sizeof(path), "%s/.local/state/swm/windows", home ? home : "/tmp");
    return path;
}

/* Replace delimiters that would corrupt a persisted state record. */
static void state_field(char dst[MAX_WINDOW_STATE_FIELD], const char *src) {
    swm_sanitize_field(dst, MAX_WINDOW_STATE_FIELD, src, "broken");
}

/* Find persisted state matching a client. */
static window_state_t *find_window_state(client_t *c) {
    window_state_t *state;
    char            appid[MAX_WINDOW_STATE_FIELD], title[MAX_WINDOW_STATE_FIELD];

    state_field(appid, client_get_appid(c));
    state_field(title, client_get_title(c));

    wl_list_for_each(state, &window_states, link) {
        if (!strcmp(state->appid, appid) && (!state->title[0] || !strcmp(state->title, title))) {
            return state;
        }
    }
    return nullptr;
}

/* Write all remembered floating-window geometries to disk. */
void save_window_states(void) {
    window_state_t *state;
    FILE           *f;
    const char     *path = window_state_path();
    char            dir[MAX_STATE_PATH], tmppath[MAX_STATE_PATH], *slash, *p;
    int             ok;

    snprintf(dir, sizeof(dir), "%s", path);

    if ((slash = strrchr(dir, '/'))) {
        *slash = '\0';
        for (p = dir + 1; *p; p++) {
            if (*p != '/')
                continue;
            *p = '\0';
            if (mkdir(dir, 0700) < 0 && errno != EEXIST)
                return;
            *p = '/';
        }
        if (mkdir(dir, 0700) < 0 && errno != EEXIST)
            return;
    }
    if (snprintf(tmppath, sizeof(tmppath), "%s.tmp.%ld", path, (long)getpid()) >=
        (int)sizeof(tmppath))
        return;

    if (!(f = fopen(tmppath, "w"))) {
        fprintf(stderr, "swm: cannot save floating window state: %s\n", strerror(errno));
        return;
    }
    wl_list_for_each(state, &window_states, link) fprintf(
        f,
        "%s\t%s\t%d\t%d\t%d\t%d\n",
        state->appid,
        state->title,
        state->geom.x,
        state->geom.y,
        state->geom.width,
        state->geom.height
    );
    ok = !ferror(f) && fflush(f) == 0;

    if (fclose(f) < 0)
        ok = 0;

    if (!ok || rename(tmppath, path) < 0) {
        fprintf(stderr, "swm: cannot save floating window state: %s\n", strerror(errno));
        unlink(tmppath);
    }
}

/* Load remembered floating-window geometries from disk. */
void load_window_states(void) {
    FILE           *f;
    window_state_t *state;
    char            appid[MAX_WINDOW_STATE_FIELD], title[MAX_WINDOW_STATE_FIELD];
    char            line[MAX_WINDOW_STATE_LINE];
    struct swm_box  geometry;

    if (!(f = fopen(window_state_path(), "r")))
        return;

    while (fgets(line, sizeof(line), f)) {
        if (!swm_parse_window_state(line, appid, sizeof(appid), title, sizeof(title), &geometry))
            continue;

        if (!(state = pool_take(&window_state_pool))) {
            fprintf(stderr, "swm: ignoring remaining saved window states\n");
            break;
        }
        state_field(state->appid, appid);

        if (title[0])
            state_field(state->title, title);
        else
            state->title[0] = '\0';
        state->geom = (struct wlr_box){ geometry.x, geometry.y, geometry.width, geometry.height };
        wl_list_insert(window_states.prev, &state->link);
    }
    fclose(f);
}

/* Save a floating window's current geometry for a later session. */
void remember_client(client_t *c) {
    window_state_t *state;

    if (!c || !c->persist_float || !c->is_floating || client_get_parent(c))
        return;

    if (!(state = find_window_state(c))) {
        if (!(state = pool_take(&window_state_pool)))
            return;
        state_field(state->appid, client_get_appid(c));
        state_field(state->title, client_get_title(c));
        wl_list_insert(window_states.prev, &state->link);
    } else if (!state->title[0]) {
        state_field(state->title, client_get_title(c));
    }
    state->geom = c->geom;
    save_window_states();
}

/* Remove a window's saved geometry. */
void forget_client(client_t *c) {
    window_state_t *state;

    if (!c || !(state = find_window_state(c)))
        return;
    wl_list_remove(&state->link);
    pool_release(&window_state_pool, state);
    save_window_states();
}

/* Restore a floating window to its saved geometry when available. */
void restore_client(client_t *c) {
    window_state_t *state;

    if (!c->persist_float || client_get_parent(c))
        return;

    if ((state = find_window_state(c))) {
        resize(c, state->geom, 0);
        set_floating(c, 1);
    }
}

/* Make a new window visible, place it, publish it, and focus it. */
void map_notify(struct wl_listener *listener, void *data) {
    /* Called when the surface is mapped, or ready to display on-screen. */
    client_t        *p = nullptr;
    client_t        *w, *c = wl_container_of(listener, c, map);
    monitor_t       *m;
    pending_spawn_t *ps, *pstmp;
    workspace_t     *spawnws = nullptr;
    pid_t            pid     = -1;
    unsigned int     oldbw;
    int              i, inherit_fullscreen = 0;

    /* Create drawing nodes for the window and its border. */
    c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LAYER_TILE]);

    if (!c->scene) {
        client_send_close(c);
        return;
    }
    /* arrange() makes the window visible later. */
    wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
    c->scene_surface = c->type == XDG_SHELL
                           ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
                           : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));

    if (!c->scene_surface) {
        wlr_scene_node_destroy(&c->scene->node);
        c->scene = client_surface(c)->data = nullptr;
        client_send_close(c);
        return;
    }
    c->scene->node.data = c->scene_surface->node.data = c;

    client_get_geometry(c, &c->geom);

    /* Handle unmanaged windows before creating borders. */
    if (client_is_unmanaged(c)) {
        /* Unmanaged X11 windows always float. */
        wlr_scene_node_reparent(&c->scene->node, layers[LAYER_FLOAT]);
        wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
        client_set_size(c, c->geom.width, c->geom.height);

        if (client_wants_focus(c)) {
            focus_client(c, 1);
            exclusive_focus = c;
        }
        return;
    }
    for (i = 0; i < 4; i++) {
        c->border[i] =
            wlr_scene_rect_create(c->scene, 0, 0, c->is_urgent ? urgentcolor : bordercolor);

        if (!c->border[i]) {
            wlr_scene_node_destroy(&c->scene->node);
            memset(c->border, 0, sizeof(c->border));
            c->scene = client_surface(c)->data = nullptr;
            client_send_close(c);
            return;
        }
        c->border[i]->node.data = c;
    }
    /* Include the border in the window's initial size. */
    client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    c->geom.width  += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Add new windows after existing tiled windows so they do not replace the
     * master. Keep the newest window first in the focus history. */
    c->pending_map = 1;
    wl_list_insert(clients.prev, &c->link);
    wl_list_insert(&fstack, &c->flink);

    if (!client_is_x11(c)) {
        wl_client_get_credentials(c->surface.xdg->client->client, &pid, nullptr, nullptr);
        wl_list_for_each_safe(ps, pstmp, &pending_spawns, link) {
            if (ps->pid != pid)
                continue;
            spawnws = ps->ws;
            wl_list_remove(&ps->link);
            pool_release(&pending_spawn_pool, ps);
            break;
        }
    }
    /* Child windows float on their parent's workspace and display. Apply the
     * configured matching rules to other windows. */
    if ((p = client_get_parent(c))) {
        c->is_floating = true;
        set_monitor(c, p->mon, p->ws);
    } else {
        apply_rules(c, spawnws);
        c->persist_float  = c->is_floating;
        oldbw             = c->bw;
        c->bw             = client_border_width(c);
        c->geom.width    += 2 * ((int)c->bw - (int)oldbw);
        c->geom.height   += 2 * ((int)c->bw - (int)oldbw);
        restore_client(c);
    }
    /* Publish the window to taskbars and switchers. */
    c->ftl = wlr_foreign_toplevel_handle_v1_create(ftl_mgr);

    if (c->ftl) {
        LISTEN(&c->ftl->events.request_activate, &c->ftl_activate, ftl_activate_notify);
        LISTEN(&c->ftl->events.request_close, &c->ftl_close, ftl_close_notify);
        LISTEN(&c->ftl->events.request_fullscreen, &c->ftl_fullscreen, ftl_fullscreen_notify);
    }
    c->extftl = wlr_ext_foreign_toplevel_handle_v1_create(
        ext_ftl_list,
        &(struct wlr_ext_foreign_toplevel_handle_v1_state){
            .app_id = client_get_appid(c),
            .title  = client_get_title(c),
        }
    );
    if (c->extftl)
        c->extftl->data = c;
    ftl_sync(c);
    /* A managed window can receive keyboard focus only after it is visible. */
    if (c->ws && c->ws->mon)
        focus_client(c, 1);
    c->pending_map = 0;

    if (c->mon)
        arrange(c->mon);
    print_status();

    m = c->mon ? c->mon : point_to_monitor(c->geom.x, c->geom.y);
    wl_list_for_each(w, &clients, link) {
        if (!p && w != c && w->is_fullscreen && m == w->mon && w->ws == c->ws) {
            inherit_fullscreen = 1;
            set_fullscreen(w, 0);
        }
    }
    if (inherit_fullscreen)
        set_fullscreen(c, 1);
}

/* Acknowledge unsupported maximize requests from older applications. */
void maximize_notify(struct wl_listener *listener, void *data) {
    /* swm does not support maximization, but older applications may still ask
     * for it. Send an unchanged configuration to acknowledge the request. */
    client_t *c = wl_container_of(listener, c, maximize);

    if (c->surface.xdg->initialized && wl_resource_get_version(c->surface.xdg->toplevel->resource) <
                                           XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

/* Fill the usable display area with every tiled window in the workspace. */
void max_stack(monitor_t *m) {
    client_t *c;

    wl_list_for_each(c, &clients, link) {
        if (!VISIBLEON(c, m) || c->is_fullscreen)
            continue;

        if (c->is_floating && !c->is_max_stacked) {
            c->maxstack_prev  = c->geom;
            c->is_max_stacked = true;
        }
        resize(c, m->w, 0);
    }
    if ((c = focus_top(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

/* Convert absolute pointer input into movement across the display layout. */
void motion_absolute(struct wl_listener *listener, void *data) {
    /* Convert absolute pointer coordinates, such as those from a tablet or a
     * nested compositor, into display-layout coordinates. */
    struct wlr_pointer_motion_absolute_event *event = data;
    double                                    lx, ly, dx, dy;

    if (!event->time_msec) /* Virtual pointers use a zero timestamp. */
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->pointer->base, event->x, event->y, &lx, &ly
    );
    dx = lx - cursor->x;
    dy = ly - cursor->y;
    motion_notify(event->time_msec, &event->pointer->base, dx, dy, dx, dy, 1);
}

/* Move the pointer or grabbed window and update the surface under it. */
void motion_notify(
    uint32_t                 time,
    struct wlr_input_device *device,
    double                   dx,
    double                   dy,
    double                   dx_unaccel,
    double                   dy_unaccel,
    bool                     refocus
) {
    double                            sx = 0, sy = 0, sx_confined, sy_confined;
    client_t                         *c = nullptr, *w = nullptr;
    layer_surface_t                  *l          = nullptr;
    struct wlr_surface               *surface    = nullptr;
    struct wlr_pointer_constraint_v1 *constraint = active_constraint;

    /* A zero timestamp marks an internal pointer-focus update. */
    if (time) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            relative_pointer_mgr, seat, (uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel
        );

        if (constraint && constraint->surface != seat->pointer_state.focused_surface)
            constraint = nullptr;

        if (constraint && cursor_mode != CURSOR_RESIZE && cursor_mode != CURSOR_MOVE) {
            toplevel_from_wlr_surface(constraint->surface, &c, nullptr);

            if (c) {
                sx = cursor->x - c->geom.x - c->bw;
                sy = cursor->y - c->geom.y - c->bw;

                if (wlr_region_confine(
                        &constraint->region, sx, sy, sx + dx, sy + dy, &sx_confined, &sy_confined
                    )) {
                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }
                if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
                    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
                    return;
                }
            }
        }
        wlr_cursor_move(cursor, device, dx, dy);
        wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

        /* Update the selected display even while dragging a window. */
        if (sloppyfocus)
            selmon = point_to_monitor(cursor->x, cursor->y);
    }
    /* Hit-test at the updated cursor position. */
    point_to_node(cursor->x, cursor->y, &surface, &c, nullptr, &sx, &sy);

    if (cursor_mode == CURSOR_PRESSED && !seat->drag &&
        surface != seat->pointer_state.focused_surface &&
        toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
        c       = w;
        surface = seat->pointer_state.focused_surface;
        sx      = cursor->x - (l ? l->scene->node.x : w->geom.x);
        sy      = cursor->y - (l ? l->scene->node.y : w->geom.y);
    }
    /* Keep the drag icon under the pointer. */
    wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

    /* Move or resize the grabbed window instead of changing pointer focus. */
    if (cursor_mode == CURSOR_MOVE) {
        /* Move the grabbed client to the new position. */
        resize(
            grabc,
            (struct wlr_box){ .x      = (int)round(cursor->x) - grabcx,
                              .y      = (int)round(cursor->y) - grabcy,
                              .width  = grabc->geom.width,
                              .height = grabc->geom.height },
            1
        );
        return;
    } else if (cursor_mode == CURSOR_RESIZE) {
        int            movedx = (int)round(cursor->x) - grabx;
        int            movedy = (int)round(cursor->y) - graby;
        struct swm_box pure =
            swm_box_resize((const struct swm_box *)&grabgeom, movedx, movedy, grabedges, grabc->bw);
        struct wlr_box geo  = { pure.x, pure.y, pure.width, pure.height };
        grabc->resize_edges = grabedges;
        resize(grabc, geo, 1);
        return;
    }
    /* Show the default cursor over the background and window borders. */
    if (!surface && !seat->drag)
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

    pointer_focus(c, surface, sx, sy, time, refocus);
}

/* Apply pointer movement reported relative to the previous position. */
void motion_relative(struct wl_listener *listener, void *data) {
    /* A pointer reported movement relative to its previous position. */
    struct wlr_pointer_motion_event *event = data;

    /* Move the cursor by the reported distance. wlroots applies display bounds
     * and any settings specific to the input device. */
    motion_notify(
        event->time_msec,
        &event->pointer->base,
        event->delta_x,
        event->delta_y,
        event->unaccel_dx,
        event->unaccel_dy,
        1
    );
}

/* Forward touchpad swipe gestures to clients of the focused surface. */
void gesture_swipe_begin(struct wl_listener *listener, void *data) {
    struct wlr_pointer_swipe_begin_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_swipe_begin(
        pointer_gestures, seat, event->time_msec, event->fingers
    );
}

void gesture_swipe_update(struct wl_listener *listener, void *data) {
    struct wlr_pointer_swipe_update_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_swipe_update(
        pointer_gestures, seat, event->time_msec, event->dx, event->dy
    );
}

void gesture_swipe_end(struct wl_listener *listener, void *data) {
    struct wlr_pointer_swipe_end_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_swipe_end(
        pointer_gestures, seat, event->time_msec, event->cancelled
    );
}

/* Forward touchpad pinch gestures, including scale and rotation. */
void gesture_pinch_begin(struct wl_listener *listener, void *data) {
    struct wlr_pointer_pinch_begin_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_pinch_begin(
        pointer_gestures, seat, event->time_msec, event->fingers
    );
}

void gesture_pinch_update(struct wl_listener *listener, void *data) {
    struct wlr_pointer_pinch_update_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_pinch_update(
        pointer_gestures,
        seat,
        event->time_msec,
        event->dx,
        event->dy,
        event->scale,
        event->rotation
    );
}

void gesture_pinch_end(struct wl_listener *listener, void *data) {
    struct wlr_pointer_pinch_end_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_pinch_end(
        pointer_gestures, seat, event->time_msec, event->cancelled
    );
}

/* Forward touchpad hold gestures to clients of the focused surface. */
void gesture_hold_begin(struct wl_listener *listener, void *data) {
    struct wlr_pointer_hold_begin_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_hold_begin(
        pointer_gestures, seat, event->time_msec, event->fingers
    );
}

void gesture_hold_end(struct wl_listener *listener, void *data) {
    struct wlr_pointer_hold_end_event *event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_hold_end(
        pointer_gestures, seat, event->time_msec, event->cancelled
    );
}

/* Begin moving or resizing the window under the pointer. */
void move_resize(const arg_t *arg) {
    if (cursor_mode != CURSOR_NORMAL && cursor_mode != CURSOR_PRESSED)
        return;
    point_to_node(cursor->x, cursor->y, nullptr, &grabc, nullptr, nullptr, nullptr);

    if (!grabc || !client_allows_move_resize(grabc, arg->u))
        return;

    if (grabc->is_fullscreen)
        set_fullscreen(grabc, 0);

    /* Float the window and let pointer motion move or resize it. */
    grabc->persist_float = 1;
    set_floating(grabc, 1);

    switch (cursor_mode = arg->u) {
    case CURSOR_MOVE:
        grabcx = (int)round(cursor->x) - grabc->geom.x;
        grabcy = (int)round(cursor->y) - grabc->geom.y;
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
        break;
    case CURSOR_RESIZE:
        grabx     = (int)round(cursor->x);
        graby     = (int)round(cursor->y);
        grabgeom  = grabc->geom;
        grabedges = (grabx < grabgeom.x + grabgeom.width / 2 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) |
                    (graby < grabgeom.y + grabgeom.height / 2 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
        client_set_resizing(grabc, 1);
        wlr_cursor_set_xcursor(
            cursor,
            cursor_mgr,
            grabedges == (WLR_EDGE_TOP | WLR_EDGE_LEFT)      ? "nw-resize"
            : grabedges == (WLR_EDGE_TOP | WLR_EDGE_RIGHT)   ? "ne-resize"
            : grabedges == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT) ? "sw-resize"
                                                             : "se-resize"
        );
        break;
    }
}

/* Track a new request to capture window-manager shortcuts. */
void new_shortcuts_inhibitor(struct wl_listener *listener, void *data) {
    update_shortcuts_inhibitors(seat->keyboard_state.focused_surface);
}

/* Activate only the shortcut inhibitor belonging to the focused surface. */
void update_shortcuts_inhibitors(struct wlr_surface *surface) {
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

    wl_list_for_each(inhibitor, &kb_inhibit_mgr->inhibitors, link) {
        if (inhibitor->surface == surface) {
            if (!inhibitor->active)
                wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
        } else if (inhibitor->active) {
            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
        }
    }
}

/* Return whether the focused application currently captures shortcuts. */
bool shortcuts_inhibited(void) {
    struct wlr_surface                         *surface = seat->keyboard_state.focused_surface;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

    if (!surface)
        return false;
    wl_list_for_each(inhibitor, &kb_inhibit_mgr->inhibitors, link) {
        if (inhibitor->surface == surface && inhibitor->active)
            return true;
    }
    return false;
}

/* Apply a display configuration requested by an external client. */
void output_manager_apply(struct wl_listener *listener, void *data) {
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply_or_test(config, 0);
}

/* Validate a requested display configuration and optionally apply it. */
void output_manager_apply_or_test(struct wlr_output_configuration_v1 *config, bool test) {
    /* Test or apply a display change requested by a tool such as wlr-randr.
     * update_monitors() updates local state after the layout reports the change. */
    struct wlr_output_configuration_head_v1 *config_head;
    size_t                                   states_len = wl_list_length(&config->heads), i = 0;
    int                                      ok;

    if (!states_len || states_len > MAX_MONITORS) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }
    wl_list_for_each(config_head, &config->heads, link) {
        output_state_pool[i].output = config_head->state.output;
        wlr_output_state_init(&output_state_pool[i].base);
        wlr_output_head_v1_state_apply(&config_head->state, &output_state_pool[i].base);
        i++;
    }
    ok = wlr_backend_test(backend, output_state_pool, states_len);

    if (ok && !test)
        ok = wlr_backend_commit(backend, output_state_pool, states_len);

    for (i = 0; i < states_len; i++)
        wlr_output_state_finish(&output_state_pool[i].base);

    if (ok && !test) {
        wl_list_for_each(config_head, &config->heads, link) {
            struct wlr_output *wlr_output = config_head->state.output;
            monitor_t         *m          = wlr_output->data;

            /* An applied output-management configuration supersedes DPMS. */
            m->asleep = 0;
            /* Don't move disabled outputs or positions which did not change. */
            if (wlr_output->enabled &&
                (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
                wlr_output_layout_add(
                    output_layout, wlr_output, config_head->state.x, config_head->state.y
                );
        }
    }
    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);

    wlr_output_configuration_v1_destroy(config);

    if (!test && ok)
        update_monitors(nullptr, nullptr);
}

/* Validate a requested display configuration without applying it. */
void output_manager_test(struct wl_listener *listener, void *data) {
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply_or_test(config, 1);
}

/* Notify applications when the pointer enters, moves within, or leaves a surface. */
void pointer_focus(
    client_t *c, struct wlr_surface *surface, double sx, double sy, uint32_t time, bool refocus
) {
    struct wlr_pointer_constraint_v1 *constraint, *focused = nullptr;
    struct timespec                   now;

    if (surface != seat->pointer_state.focused_surface && sloppyfocus && refocus && c &&
        !client_is_unmanaged(c))
        focus_client(c, 0);

    /* Clear pointer focus when there is no surface. */
    if (!surface) {
        cursor_constrain(nullptr);
        wlr_seat_pointer_notify_clear_focus(seat);
        return;
    }
    if (!time) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }
    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots does nothing if the surface is already focused. */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    wl_list_for_each(constraint, &pointer_constraints->constraints, link) {
        if (constraint->surface == surface) {
            focused = constraint;
            break;
        }
    }
    cursor_constrain(focused);
}

/* Replace line-breaking characters before writing a status field. */
static void status_field(char *dst, size_t size, const char *src) {
    swm_sanitize_field(dst, size, src, nullptr);
}

/* Publish visible window rectangles in slurp's predefined-region format. */
void publish_windows(const char *runtime) {
    client_t *c;
    FILE     *file;
    char      path[MAX_STATE_PATH], tmppath[MAX_STATE_PATH], title[MAX_WINDOW_STATE_FIELD];
    int       ok = 1;

    if (!runtime || snprintf(path, sizeof(path), "%s/swm-windows", runtime) >= (int)sizeof(path) ||
        snprintf(tmppath, sizeof(tmppath), "%s.tmp.%ld", path, (long)getpid()) >=
            (int)sizeof(tmppath) ||
        !(file = fopen(tmppath, "w")))
        return;

    wl_list_for_each(c, &clients, link) {
        if (!c->mon || !client_surface(c)->mapped || !c->scene->node.enabled)
            continue;
        status_field(title, sizeof(title), client_get_title(c));
        if (fprintf(
                file, "%d,%d %dx%d %s\n", c->geom.x, c->geom.y, c->geom.width, c->geom.height, title
            ) < 0)
            ok = 0;
    }
    if (fflush(file) < 0 || fclose(file) < 0)
        ok = 0;
    if (!ok || rename(tmppath, path) < 0)
        unlink(tmppath);
}

/* Publish current display, workspace, and focus state on standard output. */
void print_status(void) {
    monitor_t  *m = nullptr;
    client_t   *c;
    FILE       *titlefile;
    char        titlepath[4096], tmppath[4096], title[1024], appid[1024];
    const char *runtime, *src;
    uint32_t    occ, urg;
    int         i;

    occ = urg = 0;
    wl_list_for_each(c, &clients, link) {
        ftl_sync(c);

        if (!c->ws)
            continue;
        occ |= 1u << c->ws->idx;

        if (c->is_urgent)
            urg |= 1u << c->ws->idx;
    }
    wl_list_for_each(m, &mons, link) {
        if ((c = focus_top(m))) {
            status_field(title, sizeof(title), client_get_title(c));
            status_field(appid, sizeof(appid), client_get_appid(c));
            printf("%s title %s\n", m->wlr_output->name, title);
            printf("%s appid %s\n", m->wlr_output->name, appid);
            printf("%s fullscreen %d\n", m->wlr_output->name, c->is_fullscreen);
            printf("%s floating %d\n", m->wlr_output->name, c->is_floating);
        } else {
            printf("%s title \n", m->wlr_output->name);
            printf("%s appid \n", m->wlr_output->name);
            printf("%s fullscreen \n", m->wlr_output->name);
            printf("%s floating \n", m->wlr_output->name);
        }
        printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
        printf("%s workspace %s\n", m->wlr_output->name, m->ws ? m->ws->name : "");
        printf(
            "%s tags %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
            m->wlr_output->name,
            occ,
            m->ws ? 1u << m->ws->idx : 0,
            m->ws ? 1u << m->ws->idx : 0,
            urg
        );
    }
    for (i = 0; i < WSCOUNT; i++) {
        status_field(title, sizeof(title), workspaces[i].title);
        printf("workspace %d title %s\n", i + 1, title);
        if (workspaces[i].color)
            printf("workspace %d color #%08" PRIx32 "\n", i + 1, workspaces[i].color);
        else
            printf("workspace %d color \n", i + 1);
    }
    runtime = getenv("XDG_RUNTIME_DIR");
    publish_windows(runtime);

    if (runtime && selmon) {
        src = (c = focus_top(selmon)) ? client_get_title(c) : "";
        status_field(title, sizeof(title), src);
        snprintf(titlepath, sizeof(titlepath), "%s/swm-title", runtime);

        if (snprintf(tmppath, sizeof(tmppath), "%s.tmp.%ld", titlepath, (long)getpid()) <
                (int)sizeof(tmppath) &&
            (titlefile = fopen(tmppath, "w"))) {
            int ok = fprintf(titlefile, "%s\n", title) >= 0 && fflush(titlefile) == 0;

            if (fclose(titlefile) < 0)
                ok = 0;

            if (!ok || rename(tmppath, titlepath) < 0)
                unlink(tmppath);
        }
    }
    fflush(stdout);
    workspace_broadcast();
}

/* Restore process state that child commands should not inherit from swm. */
void prepare_child(void) {
    if (sigprocmask(SIG_SETMASK, &original_signal_mask, nullptr) < 0)
        die("sigprocmask:");
    signal(SIGPIPE, SIG_DFL);
}

/* Turn a display on or off at an external client's request. */
void power_manager_set_mode(struct wl_listener *listener, void *data) {
    struct wlr_output_power_v1_set_mode_event *event = data;
    struct wlr_output_state                    state = {};
    monitor_t                                 *m     = event->output->data;

    if (!m)
        return;

    m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
    wlr_output_state_set_enabled(&state, event->mode);
    wlr_output_commit_state(m->wlr_output, &state);

    m->asleep = !event->mode;
    update_monitors(nullptr, nullptr);
}

/* Stop the Wayland event loop and begin shutdown. */
void quit(const arg_t *arg) {
    wl_display_terminate(dpy);
}

/* Draw a display when its next frame is due. */
void render_monitor(struct wl_listener *listener, void *data) {
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    monitor_t              *m = wl_container_of(listener, m, frame);
    client_t               *c;
    struct wlr_output_state pending = {};
    struct timespec         now;

    /* Render if no XDG clients have an outstanding resize and are visible on
     * this monitor. */
    wl_list_for_each(c, &clients, link) {
        if (c->resize && !c->is_floating && client_is_rendered_on_mon(c, m) &&
            !client_is_stopped(c))
            goto skip;
    }
    wlr_scene_output_commit(m->scene_output, nullptr);

skip:
    /* Tell applications that the frame has been drawn. */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(m->scene_output, &now);
    wlr_output_state_finish(&pending);
}

/* Choose and send the border-decoration mode for a Wayland window. */
void request_decoration_mode(struct wl_listener *listener, void *data) {
    client_t                                *c = wl_container_of(listener, c, set_decoration_mode);
    unsigned int                             oldbw = c->bw;
    enum wlr_xdg_toplevel_decoration_v1_mode mode =
        c->decoration->requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
            ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
            : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    c->bw = client_border_width(c);
    /* Keep the requested border mode until the surface is initialized and
     * wlroots can send it to the application. */
    if (c->surface.xdg->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration, mode);

        if (oldbw != c->bw && client_surface(c)->mapped && c->scene)
            resize(c, c->geom, 0);
    }
}

/* Accept a drag only when its button press has a valid serial number. */
void request_start_drag(struct wl_listener *listener, void *data) {
    struct wlr_seat_request_start_drag_event *event = data;

    if (wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

/* Commit a display-state change requested by the backend. */
void request_monitor_state(struct wl_listener *listener, void *data) {
    struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(event->output, event->state);
    update_monitors(nullptr, nullptr);
}

/* Request a new window geometry and apply it when the application is ready. */
void resize(client_t *c, struct wlr_box geo, bool interact) {
    struct wlr_box *bbox;

    if (!c->mon || !client_surface(c)->mapped)
        return;

    bbox = interact ? &sgeom : &c->mon->w;

    geo.width  = MAX(1 + 2 * (int)c->bw, geo.width);
    geo.height = MAX(1 + 2 * (int)c->bw, geo.height);
    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    apply_bounds(c, bbox);

    /* During an interactive Wayland resize, keep drawing the old size until
     * the application confirms the new one. XWayland resizes immediately. */
    c->resize = client_set_size(c, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);

    if (c->resize)
        c->pending_geom = c->geom;

    if (interact && c->resize) {
        return;
    }
    resize_apply(c);

    if (c == focus_top(c->mon))
        position_input_popups();
}

/* Commit a window's geometry to its surface, drawing nodes, and borders. */
void resize_apply(client_t *c) {
    struct wlr_box clip;

    /* Update the window's drawing nodes, including its borders. */
    wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
    wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

    client_get_clip(c, &clip);
    wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

/* Start the backend and child commands, then run the Wayland event loop. */
void run(char *startup_cmd) {
    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(dpy);

    if (!socket)
        die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);
    setenv("XDG_CURRENT_DESKTOP", "swm", 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, and control the display devices. */
    if (!wlr_backend_start(backend))
        die("startup: backend_start");

    /* Now that the socket exists and the backend is started, run the
     * autostart commands and the startup command. */
    if (!getenv("SWM_NO_AUTOSTART"))
        autostart_exec();

    if (startup_cmd) {
        int piperw[2];

        if (pipe(piperw) < 0)
            die("startup: pipe:");

        if ((child_pid = fork()) < 0)
            die("startup: fork:");

        if (child_pid == 0) {
            prepare_child();
            setsid();
            dup2(piperw[0], STDIN_FILENO);
            close(piperw[0]);
            close(piperw[1]);
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, nullptr);
            die("startup: execl:");
        }
        dup2(piperw[1], STDOUT_FILENO);
        close(piperw[1]);
        close(piperw[0]);
    }
    /* Mark stdout as non-blocking to avoid the startup script
     * causing swm to freeze when a user neither closes stdin
     * nor reads standard input in the startup script. */

    if (fd_set_nonblock(STDOUT_FILENO) < 0)
        close(STDOUT_FILENO);

    print_status();

    /* At this point the outputs are initialized, choose initial selmon based on
     * cursor position, and set the default cursor image. */
    selmon = point_to_monitor(cursor->x, cursor->y);

    /* TODO: Avoid the cursor briefly appearing at (0, 0) during startup. */
    wlr_cursor_warp_closest(cursor, nullptr, cursor->x, cursor->y);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wl_display_run(dpy);
}

/* Use the cursor image requested by the application under the pointer. */
void set_cursor(struct wl_listener *listener, void *data) {
    /* An application supplied a new cursor image. */
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    /* Keep the move or resize cursor until the pointer grab ends. The
     * application will request its cursor again when the pointer re-enters. */
    if (cursor_mode != CURSOR_NORMAL && cursor_mode != CURSOR_PRESSED)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == seat->pointer_state.focused_client)
        wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

/* Use the named cursor shape requested by the application under the pointer. */
void set_cursor_shape(struct wl_listener *listener, void *data) {
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

    if (cursor_mode != CURSOR_NORMAL && cursor_mode != CURSOR_PRESSED)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided cursor shape. */
    if (event->seat_client == seat->pointer_state.focused_client)
        wlr_cursor_set_xcursor(cursor, cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
}

/* Move a window between tiled and floating layout behavior. */
void set_floating(client_t *c, bool floating) {
    client_t *p    = client_get_parent(c);
    c->is_floating = floating;
    c->bw          = client_border_width(c);
    client_set_tiled(
        c, floating ? 0 : WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT
    );
    /* Preserve the window's layer when the whole layout is floating. */
    if (!c->mon || !client_surface(c)->mapped || !c->mon->ws || !c->mon->ws->lt->arrange)
        return;
    wlr_scene_node_reparent(
        &c->scene->node,
        layers
            [c->is_fullscreen || (p && p->is_fullscreen) ? LAYER_FULLSCREEN
             : c->is_floating                            ? LAYER_FLOAT
                                                         : LAYER_TILE]
    );
    resize(c, c->geom, 0);

    if (!c->pending_map) {
        arrange(c->mon);
        print_status();
    }
}

/* Enter or leave fullscreen while preserving the window's previous geometry. */
void set_fullscreen(client_t *c, bool fullscreen) {
    if (c->is_fullscreen == fullscreen) {
        if (c->mon && client_surface(c)->mapped)
            client_set_fullscreen(c, fullscreen);
        return;
    }
    c->is_fullscreen = fullscreen;

    if (!c->mon || !client_surface(c)->mapped)
        return;
    c->bw = client_border_width(c);
    client_set_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(
        &c->scene->node,
        layers
            [c->is_fullscreen ? LAYER_FULLSCREEN
             : c->is_floating ? LAYER_FLOAT
                              : LAYER_TILE]
    );

    if (fullscreen) {
        c->prev = c->geom;
        resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * their positions were chosen by the user. */
        resize(c, c->prev, 0);
    }
    if (!c->pending_map) {
        arrange(c->mon);
        print_status();
    }
}

/* Select the next or previous layout for the focused workspace. */
void cycle_layout(const arg_t *arg) {
    workspace_t *ws;
    bool         cycle[LENGTH(layouts)];
    int          current = -1, next;
    size_t       i;

    if (!selmon || !(ws = selmon->ws))
        return;

    for (i = 0; i < LENGTH(layouts); i++)

        if (ws->lt == &layouts[i]) {
            current = (int)i;
            break;
        }
    if (current < 0)
        return;

    for (i = 0; i < LENGTH(layouts); i++)
        cycle[i] = layouts[i].cycle;
    next = swm_next_layout(current, (int)LENGTH(layouts), cycle);

    if (next < 0)
        return;
    ws->prevlt = ws->lt;
    ws->lt     = &layouts[next];
    arrange(selmon);
    print_status();
}

/* Select a neighboring workspace, optionally moving the focused window with it. */
void cycle_workspace(const arg_t *arg) {
    /* Handle next/previous workspaces, optionally including empty ones or
     * taking the focused window along. Workspaces visible on other outputs
     * are skipped. */
    int dir =
        (arg->i == WORKSPACE_NEXT || arg->i == WORKSPACE_NEXT_ALL || arg->i == WORKSPACE_NEXT_MOVE)
            ? +1
            : -1;
    int          allowempty = (arg->i != WORKSPACE_NEXT && arg->i != WORKSPACE_PREVIOUS);
    int          mv         = (arg->i == WORKSPACE_NEXT_MOVE || arg->i == WORKSPACE_PREVIOUS_MOVE);
    workspace_t *ws, *cur;
    client_t    *c, *sel;
    bool         visible_elsewhere[WSCOUNT] = {};
    bool         occupied[WSCOUNT]          = {};
    int          i, idx;

    if (!selmon || !(cur = selmon->ws))
        return;
    sel = focus_top(selmon);

    for (i = 0; i < WSCOUNT; i++)
        visible_elsewhere[i] = workspaces[i].mon && workspaces[i].mon != selmon;
    wl_list_for_each(c, &clients, link) if (c->ws) occupied[c->ws->idx] = 1;
    idx = swm_workspace_next(cur->idx, WSCOUNT, dir, allowempty, visible_elsewhere, occupied);

    if (idx < 0)
        return;
    ws = &workspaces[idx];

    if (mv && sel)
        set_workspace(sel, ws);
    view_workspace(ws, selmon);

    if (mv && sel)
        focus_client(sel, 1);
}

/* Move a window to a display and keep its workspace and scale consistent. */
void set_monitor(client_t *c, monitor_t *m, workspace_t *ws) {
    monitor_t *oldmon = c->mon;

    if (!ws)
        ws = m ? m->ws : c->ws;
    c->ws = ws;
    /* If moving to a monitor and the target workspace is already visible,
     * use that monitor. A nullptr monitor explicitly detaches an unmapped client
     * and must not be redirected back onto the workspace's monitor. */
    if (m && ws && ws->mon)
        m = ws->mon;

    if (oldmon == m) {
        if (m)
            arrange(m);
        return;
    }
    c->mon = m;

    if (!c->is_fullscreen)
        c->prev = c->geom;

    /* Moving this node tells applications which displays they overlap. */
    if (oldmon)
        arrange(oldmon);
    /* An initial commit may assign a monitor before map_notify() creates the
     * client's scene tree. Defer all scene operations until the surface maps. */
    if (m && client_surface(c)->mapped) {
        /* Keep the window overlapping its display. */
        if (c->is_fullscreen) {
            client_set_fullscreen(c, 1);
            wlr_scene_node_reparent(&c->scene->node, layers[LAYER_FULLSCREEN]);
            resize(c, m->m, 0);
        } else {
            resize(c, c->geom, 0);
            set_floating(c, c->is_floating);
        }
    }
    focus_client(focus_top(selmon), 1);
}

/* Move a window and any related windows to another workspace. */
void set_workspace(client_t *c, workspace_t *ws) {
    /* Move a window to another workspace. */
    client_t  *w;
    monitor_t *oldmon;
    client_t  *main = client_main(c);

    if (!main || !ws || main->ws == ws)
        return;
    oldmon = main->mon;
    wl_list_for_each(w, &clients, link) {
        if (!clients_related(w, main))
            continue;
        w->ws = ws;

        if (ws->mon && ws->mon != w->mon) {
            w->mon = ws->mon;

            if (w->is_fullscreen) {
                client_set_fullscreen(w, 1);
                wlr_scene_node_reparent(&w->scene->node, layers[LAYER_FULLSCREEN]);
                resize(w, w->mon->m, 0);
            } else {
                w->prev = w->geom;
                resize(w, w->geom, 0);
                set_floating(w, w->is_floating);
            }
        }
    }
    if (oldmon)
        arrange(oldmon);

    if (ws->mon && ws->mon != oldmon)
        arrange(ws->mon);
    focus_client(focus_top(selmon), 1);
    print_status();
}

/* Show a workspace on a display and synchronize all of its windows. */
void assign_workspace(workspace_t *ws, monitor_t *m) {
    /* Attach a workspace to a display and update all of its windows. */
    client_t *c;

    ws->mon = m;

    if (!m)
        return;
    wl_list_for_each(c, &clients, link) {
        if (c->ws != ws)
            continue;

        if (c->mon != m) {
            c->mon = m;

            if (!c->is_fullscreen)
                c->prev = c->geom;
            resize(c, c->geom, 0);
        }
        if (c->is_fullscreen)
            resize(c, m->m, 0);
    }
}

/* Return the first workspace not shown on an output. */
workspace_t *free_workspace(void) {
    workspace_t *ws;

    for (ws = workspaces; ws < END(workspaces); ws++) {
        if (!ws->mon)
            return ws;
    }
    return nullptr;
}

/* Show a workspace, exchanging it with another display when already visible. */
void view_workspace(workspace_t *ws, monitor_t *m) {
    /* show workspace ws on output m; if ws is visible on another output,
     * the two displays exchange workspaces. */
    monitor_t   *other;
    workspace_t *old;

    if (!m || !ws || ws == m->ws)
        return;
    old = m->ws;

    if ((other = ws->mon)) {
        other->ws                 = old;
        other->previous_workspace = ws;

        if (old)
            assign_workspace(old, other);
    } else if (old) {
        assign_workspace(old, nullptr);
    }
    m->previous_workspace = old;
    m->ws                 = ws;
    assign_workspace(ws, m);
    arrange(m);

    if (other)
        arrange(other);
    /* Arrange first, then transfer keyboard focus to the workspace's most
     * recently focused client. */
    focus_client(focus_top(m), 1);
    print_status();
}

/* Send one workspace's ephemeral metadata to a control client. */
static void workspace_metadata_send(struct wl_resource *resource, workspace_t *ws) {
    uint32_t flags = 0;

    if (ws->title[0])
        flags |= SWM_WORKSPACE_MANAGER_V1_METADATA_TITLE;
    if (ws->color)
        flags |= SWM_WORKSPACE_MANAGER_V1_METADATA_COLOR;
    if (selmon && selmon->ws == ws)
        flags |= SWM_WORKSPACE_MANAGER_V1_METADATA_SELECTED;
    swm_workspace_manager_v1_send_metadata(
        resource, (uint32_t)ws->idx + 1, flags, ws->title, ws->color
    );
}

/* Send a complete workspace metadata snapshot to one control client. */
static void workspace_metadata_snapshot(struct wl_resource *resource) {
    int i;

    for (i = 0; i < WSCOUNT; i++)
        workspace_metadata_send(resource, &workspaces[i]);
    swm_workspace_manager_v1_send_done(resource);
}

/* Publish all workspace metadata to every connected control client. */
static void workspace_metadata_broadcast(void) {
    metadata_manager_t *mgr;

    wl_list_for_each(mgr, &metadata_managers, link) workspace_metadata_snapshot(mgr->resource);
}

/* Return a requested workspace or report a protocol error. */
static workspace_t *workspace_metadata_get(struct wl_resource *resource, uint32_t number) {
    if (!number || number > WSCOUNT) {
        wl_resource_post_error(
            resource,
            SWM_WORKSPACE_MANAGER_V1_ERROR_INVALID_WORKSPACE,
            "workspace %" PRIu32 " is outside 1..%d",
            number,
            WSCOUNT
        );
        return nullptr;
    }
    return &workspaces[number - 1];
}

/* Change an ephemeral workspace title. */
static void workspace_metadata_set_title(
    struct wl_client *client, struct wl_resource *resource, uint32_t number, const char *title
) {
    workspace_t *ws = workspace_metadata_get(resource, number);

    if (!ws)
        return;
    if (strlen(title) >= sizeof(ws->title)) {
        wl_resource_post_error(
            resource,
            SWM_WORKSPACE_MANAGER_V1_ERROR_TITLE_TOO_LONG,
            "workspace title exceeds %zu bytes",
            sizeof(ws->title) - 1
        );
        return;
    }
    if (!strcmp(ws->title, title))
        return;
    strcpy(ws->title, title);
    print_status();
}

/* Change an ephemeral workspace color. */
static void workspace_metadata_set_color(
    struct wl_client *client, struct wl_resource *resource, uint32_t number, uint32_t color
) {
    workspace_t *ws = workspace_metadata_get(resource, number);

    if (!ws)
        return;
    if (ws->color == color)
        return;
    ws->color = color;
    print_status();
}

/* Release an ephemeral workspace metadata manager. */
static void workspace_metadata_destroy_request(
    struct wl_client *client, struct wl_resource *resource
) {
    wl_resource_destroy(resource);
}

static const struct swm_workspace_manager_v1_interface workspace_metadata_impl = {
    .set_title = workspace_metadata_set_title,
    .set_color = workspace_metadata_set_color,
    .destroy   = workspace_metadata_destroy_request,
};

/* Stop publishing workspace metadata after a client disconnects. */
static void workspace_metadata_destroy(struct wl_resource *resource) {
    metadata_manager_t *mgr = wl_resource_get_user_data(resource);

    if (!mgr)
        return;
    wl_list_remove(&mgr->link);
    pool_release(&metadata_manager_pool, mgr);
}

/* Publish all ephemeral workspace metadata to a newly connected client. */
static void workspace_metadata_bind(
    struct wl_client *client, void *data, uint32_t version, uint32_t id
) {
    metadata_manager_t *mgr = pool_take(&metadata_manager_pool);

    if (!mgr) {
        wl_client_post_no_memory(client);
        return;
    }
    mgr->resource =
        wl_resource_create(client, &swm_workspace_manager_v1_interface, (int)version, id);

    if (!mgr->resource) {
        pool_release(&metadata_manager_pool, mgr);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        mgr->resource, &workspace_metadata_impl, mgr, workspace_metadata_destroy
    );
    wl_list_insert(&metadata_managers, &mgr->link);
    workspace_metadata_snapshot(mgr->resource);
}

/*
 * ext-workspace-v1 implementation. wlroots 0.19 ships no helper for this
 * protocol, so the resources are managed by hand. swm advertises a single
 * workspace group spanning all outputs, containing every workspace; the
 * only capability is activate, which behaves like a workspace switch.
 */
static void workspace_manager_commit(struct wl_client *client, struct wl_resource *resource) {
    workspace_manager_t *mgr = wl_resource_get_user_data(resource);
    workspace_t         *ws;

    if (!mgr || !(ws = mgr->pending))
        return;
    mgr->pending = nullptr;
    view_workspace(ws, selmon);
}

/* Stop publishing workspaces to a manager. */
static void workspace_manager_stop(struct wl_client *client, struct wl_resource *resource) {
    ext_workspace_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct ext_workspace_manager_v1_interface ws_manager_impl = {
    .commit = workspace_manager_commit,
    .stop   = workspace_manager_stop,
};

/* Stop publishing workspaces after a manager client disconnects. */
static void workspace_manager_destroy(struct wl_resource *resource) {
    workspace_manager_t *mgr = wl_resource_get_user_data(resource);
    workspace_handle_t  *h;
    int                  i;

    if (!mgr)
        return;
    /* Handles and the group may outlive the manager resource. */
    for (i = 0; i < WSCOUNT; i++) {
        wl_list_for_each(h, &workspaces[i].handles, link) {
            if (h->mgr == mgr)
                h->mgr = nullptr;
        }
    }
    if (mgr->group)
        wl_resource_set_user_data(mgr->group, nullptr);
    wl_list_remove(&mgr->link);
    pool_release(&workspace_manager_pool, mgr);
}

/* Reject a client-created workspace. */
static void workspace_group_create_workspace(
    struct wl_client *client, struct wl_resource *resource, const char *name
) {
    /* Ignore creation because swm has a fixed set of workspaces. */
}

/* Reject destruction of the global workspace group. */
static void workspace_group_destroy_request(
    struct wl_client *client, struct wl_resource *resource
) {
    wl_resource_destroy(resource);
}

static const struct ext_workspace_group_handle_v1_interface ws_group_impl = {
    .create_workspace = workspace_group_create_workspace,
    .destroy          = workspace_group_destroy_request,
};

/* Remove a disconnected client's global workspace group. */
static void workspace_group_destroy(struct wl_resource *resource) {
    workspace_manager_t *mgr = wl_resource_get_user_data(resource);

    if (mgr)
        mgr->group = nullptr;
}

/* Queue workspace activation. */
static void workspace_activate(struct wl_client *client, struct wl_resource *resource) {
    workspace_handle_t *h = wl_resource_get_user_data(resource);

    if (h && h->mgr)
        h->mgr->pending = h->ws;
}

/* Ignore workspace deactivation. */
static void workspace_deactivate(struct wl_client *client, struct wl_resource *resource) {
    /* Ignore deactivation because swm always has an active workspace. */
}

/* Ignore client workspace assignment. */
static void workspace_assign(
    struct wl_client *client, struct wl_resource *resource, struct wl_resource *group
) {
    /* Ignore assignment because swm has only one workspace group. */
}

/* Ignore client workspace removal. */
static void workspace_remove_request(struct wl_client *client, struct wl_resource *resource) {
    /* Ignore removal because swm has a fixed set of workspaces. */
}

/* Ignore client workspace destruction. */
static void workspace_destroy_request(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct ext_workspace_handle_v1_interface ws_handle_impl = {
    .destroy    = workspace_destroy_request,
    .activate   = workspace_activate,
    .deactivate = workspace_deactivate,
    .assign     = workspace_assign,
    .remove     = workspace_remove_request,
};

/* Remove one client's handle for a published workspace. */
static void workspace_handle_destroy(struct wl_resource *resource) {
    workspace_handle_t *h = wl_resource_get_user_data(resource);

    if (!h)
        return;
    wl_list_remove(&h->link);
    pool_release(&workspace_handle_pool, h);
}

/* Publish one workspace to a connected workspace manager. */
void workspace_handle_create(workspace_manager_t *mgr, workspace_t *ws) {
    workspace_handle_t *h = pool_take(&workspace_handle_pool);
    uint32_t            coord;
    struct wl_array     coords = { sizeof(coord), sizeof(coord), &coord };
    char                id[MAX_WS_ID];

    if (!h) {
        wl_client_post_no_memory(wl_resource_get_client(mgr->resource));
        return;
    }
    h->ws       = ws;
    h->mgr      = mgr;
    h->resource = wl_resource_create(
        wl_resource_get_client(mgr->resource), &ext_workspace_handle_v1_interface, 1, 0
    );

    if (!h->resource) {
        pool_release(&workspace_handle_pool, h);
        wl_client_post_no_memory(wl_resource_get_client(mgr->resource));
        return;
    }
    wl_resource_set_implementation(h->resource, &ws_handle_impl, h, workspace_handle_destroy);
    wl_list_insert(&ws->handles, &h->link);

    ext_workspace_manager_v1_send_workspace(mgr->resource, h->resource);
    snprintf(id, sizeof(id), "ws%d", ws->idx + 1);
    ext_workspace_handle_v1_send_id(h->resource, id);
    ext_workspace_handle_v1_send_name(h->resource, ws->name);
    coord = (uint32_t)ws->idx;
    ext_workspace_handle_v1_send_coordinates(h->resource, &coords);
    ext_workspace_handle_v1_send_capabilities(
        h->resource, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE
    );
    ext_workspace_handle_v1_send_state(h->resource, workspace_state(ws));

    if (mgr->group)
        ext_workspace_group_handle_v1_send_workspace_enter(mgr->group, h->resource);
}

/* Publish swm's workspace group to a newly connected client. */
void workspace_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    workspace_manager_t *mgr = pool_take(&workspace_manager_pool);
    monitor_t           *m;
    struct wl_resource  *out;
    int                  i;

    if (!mgr) {
        wl_client_post_no_memory(client);
        return;
    }
    mgr->resource =
        wl_resource_create(client, &ext_workspace_manager_v1_interface, (int)version, id);

    if (!mgr->resource) {
        pool_release(&workspace_manager_pool, mgr);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(mgr->resource, &ws_manager_impl, mgr, workspace_manager_destroy);
    wl_list_insert(&ws_managers, &mgr->link);

    mgr->group = wl_resource_create(client, &ext_workspace_group_handle_v1_interface, 1, 0);

    if (!mgr->group) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(mgr->group, &ws_group_impl, mgr, workspace_group_destroy);
    ext_workspace_manager_v1_send_workspace_group(mgr->resource, mgr->group);
    ext_workspace_group_handle_v1_send_capabilities(mgr->group, 0);
    wl_list_for_each(m, &mons, link) {
        wl_resource_for_each(out, &m->wlr_output->resources) {
            if (wl_resource_get_client(out) == client)
                ext_workspace_group_handle_v1_send_output_enter(mgr->group, out);
        }
    }
    for (i = 0; i < WSCOUNT; i++)
        workspace_handle_create(mgr, &workspaces[i]);
    ext_workspace_manager_v1_send_done(mgr->resource);
}

/* Add a newly bound display to every published workspace group. */
void workspace_output_bind(struct wl_listener *listener, void *data) {
    /* Tell workspace clients about the newly bound display. */
    struct wlr_output_event_bind *event  = data;
    struct wl_client             *client = wl_resource_get_client(event->resource);
    workspace_manager_t          *mgr;

    wl_list_for_each(mgr, &ws_managers, link) {
        if (mgr->group && wl_resource_get_client(mgr->resource) == client) {
            ext_workspace_group_handle_v1_send_output_enter(mgr->group, event->resource);
            ext_workspace_manager_v1_send_done(mgr->resource);
        }
    }
}

/* Compute and publish a workspace's active, hidden, urgent, and focus state. */
uint32_t workspace_state(workspace_t *ws) {
    _Static_assert(
        (unsigned int)SWM_WORKSPACE_ACTIVE == (unsigned int)EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE,
        "workspace active bit changed"
    );
    _Static_assert(
        (unsigned int)SWM_WORKSPACE_URGENT == (unsigned int)EXT_WORKSPACE_HANDLE_V1_STATE_URGENT,
        "workspace urgent bit changed"
    );
    _Static_assert(
        (unsigned int)SWM_WORKSPACE_HIDDEN == (unsigned int)EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN,
        "workspace hidden bit changed"
    );
    client_t *c;
    uint32_t  state;
    int       occupied = 0;
    int       urgent   = 0;

    wl_list_for_each(c, &clients, link) {
        if (c->ws != ws)
            continue;
        occupied  = 1;
        urgent   |= c->is_urgent;
    }
    /* ext-workspace-v1 has no occupied state. Mark empty, inactive
     * workspaces hidden so bars can omit them while retaining occupied and
     * currently selected workspaces. */
    state = swm_workspace_state(ws->mon != nullptr, occupied, urgent);
    return state;
}

/* Publish current workspace state to every connected workspace client. */
void workspace_broadcast(void) {
    workspace_manager_t *mgr;
    workspace_handle_t  *h;
    uint32_t             state;
    int                  i;

    if (!wl_list_empty(&ws_managers)) {
        for (i = 0; i < WSCOUNT; i++) {
            state = workspace_state(&workspaces[i]);
            wl_list_for_each(h, &workspaces[i].handles, link)
                ext_workspace_handle_v1_send_state(h->resource, state);
        }
        wl_list_for_each(mgr, &ws_managers, link) ext_workspace_manager_v1_send_done(mgr->resource);
    }
    workspace_metadata_broadcast();
}

/* Accept an application's request to replace the primary selection. */
void set_primary_selection(struct wl_listener *listener, void *data) {
    /* Honor an application's request to set the primary selection. */
    struct wlr_seat_request_set_primary_selection_event *event = data;

    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

/* Accept an application's request to replace the clipboard contents. */
void set_selection(struct wl_listener *listener, void *data) {
    /* Honor an application's request to set the clipboard selection. */
    struct wlr_seat_request_set_selection_event *event = data;

    wlr_seat_set_selection(seat, event->source, event->serial);
}

/* Create the Wayland server, protocols, input state, and rendering resources. */
void setup(void) {
    static const int signals[] = { SIGCHLD, SIGINT, SIGTERM };
    int              drm_fd, i;

    wlr_log_init(log_level, nullptr);

    /* Create the Wayland server and its event loop. */
    dpy        = wl_display_create();
    event_loop = wl_display_get_event_loop(dpy);

    if (sigprocmask(SIG_BLOCK, nullptr, &original_signal_mask) < 0)
        die("failed to read signal mask:");

    for (i = 0; i < (int)LENGTH(signals); i++) {
        signal_sources[i] =
            wl_event_loop_add_signal(event_loop, signals[i], handle_signal, nullptr);

        if (!signal_sources[i])
            die("failed to register signal handler");
    }
    signal(SIGPIPE, SIG_IGN);

    /* Choose a backend for the current environment and its input and display devices. */
    if (!(backend = wlr_backend_autocreate(event_loop, &session)))
        die("couldn't create backend");

    /* Create the tree that controls window position and drawing order. */
    scene = wlr_scene_create();

    if (!scene)
        die("failed to create scene");
    root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);

    if (!root_bg)
        die("failed to create root background");

    for (i = 0; i < NUM_LAYERS; i++) {
        layers[i] = wlr_scene_tree_create(&scene->tree);

        if (!layers[i])
            die("failed to create scene layer");
    }
    drag_icon = wlr_scene_tree_create(&scene->tree);

    if (!drag_icon)
        die("failed to create drag icon layer");
    wlr_scene_node_place_below(&drag_icon->node, &layers[LAYER_BLOCK]->node);

    /* Choose a renderer. WLR_RENDERER can override the automatic choice. */
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't create renderer");
    wl_signal_add(&drw->events.lost, &gpu_reset_listener);

    /* Advertise the buffer types supported by both the renderer and scene code. */
    wlr_renderer_init_wl_shm(drw, dpy);

    if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
        wlr_drm_create(dpy, drw);
        wlr_scene_set_linux_dmabuf_v1(scene, wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
    }
    if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline &&
        backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

    /* Create the buffers that connect the renderer to the display backend. */
    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't create allocator");

    /* Advertise the core protocols for windows, buffers, capture, and clipboard data. */
    compositor = wlr_compositor_create(dpy, 6, drw);
    wlr_subcompositor_create(dpy);
    wlr_data_device_manager_create(dpy);
    wlr_export_dmabuf_manager_v1_create(dpy);
    wlr_screencopy_manager_v1_create(dpy);
    wlr_data_control_manager_v1_create(dpy);
    wlr_ext_data_control_manager_v1_create(dpy, 1);
    wlr_primary_selection_v1_device_manager_create(dpy);
    wlr_viewporter_create(dpy);
    wlr_single_pixel_buffer_manager_v1_create(dpy);
    wlr_fractional_scale_manager_v1_create(dpy, 1);
    wlr_presentation_create(dpy, backend, 2);
    wlr_alpha_modifier_v1_create(dpy);

    /* Accept application requests for attention. */
    activation = wlr_xdg_activation_v1_create(dpy);
    wl_signal_add(&activation->events.request_activate, &request_activate);

    system_bell = wlr_xdg_system_bell_v1_create(dpy, 1);
    wl_signal_add(&system_bell->events.ring, &system_bell_ring);

    wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

    power_mgr = wlr_output_power_manager_v1_create(dpy);
    wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

    /* Track the position of each display in the combined desktop. */
    output_layout = wlr_output_layout_create(dpy);
    wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

    /* Workspaces are global and independent of outputs. */
    for (i = 0; i < WSCOUNT; i++) {
        workspaces[i].idx = i;
        snprintf(workspaces[i].name, sizeof(workspaces[i].name), "%d", i + 1);
        workspaces[i].lt     = &layouts[0];
        workspaces[i].prevlt = nullptr;
        workspaces[i].v = workspaces[i].h = (stack_state_t){ SLICE / 2, 1, 1 };
        wl_list_init(&workspaces[i].handles);
    }
    wl_list_init(&ws_managers);
    wl_list_init(&metadata_managers);
    wl_list_init(&pending_spawns);
    wl_list_init(&window_states);
    wl_list_init(&input_popups);
    load_window_states();
    wl_global_create(dpy, &ext_workspace_manager_v1_interface, 1, nullptr, workspace_manager_bind);
    wl_global_create(dpy, &swm_workspace_manager_v1_interface, 1, nullptr, workspace_metadata_bind);

    /* Publish window lists for taskbars and switchers. */
    ftl_mgr      = wlr_foreign_toplevel_manager_v1_create(dpy);
    ext_ftl_list = wlr_ext_foreign_toplevel_list_v1_create(dpy, 1);
    wlr_ext_image_copy_capture_manager_v1_create(dpy, 1);
    ext_ftl_capture_mgr = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(dpy, 1);
    wl_signal_add(&ext_ftl_capture_mgr->events.new_request, &ext_ftl_capture_request);

    /* Let virtual machines and remote desktops capture window-manager shortcuts. */
    kb_inhibit_mgr = wlr_keyboard_shortcuts_inhibit_v1_create(dpy);
    wl_signal_add(&kb_inhibit_mgr->events.new_inhibitor, &new_shortcuts_inhibitor_listener);

    /* Listen for newly connected displays. */
    wl_list_init(&mons);
    wl_signal_add(&backend->events.new_output, &new_output);

    /* Set up application windows, panels, backgrounds, and other desktop surfaces. */
    wl_list_init(&clients);
    wl_list_init(&fstack);

    xdg_shell = wlr_xdg_shell_create(dpy, 6);
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
    wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

    xdg_dialog_mgr = wlr_xdg_wm_dialog_v1_create(dpy, 1);
    wl_signal_add(&xdg_dialog_mgr->events.new_dialog, &new_xdg_dialog);

    layer_shell = wlr_layer_shell_v1_create(dpy, 3);
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

    idle_notifier = wlr_idle_notifier_v1_create(dpy);

    idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
    wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

    session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
    wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
    locked_bg = wlr_scene_rect_create(
        layers[LAYER_BLOCK], sgeom.width, sgeom.height, (float[4]){ 0.1f, 0.1f, 0.1f, 1.0f }
    );

    if (!locked_bg)
        die("failed to create lock background");
    wlr_scene_node_set_enabled(&locked_bg->node, 0);

    /* Prefer window borders drawn by the compositor. */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(dpy), WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );
    xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
    wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

    pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
    wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

    relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);
    pointer_gestures     = wlr_pointer_gestures_v1_create(dpy);

    /* Track the pointer across all displays. */
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);

    /* Load cursor images at the scales required by connected displays. */
    cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
    setenv("XCURSOR_SIZE", "24", 1);

    /* Combine pointer devices and handle their movement, buttons, scrolling, and frames. */
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame_listener);
    wl_signal_add(&cursor->events.swipe_begin, &cursor_swipe_begin);
    wl_signal_add(&cursor->events.swipe_update, &cursor_swipe_update);
    wl_signal_add(&cursor->events.swipe_end, &cursor_swipe_end);
    wl_signal_add(&cursor->events.pinch_begin, &cursor_pinch_begin);
    wl_signal_add(&cursor->events.pinch_update, &cursor_pinch_update);
    wl_signal_add(&cursor->events.pinch_end, &cursor_pinch_end);
    wl_signal_add(&cursor->events.hold_begin, &cursor_hold_begin);
    wl_signal_add(&cursor->events.hold_end, &cursor_hold_end);

    cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
    wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

    /* A seat groups the input devices controlled by one user. */
    wl_signal_add(&backend->events.new_input, &new_input_device);
    virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
    wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard, &new_virtual_keyboard);
    virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer, &new_virtual_pointer);

    seat = wlr_seat_create(dpy, "seat0");
    wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
    wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag_listener);
    wl_signal_add(&seat->events.start_drag, &start_drag_listener);

    /* Connect applications to an input method such as fcitx5. */
    im_mgr = wlr_input_method_manager_v2_create(dpy);
    wl_signal_add(&im_mgr->events.new_input_method, &new_input_method);
    ti_mgr = wlr_text_input_manager_v3_create(dpy);
    wl_signal_add(&ti_mgr->events.new_text_input, &new_text_input);

    kb_group = create_keyboard_group(false);

    if (!kb_group)
        die("failed to allocate primary keyboard group");
    wl_list_init(&kb_group->destroy.link);

    output_mgr = wlr_output_manager_v1_create(dpy);
    wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
    wl_signal_add(&output_mgr->events.test, &output_mgr_test);

    /* Keep XWayland applications on this compositor rather than a parent X server. */
    unsetenv("DISPLAY");
    /* Create the XWayland server; it starts when the first X11 application opens. */
    if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready_listener);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);
        xwayland_listeners_registered = true;

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
    }
}

/* Start a command and assign its first window to the current workspace. */
void spawn(const arg_t *arg) {
    pid_t            pid;
    pending_spawn_t *ps;

    if ((pid = fork()) == 0) {
        char *argv[MAX_COMMAND_ARGS], storage[MAX_COMMAND_SIZE];

        expand_argv(arg->v, argv, storage);

        prepare_child();
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        execvp(argv[0], argv);
        die("execvp %s failed:", argv[0]);
    }
    if (pid > 0 && selmon && selmon->ws) {
        ps = pool_take(&pending_spawn_pool);

        if (!ps)
            return;
        ps->pid = pid;
        ps->ws  = selmon->ws;
        wl_list_insert(&pending_spawns, &ps->link);
    }
}

/* Display and track an application's drag icon. */
void start_drag(struct wl_listener *listener, void *data) {
    struct wlr_drag       *drag = data;
    struct wlr_scene_tree *icon;

    if (!drag->icon)
        return;

    icon = wlr_scene_drag_icon_create(drag_icon, drag->icon);

    if (!icon)
        return;
    drag->icon->data = &icon->node;
    LISTEN_STATIC(&drag->icon->events.destroy, destroy_drag_icon);
}

/* Move the focused window to the requested workspace. */
void tag(const arg_t *arg) {
    client_t *sel = focus_top(selmon);

    if (!sel || arg->i < 0 || arg->i >= WSCOUNT)
        return;

    set_workspace(sel, &workspaces[arg->i]);
    arrange(selmon);
    print_status();
}

/* Move the focused window to the workspace on a neighboring display. */
void tag_monitor(const arg_t *arg) {
    client_t *sel = focus_top(selmon);

    if (sel)
        set_monitor(sel, direction_to_monitor(arg->i), nullptr);
}

/* Adjust the master size, master count, stack count, or layout direction. */
void stack_config(const arg_t *arg) {
    /* adjust the master/stack parameters of the focused workspace's
     * current layout. */
    workspace_t    *ws;
    stack_state_t  *st;
    const layout_t *next        = nullptr;
    void (*target)(monitor_t *) = nullptr;
    struct swm_stack_state state;
    int                    side, newside;
    size_t                 i;

    if (!arg || !selmon || !(ws = selmon->ws))
        return;

    if (ws->lt->arrange == master_left || ws->lt->arrange == master_right)
        st = &ws->v;
    else if (ws->lt->arrange == master_top || ws->lt->arrange == master_bottom)
        st = &ws->h;
    else
        return;

    if (ws->lt->arrange == master_left)
        side = SWM_MASTER_LEFT;
    else if (ws->lt->arrange == master_top)
        side = SWM_MASTER_TOP;
    else if (ws->lt->arrange == master_right)
        side = SWM_MASTER_RIGHT;
    else
        side = SWM_MASTER_BOTTOM;

    state = (struct swm_stack_state){ st->msize, st->mwin, st->stacks };

    if (!swm_stack_configure(&state, arg->i, side, &newside))
        return;
    *st = (stack_state_t){ state.msize, state.mwin, state.stacks };
    if (newside != side)
        target = newside == SWM_MASTER_LEFT    ? master_left
                 : newside == SWM_MASTER_TOP   ? master_top
                 : newside == SWM_MASTER_RIGHT ? master_right
                                               : master_bottom;

    if (target) {
        for (i = 0; i < LENGTH(layouts); i++)

            if (layouts[i].arrange == target) {
                next = &layouts[i];
                break;
            }
        if (next) {
            ws->prevlt = ws->lt;
            ws->lt     = next;
        }
    }
    arrange(selmon);
    print_status();
}

/* Tile windows into a master area followed by evenly divided stacks. */
void stack_master(monitor_t *m, int rot, int flip) {
    /* Master/stack tiler: a master area holding up to mwin
     * windows and a stacking area split into st->stacks columns. All
     * geometry is computed in "vertical" coordinates and transposed at
     * the end for the horizontal layout. */
    client_t              *c;
    stack_state_t         *st = rot ? &m->ws->h : &m->ws->v;
    struct swm_box         boxes[MAX_CLIENTS];
    struct swm_stack_state state = { st->msize, st->mwin, st->stacks };
    int                    n     = 0;
    size_t                 i     = 0;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->is_floating && !c->is_fullscreen)
        n++;
    swm_stack_layout((const struct swm_box *)&m->w, &state, rot, flip, (size_t)n, boxes);
    wl_list_for_each(c, &clients, link) {
        if (!VISIBLEON(c, m) || c->is_floating || c->is_fullscreen)
            continue;
        resize(c, (struct wlr_box){ boxes[i].x, boxes[i].y, boxes[i].width, boxes[i].height }, 0);
        i++;
    }
}

/* Tile the master area along the bottom edge. */
void master_bottom(monitor_t *m) {
    stack_master(m, 1, 1);
}

/* Tile the master area along the left edge. */
void master_left(monitor_t *m) {
    stack_master(m, 0, 0);
}

/* Tile the master area along the right edge. */
void master_right(monitor_t *m) {
    stack_master(m, 0, 1);
}

/* Tile the master area along the top edge. */
void master_top(monitor_t *m) {
    stack_master(m, 1, 0);
}

/* Toggle the focused window between tiled and floating. */
void toggle_floating(const arg_t *arg) {
    client_t *sel = focus_top(selmon);
    /* Fullscreen windows cannot become floating. */
    if (!sel || sel->is_fullscreen)
        return;

    if (sel->is_floating) {
        sel->persist_float = 0;
        set_floating(sel, 0);
        forget_client(sel);
    } else {
        sel->persist_float = 1;
        set_floating(sel, 1);
    }
}

/* Toggle fullscreen for the focused window. */
void toggle_fullscreen(const arg_t *arg) {
    client_t *sel = focus_top(selmon);

    if (sel)
        set_fullscreen(sel, !sel->is_fullscreen);
}

/* Toggle the workspace between its normal layout and overlapping maximized windows. */
void toggle_max_stack(const arg_t *arg) {
    workspace_t    *ws;
    const layout_t *max = nullptr;
    size_t          i;

    if (!selmon || !(ws = selmon->ws))
        return;

    for (i = 0; i < LENGTH(layouts); i++)

        if (layouts[i].arrange == max_stack) {
            max = &layouts[i];
            break;
        }
    if (!max)
        return;

    if (ws->lt == max)
        ws->lt = ws->prevlt && ws->prevlt != max ? ws->prevlt : &layouts[0];
    else {
        ws->prevlt = ws->lt;
        ws->lt     = max;
    }
    arrange(selmon);
    print_status();
}

/* Remove the active session lock after the locker authenticates the user. */
void unlock_session(struct wl_listener *listener, void *data) {
    session_lock_t *lock = wl_container_of(listener, lock, unlock);
    destroy_lock(lock, 1);
}

/* Hide an unmapped desktop-layer surface and reclaim its reserved space. */
void layer_surface_unmap_notify(struct wl_listener *listener, void *data) {
    layer_surface_t *l = wl_container_of(listener, l, unmap);

    l->mapped = 0;
    wlr_scene_node_set_enabled(&l->scene->node, 0);

    if (l->dim)
        wlr_scene_node_set_enabled(&l->dim->node, 0);

    if (l == exclusive_focus)
        exclusive_focus = nullptr;

    if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
        arrange_layers(l->mon);

    if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
        focus_client(focus_top(selmon), 1);
    motion_notify(0, nullptr, 0, 0, 0, 0, 1);
}

/* Hide an unmapped window, save its state, and choose new focus. */
void unmap_notify(struct wl_listener *listener, void *data) {
    /* Called when the surface is unmapped, and should no longer be shown. */
    client_t  *c    = wl_container_of(listener, c, unmap);
    client_t  *next = nullptr, *under = nullptr;
    monitor_t *oldmon     = c->mon;
    bool       hadfocus   = client_surface(c) == seat->keyboard_state.focused_surface;
    int        wasfocused = !client_is_unmanaged(c) && c == focus_top(c->mon);

    if (c == grabc) {
        if (cursor_mode == CURSOR_RESIZE)
            client_set_resizing(c, 0);
        cursor_mode = CURSOR_NORMAL;
        grabc       = nullptr;
    }
    if (!c->scene)
        return;

    if (client_is_unmanaged(c)) {
        if (c == exclusive_focus) {
            exclusive_focus = nullptr;
            focus_client(focus_top(selmon), 1);
        }
    } else {
        remember_client(c);

        if (wasfocused)
            next = focus_close(c);
        wl_list_remove(&c->link);
        wl_list_remove(&c->flink);
        c->mon = nullptr;

        if (hadfocus && !sloppyfocus)
            focus_client(next ? next : focus_top(selmon), 1);

        if (oldmon)
            arrange(oldmon);
    }
    if (c->ftl) {
        wl_list_remove(&c->ftl_activate.link);
        wl_list_remove(&c->ftl_close.link);
        wl_list_remove(&c->ftl_fullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(c->ftl);
        c->ftl         = nullptr;
        c->ftl_monitor = nullptr;
    }
    if (c->extftl) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(c->extftl);
        c->extftl = nullptr;
    }
    c->resize       = 0;
    c->resize_edges = WLR_EDGE_NONE;
    /* Hide the old scene before hit-testing, but keep its borders alive until
     * focus_client() has finished deactivating the old surface. */
    wlr_scene_node_set_enabled(&c->scene->node, 0);

    if (hadfocus && sloppyfocus) {
        /* Hit-test after hiding the old scene node so focus follows the
         * surface newly exposed beneath the stationary cursor. */
        point_to_node(cursor->x, cursor->y, nullptr, &under, nullptr, nullptr, nullptr);

        if (under && !client_is_unmanaged(under))
            focus_client(under, 0);
        else
            focus_client(next ? next : focus_top(selmon), 1);
    }
    wlr_scene_node_destroy(&c->scene->node);
    c->scene = client_surface(c)->data = nullptr;
    c->scene_surface                   = nullptr;
    memset(c->border, 0, sizeof(c->border));
    print_status();
    motion_notify(0, nullptr, 0, 0, 0, 0, 1);
}

/* Reconcile display layout changes with workspaces, windows, focus, and color state. */
void update_monitors(struct wl_listener *listener, void *data) {
    /*
     * Called whenever the output layout changes: adding or removing a
     * monitor, changing an output's mode or position, etc. This is where
     * the change officially happens and we update geometry, window
     * positions, focus, and the stored configuration in wlroots'
     * output-manager implementation.
     */
    struct wlr_output_configuration_v1      *config = wlr_output_configuration_v1_create();
    client_t                                *c;
    struct wlr_output_configuration_head_v1 *config_head;
    monitor_t                               *m;

    /* Remove disabled displays from the layout first. */
    wl_list_for_each(m, &mons, link) {
        if (m->wlr_output->enabled || m->asleep)
            continue;
        config_head                = wlr_output_configuration_head_v1_create(config, m->wlr_output);
        config_head->state.enabled = 0;
        /* Remove the display so the pointer cannot enter it. */
        wlr_output_layout_remove(output_layout, m->wlr_output);
        close_monitor(m);
        m->m = m->w = (struct wlr_box){};
    }
    /* Add newly enabled displays to the layout. */
    wl_list_for_each(m, &mons, link) {
        if (m->wlr_output->enabled && !wlr_output_layout_get(output_layout, m->wlr_output))
            wlr_output_layout_add_auto(output_layout, m->wlr_output);
    }
    /* Recalculate the bounds of the combined desktop. */
    wlr_output_layout_get_box(output_layout, nullptr, &sgeom);

    wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

    /* Cover all windows while the session is locked. */
    wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

    wl_list_for_each(m, &mons, link) {
        if (!m->wlr_output->enabled)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

        /* Read the display's position in the combined desktop. */
        wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
        m->w = m->m;
        wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

        wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
        wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

        if (m->lock_surface) {
            struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
            wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
        }
        /* Reserve panel space before arranging regular windows. */
        arrange_layers(m);
        /* Sync clients of this output's workspace (it may have been
         * hidden or shown elsewhere before this display appeared). */
        if (m->ws)
            assign_workspace(m->ws, m);
        /* Keep windows on their workspace when displays are connected. */
        arrange(m);
        /* Resize fullscreen windows to fill their display. */
        if ((c = focus_top(m)) && c->is_fullscreen)
            resize(c, m->m, 0);

        /* Reapply color correction in case this display was just re-enabled. */
        m->gamma_lut_changed = 1;

        config_head->state.x = m->m.x;
        config_head->state.y = m->m.y;

        if (!selmon) {
            selmon = m;
        }
    }
    if (selmon && selmon->wlr_output->enabled) {
        wl_list_for_each(c, &clients, link) {
            if (!c->mon && client_surface(c)->mapped)
                set_monitor(c, selmon, c->ws);
        }
        focus_client(focus_top(selmon), 1);

        if (selmon->lock_surface) {
            client_notify_enter(selmon->lock_surface->surface, wlr_seat_get_keyboard(seat));
            client_activate_surface(selmon->lock_surface->surface, 1);
        }
    }
    /* Refresh the cursor image after displays return without reporting false
     * pointer movement to applications. */
    wlr_cursor_move(cursor, nullptr, 0, 0);

    wlr_output_manager_v1_set_configuration(output_mgr, config);
}

/* Publish a window's changed application ID. */
void update_app_id(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, set_appid);

    ftl_sync(c);

    if (c == focus_top(c->mon))
        print_status();
}

/* Publish a window's changed title. */
void update_title(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, set_title);

    ftl_sync(c);

    if (c == focus_top(c->mon))
        print_status();
}

/* Mark an unfocused managed window as needing attention. */
void mark_urgent(client_t *c) {
    if (!c || c == focus_top(selmon))
        return;

    c->is_urgent = true;
    print_status();

    if (client_surface(c)->mapped)
        client_set_border_color(c, urgentcolor);
}

/* Handle an activation request as an urgency notification. */
void urgent(struct wl_listener *listener, void *data) {
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    client_t                                            *c     = nullptr;

    toplevel_from_wlr_surface(event->surface, &c, nullptr);
    mark_urgent(c);
}

/* Turn a system bell request associated with a surface into window urgency. */
void ring_system_bell(struct wl_listener *listener, void *data) {
    struct wlr_xdg_system_bell_v1_ring_event *event = data;
    client_t                                 *c     = nullptr;

    toplevel_from_wlr_surface(event->surface, &c, nullptr);
    mark_urgent(c);
}

/* Show a numbered workspace or return to the previously shown one. */
void view(const arg_t *arg) {
    /* A negative index selects the previously shown workspace. */
    if (!selmon)
        return;

    if (arg->i < 0)
        view_workspace(selmon->previous_workspace, selmon);
    else if (arg->i < WSCOUNT)
        view_workspace(&workspaces[arg->i], selmon);
}

/* Create an independent keyboard group for a virtual keyboard. */
void virtual_keyboard(struct wl_listener *listener, void *data) {
    struct wlr_virtual_keyboard_v1 *kb = data;

    /* Give each virtual keyboard its own group. */
    keyboard_group_t *group = create_keyboard_group(true);

    if (!group) {
        wl_resource_post_no_memory(kb->resource);
        return;
    }
    /* Use the virtual keyboard's key mapping. */
    wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
    LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroy_keyboard_group);

    /* Add the virtual keyboard to its group. */
    wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
    virtual_keyboards++;
    wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

/* Attach a virtual pointer to the shared cursor. */
void virtual_pointer(struct wl_listener *listener, void *data) {
    struct wlr_virtual_pointer_v1_new_pointer_event *event  = data;
    struct wlr_input_device                         *device = &event->new_pointer->pointer.base;

    wlr_cursor_attach_input_device(cursor, device);

    if (event->suggested_output)
        wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

/* Return the display containing a point, or the nearest display to it. */
monitor_t *point_to_monitor(double x, double y) {
    struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);

    return o ? o->data : nullptr;
}

/* Find the surface, window, and local coordinates at a layout position. */
void point_to_node(
    double               x,
    double               y,
    struct wlr_surface **psurface,
    client_t           **pc,
    layer_surface_t    **pl,
    double              *nx,
    double              *ny
) {
    struct wlr_scene_node    *node, *pnode;
    struct wlr_scene_surface *scene_surface;
    struct wlr_surface       *surface = nullptr;
    client_t                 *c       = nullptr;
    layer_surface_t          *l       = nullptr;
    int                       layer;

    for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
        if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
            continue;

        if (node->type == WLR_SCENE_NODE_BUFFER &&
            (scene_surface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node))))
            surface = scene_surface->surface;
        /* Walk upward until a node identifies its window. */
        for (pnode = node; pnode && !c;) {
            c = pnode->data;

            if (!c)
                pnode = pnode->parent ? &pnode->parent->node : nullptr;
        }
        if (c && c->type == LAYER_SHELL) {
            c = nullptr;
            l = pnode->data;
        }
        if (c && client_is_blocked(c)) {
            c       = nullptr;
            surface = nullptr;
        }
    }
    if (psurface)
        *psurface = surface;
    if (pc)
        *pc = c;
    if (pl)
        *pl = l;
}

/* Swap the focused tiled window with its neighbor. */
void swap_client(const arg_t *arg) {
    /* swap the focused window with the next/previous window in the
     * stacking order. */
    client_t       *c = nullptr, *sel = focus_top(selmon);
    struct wl_list *node;

    if (!sel || !selmon || !selmon->ws || !selmon->ws->lt->arrange || sel->is_floating ||
        sel->is_fullscreen)
        return;

    for (node = arg->i > 0 ? sel->link.next : sel->link.prev; node != &sel->link;
         node = arg->i > 0 ? node->next : node->prev) {
        if (node == &clients)
            continue;
        c = wl_container_of(node, c, link);

        if (VISIBLEON(c, selmon) && !c->is_floating && !c->is_fullscreen)
            break;
    }
    if (node == &sel->link || !c)
        return;

    /* Move the selected window next to its swap target. */
    wl_list_remove(&sel->link);

    if (arg->i > 0)
        wl_list_insert(&c->link, &sel->link);
    else
        wl_list_insert(c->link.prev, &sel->link);

    arrange(selmon);
    print_status();
}

/* Raise the focused floating window above other windows. */
void raise_client(const arg_t *arg) {
    /* Raise the focused window above the others. */
    client_t *sel = focus_top(selmon);

    if (sel)
        wlr_scene_node_raise_to_top(&sel->scene->node);
}

/* Swap the focused tiled window with the master window. */
void zoom(const arg_t *arg) {
    client_t *c, *sel = focus_top(selmon);

    if (!sel || !selmon || !selmon->ws || !selmon->ws->lt->arrange || sel->is_floating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * nullptr if we pass it along the way. */
    wl_list_for_each(c, &clients, link) {
        if (VISIBLEON(c, selmon) && !c->is_floating) {
            if (c != sel)
                break;
            sel = nullptr;
        }
    }
    /* Stop if there is no other tiled window. */
    if (&c->link == &clients)
        return;

    /* If we passed sel, move c to the front; otherwise, move sel to the
     * front. */
    if (!sel)
        sel = c;
    wl_list_remove(&sel->link);
    wl_list_insert(&clients, &sel->link);

    focus_client(sel, 1);
    arrange(selmon);
}

/* Honor an activation request for a managed X11 window. */
void activate_x11(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, activate);

    /* Only managed X11 windows can be activated. */
    if (!client_is_unmanaged(c))
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

/* Attach a newly ready X11 surface to its window state. */
void associate_x11(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, associate);

    LISTEN(&client_surface(c)->events.map, &c->map, map_notify);
    LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmap_notify);
}

/* Apply an unmanaged X11 window's requested position and size. */
void configure_x11(struct wl_listener *listener, void *data) {
    client_t                                    *c     = wl_container_of(listener, c, configure);
    struct wlr_xwayland_surface_configure_event *event = data;

    if (!client_surface(c) || !client_surface(c)->mapped) {
        wlr_xwayland_surface_configure(
            c->surface.xwayland, event->x, event->y, event->width, event->height
        );
        return;
    }
    if (client_is_unmanaged(c)) {
        wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
        wlr_xwayland_surface_configure(
            c->surface.xwayland, event->x, event->y, event->width, event->height
        );
        return;
    }
    if ((c->is_floating && c != grabc) || !c->mon || !c->mon->ws || !c->mon->ws->lt->arrange) {
        resize(
            c,
            (struct wlr_box){ .x      = event->x - c->bw,
                              .y      = event->y - c->bw,
                              .width  = event->width + c->bw * 2,
                              .height = event->height + c->bw * 2 },
            0
        );
    } else {
        arrange(c->mon);
    }
}

/* Allocate state and listeners for a new X11 window. */
void create_notify_x11(struct wl_listener *listener, void *data) {
    struct wlr_xwayland_surface *xsurface = data;
    client_t                    *c;

    /* Allocate window state for this X11 surface. */
    if (!(c = pool_take(&client_pool))) {
        wlr_xwayland_surface_close(xsurface);
        return;
    }
    xsurface->data      = c;
    c->surface.xwayland = xsurface;
    c->type             = X11;
    c->bw               = client_is_unmanaged(c) ? 0 : borderwidth;

    /* Listen for changes to the X11 window. */
    LISTEN(&xsurface->events.associate, &c->associate, associate_x11);
    LISTEN(&xsurface->events.destroy, &c->destroy, destroy_notify);
    LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociate_x11);
    LISTEN(&xsurface->events.request_activate, &c->activate, activate_x11);
    LISTEN(&xsurface->events.request_configure, &c->configure, configure_x11);
    LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreen_notify);
    LISTEN(&xsurface->events.set_hints, &c->set_hints, set_hints);
    LISTEN(&xsurface->events.set_title, &c->set_title, update_title);
    LISTEN(&xsurface->events.set_class, &c->set_appid, update_app_id);
}

/* Detach an X11 surface while retaining its window state for reassociation. */
void dissociate_x11(struct wl_listener *listener, void *data) {
    client_t *c = wl_container_of(listener, c, dissociate);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
}

/* Update urgency and input behavior from an X11 window's hints. */
void set_hints(struct wl_listener *listener, void *data) {
    client_t           *c       = wl_container_of(listener, c, set_hints);
    struct wlr_surface *surface = client_surface(c);

    if (c == focus_top(selmon) || !c->surface.xwayland->hints)
        return;

    c->is_urgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
    print_status();

    if (c->is_urgent && surface && surface->mapped)
        client_set_border_color(c, urgentcolor);
}

/* Connect XWayland to swm's seat and set its default cursor. */
void xwayland_ready(struct wl_listener *listener, void *data) {
    struct wlr_xcursor *xcursor;

    /* Assign swm's single seat to XWayland. */
    wlr_xwayland_set_seat(xwayland, seat);

    /* Set the default XWayland cursor to match the rest of swm. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
        wlr_xwayland_set_cursor(
            xwayland,
            wlr_xcursor_image_get_buffer(xcursor->images[0]),
            xcursor->images[0]->hotspot_x,
            xcursor->images[0]->hotspot_y
        );
}

/* Parse command-line options, validate the environment, and run swm. */
int main(int argc, char *argv[]) {
    char *startup_cmd = nullptr;
    int   c;

    while ((c = getopt(argc, argv, "s:hdv")) != -1) {
        if (c == 's')
            startup_cmd = optarg;
        else if (c == 'd')
            log_level = WLR_DEBUG;
        else if (c == 'v') {
            printf("swm %s\n", VERSION);
            return EXIT_SUCCESS;
        } else
            goto usage;
    }
    if (optind < argc)
        goto usage;

    /* Wayland needs XDG_RUNTIME_DIR to create its communication socket. */
    if (!getenv("XDG_RUNTIME_DIR"))
        die("XDG_RUNTIME_DIR must be set");
    setup();
    run(startup_cmd);
    cleanup();
    return EXIT_SUCCESS;

usage:
    die("usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
