#!/usr/bin/env python3

"""Protocol clients used by the Python integration runner."""

from __future__ import annotations

import ctypes
import mmap
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from pywayland.client import Display

from protocols.ext_session_lock_v1 import (
    ExtSessionLockManagerV1,
)
from protocols.ext_workspace_v1 import ExtWorkspaceManagerV1
from protocols.idle_inhibit_unstable_v1 import ZwpIdleInhibitManagerV1
from protocols.input_method_unstable_v2 import ZwpInputMethodManagerV2
from protocols.keyboard_shortcuts_inhibit_unstable_v1 import (
    ZwpKeyboardShortcutsInhibitManagerV1,
)
from protocols.pointer_constraints_unstable_v1 import ZwpPointerConstraintsV1
from protocols.relative_pointer_unstable_v1 import ZwpRelativePointerManagerV1
from protocols.text_input_unstable_v3 import ZwpTextInputManagerV3
from protocols.virtual_keyboard_unstable_v1 import ZwpVirtualKeyboardManagerV1
from protocols.wayland import (
    WlCompositor,
    WlKeyboard,
    WlOutput,
    WlSeat,
    WlShm,
)
from protocols.wlr_foreign_toplevel_management_unstable_v1 import (
    ZwlrForeignToplevelManagerV1,
)
from protocols.wlr_layer_shell_unstable_v1 import ZwlrLayerShellV1
from protocols.wlr_output_power_management_unstable_v1 import (
    ZwlrOutputPowerManagerV1,
)
from protocols.wlr_virtual_pointer_unstable_v1 import ZwlrVirtualPointerManagerV1
from protocols.xdg_shell import XdgPositioner, XdgToplevel, XdgWmBase


class Failure(RuntimeError):
    """Report a protocol-client failure."""


class Connection:
    """Bind requested globals on one Wayland connection."""

    def __init__(self, *interfaces):
        self.display = Display()
        self.display.connect()
        self.registry = self.display.get_registry()
        self.interfaces = {interface.name: interface for interface in interfaces}
        self.globals: dict[str, object] = {}
        self.registry.dispatcher["global"] = self._global
        self.display.roundtrip()

    def _global(self, registry, name: int, interface: str, version: int) -> None:
        wanted = self.interfaces.get(interface)
        if wanted is not None and interface not in self.globals:
            self.globals[interface] = registry.bind(name, wanted, min(version, wanted.version))

    def get(self, interface):
        """Return a required bound global."""

        try:
            return self.globals[interface.name]
        except KeyError as error:
            raise Failure(f"compositor does not advertise {interface.name}") from error

    def roundtrips(self, count: int = 1) -> None:
        """Complete several request/event exchanges."""

        for _ in range(count):
            if self.display.roundtrip() < 0:
                raise Failure("Wayland roundtrip failed")

    def close(self) -> None:
        """Disconnect from the compositor."""

        self.display.disconnect()


def create_buffer(shm, width: int, height: int, color: int):
    """Create a solid-color shared-memory buffer."""

    size = width * height * 4
    with tempfile.TemporaryFile(dir=os.environ.get("SWM_TEST_DIR")) as stream:
        stream.truncate(size)
        pixels = mmap.mmap(stream.fileno(), size)
        pixels[:] = struct.pack("=I", color) * (width * height)
        pixels.flush()
        pool = shm.create_pool(stream.fileno(), size)
        buffer = pool.create_buffer(0, width, height, width * 4, WlShm.format.argb8888)
        pool.destroy()
    buffer.dispatcher["release"] = lambda proxy: proxy.destroy()
    return buffer


