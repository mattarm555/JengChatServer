# Chess clock protocol v1

Timed chess is opt-in per client and enforced by the server. Old clients see no change: they never probe the capability, never receive `CHESS_CLOCK`, and an untimed `/chess <user>` behaves exactly as before. New clients on old servers get no `CHESS_CAPS` reply and must not offer time controls there. No schema migration, persistent state or game resume is introduced.

## Capability

After `AUTH_OK` (including `AUTH_OK` after a session resume), a client that can show clocks sends `CLIENT_CAPS|chess-clock-v1`. The server answers `CHESS_CAPS|clock-v1` and marks that transport clock-capable. The flag belongs to the transport, like `hub-v1` and `lounge-v1`: a resumed session is a new transport, so the client probes again after the resume's `AUTH_OK`. Games are not resumed (RECOVERY_PROTOCOL.md), so no clock carries across a reconnect. Unknown `CLIENT_CAPS` values stay ignored, and the probe does not imply resume-v1 or hub-v1.

## Starting a timed game

- `/chess <username> <minutes>+<increment>`, e.g. `/chess Ana 5+3`. Minutes are 1-60, the increment is 0-60 seconds, written as one or two digits each. Anything else (`0+0`, `61+0`, `5`, `5+3 extra`) returns `ERR|Time control must be minutes+increment, e.g. 5+3 (1-60 minutes, 0-60 second increment).` and no challenge is sent. With no time argument the challenge is untimed, as before.
- The time control is stored with the pending challenge and applied at `/accept`. Declining, cancelling or expiring the challenge clears it. A timed challenge's notices mention it: `CHESS_NOTICE|Challenge sent to Ana (5+3). You will play White.` and `GAME|*** Bob challenged you to Chess (5+3)! ***`.
- Hub invitations: `HUB_INVITE_PLAYER|request|chess|username|5+3` carries the same time control (the fifth field is accepted for `chess` only, and validated by the same `/chess` handler, so a bad value returns `HUB_RESULT|request|0|chess|Time control must be ...`). The `HUB_INVITE` settings field reads `Chess 5+3` instead of `Standard chess`. Only send the five-field form to servers that answered `CHESS_CAPS|clock-v1`; older servers answer `Invalid hub request.` The four-field form stays untimed. `HUB_ACCEPT` uses `/accept`, so the clock applies the same way.
- At `/accept` a timed game starts only if both players' transports are clock-capable. Otherwise the game starts untimed and both players (and any spectators) get `CHESS_NOTICE|<name>'s client doesn't support clocks; playing untimed.` once per non-capable player. A timed start also sends `CHESS_NOTICE|Time control 5+3. Clocks start after each side's first move.`
- JengEngine games (`CHESS_ENGINE|level`) are always untimed.

## Clock rules

Both sides start with the base time. No clock runs until each side has made its first move (the Lichess rule; there is no first-move abort timer). From then on the side to move's clock runs. When a move is accepted, the mover is charged the elapsed time and, if their clock was running, the increment is added; then the opponent's clock starts. Increments are therefore not added for each side's first move. A move that arrives after the mover's time has reached zero is not played: the game ends on time instead. Time is measured with the server's steady clock in milliseconds; network latency is not compensated.

The server checks running clocks on every loop tick (about every 4 ms). At zero the game ends through the normal `CHESS_END` path:

- `CHESS_NOTICE|<flagged player> ran out of time.`
- `CHESS_END|TIME - <winner> wins on time.|board|white|black|turn|WHITE-or-BLACK-or-SPECTATOR`

The text contains "wins", so old clients show it like other wins. A flag always awards the win (insufficient-material draws on time are not implemented).

## CHESS_CLOCK

`CHESS_CLOCK|whiteMs|blackMs|running|baseSec|incSec[|graceMs]`

- `whiteMs`, `blackMs`: remaining milliseconds at the moment the packet was built, never negative.
- `running`: `WHITE`, `BLACK`, or `-` when no clock runs (before both first moves, and after the game ends).
- `baseSec`, `incSec`: the time control, e.g. `300|3` for 5+3.
- `graceMs`: optional sixth field with milliseconds left before the running side begins losing time. The grace server includes it on every clock packet, with zero when absent or frozen. A five-field packet from an older server means zero grace.

After an accepted capture (including en passant) or a move giving check, the opponent receives a two-second delay before their clock counts down. Quiet moves grant zero delay. This applies equally to both sides regardless of animation settings. Existing first-move rules still apply.

A move made during the delay consumes no clock time. The normal increment still applies after a running turn. Unused delay is discarded on a move, and the next turn receives a fresh delay only if that move captures or gives check. State resends and spectator joins report the remaining delay without restarting it. Ending a game freezes its clock with zero remaining delay.

New clients subtract elapsed time only after the reported grace is exhausted. Older clock clients can ignore the additional field and remain protocol-compatible, but their displayed countdown can temporarily run ahead during grace until the next server update. The server enforces the actual flag time.

It is sent only for timed games and only to clock-capable transports (players and spectators):

- after every `CHESS_STATE` sent to that transport, so after the game starts, after every move, when a spectator joins, and whenever the state is re-sent (for example when a spectator leaves);
- at game end (checkmate, stalemate, resignation, flag), frozen with `running` = `-`, sent just before `CHESS_END`. When an opponent disconnects, the remaining player gets the frozen clock before their `CHESS_END`.

Clients count down locally from the latest packet for display and treat the server's `CHESS_END` as the only authority on a flag.

## Tests

Windows: `Test-ChessClock.ps1` (MSYS2 UCRT64; adds `-lws2_32`). Linux: `g++ -std=c++17 -O1 -DJENGCHAT_TEST_PORT=54991 tests/chess_clock.cpp accounts.cpp -o build/chess-clock-tests -lssl -lcrypto -lsodium -lsqlite3 -pthread`, also run by Server recovery validation. The test replaces the server's clock function to step time, and covers the capability probe, valid and invalid time controls, untimed games staying clock-free, the first-move rule, elapsed time and increment, flags from the loop tick and from a late move, the final clock on resignation, untimed fallback with a non-capable player on either side, capable and non-capable spectators, untimed engine games, hub invitations with and without a time control, and decline cleanup. Existing hub, recovery, Roulette and lounge tests remain required.
