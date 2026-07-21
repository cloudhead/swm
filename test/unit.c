#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define main swm_program_main
#include "swm.c"
#undef main

/* Verify that releasing a pool item clears its storage. */
static void test_pool(pool_t *pool) {
    unsigned char *item = pool_take(pool);
    size_t         i;

    assert(item);
    memset(item, 0xa5, pool->item_size);
    pool_release(pool, item);

    for (i = 0; i < pool->item_size; i++)
        assert(item[i] == 0);
}

/* Verify pool allocation limits and clearing for every compositor object type. */
static void test_pools(void) {
    client_t *allocated[MAX_CLIENTS];
    int       saved, nullfd;
    size_t    i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        allocated[i] = pool_take(&client_pool);
        assert(allocated[i] == &client_items[i]);
    }
    saved  = dup(STDERR_FILENO);
    nullfd = open("/dev/null", O_WRONLY);
    assert(saved >= 0 && nullfd >= 0);
    assert(dup2(nullfd, STDERR_FILENO) >= 0);
    assert(pool_take(&client_pool) == nullptr);
    assert(dup2(saved, STDERR_FILENO) >= 0);
    close(nullfd);
    close(saved);

    for (i = 0; i < MAX_CLIENTS; i++)
        pool_release(&client_pool, allocated[i]);

    test_pool(&monitor_pool);
    test_pool(&layer_surface_pool);
    test_pool(&keyboard_group_pool);
    test_pool(&pointer_constraint_pool);
    test_pool(&text_input_pool);
    test_pool(&input_popup_pool);
    test_pool(&session_lock_pool);
    test_pool(&pending_spawn_pool);
    test_pool(&window_state_pool);
    test_pool(&static_listener_pool);
    test_pool(&workspace_manager_pool);
    test_pool(&workspace_handle_pool);
    test_pool(&metadata_manager_pool);
}

/* Require a geometry helper to return the expected rectangle. */
static void assert_box(struct swm_box box, int x, int y, int width, int height) {
    assert(box.x == x);
    assert(box.y == y);
    assert(box.width == width);
    assert(box.height == height);
}

/* Verify pure geometry helpers. */
static void test_geometry(void) {
    struct swm_box bounds = { 10, 20, 100, 80 };
    struct swm_box box    = { 200, 200, 0, -2 };
    struct swm_box client = { 100, 100, 300, 200 };
    struct swm_box cursor = { 20, 30, 4, 10 };
    struct swm_box output = { 0, 0, 500, 400 };

    swm_box_apply_bounds(&box, &bounds, 2);
    assert_box(box, 105, 95, 5, 5);
    box = (struct swm_box){ 10, 20, 100, 80 };
    assert_box(swm_box_resize(&box, 20, 10, 10, 1), 10, 20, 120, 90);
    assert_box(swm_box_resize(&box, 500, 500, 5, 2), 105, 95, 5, 5);
    swm_box_reconcile_commit(&box, 90, 70, 2, 5);
    assert_box(box, 16, 26, 94, 74);
    assert_box(swm_popup_position(&client, 2, &cursor, 400, 300, &output), 100, 0, 400, 300);
}

/* Verify pure workspace and stack-state helpers. */
static void test_state(void) {
    bool                   visible[]  = { false, false, false, false, false };
    bool                   occupied[] = { false, false, true, false, true };
    struct swm_stack_state state      = { 16, 1, 1 };
    int                    side;

    assert(swm_workspace_state(false, false, false) == SWM_WORKSPACE_HIDDEN);
    assert(swm_workspace_state(true, true, true) == (SWM_WORKSPACE_ACTIVE | SWM_WORKSPACE_URGENT));
    assert(swm_border_width(3, false, false, false, false, false) == 3);
    assert(swm_border_width(3, true, false, false, false, false) == 0);
    assert(swm_workspace_next(0, 5, 1, false, visible, occupied) == 2);
    assert(swm_workspace_next(0, 5, -1, false, visible, occupied) == 4);
    visible[2] = true;
    assert(swm_workspace_next(0, 5, 1, false, visible, occupied) == 4);
    assert(swm_workspace_next(0, 5, 1, true, visible, occupied) == 1);
    assert(swm_stack_configure(&state, SWM_MASTER_SHRINK, SWM_MASTER_LEFT, &side));
    assert(state.msize == 15 && side == SWM_MASTER_LEFT);

    for (int command = SWM_MASTER_GROW; command <= SWM_FLIP_LAYOUT; command++)
        assert(swm_stack_configure(&state, command, SWM_MASTER_LEFT, &side));
    assert(!swm_stack_configure(&state, 99, SWM_MASTER_LEFT, &side));
}

