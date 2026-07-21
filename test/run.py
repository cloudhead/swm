#!/usr/bin/env python3

"""Run swm's unit and headless integration tests."""

from __future__ import annotations

import glob
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path


SCRIPT = Path(__file__).resolve()
TESTDIR = SCRIPT.parent
ROOT = TESTDIR.parent
BUILD = Path(os.environ.get("SWM_TEST_BUILD", TESTDIR / ".build")).resolve()
SOURCE = BUILD / "source"
BIN = BUILD / "bin"
PROFILES = BUILD / "profiles"
LOGS = BUILD / "logs"
CHILDREN: list[subprocess.Popen] = []
CURRENT_COMMAND: list[str] = []
CURRENT_LOG: Path | None = None


class Failure(RuntimeError):
    """Report a test failure without an internal traceback."""


def command_text(command: list[str]) -> str:
    """Format a command for diagnostics."""

    return shlex.join(map(str, command))


def run(*args: object, env: dict[str, str] | None = None) -> str:
    """Run a command and return its standard output."""

    global CURRENT_COMMAND
    command = [str(arg) for arg in args]
    CURRENT_COMMAND = command
    result = subprocess.run(command, text=True, capture_output=True, env=env)
    if result.returncode:
        output = result.stdout + result.stderr
        raise Failure(
            f"command: {command_text(command)}\nstatus: {result.returncode}\n{output}"
        )
    return result.stdout


def spawn(log: Path, *args: object, env: dict[str, str] | None = None) -> subprocess.Popen:
    """Start and track a background command."""

    global CURRENT_COMMAND, CURRENT_LOG
    command = [str(arg) for arg in args]
    CURRENT_COMMAND = command
    CURRENT_LOG = log
    log.parent.mkdir(parents=True, exist_ok=True)
    stream = log.open("w")
    process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env=env)
    stream.close()
    CHILDREN.append(process)
    return process


def terminate(process: subprocess.Popen) -> None:
    """Terminate one tracked process."""

    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    if process in CHILDREN:
        CHILDREN.remove(process)


def cleanup_processes() -> None:
    """Terminate all tracked processes in reverse order."""

    for process in reversed(CHILDREN[:]):
        terminate(process)


def tail(path: Path, lines: int = 40) -> str:
    """Return the tail of a diagnostic file."""

    if not path.exists():
        return ""
    return "\n".join(path.read_text(errors="replace").splitlines()[-lines:])


def eventually(description: str, predicate, timeout: float = 2.0):
    """Wait until a predicate returns a truthy value."""

    deadline = time.monotonic() + timeout
    last: Exception | None = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError, IndexError) as error:
            last = error
        time.sleep(0.02)
    detail = f": {last}" if last else ""
    raise Failure(f"timeout waiting for {description}{detail}")


def test(name: str, body) -> None:
    """Run and report one named test."""

    global CURRENT_COMMAND, CURRENT_LOG
    CURRENT_COMMAND = []
    CURRENT_LOG = None
    progress = os.environ.get("SWM_TEST_PROGRESS")
    print(f"{name} ... ", end="", flush=True)
    if progress:
        with open(progress, "a") as stream:
            stream.write(f"{name} ... ")
    try:
        body()
    except Exception:
        print("FAIL", flush=True)
        if progress:
            with open(progress, "a") as stream:
                stream.write("FAIL\n")
        raise
    print("ok", flush=True)
    if progress:
        with open(progress, "a") as stream:
            stream.write("ok\n")


