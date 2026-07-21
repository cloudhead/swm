/*
 * swmctl.c
 * Control and inspect swm's state.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "swm-workspace-v1-client-protocol.h"

#define MAX_WORKSPACES      32
#define MAX_WORKSPACE_TITLE 256

typedef struct {
    bool     seen;
    uint32_t flags;
    char     title[MAX_WORKSPACE_TITLE];
    uint32_t color;
} workspace_t;

static struct wl_display               *display;
static struct swm_workspace_manager_v1 *manager;
static workspace_t                      workspaces[MAX_WORKSPACES];
static bool                             subscribing;
static bool                             waybar;

[[noreturn]] static void usage(void) {
    fprintf(
        stderr,
        "usage:\n"
        "  swmctl workspace list\n"
        "  swmctl workspace get N (title | color | selected)\n"
        "  swmctl workspace set N (title TITLE | color COLOR)\n"
        "  swmctl workspace clear N (title | color)\n"
        "  swmctl workspace subscribe [--format=waybar]\n"
    );
    exit(EXIT_FAILURE);
}

/* Print a color in its shortest lossless notation. */
static void print_color(uint32_t color) {
    if ((color & 0xff) == 0xff)
        printf("#%06" PRIx32, color >> 8);
    else
        printf("#%08" PRIx32, color);
}

/* Write a string with JSON quoting. */
static void json_string(const char *value) {
    const unsigned char *p;

    putchar('"');
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\')
            printf("\\%c", *p);
        else if (*p == '\n')
            fputs("\\n", stdout);
        else if (*p == '\r')
            fputs("\\r", stdout);
        else if (*p == '\t')
            fputs("\\t", stdout);
        else if (*p < 0x20)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    putchar('"');
}

/* Print one workspace as a JSON object. */
static void print_json_workspace(uint32_t number, workspace_t *ws) {
    printf("{\"workspace\":%" PRIu32 ",\"title\":", number);
    if (ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_TITLE)
        json_string(ws->title);
    else
        fputs("null", stdout);
    fputs(",\"color\":", stdout);
    if (ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_COLOR) {
        putchar('"');
        print_color(ws->color);
        putchar('"');
    } else {
        fputs("null", stdout);
    }
    printf(
        ",\"selected\":%s}",
        ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_SELECTED ? "true" : "false"
    );
}

/* Write a JSON string safe for use as Pango markup in Waybar. */
static void json_markup_string(const char *value, bool has_color, uint32_t color) {
    const unsigned char *p;

    putchar('"');
    if (has_color) {
        printf("<span foreground=\\\"#%06" PRIx32 "\\\"", color >> 8);
        if ((color & 0xff) != 0xff)
            printf(" foreground_alpha=\\\"%" PRIu32 "\\\"", (color & 0xff) * 257);
        putchar('>');
    }
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '&')
            fputs("&amp;", stdout);
        else if (*p == '<')
            fputs("&lt;", stdout);
        else if (*p == '>')
            fputs("&gt;", stdout);
        else if (*p == '"')
            fputs("&quot;", stdout);
        else if (*p == '\'')
            fputs("&apos;", stdout);
        else if (*p == '\\')
            fputs("\\\\", stdout);
        else if (*p == '\n')
            fputs("\\n", stdout);
        else if (*p == '\r')
            fputs("\\r", stdout);
        else if (*p == '\t')
            fputs("\\t", stdout);
        else if (*p < 0x20)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    if (has_color)
        fputs("</span>", stdout);

    putchar('"');
}

/* Print the selected workspace label as a Waybar custom-module update. */
static void print_waybar(void) {
    workspace_t *selected = nullptr;
    uint32_t     number   = 0;
    bool         has_title;

    for (uint32_t i = 0; i < MAX_WORKSPACES; i++) {
        if (workspaces[i].seen &&
            workspaces[i].flags & SWM_WORKSPACE_MANAGER_V1_METADATA_SELECTED) {
            selected = &workspaces[i];
            number   = i + 1;
            break;
        }
    }
    has_title = selected && selected->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_TITLE;
    fputs("{\"text\":", stdout);
    json_markup_string(
        has_title ? selected->title : "",
        has_title && selected->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_COLOR,
        selected ? selected->color : 0
    );
    printf(",\"tooltip\":\"Workspace %" PRIu32 "\"}\n", number);
    fflush(stdout);
}

/* Print the complete cached workspace metadata as one JSON value. */
static void print_json_workspaces(void) {
    bool first = true;

    putchar('[');
    for (uint32_t i = 0; i < MAX_WORKSPACES; i++) {
        if (!workspaces[i].seen)
            continue;
        if (!first)
            putchar(',');
        print_json_workspace(i + 1, &workspaces[i]);
        first = false;
    }
    puts("]");
    fflush(stdout);
}

/* Cache one workspace metadata event. */
static void metadata(
    void                            *data,
    struct swm_workspace_manager_v1 *object,
    uint32_t                         number,
    uint32_t                         flags,
    const char                      *title,
    uint32_t                         color
) {
    workspace_t *ws;

    if (!number || number > MAX_WORKSPACES)
        return;
    ws        = &workspaces[number - 1];
    ws->seen  = true;
    ws->flags = flags;
    ws->color = color;

    snprintf(ws->title, sizeof(ws->title), "%s", title);
}

/* Finish a snapshot, printing it when following changes. */
static void done(void *data, struct swm_workspace_manager_v1 *object) {
    if (!subscribing)
        return;
    if (waybar)
        print_waybar();
    else
        print_json_workspaces();
}