/* Verify pure rule and persistent-state parsing helpers. */
static void test_parsing(void) {
    char           small[5], appid[MAX_WINDOW_STATE_FIELD], title[MAX_WINDOW_STATE_FIELD];
    struct swm_box geometry;

    assert(swm_rule_matches("*", nullptr, "anything", "title"));
    assert(swm_rule_matches("term", "shell", "my-terminal", "a shell"));
    assert(!swm_rule_matches("browser", nullptr, "terminal", "browser"));
    swm_sanitize_field(small, sizeof(small), "a\tb\nc", nullptr);
    assert(!strcmp(small, "a b "));
    assert(swm_parse_window_state(
        "org.app\tWindow\t1\t-2\t300\t200\n", appid, sizeof(appid), title, sizeof(title), &geometry
    ));
    assert(!strcmp(appid, "org.app"));
    assert(!strcmp(title, "Window"));
    assert_box(geometry, 1, -2, 300, 200);
    assert(
        !swm_parse_window_state("broken", appid, sizeof(appid), title, sizeof(title), &geometry)
    );
}

/* Verify layout cycling and complete output-area partitioning. */
static void test_layouts(void) {
    bool                   cycle[] = { true, true, false, true };
    struct swm_box         area    = { -3, 7, 64, 61 };
    struct swm_box         boxes[64];
    struct swm_stack_state state;
    size_t                 count, produced;
    int                    rotate, flip, masters, stacks;

    assert(swm_next_layout(0, 4, cycle) == 1);
    assert(swm_next_layout(1, 4, cycle) == 3);
    assert(swm_next_layout(3, 4, cycle) == 0);

    for (rotate = 0; rotate <= 1; rotate++)
        for (flip = 0; flip <= 1; flip++)
            for (masters = 0; masters < 5; masters++)
                for (stacks = 1; stacks < 5; stacks++)
                    for (count = 1; count < 13; count++) {
                        int covered = 0;

                        state    = (struct swm_stack_state){ 13, masters, stacks };
                        produced = swm_stack_layout(&area, &state, rotate, flip, count, boxes);
                        assert(produced == count);

                        for (size_t i = 0; i < count; i++)
                            covered += boxes[i].width * boxes[i].height;
                        assert(covered == area.width * area.height);
                    }
}

/* Verify environment expansion and output bounds. */
static void test_utilities(void) {
    char output[64];

    assert(setenv("SWM_TEST_ENV", "hello world", 1) == 0);
    assert(env_expand(output, sizeof(output), "$SWM_TEST_ENV/tail") == 17);
    assert(!strcmp(output, "hello world/tail"));
    assert(env_expand(output, 4, "$SWM_TEST_ENV") == 0);
    assert(env_expand(output, 0, "value") == 0);
    assert(env_expand(output, sizeof(output), "$1") == 3);
    assert(!strcmp(output, "$1"));
}

/* Verify free-workspace selection and published workspace states. */
static void test_workspace_queries(void) {
    client_t    *client;
    workspace_t *workspace;
    int          i;

    for (i = 0; i < WSCOUNT; i++) {
        workspaces[i].mon = (monitor_t *)1;
        wl_list_init(&workspaces[i].handles);
    }
    workspaces[4].mon = nullptr;
    assert(free_workspace() == &workspaces[4]);
    workspaces[4].mon = (monitor_t *)1;
    assert(free_workspace() == nullptr);

    wl_list_init(&clients);
    workspace      = &workspaces[2];
    workspace->mon = nullptr;
    assert(workspace_state(workspace) == EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN);
    client = pool_take(&client_pool);
    assert(client);
    client->ws        = workspace;
    client->is_urgent = 1;
    wl_list_insert(&clients, &client->link);
    assert(workspace_state(workspace) == EXT_WORKSPACE_HANDLE_V1_STATE_URGENT);
    workspace->mon = (monitor_t *)1;
    assert(
        workspace_state(workspace) ==
        (EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE | EXT_WORKSPACE_HANDLE_V1_STATE_URGENT)
    );
    wl_list_remove(&client->link);
    pool_release(&client_pool, client);
}

