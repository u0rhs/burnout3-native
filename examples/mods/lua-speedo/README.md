# lua-speedo: a speedometer in Lua

A script mod (the mod kit's Lua scripting): during a race it draws a box with the player's speed, as the game's
speedometer shows it, and the race time, read from the script API's read-only views. It logs the race start and each
lap time. It only reads and draws (`gameplay = no`), so the game and its replays are the same with it.
[Tutorial 06](../../../docs/modding/tutorials/06-a-lua-script.md) explains each line; the API is in
[lua-reference.md](../../../docs/modding/lua-reference.md).

No build step: copy the folder into the mods folder and turn it on in the launcher. A PC SETTINGS > MODS row
("Speedometer box") puts the box at the top left or the bottom left. No game data in it.