def generate_python_protocols(pkgconfig_executable: str) -> None:
    """Generate Python client protocol bindings."""

    wlproto = Path(
        run(pkgconfig_executable, "--variable=pkgdatadir", "wayland-protocols").strip()
    )
    xmls = [
        Path("/usr/share/wayland/wayland.xml"),
        wlproto / "stable/xdg-shell/xdg-shell.xml",
        wlproto / "staging/ext-workspace/ext-workspace-v1.xml",
        wlproto / "staging/ext-session-lock/ext-session-lock-v1.xml",
        wlproto / "unstable/idle-inhibit/idle-inhibit-unstable-v1.xml",
        wlproto
        / "unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml",
        wlproto / "unstable/pointer-constraints/pointer-constraints-unstable-v1.xml",
        wlproto / "unstable/relative-pointer/relative-pointer-unstable-v1.xml",
        wlproto / "unstable/text-input/text-input-unstable-v3.xml",
        ROOT / "protocols/input-method-unstable-v2.xml",
        ROOT / "protocols/virtual-keyboard-unstable-v1.xml",
        ROOT / "protocols/wlr-foreign-toplevel-management-unstable-v1.xml",
        ROOT / "protocols/wlr-layer-shell-unstable-v1.xml",
        ROOT / "protocols/wlr-output-power-management-unstable-v1.xml",
        ROOT / "protocols/wlr-virtual-pointer-unstable-v1.xml",
    ]
    package = BUILD / "python" / "protocols"
    shutil.rmtree(package, ignore_errors=True)
    package.mkdir(parents=True)
    (package / "__init__.py").touch()
    run("pywayland-scanner", "-o", package, "-i", *xmls)

    # pywayland 0.4 generates a circular import for ext-workspace-v1. The
    # group events refer to objects that already exist, so they do not need an
    # interface constructor. Dropping that annotation breaks the cycle while
    # preserving the wire signature.
    workspace_group = package / "ext_workspace_v1/ext_workspace_group_handle_v1.py"
    generated = workspace_group.read_text()
    generated = generated.replace(
        "from .ext_workspace_handle_v1 import ExtWorkspaceHandleV1\n", ""
    ).replace(
        "Argument(ArgumentType.Object, interface=ExtWorkspaceHandleV1)",
        "Argument(ArgumentType.Object)",
    )
    workspace_group.write_text(generated)


def prepare_suite(unit: bool, integration: bool) -> None:
    """Prepare runtime files for the selected tests."""

    binaries = [
        BIN / name
        for name, selected in (("unit", unit), ("swm", integration), ("swmctl", integration))
        if selected
    ]
    missing = [binary for binary in binaries if not binary.exists()]
    if missing:
        raise Failure(f"test binary not built: {missing[0]}; run tests through make")
    for directory in (PROFILES, LOGS):
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
    if unit:
        stubs = BUILD / "stubs"
        shutil.rmtree(stubs, ignore_errors=True)
        stubs.mkdir()
        stub_commands = (
            "dbus-update-activation-environment",
            "foot",
            "waybar",
            "swaybg",
            "sh",
            "wl-paste",
        )
        for name in stub_commands:
            (stubs / name).symlink_to("/bin/true")
    if integration:
        for executable in ("pywayland-scanner", "timeout"):
            if not shutil.which(executable):
                raise Failure(f"required tool not found: {executable}")
        generate_python_protocols(os.environ.get("PKG_CONFIG", "pkg-config"))
        source_config = (SOURCE / "config.h").read_text()
        if re.search(r"#define\s+(?:MODKEY|MOD)\s+WLR_MODIFIER_ALT", source_config):
            modkey = 8
        elif re.search(r"#define\s+(?:MODKEY|MOD)\s+WLR_MODIFIER_LOGO", source_config):
            modkey = 64
        else:
            raise Failure("unsupported modifier in config.def.h")
        (BUILD / "modkey").write_text(str(modkey))


def run_unit() -> None:
    """Run every compositor unit test in an isolated process."""

    runtime = BUILD / "unit-runtime"
    shutil.rmtree(runtime, ignore_errors=True)
    runtime.mkdir()
    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = str(PROFILES / "unit-%m-%p.profraw")
    env["SWM_TEST_STUB_PATH"] = str(BUILD / "stubs")
    env["SWM_TEST_DIR"] = str(runtime)
    for name in run(BIN / "unit", "--list", env=env).splitlines():
        test(name, lambda name=name: run(BIN / "unit", name, env=env))
    shutil.rmtree(runtime)


