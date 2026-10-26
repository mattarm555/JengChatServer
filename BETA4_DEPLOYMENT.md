# Beta 4 hub rollout — staged, awaiting live approval

Prior production server: commit `4d744c8e3ebcd066f32c1a4cd5c5b297a0ff130a`, Debian x86_64 binary SHA-256 `c7285cef2f02b871b40a88133865f9db7a44d109be4a186f93448593929569ab`. Last reported PID 1287360, detached Screen `jengchat-beta3`. Recheck PID, executable, cwd and listener before deployment; these are observations, not permanent identifiers.

Existing VM: Debian 13 x86_64, user mattarm555, original working directory `/home/mattarm555/jengchat-server`. Keep `server.crt`, `server.key`, and `accounts.sqlite3` there. Production port remains 54000. No configuration/schema migration; lobbies and resume sessions remain in memory.

## Stage without stopping production

After reviewed merge, fetch the exact commit recorded by the beta 4 handoff. Extract `git archive <commit>` to a new `/home/mattarm555/jengchat-releases/<commit>` directory; never overwrite an existing stage. Run build_server.sh and compile/run tests/recovery.cpp, tests/roulette_broadcast.cpp and tests/hub.cpp with accounts.cpp and the existing libraries. Write COMMIT.txt, dependency versions and SHA-256 of the Debian-built jengchat_server. Do not copy certificates, account databases or keys into release/source artifacts. Ubuntu CI binaries are evidence; native Debian build is preferred, as in beta 3.

## Approval and restart

Present exact old/new commits and binary hashes, resolved active process/Screen, paths, backup destination and expected interruption. Stop here for user approval. Restart disconnects users and ends/removes existing games; choose an idle time.

After approval: create a private timestamped backup directory outside the repository. Preserve current binary, launch command/Screen configuration and source revision. Stop the actual current server gracefully and confirm port 54000 is free. Use Python sqlite3 backup API on the stopped database, check `PRAGMA integrity_check` equals `ok`, and preserve WAL/SHM evidence if present. Keep certificate/key originals unchanged. Launch the new staged executable in detached Screen `jengchat-beta4`, with the original directory as cwd and the same absolute certificate/key/database arguments. Confirm process stability, port ownership and verified external TLS.

## Health checks and rollback

Verify beta 3 password login/chat and multiplayer entry; then beta 4 presence, private lobby default, host opening, joining, invitation accept/decline and ordinary disconnect cleanup with disposable accounts. No game reconnect is claimed. Keep current production feed at beta 3 until server checks pass and the installed beta 3-to-beta 4 update is verified.

If health fails, stop only the new server and restart `/home/mattarm555/jengchat-releases/4d744c8e3ebcd066f32c1a4cd5c5b297a0ff130a/jengchat_server` with the original cwd and absolute certificate/key/database arguments in detached Screen. Preserve the current database because the schema is unchanged; restore a verified backup only for demonstrated corruption after preserving current files. Server restart invalidates resume tokens and requires sign-in. Beta 4 clients retain manual login and legacy invitations on the old server. If a public client release later needs repair, use a higher version; never overwrite published assets.
