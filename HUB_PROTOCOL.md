# Friends hub protocol v1

Hub support is independent of resume-v1. After authentication/resume, the server advertises `HUB_CAPS|hub-v1`; interested clients send `HUB_HELLO|hub-v1`. Old clients retain USERS_REQUEST/USERS_LIST, all existing game commands and invitations. New clients on old servers retain the existing invitation path. No database migration or game resume is introduced.

## Snapshots

`HUB_REQUEST` returns `HUB_BEGIN|`, zero or more `HUB_PLAYER|name|activity|game` and `HUB_LOBBY|id|game|host|seats|capacity|settings` rows, own participation (`HUB_OWN|none`, followed by `HUB_OWN|id|game|open|hostFlag|phase` when seated), then `HUB_END|`. Clients commit complete player/lobby lists at END, retaining search/scroll. Refresh requests are limited to one per second; UI refreshes every five seconds while visible or seated. At most 128 transports exist; rows remain below the 4096-byte input limit. Names and settings use existing server validation, never arbitrary client-supplied display strings.

Activity is Available, In lobby, Playing, or Spectating; only authenticated server-side membership is represented. Blackjack, Poker, Roulette, and Arena expose open LOBBY phase entries. Full entries remain visible. Chess stays direct-challenge/spectator only. IDs are monotonic and unique for the server process, stable across vector moves. All lobbies default invite-only; phase changes and host changes reset openness. Existing closure/host-transfer rules remain authoritative.

## Mutations and invitations

- `HUB_VISIBILITY|request|lobbyId|0-or-1`: host of a waiting lobby only.
- `HUB_JOIN|request|lobbyId`: revalidate visibility, phase, capacity and participant availability, then use the existing /accept admission handler. Never auto-start.
- `HUB_INVITE_PLAYER|request|game|username`: use existing host/challenge permission checks. `chess_spectate` is distinct from `chess`.
- `HUB_ACCEPT|request|invitationId` and `HUB_DECLINE|request|invitationId`: operate only on the current invitation.
- `HUB_RESULT|request|0-or-1|game|message`: correlated outcome. Last 16 results per transport are cached; duplicate request IDs return their original result. Request IDs are 1-32 alphanumeric/hyphen characters. Mutations are limited to 20/minute/transport; connection/session/authentication bounds remain in force.
- `HUB_INVITE|id|game|inviter|occupied|capacity|settings|play-or-spectate|remainingSeconds`: current invitation details. Details refresh when capacity/settings change without extending expiry; admission is rechecked on acceptance. Zero capacity identifies a spectator invitation.
- `HUB_INVALID|id|reason` and `HUB_NOTICE|message`: invalidation/status. Legacy clients receive existing challenge packets and cancellation notices.

Only one incoming invitation exists. Invitations expire after two minutes using steady-clock time, and invalidate on disconnect, successful participation elsewhere, closed/started/full lobbies, or changed hosting. Poker's existing between-hands direct invitation rule is retained; browser joining is waiting-lobby only.

No outgoing action is queued across transports. An eight-second missing confirmation becomes uncertain delivery; the client obtains a complete snapshot before another mutation. Admission is single-threaded in the server loop, so two contenders for the final seat are checked sequentially and cannot exceed capacity. Tokens, credentials, private cards and bets never enter hub snapshots.

Tests: `Test-Hub.ps1` on Windows; on Linux compile tests/hub.cpp plus accounts.cpp using the existing SSL/crypto/sodium/sqlite3/pthread libraries. Existing recovery and Roulette fixtures remain required.