/* Verify state-file paths and client border-width policy. */
static void test_paths_and_border_policy(void) {
    const char *directory = getenv("SWM_TEST_DIR");
    client_t    client    = {};
    char        path[PATH_MAX], expected[PATH_MAX];

    assert(directory);
    assert(snprintf(path, sizeof(path), "%s/state", directory) < (int)sizeof(path));
    assert(setenv("XDG_STATE_HOME", path, 1) == 0);
    assert(snprintf(expected, sizeof(expected), "%s/swm/windows", path) < (int)sizeof(expected));
    assert(!strcmp(window_state_path(), expected));
    unsetenv("XDG_STATE_HOME");
    assert(snprintf(path, sizeof(path), "%s/home", directory) < (int)sizeof(path));
    assert(setenv("HOME", path, 1) == 0);
    assert(
        snprintf(expected, sizeof(expected), "%s/.local/state/swm/windows", path) <
        (int)sizeof(expected)
    );
    assert(!strcmp(window_state_path(), expected));
    client.bw = 9;
    assert(client_border_width(&client) == borderwidth);
    client.is_fullscreen = 1;
    assert(client_border_width(&client) == 0);
}

/* Initialize a minimal XDG client object for internal tests. */
static void init_xdg_client(
    client_t                *client,
    struct wlr_xdg_surface  *surface,
    struct wlr_xdg_toplevel *toplevel,
    const char              *appid,
    const char              *title
) {
    memset(client, 0, sizeof(*client));
    memset(surface, 0, sizeof(*surface));
    memset(toplevel, 0, sizeof(*toplevel));
    client->type        = XDG_SHELL;
    client->surface.xdg = surface;
    surface->toplevel   = toplevel;
    toplevel->base      = surface;
    toplevel->app_id    = (char *)appid;
    toplevel->title     = (char *)title;
}

/* Verify matching rules update client state and workspace assignment. */
static void test_rule_application(void) {
    client_t                client;
    struct wlr_xdg_surface  surface;
    struct wlr_xdg_toplevel toplevel;

    init_xdg_client(&client, &surface, &toplevel, "org.telegram.desktop", "Telegram");
    wl_list_init(&mons);
    selmon = nullptr;
    apply_rules(&client, nullptr);
    assert(client.is_floating == 1);
    assert(client.is_borderless == 1);
    assert(client.mon == nullptr);
}

/* Verify focus queries across mapped, hidden, and unmanaged clients. */
static void test_focus_queries(void) {
    client_t                first, second, hidden;
    struct wlr_xdg_surface  surfaces[3];
    struct wlr_xdg_toplevel toplevels[3];
    monitor_t               monitor = {};
    workspace_t             shown = {}, other = {};

    shown.mon  = &monitor;
    monitor.ws = &shown;
    other.mon  = nullptr;
    init_xdg_client(&first, &surfaces[0], &toplevels[0], "first", "First");
    init_xdg_client(&second, &surfaces[1], &toplevels[1], "second", "Second");
    init_xdg_client(&hidden, &surfaces[2], &toplevels[2], "hidden", "Hidden");
    first.ws = second.ws = &shown;
    hidden.ws            = &other;
    first.mon = second.mon = &monitor;

    wl_list_init(&clients);
    wl_list_init(&fstack);
    wl_list_insert(clients.prev, &first.link);
    wl_list_insert(clients.prev, &second.link);
    wl_list_insert(clients.prev, &hidden.link);
    wl_list_insert(&fstack, &hidden.flink);
    wl_list_insert(&fstack, &first.flink);
    wl_list_insert(&fstack, &second.flink);
    assert(focus_top(&monitor) == &second);
    assert(focus_top(nullptr) == nullptr);
    assert(focus_close(&second) == &first);
    assert(focus_close(nullptr) == nullptr);
    assert(focus_close(&hidden) == nullptr);
    assert(client_main(nullptr) == nullptr);
    assert(!clients_related(nullptr, &first));
    assert(clients_related(&first, &first));
}

/* Release every saved window-state entry. */
static void clear_window_states(void) {
    window_state_t *state, *tmp;

    wl_list_for_each_safe(state, tmp, &window_states, link) {
        wl_list_remove(&state->link);
        pool_release(&window_state_pool, state);
    }
}

