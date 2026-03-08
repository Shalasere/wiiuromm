#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parents[1]
HARNESS_ROOT = ROOT / ".harness"
SESSIONS_DIR = HARNESS_ROOT / "sessions"


class HarnessError(RuntimeError):
    def __init__(self, kind: str, message: str, details: Optional[dict[str, Any]] = None):
        super().__init__(message)
        self.kind = kind
        self.details = details or {}


@dataclass
class BackendCapabilities:
    isolatedUserDir: bool
    headlessLaunch: bool
    deterministicInputReplay: bool
    saveStateBoot: bool
    screenshotApi: bool
    windowAutomationRequired: bool


@dataclass
class BuildArtifact:
    target: str
    outputKind: str
    path: str
    symbolsPath: Optional[str]
    metadata: dict[str, Any]


@dataclass
class LaunchedProcess:
    argv: list[str]
    log_path: str
    pid: int
    popen: subprocess.Popen[Any]


class EventSink:
    def __init__(self, path: Path):
        self.path = path

    def emit(self, event_type: str, **fields: Any) -> None:
        payload = {"ts": utc_now(), "type": event_type, **fields}
        with self.path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(payload, sort_keys=True) + "\n")


class HarnessSession:
    def __init__(self, backend: str, scenario: str, keep_session: bool):
        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H-%M-%S.%fZ")
        self.id = f"{stamp}-{uuid.uuid4().hex[:8]}"
        self.path = SESSIONS_DIR / self.id
        self.runtime = self.path / "runtime"
        self.logs = self.path / "artifacts" / "logs"
        self.screenshots = self.path / "artifacts" / "screenshots"
        self.emulator_user = self.path / "emulator-user"
        self.stage = self.path / "stage"
        self.events_file = self.path / "events.ndjson"
        self.session_file = self.path / "session.json"
        self.keep_session = keep_session
        self.backend = backend
        self.scenario = scenario

        for p in [
            self.path,
            self.runtime,
            self.logs,
            self.screenshots,
            self.emulator_user,
            self.stage,
        ]:
            p.mkdir(parents=True, exist_ok=True)

        self.events = EventSink(self.events_file)

    def write_summary(self, summary: dict[str, Any]) -> None:
        self.session_file.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")


class EmulatorBackend:
    backend_name: str

    def capabilities(self) -> BackendCapabilities:
        raise NotImplementedError

    def doctor(self) -> dict[str, Any]:
        raise NotImplementedError

    def prepare_session(self, session: HarnessSession) -> None:
        raise NotImplementedError

    def stage_app_build(self, session: HarnessSession, build: BuildArtifact) -> Path:
        raise NotImplementedError

    def launch(self, session: HarnessSession, launch_spec: dict[str, Any]) -> LaunchedProcess:
        raise NotImplementedError

    def wait_for_ready(self, session: HarnessSession, proc: LaunchedProcess) -> dict[str, Any]:
        raise NotImplementedError

    def send_input(self, session: HarnessSession, input_script: dict[str, Any]) -> None:
        _ = (session, input_script)

    def capture(self, session: HarnessSession, name: str) -> dict[str, Any]:
        return capture_display(session, name)

    def reset(self, session: HarnessSession, proc: LaunchedProcess, mode: str) -> None:
        _ = mode
        self.stop(session, proc)

    def stop(self, session: HarnessSession, proc: LaunchedProcess) -> dict[str, Any]:
        _ = session
        return stop_process(proc)

    def collect_artifacts(self, session: HarnessSession) -> dict[str, Any]:
        return {
            "session": str(session.path),
            "events": str(session.events_file),
            "screenshots": str(session.screenshots),
            "logs": str(session.logs),
        }


