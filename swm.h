#ifndef SWM_H
#define SWM_H

#include <stdbool.h>
#include <stddef.h>

#define SWM_SLICE 32

/* Integer rectangle in compositor layout coordinates. */
struct swm_box {
    int x, y, width, height;
};

/* Parameters controlling a master-stack layout. */
struct swm_stack_state {
    int msize;
    int mwin;
    int stacks;
};

/* Workspace state flags exported through ext-workspace. */
enum swm_workspace_state {
    SWM_WORKSPACE_ACTIVE = 1u << 0,
    SWM_WORKSPACE_URGENT = 1u << 1,
    SWM_WORKSPACE_HIDDEN = 1u << 2,
};

/* Commands which update master-stack parameters. */
enum swm_stack_command {
    SWM_MASTER_SHRINK,
    SWM_MASTER_GROW,
    SWM_MASTER_ADD,
    SWM_MASTER_DEL,
    SWM_STACK_INC,
    SWM_STACK_DEC,
    SWM_STACK_RESET,
    SWM_FLIP_LAYOUT,
};

/* Side of the output occupied by the master area. */
enum swm_layout_side {
    SWM_MASTER_LEFT,
    SWM_MASTER_TOP,
    SWM_MASTER_RIGHT,
    SWM_MASTER_BOTTOM,
};

/* Clamp a box to the minimum client size and output bounds. */
void swm_box_apply_bounds(struct swm_box *box, const struct swm_box *bounds, unsigned int border);
/* Resize a box from the selected edges. */
struct swm_box swm_box_resize(
    const struct swm_box *box, int dx, int dy, unsigned int edges, unsigned int border
);
/* Arrange clients into master and stack areas. */
size_t swm_stack_layout(
    const struct swm_box         *area,
    const struct swm_stack_state *state,
    int                           rotate,
    int                           flip,
    size_t                        count,
    struct swm_box               *boxes
);
/* Reconcile pending geometry with a committed client size. */
void swm_box_reconcile_commit(
    struct swm_box *box, int width, int height, unsigned int border, unsigned int edges
);
/* Position an input popup inside an output. */
struct swm_box swm_popup_position(
    const struct swm_box *client,
    unsigned int          border,
    const struct swm_box *cursor_rect,
    int                   width,
    int                   height,
    const struct swm_box *output
);
/* Compute the exported state flags for a workspace. */
unsigned int swm_workspace_state(bool active, bool occupied, bool urgent);
/* Find the next eligible workspace in the requested direction. */
int swm_workspace_next(
    int         current,
    int         count,
    int         direction,
    int         allow_empty,
    const bool *visible_elsewhere,
    const bool *occupied
);
/* Apply a command to master-stack parameters. */
bool swm_stack_configure(struct swm_stack_state *state, int command, int side, int *new_side);
/* Return whether a rule matches the client identity. */
bool swm_rule_matches(
    const char *rule_id, const char *rule_title, const char *appid, const char *title
);
/* Copy a field while replacing state-file delimiters. */
void swm_sanitize_field(char *dst, size_t size, const char *src, const char *fallback);
/* Parse one persisted window-state record. */
bool swm_parse_window_state(
    const char     *line,
    char           *appid,
    size_t          appid_size,
    char           *title,
    size_t          title_size,
    struct swm_box *geometry
);
/* Compute the effective border width for a client. */
unsigned int swm_border_width(
    unsigned int configured,
    bool         fullscreen,
    bool         borderless,
    bool         x11,
    bool         unmanaged,
    bool         client_side
);
/* Find the next enabled layout in the cycle. */
int swm_next_layout(int current, int count, const bool *cycle);

#endif