/* Verify saving, loading, finding, and forgetting persistent window state. */
static void test_window_state_storage(void) {
    char                    statehome[PATH_MAX];
    char                    filepath[MAX_STATE_PATH];
    client_t                client;
    struct wlr_xdg_surface  surface;
    struct wlr_xdg_toplevel toplevel;
    window_state_t         *state;
    FILE                   *file;

    assert(
        snprintf(statehome, sizeof(statehome), "%s/window-state-XXXXXX", getenv("SWM_TEST_DIR")) <
        (int)sizeof(statehome)
    );
    assert(mkdtemp(statehome));
    setenv("XDG_STATE_HOME", statehome, 1);
    wl_list_init(&window_states);
    init_xdg_client(&client, &surface, &toplevel, "org.test\tapp", "Window\nTitle");
    client.persist_float = client.is_floating = 1;
    client.geom                               = (struct wlr_box){ 11, -12, 640, 480 };
    remember_client(&client);
    state = find_window_state(&client);
    assert(state);
    assert(!strcmp(state->appid, "org.test app"));
    assert(!strcmp(state->title, "Window Title"));
    assert(state->geom.x == 11 && state->geom.height == 480);

    snprintf(filepath, sizeof(filepath), "%s/swm/windows", statehome);
    file = fopen(filepath, "r");
    assert(file);
    assert(fclose(file) == 0);
    clear_window_states();
    load_window_states();
    assert(find_window_state(&client));
    forget_client(&client);
    assert(find_window_state(&client) == nullptr);

    file = fopen(filepath, "w");
    assert(file);
    assert(fputs("bad line\nlegacy\t1\t2\t3\t4\n", file) >= 0);
    assert(fclose(file) == 0);
    load_window_states();
    assert(!wl_list_empty(&window_states));
    clear_window_states();
    unlink(filepath);
    snprintf(filepath, sizeof(filepath), "%s/swm", statehome);
    rmdir(filepath);
    rmdir(statehome);
}

/* Verify SIGCHLD handling releases tracked child processes. */
static void test_child_reaping(void) {
    pending_spawn_t *spawned;
    struct timespec  delay = { 0, 1000000 };
    pid_t            pid;
    int              i;

    wl_list_init(&pending_spawns);
    pid = fork();
    assert(pid >= 0);

    if (pid == 0)
        _exit(0);
    spawned = pool_take(&pending_spawn_pool);
    assert(spawned);
    spawned->pid = pid;
    wl_list_insert(&pending_spawns, &spawned->link);
    child_pid = pid;

    for (i = 0; i < 100 && !wl_list_empty(&pending_spawns); i++) {
        handle_signal(SIGCHLD, nullptr);
        nanosleep(&delay, nullptr);
    }
    assert(wl_list_empty(&pending_spawns));
    assert(child_pid == -1);
    assert(handle_signal(0, nullptr) == 0);
}

/* Verify spawned commands are tracked and reaped. */
static void test_spawn_tracking(void) {
    const char     *command[] = { "/bin/true", nullptr };
    monitor_t       monitor   = {};
    workspace_t     workspace = {};
    struct timespec delay     = { 0, 1000000 };
    int             i;

    assert(sigprocmask(SIG_BLOCK, nullptr, &original_signal_mask) == 0);
    wl_list_init(&pending_spawns);
    monitor.ws = &workspace;
    selmon     = &monitor;
    spawn(&(arg_t){ .v = command });
    assert(!wl_list_empty(&pending_spawns));

    for (i = 0; i < 100 && !wl_list_empty(&pending_spawns); i++) {
        handle_signal(SIGCHLD, nullptr);
        nanosleep(&delay, nullptr);
    }
    assert(wl_list_empty(&pending_spawns));
    selmon = nullptr;
}

/* Verify client commands remain safe when no client can receive them. */
static void test_commands_without_clients(void) {
    struct wlr_output output  = {};
    monitor_t         monitor = {};
    arg_t             argument;
    int               i;

    monitor.wlr_output   = &output;
    monitor.ws           = &workspaces[0];
    workspaces[0].mon    = &monitor;
    workspaces[0].lt     = &layouts[0];
    workspaces[0].prevlt = nullptr;
    workspaces[0].v = workspaces[0].h = (stack_state_t){ 16, 1, 1 };

    for (i = 1; i < WSCOUNT; i++)
        workspaces[i].mon = (monitor_t *)1;
    wl_list_init(&clients);
    wl_list_init(&fstack);
    wl_list_init(&mons);
    wl_list_init(&ws_managers);
    wl_list_init(&metadata_managers);
    selmon = &monitor;
    unsetenv("XDG_RUNTIME_DIR");

    cycle_layout(nullptr);
    assert(workspaces[0].lt == &layouts[1]);
    assert(workspaces[0].prevlt == &layouts[0]);
    toggle_max_stack(nullptr);
    assert(workspaces[0].lt == &layouts[4]);
    toggle_max_stack(nullptr);
    assert(workspaces[0].lt == &layouts[1]);
    argument.i = MASTER_GROW;
    stack_config(&argument);
    assert(workspaces[0].h.msize == 17);
    argument.i = LAYOUT_FLIP;
    stack_config(&argument);
    assert(workspaces[0].lt == &layouts[3]);

    assert(focus_top(&monitor) == nullptr);
    focus_stack(&(arg_t){ .i = 1 });
    focus_main(nullptr);
    focus_urgent(nullptr);
    tag(&(arg_t){ .i = 0 });
    tag(&(arg_t){ .i = WSCOUNT });
    tag_monitor(&(arg_t){ .i = WLR_DIRECTION_LEFT });
    toggle_floating(nullptr);
    toggle_fullscreen(nullptr);
    kill_client(nullptr);
    raise_client(nullptr);
    zoom(nullptr);
    argument.i = WORKSPACE_NEXT;
    cycle_workspace(&argument);
    assert(monitor.ws == &workspaces[0]);
    view(&(arg_t){ .i = WSCOUNT });

    selmon = nullptr;
    cycle_layout(nullptr);
    toggle_max_stack(nullptr);
    stack_config(&argument);
    cycle_workspace(&argument);
    view(&(arg_t){ .i = 0 });
}