class DolphinBackend(EmulatorBackend):
    backend_name = "wii-dolphin"

    def __init__(self) -> None:
        self.bin = shutil.which("dolphin-emu") or shutil.which("dolphin")

    def capabilities(self) -> BackendCapabilities:
        return BackendCapabilities(
            isolatedUserDir=True,
            headlessLaunch=True,
            deterministicInputReplay=True,
            saveStateBoot=True,
            screenshotApi=False,
            windowAutomationRequired=False,
        )

    def doctor(self) -> dict[str, Any]:
        return {
            "backend": self.backend_name,
            "binary": self.bin,
            "available": bool(self.bin),
            "capabilities": asdict(self.capabilities()),
        }

    def prepare_session(self, session: HarnessSession) -> None:
        if not self.bin:
            raise HarnessError("launch_failed", "dolphin binary not found")

    def stage_app_build(self, session: HarnessSession, build: BuildArtifact) -> Path:
        src = Path(build.path)
        dst = session.stage / src.name
        shutil.copy2(src, dst)
        return dst

    def launch(self, session: HarnessSession, launch_spec: dict[str, Any]) -> LaunchedProcess:
        stage_path = Path(launch_spec["stage_path"])
        cmd = [
            self.bin or "dolphin-emu",
            "--user",
            str(session.emulator_user),
            "--exec",
            str(stage_path),
        ]
        if launch_spec.get("batch", True):
            cmd.append("--batch")
        for kv in launch_spec.get("config", []):
            cmd.extend(["--config", kv])
        movie = launch_spec.get("movie")
        if movie:
            cmd.extend(["--movie", movie])
        save_state = launch_spec.get("save_state")
        if save_state:
            cmd.extend(["--save_state", save_state])

        log_path = session.logs / "dolphin.log"
        f = log_path.open("w", encoding="utf-8")
        popen = subprocess.Popen(
            cmd,
            cwd=ROOT,
            stdout=f,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=bool(launch_spec.get("detach", False)),
        )
        session.events.emit("launch", backend=self.backend_name, argv=cmd)
        return LaunchedProcess(argv=cmd, log_path=str(log_path), pid=popen.pid, popen=popen)

    def wait_for_ready(self, session: HarnessSession, proc: LaunchedProcess) -> dict[str, Any]:
        timeout_s = 12.0
        start = time.monotonic()
        while time.monotonic() - start < timeout_s:
            if proc.popen.poll() is not None:
                raise HarnessError(
                    "launch_failed",
                    "dolphin exited before ready",
                    {"returncode": proc.popen.returncode, "log": proc.log_path},
                )
            if Path(proc.log_path).exists():
                session.events.emit(
                    "ready",
                    backend=self.backend_name,
                    process_alive=True,
                    ready_after_ms=int((time.monotonic() - start) * 1000),
                )
                return {"process_alive": True, "ready_after_ms": int((time.monotonic() - start) * 1000)}
            time.sleep(0.2)
        raise HarnessError("ready_timeout", "dolphin ready timeout", {"timeout_s": timeout_s})


class CemuBackend(EmulatorBackend):
    backend_name = "wiiu-cemu"

    def __init__(self) -> None:
        self.bin = shutil.which("cemu")

    def capabilities(self) -> BackendCapabilities:
        return BackendCapabilities(
            isolatedUserDir=False,
            headlessLaunch=False,
            deterministicInputReplay=False,
            saveStateBoot=False,
            screenshotApi=False,
            windowAutomationRequired=True,
        )

    def doctor(self) -> dict[str, Any]:
        return {
            "backend": self.backend_name,
            "binary": self.bin,
            "available": bool(self.bin),
            "capabilities": asdict(self.capabilities()),
        }

    def prepare_session(self, session: HarnessSession) -> None:
        if not self.bin:
            raise HarnessError("launch_failed", "cemu binary not found")

    def stage_app_build(self, session: HarnessSession, build: BuildArtifact) -> Path:
        src = Path(build.path)
        dst = session.stage / src.name
        shutil.copy2(src, dst)
        return dst

    def launch(self, session: HarnessSession, launch_spec: dict[str, Any]) -> LaunchedProcess:
        stage_path = Path(launch_spec["stage_path"])
        cmd = [self.bin or "cemu", "--game", str(stage_path)]
        if launch_spec.get("fullscreen", False):
            cmd.append("--fullscreen")

        log_path = session.logs / "cemu.log"
        f = log_path.open("w", encoding="utf-8")
        popen = subprocess.Popen(
            cmd,
            cwd=ROOT,
            stdout=f,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=bool(launch_spec.get("detach", False)),
        )
        session.events.emit("launch", backend=self.backend_name, argv=cmd)
        return LaunchedProcess(argv=cmd, log_path=str(log_path), pid=popen.pid, popen=popen)

    def wait_for_ready(self, session: HarnessSession, proc: LaunchedProcess) -> dict[str, Any]:
        timeout_s = 12.0
        start = time.monotonic()
        while time.monotonic() - start < timeout_s:
            if proc.popen.poll() is not None:
                raise HarnessError(
                    "launch_failed",
                    "cemu exited before ready",
                    {"returncode": proc.popen.returncode, "log": proc.log_path},
                )
            # Degraded readiness: process-alive only.
            if time.monotonic() - start > 2.0:
                ready = {"process_alive": True, "window_seen": None, "first_frame_seen": None,
                         "ready_after_ms": int((time.monotonic() - start) * 1000)}
                session.events.emit("ready", backend=self.backend_name, **ready)
                return ready
            time.sleep(0.2)
        raise HarnessError("ready_timeout", "cemu ready timeout", {"timeout_s": timeout_s})