class Window:
    """Own one mapped XDG toplevel and report configure state."""

    def __init__(
        self,
        connection: Connection,
        title: str,
        color: int,
        parent=None,
        fullscreen: bool = False,
        report: Path | None = None,
    ):
        self.connection = connection
        self.compositor = connection.get(WlCompositor)
        self.shm = connection.get(WlShm)
        self.wm_base = connection.get(XdgWmBase)
        self.title = title
        self.color = color
        self.width = 320
        self.height = 200
        self.fullscreen = False
        self.running = True
        self.report = report
        self.wm_base.dispatcher["ping"] = lambda proxy, serial: proxy.pong(serial)
        self.surface = self.compositor.create_surface()
        self.xdg_surface = self.wm_base.get_xdg_surface(self.surface)
        self.xdg_surface.dispatcher["configure"] = self._surface_configure
        self.toplevel = self.xdg_surface.get_toplevel()
        self.toplevel.dispatcher["configure"] = self._toplevel_configure
        self.toplevel.dispatcher["close"] = lambda proxy: setattr(self, "running", False)
        self.toplevel.set_title(title)
        self.toplevel.set_app_id("swm-test")
        if parent is not None:
            self.toplevel.set_parent(parent)
        if fullscreen:
            self.toplevel.set_fullscreen(None)
        self.surface.commit()

    def _toplevel_configure(self, proxy, width: int, height: int, states) -> None:
        if width > 0:
            self.width = width
        if height > 0:
            self.height = height
        self.fullscreen = int(XdgToplevel.state.fullscreen) in list(states)
        self._write_report()

    def _surface_configure(self, proxy, serial: int) -> None:
        proxy.ack_configure(serial)
        self.draw()

    def _write_report(self) -> None:
        if self.report:
            self.report.write_text(f"{self.width} {self.height} {int(self.fullscreen)}\n")

    def draw(self) -> None:
        """Attach a fresh buffer and commit the surface."""

        buffer = create_buffer(self.shm, self.width, self.height, self.color)
        self.surface.attach(buffer, 0, 0)
        self.surface.damage_buffer(0, 0, self.width, self.height)
        self.surface.commit()
        self._write_report()

    def unmap(self) -> None:
        """Detach the surface buffer."""

        self.surface.attach(None, 0, 0)
        self.surface.commit()

    def destroy(self) -> None:
        """Destroy the complete toplevel role."""

        self.toplevel.destroy()
        self.xdg_surface.destroy()
        self.surface.destroy()


def xdg_client(arguments: list[str]) -> None:
    """Run one persistent XDG window."""

    color = int(arguments[0], 16) if arguments else 0xFF336699
    title = arguments[1] if len(arguments) > 1 else "swm-test"
    report = Path(os.environ["SWM_CLIENT_REPORT"]) if os.environ.get("SWM_CLIENT_REPORT") else None
    connection = Connection(WlCompositor, WlShm, XdgWmBase)
    window = Window(
        connection,
        title,
        color,
        fullscreen="fullscreen" in arguments[2:],
        report=report,
    )
    while window.running:
        connection.display.dispatch(block=True)
    window.destroy()
    connection.close()


def xdg_lifecycle(arguments: list[str]) -> None:
    """Repeatedly map and unmap a focused XDG toplevel."""

    count = int(arguments[0]) if arguments else 8
    connection = Connection(WlCompositor, WlShm, XdgWmBase)
    fallback = Window(connection, "swm-xdg-fallback", 0xFF446622)
    window = Window(connection, "swm-xdg-lifecycle", 0xFF224466)
    connection.roundtrips(3)
    for index in range(count):
        window.unmap()
        connection.roundtrips(2)
        window.color += 0x00050311
        window.surface.commit()
        connection.roundtrips(2)
    window.destroy()
    fallback.destroy()
    connection.close()