/* Verify small geometry, focus, inhibition, and listener helpers. */
static void test_small_helpers(void) {
    client_t           client = { .geom = { -20, 100, 2, 2 }, .bw = 2 };
    struct wlr_box     bounds = { 0, 0, 80, 60 };
    static_listener_t *slot;
    struct wlr_seat    fake_seat = {};

    apply_bounds(&client, &bounds);
    assert(client.geom.x == 0 && client.geom.y == 55);
    assert(client.geom.width == 5 && client.geom.height == 5);
    seat = &fake_seat;
    assert(shortcuts_inhibited() == 0);
    assert(focus_close(nullptr) == nullptr);
    slot = pool_take(&static_listener_pool);
    assert(slot);
    listener_release(&slot->listener);
}

/* Verify XDG and XWayland client accessors without a running compositor. */
static void test_client_accessors(void) {
    client_t                    client   = {};
    struct wlr_xdg_surface      xdg      = { .geometry = { 3, 4, 50, 60 } };
    struct wlr_xdg_toplevel     toplevel = {};
    struct wlr_xwayland_surface xwayland = {
        .class             = "x11-app",
        .title             = "X11 title",
        .x                 = 7,
        .y                 = 8,
        .width             = 90,
        .height            = 100,
        .modal             = true,
        .override_redirect = true,
        .fullscreen        = true,
    };
    xcb_size_hints_t size_hints = {
        .min_width  = 20,
        .min_height = 20,
        .max_width  = 20,
        .max_height = 40,
    };
    struct wlr_box    box;
    char              storage[MAX_COMMAND_SIZE], *expanded[MAX_COMMAND_ARGS];
    const char *const command[] = { "before-$SWM_TEST_EXPAND", "$SWM_TEST_MISSING", nullptr };

    init_xdg_client(&client, &xdg, &toplevel, "xdg-app", "XDG title");
    xdg.geometry = (struct wlr_box){ 3, 4, 50, 60 };
    client.geom  = (struct wlr_box){ 1, 2, 70, 80 };
    client.bw    = 2;
    assert(!strcmp(client_get_appid(&client), "xdg-app"));
    assert(!strcmp(client_get_title(&client), "XDG title"));
    client_get_clip(&client, &box);
    assert(box.x == 3 && box.y == 4 && box.width == 68 && box.height == 78);
    client_get_geometry(&client, &box);
    assert(box.x == 3 && box.y == 4 && box.width == 50 && box.height == 60);
    assert(client_get_parent(&client) == nullptr);
    assert(!client_is_float_type(&client));
    toplevel.current = (struct wlr_xdg_toplevel_state){
        .min_width  = 10,
        .min_height = 10,
        .max_width  = 10,
        .max_height = 20,
    };
    assert(client_is_float_type(&client));
    assert(!client_is_unmanaged(&client));
    toplevel.requested.fullscreen = true;
    assert(client_wants_fullscreen(&client));

    client.type             = X11;
    client.surface.xwayland = &xwayland;
    assert(client_set_bounds(&client, 10, 20) == 0);
    assert(!strcmp(client_get_appid(&client), "x11-app"));
    assert(!strcmp(client_get_title(&client), "X11 title"));
    client_get_clip(&client, &box);
    assert(box.x == 0 && box.y == 0);
    client_get_geometry(&client, &box);
    assert(box.x == 7 && box.y == 8 && box.width == 90 && box.height == 100);
    assert(client_get_parent(&client) == nullptr);
    assert(client_is_float_type(&client));
    assert(client_is_unmanaged(&client));
    assert(client_wants_fullscreen(&client));
    xwayland.modal      = false;
    xwayland.size_hints = &size_hints;
    assert(client_is_float_type(&client));
    xwayland.class = xwayland.title = nullptr;
    assert(!strcmp(client_get_appid(&client), "broken"));
    assert(!strcmp(client_get_title(&client), "broken"));
    client_set_resizing(&client, true);
    client_set_suspended(&client, true);

    setenv("SWM_TEST_EXPAND", "expanded value", 1);
    unsetenv("SWM_TEST_MISSING");
    expand_argv(command, expanded, storage);
    assert(!strcmp(expanded[0], "before-expanded value"));
    assert(!strcmp(expanded[1], ""));
    assert(expanded[2] == nullptr);
    publish_windows(nullptr);
}

