# Lounge presence protocol (`lounge-v1`)

The Godot client's lounge (its 3D GAMES home) places friends from the hub snapshot. With this capability, avatars of players whose lounge is open also move where those players really walk. The server is a relay only: nothing is stored beyond each client's last position, nothing is written to disk or logs, and no game state depends on it.

## Capability

- The client sends `CLIENT_CAPS|lounge-v1` after `SERVER_CAPS` (next to `CLIENT_CAPS|hub-v1`). Servers without this feature ignore unknown capabilities, so the client then never sends a `LOUNGE_` command.
- The server answers `LOUNGE_CAPS|lounge-v1` and marks the connection capable. A resumed or new connection must probe again.
- Clients that did not probe (the Raylib client, older Godot builds) never receive a `LOUNGE_` packet. If they send one anyway it is swallowed: no relay, no error, no `READY`.

## Client to server

| Line | Meaning |
| --- | --- |
| `LOUNGE_ENTER|` | The lounge is on screen. The server replies with `LOUNGE_POS` for everyone already in a lounge who has reported a position. |
| `LOUNGE_POS|x|z|yaw|anim` | Your avatar: x in ±12.5, z in ±9.5 (metres), yaw in ±3.1416 (radians), anim `idle`, `walk` or `sit`. Numbers are plain decimals of at most 8 characters (no exponents, no commas). Ignored unless entered. |
| `LOUNGE_EMOTE|id` | `wave`, `cheer` or `dance`. Ignored unless entered. |
| `LOUNGE_LEAVE|` | The lounge is no longer on screen. |

The Godot client sends positions at most 8 times a second while moving, the stop when it stops, and a refresh every 2 seconds while still. Receivers drop avatars not heard from for 6 seconds.

## Server to client

| Line | Meaning |
| --- | --- |
| `LOUNGE_POS|name|x|z|yaw|anim` | Another player's avatar. `name` is the server's account name, never client text. Numbers are rewritten with two decimals. |
| `LOUNGE_EMOTE|name|id` | Another player's emote. |
| `LOUNGE_GONE|name` | That player's lounge closed (`LOUNGE_LEAVE`, disconnect, or the old name after an account rename). |

Packets go only to other capable connections whose lounge is open.

## Limits

- Positions: a burst of 4, refilled at 10 per second per connection; extra lines are dropped.
- Emotes: one per 0.8 seconds per connection.
- Invalid lines are dropped silently.
- A receiver with more than 64 KiB of unsent output misses position updates instead of being disconnected by the 2 MiB output limit.
- A rename sends `LOUNGE_GONE|old` and the last position under the new name.

## Tests

`Test-Lounge.ps1` (Windows) builds `tests/lounge.cpp` against `server.cpp` and checks the probe, legacy isolation, the snapshot on entering, canonical rewriting, spoofing and junk, rate limits, emotes, backlogged receivers, rename, leaving and disconnecting.