def build_artifact(target: str, profile: str, session: HarnessSession) -> BuildArtifact:
    if target == "wii":
        cmd = ["make", "-C", str(ROOT / "wii")]
        output = ROOT / "wii" / "wiiuromm.dol"
        kind = "dol"
    elif target == "wiiu":
        cmd = ["make", "-C", str(ROOT)]
        output = ROOT / "wiiuromm.rpx"
        kind = "rpx"
    else:
        raise HarnessError("build_failed", f"unsupported target: {target}")

    log_path = session.logs / f"build-{target}.log"
    run_checked(cmd, log_path=log_path)
    if not output.exists():
        raise HarnessError("build_failed", f"missing build output: {output}", {"log": str(log_path)})

    return BuildArtifact(
        target=target,
        outputKind=kind,
        path=str(output),
        symbolsPath=None,
        metadata={
            "gitCommit": git_commit(),
            "buildTime": utc_now(),
            "profile": profile,
        },
    )


def scenario_def(backend: str, scenario: str) -> dict[str, Any]:
    scenarios = {
        "wii-dolphin": {
            "smoke_boot": {
                "target": "wii",
                "launch": {
                    "batch": True,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                    "use_movie": True,
                },
                "steps": [
                    {"wait_ms": 2500},
                    {"assert_process_alive": True},
                    {"capture": "wii_smoke"},
                ],
            },
            "smoke_boot_headed": {
                "target": "wii",
                "launch": {
                    "batch": False,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                    "use_movie": True,
                },
                "steps": [
                    {"wait_ms": 3500},
                    {"assert_process_alive": True},
                    {"capture": "wii_smoke_headed"},
                ],
            },
            "navigate_menu_headed": {
                "target": "wii",
                "launch": {
                    "batch": False,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                    "use_movie": True,
                },
                "steps": [
                    {"wait_ms": 1800},
                    {"capture": "menu_step_1"},
                    {"wait_ms": 2400},
                    {"capture": "menu_step_2"},
                    {"assert_process_alive": True},
                ],
            },
            "navigate_menu_headed_strict": {
                "target": "wii",
                "launch": {
                    "batch": False,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                    "use_movie": True,
                },
                "steps": [
                    {"wait_ms": 1200},
                    {"capture": "nav_frame_a"},
                    {"wait_ms": 5200},
                    {"capture": "nav_frame_b"},
                    {"assert_images_different": ["nav_frame_a", "nav_frame_b"]},
                    {"assert_process_alive": True},
                ],
            },
            "observe_no_input_headed": {
                "target": "wii",
                "launch": {
                    "batch": False,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                },
                "steps": [
                    {"wait_ms": 1200},
                    {"capture": "nav_frame_a"},
                    {"wait_ms": 5200},
                    {"capture": "nav_frame_b"},
                    {"assert_process_alive": True},
                ],
            },
            "manual_control_headed": {
                "target": "wii",
                "launch": {
                    "batch": False,
                    "config": [
                        "Display.RenderToMain=True",
                        "Interface.ConfirmStop=False",
                    ],
                },
                "steps": [
                    {"wait_ms": 1200},
                    {"assert_process_alive": True},
                ],
            },
        },
        "wiiu-cemu": {
            "boot_menu": {
                "target": "wiiu",
                "launch": {"fullscreen": False},
                "steps": [
                    {"wait_ms": 4000},
                    {"assert_process_alive": True},
                    {"capture": "wiiu_boot"},
                ],
            }
        },
    }
    try:
        return scenarios[backend][scenario]
    except KeyError as exc:
        raise HarnessError("artifact_missing", f"unknown scenario: {backend}/{scenario}") from exc