/* Verify configured autostart commands and child tracking with harmless stubs. */
static void test_autostart_execution(void) {
    const char     *stub_path = getenv("SWM_TEST_STUB_PATH");
    const char     *old_path  = getenv("PATH");
    char           *saved_path;
    struct timespec delay = { 0, 1000000 };
    size_t          i;
    int             pending;

    assert(stub_path && old_path);
    saved_path = strdup(old_path);
    assert(saved_path);
    assert(setenv("PATH", stub_path, 1) == 0);
    assert(sigprocmask(SIG_BLOCK, nullptr, &original_signal_mask) == 0);
    wl_list_init(&pending_spawns);
    autostart_len = 0;
    autostart_exec();
    assert(autostart_len > 0);

    for (i = 0; i < 100; i++) {
        pending = 0;
        handle_signal(SIGCHLD, nullptr);

        for (size_t j = 0; j < autostart_len; j++)
            pending |= autostart_pids[j] > 0;

        if (!pending)
            break;
        nanosleep(&delay, nullptr);
    }
    assert(!pending);
    assert(setenv("PATH", saved_path, 1) == 0);
    free(saved_path);
}

/* Verify forwarding of every pointer-gesture event family. */
static void test_pointer_gestures(void) {
    struct wlr_pointer_swipe_begin_event  swipe_begin  = { .time_msec = 1, .fingers = 3 };
    struct wlr_pointer_swipe_update_event swipe_update = { .time_msec = 2, .dx = 1, .dy = -1 };
    struct wlr_pointer_swipe_end_event    swipe_end    = { .time_msec = 3, .cancelled = false };
    struct wlr_pointer_pinch_begin_event  pinch_begin  = { .time_msec = 4, .fingers = 2 };
    struct wlr_pointer_pinch_update_event pinch_update = {
        .time_msec = 5,
        .dx        = 1,
        .dy        = 2,
        .scale     = 1.1,
        .rotation  = 3,
    };
    struct wlr_pointer_pinch_end_event  pinch_end  = { .time_msec = 6, .cancelled = true };
    struct wlr_pointer_hold_begin_event hold_begin = { .time_msec = 7, .fingers = 2 };
    struct wlr_pointer_hold_end_event   hold_end   = { .time_msec = 8, .cancelled = false };

    dpy = wl_display_create();
    assert(dpy);
    seat             = wlr_seat_create(dpy, "test-seat");
    idle_notifier    = wlr_idle_notifier_v1_create(dpy);
    pointer_gestures = wlr_pointer_gestures_v1_create(dpy);
    assert(seat && idle_notifier && pointer_gestures);
    gesture_swipe_begin(nullptr, &swipe_begin);
    gesture_swipe_update(nullptr, &swipe_update);
    gesture_swipe_end(nullptr, &swipe_end);
    gesture_pinch_begin(nullptr, &pinch_begin);
    gesture_pinch_update(nullptr, &pinch_update);
    gesture_pinch_end(nullptr, &pinch_end);
    gesture_hold_begin(nullptr, &hold_begin);
    gesture_hold_end(nullptr, &hold_end);
    wl_display_destroy(dpy);
    dpy = nullptr;
}