def transient(arguments: list[str]) -> None:
    """Exercise popup, parented, and fullscreen XDG surfaces."""

    count = int(arguments[0]) if arguments else 4
    connection = Connection(WlCompositor, WlShm, XdgWmBase)
    parent = Window(connection, "swm-child-parent", 0xFF335577)
    fullscreen = Window(connection, "swm-fullscreen", 0xFF224466, fullscreen=True)
    connection.roundtrips(4)
    if not fullscreen.fullscreen:
        raise Failure("fullscreen window was not configured fullscreen")

    positioner = connection.get(XdgWmBase).create_positioner()
    positioner.set_size(120, 60)
    positioner.set_anchor_rect(30, 20, 10, 10)
    positioner.set_anchor(XdgPositioner.anchor.bottom_right)
    positioner.set_gravity(XdgPositioner.gravity.bottom_right)
    positioner.set_constraint_adjustment(
        XdgPositioner.constraint_adjustment.slide_x
        | XdgPositioner.constraint_adjustment.slide_y
    )
    popup_surface = connection.get(WlCompositor).create_surface()
    popup_xdg = connection.get(XdgWmBase).get_xdg_surface(popup_surface)
    popup_configured = []

    def configure(proxy, serial) -> None:
        proxy.ack_configure(serial)
        buffer = create_buffer(connection.get(WlShm), 120, 60, 0xFF557733)
        popup_surface.attach(buffer, 0, 0)
        popup_surface.damage_buffer(0, 0, 120, 60)
        popup_surface.commit()
        popup_configured.append(True)

    popup_xdg.dispatcher["configure"] = configure
    popup = popup_xdg.get_popup(parent.xdg_surface, positioner)
    popup.dispatcher["configure"] = lambda proxy, x, y, width, height: None
    popup.dispatcher["popup_done"] = lambda proxy: None
    popup.dispatcher["repositioned"] = lambda proxy, token: None
    positioner.destroy()
    popup_surface.commit()
    connection.roundtrips(4)
    if not popup_configured:
        raise Failure("transient popup was not configured")
    popup.destroy()
    popup_xdg.destroy()
    popup_surface.destroy()
    connection.roundtrips()

    for index in range(count):
        child = Window(
            connection,
            f"swm-child-{index}",
            0xFF773355 + index * 0x0003070D,
            parent=parent.toplevel,
        )
        connection.roundtrips(3)
        if not fullscreen.fullscreen:
            raise Failure("transient child stole fullscreen")
        child.destroy()
        connection.roundtrips()
    fullscreen.destroy()
    parent.destroy()
    connection.close()


def keymap_text() -> tuple[bytes, dict[str, int]]:
    """Build a default XKB keymap and return modifier indices."""

    xkb = ctypes.CDLL("libxkbcommon.so.0")
    xkb.xkb_context_new.argtypes = [ctypes.c_int]
    xkb.xkb_context_new.restype = ctypes.c_void_p
    xkb.xkb_keymap_new_from_names.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    xkb.xkb_keymap_new_from_names.restype = ctypes.c_void_p
    xkb.xkb_keymap_get_as_string.argtypes = [ctypes.c_void_p, ctypes.c_int]
    xkb.xkb_keymap_get_as_string.restype = ctypes.c_void_p
    xkb.xkb_keymap_mod_get_index.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    xkb.xkb_keymap_mod_get_index.restype = ctypes.c_uint
    xkb.xkb_keymap_unref.argtypes = [ctypes.c_void_p]
    xkb.xkb_context_unref.argtypes = [ctypes.c_void_p]
    context = xkb.xkb_context_new(0)
    keymap = xkb.xkb_keymap_new_from_names(context, None, 0)
    pointer = xkb.xkb_keymap_get_as_string(keymap, 1)
    text = ctypes.string_at(pointer)
    ctypes.CDLL(None).free(ctypes.c_void_p(pointer))
    indices = {
        "shift": xkb.xkb_keymap_mod_get_index(keymap, b"Shift"),
        "ctrl": xkb.xkb_keymap_mod_get_index(keymap, b"Control"),
        "alt": xkb.xkb_keymap_mod_get_index(keymap, b"Mod1"),
        "logo": xkb.xkb_keymap_mod_get_index(keymap, b"Mod4"),
    }
    xkb.xkb_keymap_unref(keymap)
    xkb.xkb_context_unref(context)
    return text, indices


