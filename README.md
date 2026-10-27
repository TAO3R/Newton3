# (TODO: your game's title)

Author: (TODO: your name)

Design: (TODO: In two sentences or fewer, describe what is new and interesting about your game.)

Networking: (TODO: How does your game implement client/server multiplayer? What messages are transmitted? Where in the code?)

Screen Shot:

![Screen Shot](screenshot.png)

How To Play:

Connect two clients to the same server. The 90-second round starts when both players join.
Players spawn at the upper-right and lower-right. Race to the only escape pod on the left.

- Mouse: aim.
- Space: use one jet burst (three per round).
- Hold E: grab a nearby object or anchor; release E to let go.
- Q: throw or push off toward the cursor, recoiling in the opposite direction.
- Hold F: board inside the marked area while moving slowly. Boarding takes three uninterrupted seconds.
- R: restart after the round ends (requires two connected players).
- Tab: toggle sprite bounds for debugging.

The first player to board wins. Leaving the area, moving too fast, or releasing F resets progress.
If time runs out, both players lose. A simultaneous finish is a draw.
A disconnect ends the round; a replacement connection starts a fresh round.

Windows local test: run dist/server.exe 11451, then run dist/client.exe localhost 11451 in two terminals.
For LAN play, replace localhost with the server computer's current IPv4 address.
Server and clients must be rebuilt from the same version because the message format has changed.

Sources: (TODO: list a source URL for any assets you did not create yourself. Make sure you have a license for the asset.)

This game was built with [NEST](NEST.md).

