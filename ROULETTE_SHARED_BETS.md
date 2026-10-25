# Roulette shared bet markers

The client already supports colored per-player markers and RLT_TABLE_BETS. The server previously sent only each recipient's RLT_BETS, leaving shared markers empty.

Every existing sendRouletteState broadcast now includes RLT_TABLE_BETS with the full seated-table snapshot: playerName~type~value~amount, separated by semicolons; an empty table is -. RLT_STATE and each player's original RLT_BETS remain unchanged. Existing broadcast points cover placement, clearing, readiness, joins/leaves and next-round resets. The snapshot is queued before RLT_STATE to avoid briefly displaying a new round with stale markers.

Old clients ignore the additive packet. Current clients use the existing player-list order for their six-color legend and marker colors. No payout, balance or betting-rule change is included.

This source change requires deployment of the server before remote players can see each other's stakes. Repository cleanup does not deploy or restart the live service.

Run ./Test-Roulette.ps1 on Windows with the MSYS2 UCRT64 toolchain with OpenSSL, libsodium and SQLite. The fixture compiles the actual server broadcast code, captures output queues with fake disconnected sockets, and never starts a listener, uses credentials or writes account data. The production Linux build remains ./build_server.sh and requires OpenSSL, libsodium and SQLite development libraries.

The fixture also writes build/roulette-snapshot.txt. The client Test-RouletteClient.ps1 decodes that exact snapshot and verifies all six marker colors at their actual rendered pixels.
