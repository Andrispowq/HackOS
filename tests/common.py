"""Shared bounded subprocess/reporting helpers; no external Python packages."""
from pathlib import Path
import json
import os
import signal
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "out"

if not __debug__:
    raise SystemExit("Run tests without Python -O/PYTHONOPTIMIZE; checks must remain enabled.")


def command(args, *, cwd=ROOT, timeout=60, env=None):
    args = [str(x) for x in args]
    process = subprocess.Popen(args, cwd=cwd, env=env, start_new_session=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        raise subprocess.TimeoutExpired(args, timeout, output=output) from None
    return subprocess.CompletedProcess(args, process.returncode, output)


class Report:
    def __init__(self, name, directory):
        self.name, self.directory, self.results = name, Path(directory), []
        self.directory.mkdir(parents=True, exist_ok=True)

    def add(self, name, status, detail="", seconds=0):
        self.results.append(dict(name=name, status=status, detail=detail, seconds=seconds))
        print(f"{status.upper():7} {name}", flush=True)
        (self.directory / f"{name}.log").write_text(detail)

    def check(self, name, function):
        started = time.monotonic()
        try:
            detail = function() or ""
            self.add(name, "pass", detail, time.monotonic()-started)
        except (AssertionError, ValueError, struct.error) as exc:
            self.add(name, "fail", str(exc), time.monotonic()-started)
        except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
            self.add(name, "error", str(exc), time.monotonic()-started)

    def finish(self):
        failures = sum(r["status"] == "fail" for r in self.results)
        errors = sum(r["status"] in ("error", "blocked") for r in self.results)
        suite = ET.Element("testsuite", name=self.name, tests=str(len(self.results)),
                           failures=str(failures), errors=str(errors))
        for r in self.results:
            case = ET.SubElement(suite, "testcase", name=r["name"], classname=self.name,
                                 time=f'{r["seconds"]:.3f}')
            if r["status"] != "pass":
                kind = "failure" if r["status"] == "fail" else "error"
                ET.SubElement(case, kind, message=r["status"]).text = r["detail"]
        ET.ElementTree(suite).write(self.directory / "junit.xml", encoding="utf-8", xml_declaration=True)
        (self.directory / "results.json").write_text(json.dumps(self.results, indent=2))
        print(f"{self.name}: {len(self.results)-failures-errors} passed, {failures} failed, {errors} errors/blocked", flush=True)
        return 1 if failures or errors or not self.results else 0


def require_success(result, operation):
    if result.returncode:
        raise RuntimeError(f"{operation} exited {result.returncode}\n{result.stdout}")
    return result.stdout