def run_scenario(
    backend_name: str, scenario_name: str, timeout_s: int, keep_session: bool, handoff: bool
) -> int:
    session = HarnessSession(backend_name, scenario_name, keep_session)
    backend = get_backend(backend_name)
    scenario = scenario_def(backend_name, scenario_name)

    started = time.monotonic()
    proc: Optional[LaunchedProcess] = None
    failure: Optional[dict[str, Any]] = None
    captures: dict[str, Path] = {}

    session.events.emit("session_start", backend=backend_name, scenario=scenario_name)
    try:
        backend.prepare_session(session)
        build = build_artifact(scenario["target"], "debug", session)
        session.events.emit("build", target=build.target, output=build.path, outputKind=build.outputKind)
        staged = backend.stage_app_build(session, build)

        launch_spec = dict(scenario["launch"])
        launch_spec["stage_path"] = str(staged)
        if handoff:
            launch_spec["detach"] = True

        if launch_spec.pop("use_movie", False):
            movie = session.stage / "wii_smoke.dtm"
            run_checked([str(ROOT / "scripts" / "runtime-wii-make-dtm.sh"), str(movie)],
                        log_path=session.logs / "movie-gen.log")
            launch_spec["movie"] = str(movie)
            session.events.emit("movie_ready", path=str(movie))

        proc = backend.launch(session, launch_spec)
        backend.wait_for_ready(session, proc)

        for step in scenario["steps"]:
            if time.monotonic() - started > timeout_s:
                raise HarnessError("process_hung", "scenario timeout", {"timeout_s": timeout_s})
            if "wait_ms" in step:
                ms = int(step["wait_ms"])
                session.events.emit("wait", ms=ms)
                time.sleep(ms / 1000.0)
            elif "assert_process_alive" in step:
                alive = proc.popen.poll() is None if proc else False
                if not alive:
                    raise HarnessError("process_crashed", "process died during scenario", {"log": proc.log_path if proc else None})
                session.events.emit("assert_pass", name="process_alive")
            elif "capture" in step:
                cap_name = str(step["capture"])
                cap = backend.capture(session, cap_name)
                artifact = cap.get("artifact")
                if artifact:
                    captures[cap_name] = Path(artifact)
                session.events.emit("capture", **cap)
            elif "assert_images_different" in step:
                names = step["assert_images_different"]
                if not isinstance(names, list) or len(names) != 2:
                    raise HarnessError("assertion_failed", "assert_images_different requires [name_a, name_b]")
                a = captures.get(str(names[0]))
                b = captures.get(str(names[1]))
                if not a or not b or not a.exists() or not b.exists():
                    raise HarnessError("artifact_missing", "missing capture artifact for diff", {"names": names})
                if sha256_file(a) == sha256_file(b):
                    raise HarnessError("assertion_failed", "captured frames are identical", {"a": str(a), "b": str(b)})
                session.events.emit("assert_pass", name="images_different", a=str(a), b=str(b))
            elif "assert_process_exited" in step:
                exited = proc.popen.poll() is not None if proc else False
                if not exited:
                    raise HarnessError("assertion_failed", "process still running")
                session.events.emit("assert_pass", name="process_exited", returncode=proc.popen.returncode if proc else None)
            else:
                raise HarnessError("artifact_missing", f"unknown step: {step}")

        if handoff:
            pid_file = session.runtime / "pid.txt"
            pid_file.write_text(f"{proc.pid}\n", encoding="utf-8")
            session.events.emit("handoff", pid=proc.pid, pid_file=str(pid_file))
        else:
            exit_summary = backend.stop(session, proc)
            session.events.emit("stop", **exit_summary)

    except HarnessError as exc:
        failure = {"kind": exc.kind, "message": str(exc), "details": exc.details}
        session.events.emit("failure", **failure)
        if proc is not None:
            backend.stop(session, proc)
    except Exception as exc:  # noqa: BLE001
        failure = {"kind": "artifact_missing", "message": f"unexpected error: {exc}", "details": {}}
        session.events.emit("failure", **failure)
        if proc is not None:
            backend.stop(session, proc)

    result = {
        "backend": backend_name,
        "scenario": scenario_name,
        "status": "pass" if failure is None else "fail",
        "failure": failure,
        "handoff": handoff,
        "sessionDir": str(session.path),
        "artifacts": backend.collect_artifacts(session),
        "pid": (proc.pid if (proc is not None and handoff and failure is None) else None),
        "durationMs": int((time.monotonic() - started) * 1000),
    }
    session.write_summary(result)

    if failure and not keep_session:
        # Keep failed sessions for diagnostics regardless of flag.
        pass

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if failure is None else 1