def current_title() -> str:
    """Return the compositor's published title."""

    path = Path(os.environ["XDG_RUNTIME_DIR"]) / "swm-title"
    return path.read_text().rstrip("\n") if path.exists() else ""


def wait_title(title: str) -> None:
    """Wait for the compositor to publish a title."""

    eventually(f"title {title!r}", lambda: current_title() == title)


def client_command(role: str, *args: object) -> list[str]:
    """Return a Python protocol-client command."""

    return [sys.executable, str(TESTDIR / "clients.py"), role, *map(str, args)]


def client_log(name: str) -> Path:
    """Return a fixture client's log path."""

    return Path(os.environ["SWM_TEST_LOG_DIR"]) / f"{name}.log"


def start_client(name: str, color: str, fullscreen: bool = False) -> subprocess.Popen:
    """Start an XDG client and wait for its geometry report."""

    report = Path(os.environ["SWM_TEST_DIR"]) / f"{name}.geometry"
    env = os.environ.copy()
    env["SWM_CLIENT_REPORT"] = str(report)
    args: list[object] = [color, name]
    if fullscreen:
        args.append("fullscreen")
    process = spawn(client_log(name), *client_command("xdg", *args), env=env)
    wait_title(name)
    eventually(f"report {report}", lambda: report.exists() and report.stat().st_size)
    return process


def geometry(name: str) -> list[int]:
    """Return a client's reported geometry."""

    path = Path(os.environ["SWM_TEST_DIR"]) / f"{name}.geometry"
    return list(map(int, path.read_text().split()))


def protocol(role: str, *args: object) -> str:
    """Run one native-protocol action implemented in Python."""

    return run(*client_command(role, *args), env=os.environ.copy())


def virtual_keyboard(key: object, modifiers: int = 0) -> None:
    """Send one configured key combination."""

    configured = int((BUILD / "modkey").read_text()) | modifiers
    protocol("keyboard", key, configured)


def held_pointer_click(name: str, x: int, button: str | None = None) -> None:
    """Click while the compositor modifier is held by another client."""

    ready = Path(os.environ["SWM_TEST_DIR"]) / f"{name}.ready"
    release = Path(os.environ["SWM_TEST_DIR"]) / f"{name}.release"
    env = os.environ.copy()
    env["SWM_KEYBOARD_READY"] = str(ready)
    env["SWM_KEYBOARD_RELEASE"] = str(release)
    configured = int((BUILD / "modkey").read_text())
    keyboard = spawn(
        client_log(name),
        *client_command("keyboard", "none", configured),
        env=env,
    )
    eventually("held keyboard readiness", lambda: ready.exists())
    args: list[object] = [x]
    if button:
        args.append(button)
    protocol("pointer", *args)
    release.write_text("release\n")
    eventually("held keyboard exit", lambda: keyboard.poll() is not None)
    if keyboard in CHILDREN:
        CHILDREN.remove(keyboard)


