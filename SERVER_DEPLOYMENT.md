# Chat recovery deployment — approval required

This is a staged rollout. No live deployment is authorized yet. The binary, exact commit, source archive, SHA-256 manifest and dependency/runtime reports are produced by Server recovery validation on Ubuntu 22.04. The application still takes exactly three arguments: server certificate, private key and existing accounts SQLite database. Port 54000 and certificate trust remain unchanged. Recovery has no schema migration or persistent session database.

## Before requesting live deployment approval

Inspect the current host's OS/architecture, running command/service, service user, working directory, binary path, certificate/key paths and account database path. Record the existing source/binary hash and service configuration. Do not print the private key, passwords, tokens or database contents. Compare ldd requirements with the CI runtime report; rebuild the exact source on an equivalent staging OS if incompatible. Do not replace a service based on guessed paths or a guessed systemd unit name.

Prepare a timestamped release directory beside the existing deployment containing the verified new binary and manifests, owned by the existing service user. Run a second, loopback-only test instance built with JENGCHAT_TEST_PORT=54991, disposable accounts and a disposable test certificate. Confirm password login, legacy chat, native-client resume, game disconnect cleanup, heartbeat, and session rejection after restart. The test port is compile-time-only; it never changes the production build's listener.

Present the resolved service name, old/new commit or binary hash, actual paths, account backup destination, expected interruption and rollback command for approval. The restart will disconnect current users and end/remove their active games under the existing rules; arrange an idle time. Beta 3 remains unpublished until the server is live and verified.

## Deployment after approval

1. Stop the existing service. Back up its binary, launch/service configuration and accounts database in a timestamped private directory. With the service stopped, use SQLite's backup API and run PRAGMA integrity_check on the backup; copy any remaining WAL/SHM sidecars as evidence. Keep certificate/key originals in place with existing restrictive permissions. Do not copy them into release artifacts.
2. Verify staged binary/source checksums, update the existing launch reference atomically to the new release, and start the same service as the same user. Confirm listener, process stability and TLS identity from outside the host.
3. Use disposable test accounts to verify old-client password login/chat, beta 3 resume after a controlled dropped connection, single active transport, and rejected stale tokens. Confirm no schema changes and no secrets in logs. Check an actual game disconnect still ends/removes participation.
4. If health checks fail, stop the candidate, restore the old binary/launch reference and restart. Account data uses the same schema: retain the current database to avoid losing legitimate post-deployment changes. Restore the offline backup only for demonstrated corruption after separately preserving current files.
5. After health checks pass, publish beta 3 through the existing reviewed-tag/draft process. Perform the beta 2 -> beta 3 update and network-interruption pilot locally and on one friend's machine before broad announcement. Recheck appearance/audio settings when that machine is available.

## Recovery and monitoring

Watch service restarts, rejected-auth counts, active connection count and disconnect/reconnect behavior during the initial pilot. Do not add message-body/token/password telemetry. A server restart intentionally invalidates in-memory resume tokens, so clients request sign-in again. Rollback server code remains compatible with beta 3 manual login; restore the prior page/feed if needed. Already-installed clients require a higher fixed version, never overwritten assets or an assumed automatic downgrade.