def doctor(backends: list[str]) -> int:
    SESSIONS_DIR.mkdir(parents=True, exist_ok=True)
    screenshot_backend = detect_screenshot_backend()
    report = {
        "ts": utc_now(),
        "workspace": str(HARNESS_ROOT),
        "writable": os.access(SESSIONS_DIR, os.W_OK),
        "screenshotBackend": screenshot_backend,
        "backends": [get_backend(b).doctor() for b in backends],
    }
    print(json.dumps(report, indent=2, sort_keys=True))

    missing = [b["backend"] for b in report["backends"] if not b["available"]]
    return 1 if missing else 0


def stop_process(proc: LaunchedProcess) -> dict[str, Any]:
    p = proc.popen
    if p.poll() is None:
        try:
            p.send_signal(signal.SIGTERM)
            p.wait(timeout=5)
        except Exception:  # noqa: BLE001
            p.kill()
            p.wait(timeout=5)
    return {"pid": proc.pid, "returncode": p.returncode, "log": proc.log_path}


def capture_display(session: HarnessSession, name: str) -> dict[str, Any]:
    png = session.screenshots / f"{name}.png"
    backend = detect_screenshot_backend()

    if backend == "grim" and os.environ.get("WAYLAND_DISPLAY"):
        run_best_effort(["grim", str(png)])
    elif backend == "import" and os.environ.get("DISPLAY"):
        run_best_effort(["import", "-window", "root", str(png)])
    elif backend == "ffmpeg" and os.environ.get("DISPLAY"):
        display = os.environ.get("DISPLAY", ":0")
        run_best_effort([
            "ffmpeg", "-y", "-loglevel", "error", "-f", "x11grab", "-video_size", "1280x720",
            "-i", display, "-frames:v", "1", str(png)
        ])

    if png.exists() and png.stat().st_size > 0:
        return {"artifact": str(png), "kind": "screenshot"}

    note = session.screenshots / f"{name}.txt"
    note.write_text("screenshot unavailable for current display/tooling\n", encoding="utf-8")
    return {"artifact": str(note), "kind": "note"}


def detect_screenshot_backend() -> Optional[str]:
    if shutil.which("grim"):
        return "grim"
    if shutil.which("import"):
        return "import"
    if shutil.which("ffmpeg"):
        return "ffmpeg"
    return None


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def run_checked(cmd: list[str], log_path: Path) -> None:
    with log_path.open("w", encoding="utf-8") as f:
        proc = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        raise HarnessError("build_failed", f"command failed: {' '.join(cmd)}", {"log": str(log_path), "returncode": proc.returncode})


def run_best_effort(cmd: list[str]) -> None:
    try:
        subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True, check=False)
    except Exception:  # noqa: BLE001
        return


def git_commit() -> str:
    try:
        out = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        return out
    except Exception:  # noqa: BLE001
        return "unknown"


def get_backend(name: str) -> EmulatorBackend:
    if name == "wii-dolphin":
        return DolphinBackend()
    if name == "wiiu-cemu":
        return CemuBackend()
    raise HarnessError("artifact_missing", f"unknown backend: {name}")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(prog="romm-harness")
    sub = p.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("doctor", help="verify backend/tooling readiness")
    d.add_argument("--backend", choices=["wii-dolphin", "wiiu-cemu", "all"], default="all")

    r = sub.add_parser("run", help="run a scenario")
    r.add_argument("--backend", required=True, choices=["wii-dolphin", "wiiu-cemu"])
    r.add_argument("--scenario", required=True)
    r.add_argument("--timeout", type=int, default=45)
    r.add_argument("--keep-session", action="store_true")
    r.add_argument("--handoff", action="store_true", help="leave emulator running for manual control")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.cmd == "doctor":
        if args.backend == "all":
            return doctor(["wii-dolphin", "wiiu-cemu"])
        return doctor([args.backend])
    if args.cmd == "run":
        return run_scenario(args.backend, args.scenario, args.timeout, args.keep_session, args.handoff)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