def keyboard(arguments: list[str]) -> None:
    """Send a key and modifiers through virtual-keyboard-v1."""

    if len(arguments) != 2:
        raise Failure("keyboard requires KEYCODE|none MODIFIERS")
    key = None if arguments[0] == "none" else int(arguments[0], 0)
    requested = int(arguments[1], 0)
    connection = Connection(WlSeat, ZwpVirtualKeyboardManagerV1)
    seat = connection.get(WlSeat)
    manager = connection.get(ZwpVirtualKeyboardManagerV1)
    virtual = manager.create_virtual_keyboard(seat)
    text, indices = keymap_text()
    with tempfile.TemporaryFile(dir=os.environ.get("SWM_TEST_DIR")) as stream:
        stream.write(text + b"\0")
        stream.flush()
        virtual.keymap(WlKeyboard.keymap_format.xkb_v1, stream.fileno(), len(text) + 1)
    modifiers = 0
    modifier_keys = (
        (1, "shift", 42),
        (4, "ctrl", 29),
        (8, "alt", 56),
        (64, "logo", 125),
    )
    for flag, name, code in modifier_keys:
        if requested & flag:
            modifiers |= 1 << indices[name]
            virtual.key(0, code, WlKeyboard.key_state.pressed)
    virtual.modifiers(modifiers, 0, 0, 0)
    if key is not None:
        virtual.key(1, key, WlKeyboard.key_state.pressed)
        virtual.key(2, key, WlKeyboard.key_state.released)
    connection.roundtrips()
    ready = os.environ.get("SWM_KEYBOARD_READY")
    if ready:
        Path(ready).write_text("ready\n")
    release = os.environ.get("SWM_KEYBOARD_RELEASE")
    if release:
        deadline = time.monotonic() + 2
        while not Path(release).exists():
            if time.monotonic() >= deadline:
                raise Failure("timed out waiting to release virtual keyboard")
            time.sleep(0.002)
    for flag, _, code in modifier_keys:
        if requested & flag:
            virtual.key(3, code, WlKeyboard.key_state.released)
    virtual.modifiers(0, 0, 0, 0)
    virtual.destroy()
    connection.roundtrips()
    connection.close()


def pointer(arguments: list[str]) -> None:
    """Move and click through virtual-pointer-v1."""

    x = int(arguments[0])
    button = 0x111 if len(arguments) > 1 and arguments[1] == "right" else 0x110
    connection = Connection(WlSeat, ZwlrVirtualPointerManagerV1)
    manager = connection.get(ZwlrVirtualPointerManagerV1)
    virtual = manager.create_virtual_pointer(connection.get(WlSeat))
    virtual.motion_absolute(0, x, 500, 1000, 1000)
    virtual.frame()
    virtual.button(1, button, 1)
    virtual.motion(2, 25.0, 20.0)
    virtual.frame()
    virtual.button(3, button, 0)
    virtual.axis_source(0)
    virtual.axis_discrete(3, 0, 1.0, 1)
    virtual.axis_stop(4, 0)
    virtual.frame()
    connection.roundtrips()
    virtual.destroy()
    connection.close()


def foreign(arguments: list[str]) -> None:
    """Apply one foreign-toplevel action to a named window."""

    title, action = arguments
    connection = Connection(WlSeat, ZwlrForeignToplevelManagerV1)
    seat = connection.get(WlSeat)
    manager = connection.get(ZwlrForeignToplevelManagerV1)
    acted = False
    observed = False

    def toplevel(proxy, handle):
        nonlocal acted, observed
        state = {"title": "", "fullscreen": False}
        fullscreen_state = int(handle.interface.state.fullscreen)
        handle.dispatcher["title"] = lambda p, value: state.update(title=value)

        def states(p, values):
            nonlocal acted, observed
            state["fullscreen"] = fullscreen_state in list(values)
            if state["title"] != title:
                return
            if not acted:
                if action == "activate":
                    handle.activate(seat)
                elif action == "fullscreen":
                    handle.set_fullscreen(None)
                elif action == "unfullscreen":
                    handle.unset_fullscreen()
                elif action == "close":
                    handle.close()
                else:
                    raise Failure(f"unknown foreign action: {action}")
                acted = True
            observed = (
                action == "close"
                or action == "fullscreen" and state["fullscreen"]
                or action in {"activate", "unfullscreen"} and not state["fullscreen"]
            )

        handle.dispatcher["state"] = states
        handle.dispatcher["done"] = lambda p: states(
            p, [fullscreen_state] if state["fullscreen"] else []
        )
        handle.dispatcher["closed"] = lambda p: None

    manager.dispatcher["toplevel"] = toplevel
    for _ in range(20):
        connection.roundtrips()
        if acted and observed:
            break
    if not acted or not observed:
        raise Failure(f"foreign-toplevel action {action} was not observed")
    if action == "close":
        connection.roundtrips(2)
    connection.close()


