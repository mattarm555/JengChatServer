# Chat recovery protocol v1

All packets use the existing newline-delimited, certificate-verified TLS transport. Existing AUTH_LOGIN/AUTH_REGISTER and AUTH_OK payloads are unchanged. The server follows authentication with SERVER_CAPS|Automatic chat reconnection available. An older client may display this harmless notice and otherwise works unchanged.

A supporting client replies CLIENT_CAPS|resume-v1 then SESSION_START|. SESSION_TOKEN|<64 lowercase hex characters> carries a random 256-bit credential. Only its SHA-256 hash and canonical account name are retained by the server; the client keeps the token in memory. The server caps sessions at 1024 and connections at the existing 128 total/12 per IP. Authenticated repeated issuance is idempotent. Resume attempts use the existing bounded rate-limit machinery (20/IP/minute).

On a new TLS connection AUTH_RESUME|<token> replaces the old transport after full disconnect/game cleanup. AUTH_OK and capabilities confirm success; rejected/expired tokens return AUTH_ERROR and require password sign-in. It resumes chat only. No commands or missed messages are replayed, and seats/matches are not restored.

Sessions expire after 24 hours absolute or five minutes detached. Logout (SESSION_LOGOUT|) revokes the token and closes the connection; password/name changes and deletion revoke all account tokens and send SESSION_REVOKED|. An unreachable server cannot receive logout: the client still destroys its token and the detached server entry expires. Server restarts intentionally lose all sessions.

Opted-in connections exchange PING|/PONG| every 15 seconds and time out after 45 seconds with no input. Legacy clients are not subject to this new heartbeat. Certificates are never bypassed. Private keys, passwords and tokens are not logged or persisted. Session deletion does not modify account records.

Tests: Test-Recovery.ps1 exercises production cleanup/session code; the client tests/verify_recovery.py runs the real TLS server and native client through a loopback interruption proxy. Tests use generated disposable certificates/accounts/databases and never contact the live server.