def integration_session() -> None:
    """Exercise compositor behavior in a one-output session."""

    one = start_client("one", "ff5588dd")
    two = start_client("two", "ff55aa88")

    def publication() -> None:
        windows = (Path(os.environ["XDG_RUNTIME_DIR"]) / "swm-windows").read_text()
        if not re.search(r"^\d+,\d+ \d+x\d+ one$", windows, re.MULTILINE):
            raise Failure("missing published rectangle for one")
        if not re.search(r"^\d+,\d+ \d+x\d+ two$", windows, re.MULTILINE):
            raise Failure("missing published rectangle for two")
        before = current_title()
        virtual_keyboard(36)
        wait_title("two" if before == "one" else "one")

    test("window publication and focus", publication)

    def layouts() -> None:
        virtual_keyboard(33)
        eventually("max-stack geometry", lambda: geometry("one")[:2] == geometry("two")[:2])
        virtual_keyboard(33)
        focused = current_title()
        virtual_keyboard(33, 1)
        eventually("fullscreen enable", lambda: geometry(focused)[2] == 1)
        virtual_keyboard(33, 1)
        eventually("fullscreen disable", lambda: geometry(focused)[2] == 0)

    test("layouts and fullscreen", layouts)

    def workspaces() -> None:
        current = current_title()
        virtual_keyboard(20)
        virtual_keyboard(20)
        virtual_keyboard(3, 1)
        eventually("focus after moving client", lambda: current_title() not in {"", current})
        virtual_keyboard(3)
        wait_title(current)
        virtual_keyboard(2, 1)
        wait_title("")
        virtual_keyboard(2)
        wait_title(current)
        virtual_keyboard(103)
        wait_title("")
        virtual_keyboard(108)
        wait_title(current)

    test("workspaces", workspaces)

    def workspace_metadata() -> None:
        def field(name: str) -> str:
            return run(BIN / "swmctl", "workspace", "get", 3, name).removesuffix("\n")

        subscription_log = client_log("workspace-subscribe")
        subscriber = spawn(
            subscription_log,
            BIN / "swmctl",
            "workspace",
            "subscribe",
            "--format=waybar",
            env=os.environ.copy(),
        )
        eventually("initial workspace metadata", lambda: subscription_log.stat().st_size)
        run(BIN / "swmctl", "workspace", "set", 3, "title", "code")
        run(BIN / "swmctl", "workspace", "set", 3, "color", "#81a1c180")
        state = {
            "workspace": 3,
            "title": field("title"),
            "color": field("color"),
            "selected": field("selected") == "true",
        }
        if state != {
            "workspace": 3,
            "title": "code",
            "color": "#81a1c180",
            "selected": False,
        }:
            raise Failure(f"unexpected workspace metadata: {state!r}")
        listed = run(BIN / "swmctl", "workspace", "list").splitlines()
        if "3" not in listed:
            raise Failure(f"workspace missing from list: {listed!r}")
        run(BIN / "swmctl", "workspace", "set", 3, "color", "#00000000")
        if field("color"):
            raise Failure("zero RGBA color was not cleared")
        run(BIN / "swmctl", "workspace", "set", 3, "color", "#000000")
        if field("color") != "#000000":
            raise Failure("opaque black color was not retained")
        run(BIN / "swmctl", "workspace", "clear", 3, "color")
        if field("title") != "code" or field("color"):
            raise Failure("workspace color was not cleared")
        run(BIN / "swmctl", "workspace", "set", 3, "color", "#81a1c1")
        protocol("workspace", 3)
        eventually(
            "subscribed workspace metadata",
            lambda: "#81a1c1" in subscription_log.read_text()
            and "code" in subscription_log.read_text(),
        )
        run(BIN / "swmctl", "workspace", "clear", 3, "title")
        run(BIN / "swmctl", "workspace", "clear", 3, "color")
        if field("title") or field("color") or field("selected") != "true":
            raise Failure("workspace metadata was not cleared")
        protocol("workspace", 1)
        terminate(subscriber)

    test("workspace metadata", workspace_metadata)

    def layout_configuration() -> None:
        before_one = geometry("one")
        before_two = geometry("two")
        virtual_keyboard(36, 1)
        eventually(
            "client swap",
            lambda: geometry("one")[0] == before_two[0] and geometry("two")[0] == before_one[0],
        )
        for key in (28, 50, 19, 57):
            virtual_keyboard(key)
        eventually("master-top layout", lambda: geometry("one")[0] == geometry("two")[0])
        before = geometry("one")
        virtual_keyboard(35)
        eventually("master size change", lambda: geometry("one") != before)

    test("layout configuration", layout_configuration)

    def protocols() -> None:
        target = current_title()
        protocol("foreign", target, "fullscreen")
        eventually("foreign fullscreen", lambda: geometry(target)[2] == 1)
        protocol("foreign", target, "unfullscreen")
        eventually("foreign unfullscreen", lambda: geometry(target)[2] == 0)
        protocol("pointer", 100)
        protocol("pointer", 900)
        held_pointer_click("held-left", 100)
        held_pointer_click("held-right", 100, "right")
        for key, modifiers in ((51, 0), (52, 0), (51, 1), (52, 1), (57, 1), (43, 1)):
            virtual_keyboard(key, modifiers)
        protocol("output-power")
        protocol("output-management")

    test("control protocols", protocols)

    def isolation() -> None:
        protocol("workspace", 3)
        wait_title("")
        three = start_client("three", "ff8855aa")
        full = start_client("full", "ffaa5588", True)
        eventually("fullscreen state", lambda: geometry("full")[2] == 1)
        protocol("foreign", "full", "close")
        wait_title("three")
        protocol("workspace", 1)
        eventually("workspace protocol activation", lambda: current_title() in {"one", "two"})
        terminate(three)
        terminate(full)

    test("fullscreen isolation", isolation)

    def lifecycle() -> None:
        terminate(one)
        terminate(two)
        protocol("xdg-lifecycle", 2)
        protocol("transient", 2)
        protocol("layer", 2)
        protocol("session-lock", 2)
        protocol("text-input")
        protocol("x11")

    test("protocol lifecycle", lifecycle)
    terminate(one)
    terminate(two)