def workspace(arguments: list[str]) -> None:
    """Activate a named workspace through ext-workspace-v1."""

    if len(arguments) != 1:
        raise Failure("workspace requires a workspace name")
    target = arguments[0]
    connection = Connection(ExtWorkspaceManagerV1)
    manager = connection.get(ExtWorkspaceManagerV1)
    workspaces: dict[str, object] = {}

    def announced(proxy, handle):
        handle.dispatcher["name"] = lambda p, name: workspaces.update({name: handle})
        handle.dispatcher["removed"] = lambda p: None

    manager.dispatcher["workspace_group"] = lambda proxy, group: None
    manager.dispatcher["workspace"] = announced
    manager.dispatcher["done"] = lambda proxy: None
    manager.dispatcher["finished"] = lambda proxy: None
    connection.roundtrips(2)
    if target not in workspaces:
        raise Failure(f"workspace {target!r} was not advertised")
    workspaces[target].activate()
    manager.commit()
    connection.roundtrips(2)
    connection.close()


def output_power(arguments: list[str]) -> None:
    """Turn one output off and back on through output-power-v1."""

    connection = Connection(WlOutput, ZwlrOutputPowerManagerV1)
    power = connection.get(ZwlrOutputPowerManagerV1).get_output_power(connection.get(WlOutput))
    modes: list[int] = []
    failed: list[bool] = []
    power.dispatcher["mode"] = lambda proxy, mode: modes.append(mode)
    power.dispatcher["failed"] = lambda proxy: failed.append(True)
    connection.roundtrips()
    if not modes or modes[-1] != 1:
        raise Failure("output did not begin powered on")
    power.set_mode(0)
    connection.roundtrips(2)
    if modes[-1] != 0:
        raise Failure("output did not power off")
    power.set_mode(1)
    connection.roundtrips(2)
    if modes[-1] != 1 or failed:
        raise Failure("output did not power back on")
    power.destroy()
    connection.close()


def output_management(arguments: list[str]) -> None:
    """Exercise output-management through the installed reference client."""

    command = ["wlr-randr"]
    if arguments and arguments[0] in {"disable-first", "disable-second"}:
        output = "HEADLESS-1" if arguments[0] == "disable-first" else "HEADLESS-2"
        command += ["--output", output, "--off"]
    else:
        command += ["--output", "HEADLESS-1", "--on"]
        subprocess.run([*command, "--dryrun"], check=True)
    subprocess.run(command, check=True)


def layer(arguments: list[str]) -> None:
    """Create, configure, map, and destroy layer-shell surfaces."""

    count = int(arguments[0]) if arguments else 2
    connection = Connection(WlCompositor, WlShm, ZwlrLayerShellV1)
    compositor = connection.get(WlCompositor)
    shm = connection.get(WlShm)
    shell = connection.get(ZwlrLayerShellV1)
    layers = []
    for index in range(count):
        surface = compositor.create_surface()
        role = shell.get_layer_surface(
            surface,
            None,
            ZwlrLayerShellV1.layer.top,
            f"swm-layer-{index}",
        )
        configured = []

        def configure(
            proxy,
            serial,
            width,
            height,
            surface=surface,
            configured=configured,
            index=index,
        ):
            proxy.ack_configure(serial)
            width = width or 80
            height = height or 40
            surface.attach(create_buffer(shm, width, height, 0xCC112233 + index), 0, 0)
            surface.damage_buffer(0, 0, width, height)
            surface.commit()
            configured.append(True)

        role.dispatcher["configure"] = configure
        role.dispatcher["closed"] = lambda proxy: None
        role.set_size(80 + index, 40 + index)
        role.set_anchor(1 << (index % 4))
        surface.commit()
        layers.append((surface, role, configured))
    connection.roundtrips(3)
    if not all(configured for _, _, configured in layers):
        raise Failure("layer surface was not configured")
    for surface, role, _ in reversed(layers):
        role.destroy()
        surface.destroy()
    connection.roundtrips()
    connection.close()


