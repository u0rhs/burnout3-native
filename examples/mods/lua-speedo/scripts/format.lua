-- format.lua: text for the speedometer box (required by main.lua as require("format")).
local M = {}

-- seconds -> "M:SS.hh" ("-:--.--" for the game's "no time", FLT_MAX after being out of a race)
function M.time(t)
    if t == nil or t >= 3.0e38 or t < 0 then return "-:--.--" end
    local hundredths = math.floor(t * 100 + 0.5)
    return string.format("%d:%02d.%02d", hundredths // 6000, (hundredths // 100) % 60, hundredths % 100)
end

return M