def integration_multioutput() -> None:
    """Exercise workspace movement and output removal."""

    client = start_client("multi", "ff778899")

    def multioutput() -> None:
        virtual_keyboard(105, 1)
        wait_title("")
        virtual_keyboard(106, 1)
        wait_title("multi")
        virtual_keyboard(105, 4)
        wait_title("")
        virtual_keyboard(105, 1)
        wait_title("multi")
        protocol("output-management", "disable-second")
        wait_title("multi")

    test("multiple outputs", multioutput)
    terminate(client)


def fixture_main(kind: str) -> None:
    """Run one fixture inside swm's process tree."""

    result = Path(os.environ["SWM_TEST_RESULT"])
    status = "ok"
    try:
        if kind == "session":
            integration_session()
        elif kind == "multioutput":
            integration_multioutput()
        else:
            raise Failure(f"unknown fixture: {kind}")
    except Exception as error:
        status = str(error)
    finally:
        cleanup_processes()
        result.write_text(status + "\n")
        os.kill(os.getppid(), signal.SIGTERM)
    if status != "ok":
        print(status, file=sys.stderr)
        raise SystemExit(1)


def run_fixture(kind: str, outputs: int) -> None:
    """Run one isolated headless compositor fixture."""

    runtime = BUILD / f"runtime-{kind}"
    state = BUILD / f"state-{kind}"
    fixture = BUILD / f"fixture-{kind}"
    logs = LOGS / kind
    for path in (runtime, state, fixture, logs):
        shutil.rmtree(path, ignore_errors=True)
        path.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    result = BUILD / f"{kind}.result"
    progress = BUILD / f"{kind}.progress"
    result.write_text("")
    progress.write_text("")
    env = os.environ.copy()
    env.update(
        {
            "XDG_RUNTIME_DIR": str(runtime),
            "XDG_STATE_HOME": str(state),
            "SWM_NO_AUTOSTART": "1",
            "WLR_BACKENDS": "headless",
            "WLR_RENDERER": "pixman",
            "WLR_LIBINPUT_NO_DEVICES": "1",
            "WLR_HEADLESS_OUTPUTS": str(outputs),
            "SWM_TEST_RESULT": str(result),
            "SWM_TEST_PROGRESS": str(progress),
            "SWM_TEST_DIR": str(fixture),
            "SWM_TEST_LOG_DIR": str(logs),
            "PYTHONPATH": str(BUILD / "python"),
            "LLVM_PROFILE_FILE": str(PROFILES / "integration-%m-%p.profraw"),
        }
    )
    log = LOGS / f"swm-{kind}.log"
    startup = f"{shlex.quote(sys.executable)} {shlex.quote(str(SCRIPT))} --fixture {kind}"
    command = ["timeout", "--foreground", "30", str(BIN / "swm"), "-s", startup]
    compositor = spawn(log, *command, env=env)
    progress_offset = 0
    while compositor.poll() is None:
        with progress.open() as stream:
            stream.seek(progress_offset)
            update = stream.read()
            progress_offset = stream.tell()
        if update:
            print(update, end="", flush=True)
        time.sleep(0.02)
    status = compositor.wait()
    CHILDREN.remove(compositor)
    with progress.open() as stream:
        stream.seek(progress_offset)
        update = stream.read()
    if update:
        print(update, end="", flush=True)
    outcome = result.read_text().strip()
    if status or outcome != "ok":
        raise Failure(
            f"fixture {kind} failed\nresult: {outcome!r}\nstatus: {status}\n"
            f"compositor log tail ({log}):\n{tail(log, 60)}"
        )
    contents = log.read_text(errors="replace")
    if re.search(r"pool exhausted|AddressSanitizer|runtime error:|protocol error", contents):
        raise Failure(f"fixture {kind} reported a runtime failure\n{tail(log, 60)}")
    for path in (runtime, state, fixture):
        shutil.rmtree(path)