/* Verify text-input and input-method wrappers are created and released. */
static void test_input_method_lifecycle(void) {
    struct wlr_seat                  fake_seat       = {};
    struct wlr_text_input_manager_v3 fake_ti_manager = {};
    struct wlr_text_input_v3         text_input      = {};
    struct wlr_input_method_v2       method          = {};
    text_input_t                    *wrapper         = nullptr;
    size_t                           i;

    seat   = &fake_seat;
    ti_mgr = &fake_ti_manager;
    wl_list_init(&fake_ti_manager.text_inputs);
    assert(input_method_focused_text_input() == nullptr);
    input_method = nullptr;
    input_method_send_state(&text_input);
    input_method_commit_notify(nullptr, &method);

    wl_signal_init(&text_input.events.enable);
    wl_signal_init(&text_input.events.commit);
    wl_signal_init(&text_input.events.disable);
    wl_signal_init(&text_input.events.destroy);
    text_input_create_notify(nullptr, &text_input);

    for (i = 0; i < LENGTH(text_input_used); i++)

        if (text_input_used[i]) {
            wrapper = &text_input_items[i];
            break;
        }
    assert(wrapper && wrapper->ti == &text_input);
    text_input_enable_notify(&wrapper->enable, nullptr);
    text_input_commit_notify(&wrapper->commit, nullptr);
    text_input_disable_notify(&wrapper->disable, nullptr);
    text_input_destroy_notify(&wrapper->destroy, nullptr);
    assert(!text_input_used[wrapper - text_input_items]);

    wl_signal_init(&method.events.commit);
    wl_signal_init(&method.events.destroy);
    wl_signal_init(&method.events.grab_keyboard);
    wl_signal_init(&method.events.new_popup_surface);
    input_method_create_notify(nullptr, &method);
    assert(input_method == &method);
    input_method_destroy_notify(nullptr, nullptr);
    assert(input_method == nullptr);
}

/* Verify input handlers safely reject incomplete or inapplicable state. */
static void test_input_early_paths(void) {
    struct wlr_seat                                            fake_seat    = {};
    struct wlr_seat_pointer_request_set_cursor_event           cursor_event = {};
    struct wlr_cursor_shape_manager_v1_request_set_shape_event shape_event  = {};
    struct wlr_drag                                            drag         = {};
    keyboard_group_t                                           group        = {};
    monitor_t                                                  monitor      = {};
    workspace_t                                                workspace    = {};
    struct wlr_output                                          output       = {};

    seat        = &fake_seat;
    cursor_mode = CURSOR_MOVE;
    set_cursor(nullptr, &cursor_event);
    set_cursor_shape(nullptr, &shape_event);
    start_drag(nullptr, &drag);
    assert(key_repeat(&group) == 0);
    assert(key_binding(0, XKB_KEY_NoSymbol, 0) == 0);

    wl_list_init(&clients);
    wl_list_init(&fstack);
    monitor.wlr_output = &output;
    monitor.ws         = &workspace;
    workspace.mon      = &monitor;
    workspace.lt       = &layouts[0];
    workspace.v = workspace.h = (stack_state_t){ 16, 1, 1 };
    master_left(&monitor);
    master_right(&monitor);
    master_top(&monitor);
    master_bottom(&monitor);
    max_stack(&monitor);
    move_resize(&(arg_t){ .u = CURSOR_MOVE });
}

/* Assert that the program exits with the requested status. */
static void assert_program_exits(int argc, char **argv, int clear_runtime, int code) {
    int   status;
    pid_t pid = fork();

    assert(pid >= 0);

    if (pid == 0) {
        close(STDERR_FILENO);

        if (clear_runtime)
            unsetenv("XDG_RUNTIME_DIR");
        optind = 1;
        _exit(swm_program_main(argc, argv));
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == code);
}

/* Assert that the program exits with failure status. */
static void assert_program_fails(int argc, char **argv, int clear_runtime) {
    assert_program_exits(argc, argv, clear_runtime, 1);
}

/* Verify version output and invalid command-line invocations. */
static void test_command_line_errors(void) {
    char *version[]         = { "swm", "-v", nullptr };
    char *help[]            = { "swm", "-h", nullptr };
    char *extra[]           = { "swm", "extra", nullptr };
    char *missing_runtime[] = { "swm", nullptr };

    assert_program_exits(2, version, 0, 0); /* -v prints the version and succeeds. */
    assert_program_fails(2, help, 0);
    assert_program_fails(2, extra, 0);
    assert_program_fails(1, missing_runtime, 1);
}

