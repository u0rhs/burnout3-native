-- lua-speedo: the player's speed and the race timer in a box over the race, from the script API's read-only views
-- (docs/modding/lua-reference.md). It only reads and draws (mod.ini: gameplay = no).

local format = require("format")   -- scripts/format.lua: the time as M:SS.hh

-- a row on PC SETTINGS > MODS: where the box goes
b3.settings.add{
    key = "place", label = "Speedometer box", kind = "choice",
    values = { "Top left", "Bottom left" }, default = "Top left",
    description = "Where the box with the speed and the race time is drawn.",
}

local laps = {}   -- the lap times so far, for the log at the end

b3.on("race_start", function(e)
    laps = {}
    b3.log("race start at VBlank " .. e.vblank)
end)

b3.on("lap", function(e)
    laps[#laps + 1] = format.time(e.time)
    b3.log("lap " .. e.lap .. ": " .. format.time(e.time))
end)

b3.on_frame(function()
    local player, score = b3.view.player(), b3.view.score()
    if not player or not score then return end   -- not in a race: nothing to draw
    local y = b3.settings.get("place") == "Top left" and 72 or 330
    b3.draw.quad(16, y, 172, 52, b3.rgba(0, 0, 0, 170))                 -- the box
    b3.draw.quad(16, y, 4, 52, b3.rgba(255, 150, 0))                     -- an orange edge
    b3.draw.text(28, y + 8, 14, b3.rgba(255, 220, 60), string.format("SPEED %3d", player.speed_shown))
    b3.draw.text(28, y + 30, 12, b3.rgba(255, 255, 255), "TIME " .. format.time(score.time))
end)

b3.on_shutdown(function()
    if #laps > 0 then b3.log("laps this run: " .. table.concat(laps, ", ")) end
end)