def run_integration() -> None:
    """Run every integration fixture."""

    run_fixture("session", 1)
    run_fixture("multioutput", 2)


def coverage_report() -> None:
    """Merge profiles and report production coverage."""

    for executable in ("llvm-profdata", "llvm-cov"):
        if not shutil.which(executable):
            raise Failure(f"required coverage tool not found: {executable}")
    raw = glob.glob(str(PROFILES / "*.profraw"))
    if not raw:
        raise Failure("no LLVM coverage profiles were produced")
    profdata = BUILD / "coverage.profdata"
    run("llvm-profdata", "merge", "-sparse", *raw, "-o", profdata)
    report = run(
        "llvm-cov",
        "report",
        BIN / "swm",
        "-instr-profile",
        profdata,
        "-object",
        BIN / "unit",
        SOURCE / "swm.c",
        ROOT / "util.c",
    )
    files: list[str] = []
    total: list[str] | None = None
    for line in report.splitlines():
        fields = line.split()
        if len(fields) < 10:
            continue
        if fields[0] == "TOTAL":
            total = fields
        elif fields[0].endswith(".c"):
            files.append(f"{Path(fields[0]).name} {fields[9]}")
    if total is None:
        raise Failure("could not parse llvm-cov TOTAL line")
    percent = float(total[9].rstrip("%"))
    print(f"coverage: {', '.join(files)}, aggregate {percent:.2f}%")


def clean() -> None:
    """Remove all generated test artifacts."""

    shutil.rmtree(BUILD, ignore_errors=True)
    print(f"clean: removed {BUILD}")


def main() -> None:
    """Dispatch public runner modes and private fixture modes."""

    if len(sys.argv) == 3 and sys.argv[1] == "--fixture":
        fixture_main(sys.argv[2])
        return
    modes = {"unit", "integration", "coverage", "clean"}
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] not in modes):
        raise Failure("usage: ./test/run.py [unit|integration|coverage|clean]")
    mode = sys.argv[1] if len(sys.argv) == 2 else "all"
    if mode == "clean":
        clean()
        return
    run_units = mode in {"all", "unit", "coverage"}
    run_integrations = mode in {"all", "integration", "coverage"}
    prepare_suite(run_units, run_integrations)
    if run_units:
        run_unit()
    if run_integrations:
        run_integration()
    if mode == "coverage":
        coverage_report()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(error, file=sys.stderr)
        if CURRENT_COMMAND:
            print(f"last command: {command_text(CURRENT_COMMAND)}", file=sys.stderr)
        if CURRENT_LOG:
            print(f"log tail ({CURRENT_LOG}):\n{tail(CURRENT_LOG)}", file=sys.stderr)
        cleanup_processes()
        raise SystemExit(1)