/* Verify pointer-constraint and decoration lifecycle handlers. */
static void test_pointer_and_decoration_lifecycle(void) {
    struct wlr_pointer_constraint_v1      constraint = {};
    struct wlr_surface                    surface    = {};
    pointer_constraint_t                 *wrapper    = nullptr;
    struct wlr_xdg_surface                xdg        = {};
    struct wlr_xdg_toplevel               toplevel   = {};
    struct wlr_xdg_toplevel_decoration_v1 decoration = {};
    client_t                              client     = {};
    struct wlr_seat                       fake_seat  = {};
    size_t                                i;

    seat               = &fake_seat;
    constraint.surface = &surface;
    wl_signal_init(&constraint.events.destroy);
    create_pointer_constraint(nullptr, &constraint);

    for (i = 0; i < LENGTH(pointer_constraint_used); i++)

        if (pointer_constraint_used[i]) {
            wrapper = &pointer_constraint_items[i];
            break;
        }
    assert(wrapper && wrapper->constraint == &constraint);
    destroy_pointer_constraint(&wrapper->destroy, nullptr);
    assert(!pointer_constraint_used[wrapper - pointer_constraint_items]);
    cursor_constrain(nullptr);
    cursor_constrain(nullptr);

    client.type               = XDG_SHELL;
    client.surface.xdg        = &xdg;
    client.decoration         = &decoration;
    xdg.toplevel              = &toplevel;
    toplevel.base             = &xdg;
    decoration.toplevel       = &toplevel;
    decoration.requested_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    request_decoration_mode(&client.set_decoration_mode, nullptr);
    assert(client.bw == 0);
    maximize_notify(&client.maximize, nullptr);
    create_decoration(nullptr, &decoration);
    urgent(nullptr, &(struct wlr_xdg_activation_v1_request_activate_event){});
}

/* Verify modifier handling, repeatable bindings, and key-repeat dispatch. */
static void test_keybindings_and_repeat(void) {
    struct wlr_seat           fake_seat      = {};
    struct wlr_keyboard_group keyboard_group = {};
    keyboard_group_t          group          = {};
    struct wl_event_loop     *loop;
    xkb_keysym_t              symbol = XKB_KEY_j;

    seat   = &fake_seat;
    selmon = nullptr;
    wl_list_init(&fstack);
    assert(key_binding(MOD, XKB_KEY_1, false) == 1);
    assert(key_binding(MOD | WLR_MODIFIER_CAPS, XKB_KEY_1, false) == 1);
    assert(key_binding(MOD, XKB_KEY_1, true) == 0);
    assert(key_binding(MOD, XKB_KEY_j, true) == 2);
    assert(key_binding(0, XKB_KEY_NoSymbol, 0) == 0);

    loop = wl_event_loop_create();
    assert(loop);
    group.wlr_group                          = &keyboard_group;
    group.nsyms                              = 1;
    group.keysyms                            = &symbol;
    group.mods                               = MOD;
    keyboard_group.keyboard.repeat_info.rate = 25;
    group.key_repeat_source                  = wl_event_loop_add_timer(loop, key_repeat, &group);
    assert(group.key_repeat_source);
    assert(key_repeat(&group) == 0);
    wl_event_source_remove(group.key_repeat_source);
    wl_event_loop_destroy(loop);
}

/* Name one compositor-internal unit test callable by the Python runner. */
typedef struct {
    const char *name;
    void (*run)(void);
} internal_test_t;

/* Run the requested compositor-internal unit test. */
int main(int argc, char **argv) {
    static const internal_test_t tests[] = {
        { "geometry", test_geometry },
        { "state", test_state },
        { "parsing", test_parsing },
        { "layouts", test_layouts },
        { "utilities", test_utilities },
        { "pools", test_pools },
        { "workspaces", test_workspace_queries },
        { "paths-and-borders", test_paths_and_border_policy },
        { "rules", test_rule_application },
        { "focus", test_focus_queries },
        { "window-state", test_window_state_storage },
        { "child-reaping", test_child_reaping },
        { "spawn-tracking", test_spawn_tracking },
        { "commands-without-clients", test_commands_without_clients },
        { "small-helpers", test_small_helpers },
        { "client-accessors", test_client_accessors },
        { "autostart", test_autostart_execution },
        { "pointer-gestures", test_pointer_gestures },
        { "input-method", test_input_method_lifecycle },
        { "input-early-paths", test_input_early_paths },
        { "command-line", test_command_line_errors },
        { "pointer-and-decoration", test_pointer_and_decoration_lifecycle },
        { "keybindings-and-repeat", test_keybindings_and_repeat },
    };

    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < LENGTH(tests); i++)
            puts(tests[i].name);
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: unit --list|TEST\n");
        return 2;
    }
    for (size_t i = 0; i < LENGTH(tests); i++) {
        if (!strcmp(argv[1], tests[i].name)) {
            tests[i].run();
            return 0;
        }
    }
    fprintf(stderr, "unknown internal test: %s\n", argv[1]);
    return 2;
}