static const struct swm_workspace_manager_v1_listener manager_listener = {
    .metadata = metadata,
    .done     = done,
};

/* Bind swm's workspace metadata global. */
static void global(
    void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version
) {
    if (strcmp(interface, swm_workspace_manager_v1_interface.name))
        return;
    manager = wl_registry_bind(registry, name, &swm_workspace_manager_v1_interface, 1);
    swm_workspace_manager_v1_add_listener(manager, &manager_listener, nullptr);
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global        = global,
    .global_remove = global_remove,
};

/* Parse a one-based workspace number. */
static uint32_t parse_workspace(const char *value) {
    char         *end;
    unsigned long number;

    errno  = 0;
    number = strtoul(value, &end, 10);

    if (errno || *end || !number || number > MAX_WORKSPACES)
        usage();

    return (uint32_t)number;
}

/* Parse a color in #RRGGBB or #RRGGBBAA notation. */
static uint32_t parse_color(const char *value) {
    char         *end;
    unsigned long color;
    size_t        length = strlen(value);

    if ((length != 7 && length != 9) || value[0] != '#')
        usage();
    errno = 0;
    color = strtoul(value + 1, &end, 16);
    if (errno || *end || color > 0xffffffff)
        usage();

    return length == 7 ? (uint32_t)(color << 8) | 0xff : (uint32_t)color;
}

/* Print one field from a cached workspace. */
static void print_workspace_field(uint32_t number, const char *field) {
    workspace_t *ws = &workspaces[number - 1];

    if (!strcmp(field, "title")) {
        puts(ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_TITLE ? ws->title : "");
    } else if (!strcmp(field, "color")) {
        if (ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_COLOR)
            print_color(ws->color);
        putchar('\n');
    } else {
        puts(ws->flags & SWM_WORKSPACE_MANAGER_V1_METADATA_SELECTED ? "true" : "false");
    }
}

/* Print every cached workspace number. */
static void print_workspaces(void) {
    for (uint32_t i = 0; i < MAX_WORKSPACES; i++) {
        if (workspaces[i].seen)
            printf("%" PRIu32 "\n", i + 1);
    }
}

/* Connect to swm and collect its initial metadata snapshot. */
static void connect_manager(void) {
    struct wl_registry *registry;

    if (!(display = wl_display_connect(nullptr))) {
        fprintf(stderr, "swmctl: cannot connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, nullptr);

    if (wl_display_roundtrip(display) < 0 || !manager) {
        fprintf(stderr, "swmctl: compositor does not support swm-workspace-v1\n");
        exit(EXIT_FAILURE);
    }
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "swmctl: failed to read workspace metadata\n");
        exit(EXIT_FAILURE);
    }
    wl_registry_destroy(registry);
}

int main(int argc, char **argv) {
    const char *command;
    const char *field     = nullptr;
    const char *value     = nullptr;
    uint32_t    workspace = 0;

    if (argc < 3 || strcmp(argv[1], "workspace"))
        usage();
    command     = argv[2];
    subscribing = !strcmp(command, "subscribe");

    if (!strcmp(command, "list")) {
        if (argc != 3)
            usage();
    } else if (!strcmp(command, "get")) {
        if (argc != 5)
            usage();
        workspace = parse_workspace(argv[3]);
        field     = argv[4];
        if (strcmp(field, "title") && strcmp(field, "color") && strcmp(field, "selected"))
            usage();
    } else if (!strcmp(command, "set")) {
        if (argc != 6)
            usage();
        workspace = parse_workspace(argv[3]);
        field     = argv[4];
        value     = argv[5];
        if (!strcmp(field, "title")) {
            if (strlen(value) >= MAX_WORKSPACE_TITLE)
                usage();
        } else if (!strcmp(field, "color")) {
            parse_color(value);
        } else {
            usage();
        }
    } else if (!strcmp(command, "clear")) {
        if (argc != 5)
            usage();
        workspace = parse_workspace(argv[3]);
        field     = argv[4];
        if (strcmp(field, "title") && strcmp(field, "color"))
            usage();
    } else if (subscribing) {
        if (argc == 4 && !strcmp(argv[3], "--format=waybar"))
            waybar = true;
        else if (argc != 3)
            usage();
    } else {
        usage();
    }
    connect_manager();

    if (subscribing) {
        while (wl_display_dispatch(display) >= 0)
            ;
        return EXIT_FAILURE;
    }
    if (!strcmp(command, "list")) {
        print_workspaces();
    } else {
        if (!workspaces[workspace - 1].seen) {
            fprintf(stderr, "swmctl: workspace %" PRIu32 " does not exist\n", workspace);
            return EXIT_FAILURE;
        }
        if (!strcmp(command, "get")) {
            print_workspace_field(workspace, field);
        } else if (!strcmp(command, "set")) {
            if (!strcmp(field, "title"))
                swm_workspace_manager_v1_set_title(manager, workspace, value);
            else
                swm_workspace_manager_v1_set_color(manager, workspace, parse_color(value));
        } else {
            if (!strcmp(field, "title"))
                swm_workspace_manager_v1_set_title(manager, workspace, "");
            else
                swm_workspace_manager_v1_set_color(manager, workspace, 0);
        }
    }
    if (wl_display_flush(display) < 0 || wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "swmctl: command failed\n");
        return EXIT_FAILURE;
    }
    swm_workspace_manager_v1_destroy(manager);
    wl_display_disconnect(display);

    return EXIT_SUCCESS;
}