def session_lock(arguments: list[str]) -> None:
    """Create and release session locks."""

    count = int(arguments[0]) if arguments else 2
    connection = Connection(WlCompositor, WlShm, WlOutput, ExtSessionLockManagerV1)
    compositor = connection.get(WlCompositor)
    shm = connection.get(WlShm)
    output = connection.get(WlOutput)
    manager = connection.get(ExtSessionLockManagerV1)
    for index in range(count):
        lock = manager.lock()
        locked: list[bool] = []
        finished: list[bool] = []
        lock.dispatcher["locked"] = lambda proxy: locked.append(True)
        lock.dispatcher["finished"] = lambda proxy: finished.append(True)
        surface = compositor.create_surface()
        role = lock.get_lock_surface(surface, output)
        configured: list[bool] = []

        def configure(proxy, serial, width, height):
            proxy.ack_configure(serial)
            surface.attach(create_buffer(shm, width, height, 0xFF101010 + index), 0, 0)
            surface.damage_buffer(0, 0, width, height)
            surface.commit()
            configured.append(True)

        role.dispatcher["configure"] = configure
        connection.roundtrips(3)
        if not finished and (not locked or not configured):
            raise Failure("session lock was not configured")
        role.destroy()
        surface.destroy()
        if locked:
            lock.unlock_and_destroy()
        else:
            lock.destroy()
        connection.roundtrips()
    connection.close()


