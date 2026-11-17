#!/usr/bin/env python3
"""Cut over the staged JengChat roster build. Run as the current server user."""
import hashlib
import os
from pathlib import Path
import secrets
import signal
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import time

BASE = "93974f61ce5af8b8b4cef8e1bd97a9597d75e842"
PORT = 54000


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def processes_for(binary):
    matches = []
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal():
            continue
        try:
            if (process / "exe").readlink() == binary and process.stat().st_uid == os.getuid():
                matches.append(process)
        except (OSError, PermissionError):
            continue
    return matches


def launch(session, binary, args, cwd, env):
    subprocess.run(["screen", "-dmS", session, str(binary), *args], cwd=cwd, env=env, check=True)


def wait_for_exit(process, timeout=15):
    deadline = time.monotonic() + timeout
    while (process / "exe").exists():
        if time.monotonic() >= deadline:
            raise RuntimeError("The old process has not exited")
        time.sleep(0.2)


def check_roster(token, fingerprint):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with socket.create_connection(("127.0.0.1", PORT), timeout=2) as conn:
        with ctx.wrap_socket(conn) as tls:
            if hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest() != fingerprint:
                raise RuntimeError("The server presented an unexpected certificate")
            tls.sendall(b"STATUS|" + token.encode("ascii") + b"\n")
            with tls.makefile("rb") as response:
                if response.readline(4096) != b"STATUS\n":
                    raise RuntimeError("The status reply was not accepted")
                for _ in range(129):
                    line = response.readline(4096)
                    if line == b"\n":
                        return
                    if not line.endswith(b"\n"):
                        break
                raise RuntimeError("The status reply was incomplete")


def main():
    home = Path.home()
    old = (home / "jengchat-server" / "jengchat_server").resolve(strict=True)
    stage = (home / "roster-stage").resolve(strict=True)
    candidate = (stage / "jengchat_server").resolve(strict=True)
    commit = subprocess.check_output(["git", "-C", str(stage), "rev-parse", "HEAD"], text=True).strip()
    if commit != BASE or not os.access(candidate, os.X_OK):
        raise RuntimeError("The staged candidate does not match the expected revision")
    matches = processes_for(old)
    if len(matches) != 1 or processes_for(candidate):
        raise RuntimeError("Expected exactly one original JengChat server and no candidate process")
    running = matches[0]
    cwd = (running / "cwd").resolve(strict=True)
    args = [os.fsdecode(a) for a in (running / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")]
    if len(args) != 4:
        raise RuntimeError("Unexpected argument count for the live server")
    cert, key, db = [(cwd / path).resolve(strict=True) for path in args[1:]]
    if not all(path.is_file() for path in (cert, key, db)):
        raise RuntimeError("A required server file is missing")
    args = [str(cert), str(key), str(db)]
    cert_der = subprocess.check_output(["openssl", "x509", "-in", str(cert), "-outform", "DER"])
    fingerprint = hashlib.sha256(cert_der).hexdigest()

    os.umask(0o077)
    backup = Path(tempfile.mkdtemp(prefix="jengchat-roster-backup.", dir=home))
    token = secrets.token_hex(32)
    token_path = backup / "status-token"
    token_path.write_text(token + "\n", encoding="ascii")
    with sqlite3.connect(db.as_uri() + "?mode=ro", uri=True, timeout=5) as src:
        with sqlite3.connect(backup / "accounts.sqlite3") as dst:
            src.backup(dst)
            if dst.execute("PRAGMA quick_check").fetchall() != [("ok",)]:
                raise RuntimeError("Account database backup did not pass quick_check")
    print(f"Original SHA-256: {sha256(old)}", flush=True)
    print(f"Candidate SHA-256: {sha256(candidate)}", flush=True)
    print(f"Account backup and private token directory: {backup}", flush=True)
    session = f"jengchat-roster-{os.getpid()}"
    env = dict(os.environ, JENGCHAT_STATUS_TOKEN=token)

    # Reconfirm the process before sending the stop signal.
    if processes_for(old) != [running]:
        raise RuntimeError("The live process changed while preparing the cutover")
    try:
        os.kill(int(running.name), signal.SIGTERM)
        wait_for_exit(running)
        launch(session, candidate, args, cwd, env)
        for _ in range(25):
            try:
                if processes_for(candidate):
                    check_roster(token, fingerprint)
                    break
            except (OSError, RuntimeError):
                pass
            time.sleep(0.4)
        else:
            raise RuntimeError("Candidate roster check failed")
    except BaseException:
        subprocess.run(["screen", "-S", session, "-X", "quit"], capture_output=True)
        for process in processes_for(candidate):
            wait_for_exit(process)
        if not processes_for(old):
            launch(f"jengchat-rollback-{os.getpid()}", old, args, cwd, os.environ)
        print("Cutover failed; original server retained or restarted. Check the listener.", flush=True)
        raise
    print(f"Roster ready on port {PORT}. Screen session: {session}")
    print(f"JENGCHAT_CERT_SHA256={fingerprint}")
    print(f"Private token saved at {token_path}. Keep its contents out of chat.")


if __name__ == "__main__":
    main()