def text_input(arguments: list[str]) -> None:
    """Exercise text input, input methods, and input inhibitors together."""

    connection = Connection(
        WlCompositor,
        WlShm,
        WlSeat,
        XdgWmBase,
        ZwpTextInputManagerV3,
        ZwpInputMethodManagerV2,
        ZwpKeyboardShortcutsInhibitManagerV1,
        ZwpPointerConstraintsV1,
        ZwpRelativePointerManagerV1,
        ZwpIdleInhibitManagerV1,
        ZwlrVirtualPointerManagerV1,
    )
    seat = connection.get(WlSeat)
    pointer = seat.get_pointer()
    for event in ("enter", "leave", "motion", "button", "axis"):
        pointer.dispatcher[event] = lambda proxy, *values: None

    text = connection.get(ZwpTextInputManagerV3).get_text_input(seat)
    method = connection.get(ZwpInputMethodManagerV2).get_input_method(seat)
    state = {
        "activated": False,
        "surrounding": False,
        "cause": False,
        "content": False,
        "preedit": False,
        "commit": False,
        "delete": False,
        "done": False,
        "rectangle": False,
        "shortcuts": False,
        "locked": False,
        "relative": False,
        "serial": 0,
    }

    def text_enter(proxy, surface) -> None:
        if surface != window.surface:
            return
        text.enable()
        text.set_surrounding_text("before after", 6, 6)
        text.set_text_change_cause(1)
        text.set_content_type(1, 0)
        text.set_cursor_rectangle(7, 11, 3, 15)
        text.commit()

    def method_done(proxy) -> None:
        state["serial"] += 1
        if not all(state[name] for name in ("activated", "surrounding", "cause", "content")):
            return
        method.set_preedit_string("preedit", 2, 4)
        method.commit_string("committed")
        method.delete_surrounding_text(2, 3)
        method.commit(state["serial"])

    text.dispatcher["enter"] = text_enter
    text.dispatcher["leave"] = lambda proxy, surface: None
    text.dispatcher["preedit_string"] = lambda proxy, value, begin, end: state.update(
        preedit=value == "preedit" and begin == 2 and end == 4
    )
    text.dispatcher["commit_string"] = lambda proxy, value: state.update(
        commit=value == "committed"
    )
    text.dispatcher["delete_surrounding_text"] = lambda proxy, before, after: state.update(
        delete=before == 2 and after == 3
    )
    text.dispatcher["done"] = lambda proxy, serial: state.update(done=True)

    popup_surface = connection.get(WlCompositor).create_surface()
    popup = method.get_input_popup_surface(popup_surface)
    popup.dispatcher["text_input_rectangle"] = lambda proxy, x, y, width, height: state.update(
        rectangle=(x, y, width, height) == (0, -15, 3, 15)
    )

    def activate(proxy) -> None:
        state["activated"] = True
        buffer = create_buffer(connection.get(WlShm), 32, 16, 0xFF8855AA)
        popup_surface.attach(buffer, 0, 0)
        popup_surface.damage_buffer(0, 0, 32, 16)
        popup_surface.commit()

    method.dispatcher["activate"] = activate
    method.dispatcher["deactivate"] = lambda proxy: None
    method.dispatcher["surrounding_text"] = lambda proxy, value, cursor, anchor: state.update(
        surrounding=value == "before after" and cursor == 6 and anchor == 6
    )
    method.dispatcher["text_change_cause"] = lambda proxy, cause: state.update(cause=cause == 1)
    method.dispatcher["content_type"] = lambda proxy, hint, purpose: state.update(
        content=hint == 1 and purpose == 0
    )
    method.dispatcher["done"] = method_done
    method.dispatcher["unavailable"] = lambda proxy: (_ for _ in ()).throw(
        Failure("input method is unavailable")
    )

    window = Window(connection, "text-input", 0xFF556677)
    idle = connection.get(ZwpIdleInhibitManagerV1).create_inhibitor(window.surface)
    shortcuts = connection.get(ZwpKeyboardShortcutsInhibitManagerV1).inhibit_shortcuts(
        window.surface, seat
    )
    shortcuts.dispatcher["active"] = lambda proxy: state.update(shortcuts=True)
    shortcuts.dispatcher["inactive"] = lambda proxy: state.update(shortcuts=False)
    locked = connection.get(ZwpPointerConstraintsV1).lock_pointer(
        window.surface, pointer, None, 2
    )
    locked.dispatcher["locked"] = lambda proxy: state.update(locked=True)
    locked.dispatcher["unlocked"] = lambda proxy: state.update(locked=False)
    relative = connection.get(ZwpRelativePointerManagerV1).get_relative_pointer(pointer)
    relative.dispatcher["relative_motion"] = lambda proxy, hi, lo, dx, dy, ux, uy: state.update(
        relative=dx != 0
    )
    virtual = connection.get(ZwlrVirtualPointerManagerV1).create_virtual_pointer(seat)

    for index in range(100):
        virtual.motion_absolute(0, index % 10 * 100 + 50, index // 10 * 100 + 50, 1000, 1000)
        virtual.frame()
        connection.roundtrips()
        if state["locked"]:
            break
    virtual.motion(10, 5.0, 0.0)
    virtual.frame()
    connection.roundtrips(3)

    required = (
        "preedit",
        "commit",
        "delete",
        "done",
        "rectangle",
        "shortcuts",
        "locked",
        "relative",
    )
    missing = [name for name in required if not state[name]]
    if missing:
        raise Failure(f"text-input events not observed: {', '.join(missing)}")

    text.disable()
    text.commit()
    connection.roundtrips(2)
    window.destroy()
    virtual.destroy()
    relative.destroy()
    locked.destroy()
    shortcuts.destroy()
    idle.destroy()
    popup.destroy()
    popup_surface.destroy()
    text.destroy()
    method.destroy()
    connection.close()


def x11(arguments: list[str]) -> None:
    """Exercise managed, configured, fullscreen, and unmanaged X11 windows."""

    import xcffib
    import xcffib.xproto

    connection = xcffib.connect()
    setup = connection.get_setup()
    screen = setup.roots[connection.pref_screen]
    window = connection.generate_id()
    mask = xcffib.xproto.CW.BackPixel | xcffib.xproto.CW.EventMask
    values = [screen.white_pixel, xcffib.xproto.EventMask.StructureNotify]
    connection.core.CreateWindow(
        screen.root_depth,
        window,
        screen.root,
        30,
        30,
        220,
        140,
        0,
        xcffib.xproto.WindowClass.InputOutput,
        screen.root_visual,
        mask,
        values,
    )
    wm_name = connection.core.InternAtom(False, len("WM_NAME"), "WM_NAME").reply().atom
    string = connection.core.InternAtom(False, len("STRING"), "STRING").reply().atom
    title = b"swm-xwayland-test"
    connection.core.ChangeProperty(
        xcffib.xproto.PropMode.Replace,
        window,
        wm_name,
        string,
        8,
        len(title),
        title,
    )
    wm_class = connection.core.InternAtom(False, len("WM_CLASS"), "WM_CLASS").reply().atom
    class_name = b"x11-test\0X11-Test\0"
    connection.core.ChangeProperty(
        xcffib.xproto.PropMode.Replace,
        window,
        wm_class,
        string,
        8,
        len(class_name),
        class_name,
    )
    connection.core.MapWindow(window)
    configure = (
        xcffib.xproto.ConfigWindow.X
        | xcffib.xproto.ConfigWindow.Y
        | xcffib.xproto.ConfigWindow.Width
        | xcffib.xproto.ConfigWindow.Height
    )
    connection.core.ConfigureWindow(window, configure, [40, 50, 360, 240])
    connection.flush()
    for _ in range(100):
        event = connection.poll_for_event()
        if isinstance(event, xcffib.xproto.MapNotifyEvent):
            break
        time.sleep(0.01)
    else:
        raise Failure("XWayland window was not mapped")

    connection.core.ConfigureWindow(window, configure, [70, 80, 400, 260])
    connection.core.GetInputFocus().reply()

    unmanaged = connection.generate_id()
    unmanaged_mask = xcffib.xproto.CW.OverrideRedirect | xcffib.xproto.CW.EventMask
    connection.core.CreateWindow(
        screen.root_depth,
        unmanaged,
        screen.root,
        15,
        25,
        180,
        100,
        0,
        xcffib.xproto.WindowClass.InputOutput,
        screen.root_visual,
        unmanaged_mask,
        [1, xcffib.xproto.EventMask.StructureNotify],
    )
    unmanaged_title = b"x11-unmanaged"
    connection.core.ChangeProperty(
        xcffib.xproto.PropMode.Replace,
        unmanaged,
        wm_name,
        string,
        8,
        len(unmanaged_title),
        unmanaged_title,
    )
    connection.core.MapWindow(unmanaged)
    connection.flush()
    for _ in range(100):
        attributes = connection.core.GetWindowAttributes(unmanaged).reply()
        if attributes.map_state == xcffib.xproto.MapState.Viewable:
            break
        time.sleep(0.01)
    else:
        raise Failure("override-redirect XWayland window was not mapped")
    connection.core.ConfigureWindow(unmanaged, configure, [30, 40, 220, 130])
    connection.core.GetInputFocus().reply()

    net_state = connection.core.InternAtom(
        False, len("_NET_WM_STATE"), "_NET_WM_STATE"
    ).reply().atom
    net_fullscreen = connection.core.InternAtom(
        False, len("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN"
    ).reply().atom
    message = xcffib.xproto.ClientMessageEvent.synthetic(
        32,
        window,
        net_state,
        ([1, net_fullscreen, 0, 1, 0], "=5I"),
    )
    event_mask = (
        xcffib.xproto.EventMask.SubstructureRedirect
        | xcffib.xproto.EventMask.SubstructureNotify
    )
    connection.core.SendEvent(False, screen.root, event_mask, message.pack())
    connection.flush()
    connection.core.GetInputFocus().reply()

    connection.core.UnmapWindow(window)
    connection.core.UnmapWindow(unmanaged)
    connection.core.DestroyWindow(window)
    connection.core.DestroyWindow(unmanaged)
    connection.flush()
    connection.disconnect()


ROLES = {
    "xdg": xdg_client,
    "xdg-lifecycle": xdg_lifecycle,
    "transient": transient,
    "keyboard": keyboard,
    "pointer": pointer,
    "foreign": foreign,
    "workspace": workspace,
    "output-power": output_power,
    "output-management": output_management,
    "layer": layer,
    "session-lock": session_lock,
    "text-input": text_input,
    "x11": x11,
}


def main() -> None:
    """Run one selected protocol role."""

    if len(sys.argv) < 2 or sys.argv[1] not in ROLES:
        raise Failure(f"usage: clients.py {'|'.join(ROLES)} [ARGS...]")
    ROLES[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
