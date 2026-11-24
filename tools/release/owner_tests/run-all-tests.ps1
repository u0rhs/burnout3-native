# run-all-tests.ps1 -- ownertests: the scripted test sessions in a real window, one after another, no input needed
# (tools/release/owner_tests/README.txt). Started by run-all-tests.bat. Windows PowerShell 5.1 (every Windows 10 / 11);
# nothing is downloaded or installed.
#
#   run-all-tests.bat                 every run (about 20 minutes)
#   run-all-tests.bat 5 7a 7b         only these (ids or names: run-all-tests.bat list)
#   run-all-tests.bat list            the runs
#   -Game <folder or disc image>      the game data (default: the launcher's choice, launcher.ini game_dir)
#   -Exe <native_play.exe>            the game program (default: found beside this folder)
#   -Offscreen                        no window (a check of this runner itself; the numbers then mean nothing)
#   -Summarize <results\<time>>       the table again from a finished run's folder (nothing is played)
#
# Each run has its own scratch folder under results\<time>\: its user folder (pc_settings.ini written for the run, its
# logs, screenshots), a copy of the test card (card\ beside this script). Your own settings, saves, mods and logs are not
# read or written. At the end: test-results-<time>.zip beside this script, and a table in this window.
[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Only, [string]$Game = "", [string]$Exe = "", [switch]$Offscreen,
      [string]$Summarize = "")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$scen = Get-Content -Raw -Encoding UTF8 (Join-Path $here "scenarios.json") | ConvertFrom-Json
$runs = @($scen.runs)

function Read-RunLog([string]$dir) {
    $log = ""
    foreach ($f in @("stdout.txt", "stderr.txt")) { $fp = Join-Path $dir $f; if (Test-Path -LiteralPath $fp) { $log += [IO.File]::ReadAllText($fp) + "`n" } }
    return $log
}
# A run's numbers from its perf log (B3_PERF_LOG: fps a second, frame_p99) and its log (the race motion line, the
# stalls line, the env time lines)
function Measure-Run([string]$dir, [string]$log) {
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $fps = $null; $low = $null
    $perf = Join-Path $dir "perf.txt"
    if (Test-Path -LiteralPath $perf) {
        $names = $null; $fr = 0; $tm = 0.0; $p99w = 0.0; $p99n = 0
        foreach ($l in [IO.File]::ReadAllLines($perf)) {
            if ($l.StartsWith("# t_s")) { $names = $l.Substring(2).Split(" ", [StringSplitOptions]::RemoveEmptyEntries); continue }
            if ($l.StartsWith("#") -or -not $names) { continue }
            $v = $l.Split(" ", [StringSplitOptions]::RemoveEmptyEntries)
            $n = [int]$v[1]
            if ($n -lt 20) { continue }   # (loading screens, the start)
            $f1 = [double]::Parse($v[[array]::IndexOf($names, "fps")], $inv)
            if ($f1 -le 0) { continue }
            $fr += $n; $tm += $n / $f1
            $i99 = [array]::IndexOf($names, "frame_p99")
            if ($i99 -ge 0 -and $i99 -lt $v.Count) { $p99w += $n * [double]::Parse($v[$i99], $inv); $p99n += $n }
        }
        if ($tm -gt 0) { $fps = $fr / $tm }
        if ($p99n -gt 0 -and $p99w -gt 0) { $low = 1000.0 / ($p99w / $p99n) }
    }
    # the window's own race-only figures (the perf log's seconds include the unpaced front end)
    $wr = [regex]::Matches($log, "window race frames: (\d+), ([\d.]+) fps; ms p50/p95/p99: loop ([\d.]+)/([\d.]+)/([\d.]+)")
    if ($wr.Count) {
        $w = $wr[$wr.Count - 1]
        $fps = [double]::Parse($w.Groups[2].Value, $inv)
        $lp99 = [double]::Parse($w.Groups[5].Value, $inv)
        if ($lp99 -gt 0) { $low = 1000.0 / $lp99 }
    }
    $mo = [regex]::Matches($log, "race motion: (\d+) frames; the game-time step against the ideal p50 ([\d.]+) % p95 ([\d.]+) % p99 ([\d.]+) %; held (\d+) frames")
    $motion = if ($mo.Count) { $m = $mo[$mo.Count - 1]; "{0}/{1} %, held {2}" -f $m.Groups[3].Value, $m.Groups[4].Value, $m.Groups[5].Value } else { "-" }
    $st = [regex]::Match($log, "native_play: stalls: (\d+) times .*?the sound ran dry (\d+) times")
    $stalls = if ($st.Success) { $st.Groups[1].Value } else { "-" }
    $dry = if ($st.Success) { $st.Groups[2].Value } else { "-" }
    $et = [regex]::Matches($log, "env time: ([\d.]+) ms")
    $envms = if ($et.Count) { "{0:N2}" -f (($et | ForEach-Object { [double]::Parse($_.Groups[1].Value, $inv) } | Measure-Object -Average).Average) } else { "" }
    return @{
        fps = $(if ($fps) { "{0:N1}" -f $fps } else { "-" }); low = $(if ($low) { "{0:N1}" -f $low } else { "-" })
        motion = $motion; stalls = $stalls; dry = $dry; envms = $envms
    }
}
function New-Row($r, [string]$result, $num, [string]$secs) {
    [pscustomobject]@{
        Run = $r.id; Name = $r.name; Result = $result; "FPS avg" = $num.fps; "1% low" = $num.low
        "Motion p95/p99" = $num.motion; Stalls = $num.stalls; "Sound dry" = $num.dry; "Env ms" = $num.envms; Seconds = $secs
    }
}

if ($Summarize) {   # the table again from a finished results folder (nothing is run)
    $rows = @()
    foreach ($r in $runs) {
        $dir = Join-Path $Summarize ("{0}-{1}" -f $r.id, $r.name)
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $vf = Join-Path $dir "verdict.txt"
        $res = if (Test-Path -LiteralPath $vf) { ([IO.File]::ReadAllText($vf)).Split(" ")[0].Trim() } else { "?" }
        $sf = Join-Path $dir "seconds.txt"
        $secs = if (Test-Path -LiteralPath $sf) { ([IO.File]::ReadAllText($sf)).Trim() } else { "" }
        $rows += New-Row $r $res (Measure-Run $dir (Read-RunLog $dir)) $secs
    }
    $rows | Format-Table -AutoSize | Out-String -Width 200
    exit 0
}
if ($Only -and $Only[0] -eq "list") {
    "Runs (run-all-tests.bat <id or name> ... runs only those):"
    foreach ($r in $runs) { "  {0,-4} {1,-20} {2}" -f $r.id, $r.name, $r.title }
    exit 0
}
if ($Only) {
    $runs = @($runs | Where-Object { $Only -contains $_.id -or $Only -contains $_.name })
    if (-not $runs) { Write-Host "No run is called $($Only -join ', '): run-all-tests.bat list"; exit 1 }
}

# --- the game program and the game data ---
if (-not $Exe) {
    $cands = @((Join-Path $here "native_play.exe"), (Join-Path (Split-Path $here) "native_play.exe")) +
             @(Get-ChildItem -Path (Split-Path $here) -Filter "native_play.exe" -Recurse -Depth 1 -ErrorAction SilentlyContinue |
               Where-Object { $_.DirectoryName -notmatch '\\debug$' } | ForEach-Object { $_.FullName })
    $Exe = $cands | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) { Write-Host "native_play.exe not found: run-all-tests.bat -Exe <path>"; exit 1 }
$exeDir = Split-Path $Exe
function Read-Ini([string]$path, [string]$key) {
    if (-not (Test-Path -LiteralPath $path)) { return "" }
    foreach ($l in [IO.File]::ReadAllLines($path, [Text.Encoding]::UTF8)) {
        if ($l -match "^\s*$key\s*=\s*(.*?)\s*$") { return $Matches[1] }
    }
    return ""
}
# rcprep: the player's own data folder (portable beside the game, else %APPDATA%\b3port\native): the launcher's choices
$pref = @((Join-Path $exeDir "appdata\native"), (Join-Path $env:APPDATA "b3port\native")) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$remember = Join-Path $here "game.txt"   # an answer given once (below)
if (-not $Game -and $pref) { $Game = Read-Ini (Join-Path $pref "launcher.ini") "game_dir" }
if (-not $Game -and (Test-Path -LiteralPath $remember)) { $Game = ([IO.File]::ReadAllText($remember)).Trim() }
while (-not $Game -or -not (Test-Path -LiteralPath $Game)) {
    Write-Host "Where is your Burnout 3 game data? (the folder with SLUS_210.50, or the .iso / .bin / .chd file; the launcher's Game Data page shows it)"
    $Game = (Read-Host "Path").Trim('"', ' ')
    if (-not $Game) { exit 1 }
    if (Test-Path -LiteralPath $Game) { [IO.File]::WriteAllText($remember, $Game) }
}
# the memory card: card\ beside this script when the bundle has one, else a copy of the player's own saves (read only:
# each run works on its own copy; the original folder is never written)
$card = Join-Path $here "card"
if (-not (Test-Path -LiteralPath $card)) {
    $saves = if ($pref) { Read-Ini (Join-Path $pref "launcher.ini") "saves_dir" } else { "" }
    if (-not $saves -and $pref) { $saves = Join-Path $pref "saves" }
    if (-not $saves -or -not ((Test-Path -LiteralPath (Join-Path $saves "profile-1.sav")) -or (Test-Path -LiteralPath (Join-Path $saves "profile-1")))) {
        Write-Host "No save with a first profile was found ($saves): start the game once, make a profile, then run this again."; exit 1
    }
    $card = $saves
}

$running = @(Get-Process -Name native_play -ErrorAction SilentlyContinue)
if ($running) {
    Write-Host "The game is already running (native_play.exe, $($running.Count)x): close it first, the runs need the graphics card to themselves."
    exit 1
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$resRoot = Join-Path $here "results\$stamp"
New-Item -ItemType Directory -Force -Path $resRoot | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
function Quote([string]$a) { if ($a -match '[\s"]') { '"' + ($a -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"' } else { $a } }

Write-Host ("{0} run(s) with {1}; game data {2}" -f $runs.Count, $Exe, $Game)
Write-Host "Watch the window; do not press anything. Each run ends by itself.`n"
$rows = @()
foreach ($r in $runs) {
    $dir = Join-Path $resRoot ("{0}-{1}" -f $r.id, $r.name)
    $user = Join-Path $dir "user"
    New-Item -ItemType Directory -Force -Path $user | Out-Null
    Copy-Item -Recurse -LiteralPath $card -Destination (Join-Path $dir "card")
    [IO.File]::WriteAllLines((Join-Path $user "pc_settings.ini"), [string[]]@($r.ini), $utf8)
    $envs = @{}
    foreach ($p in $r.env.PSObject.Properties) { $envs[$p.Name] = [string]$p.Value }
    $envs["B3PORT_USER_DIR"] = $user
    $envs["B3NATIVE_SAVES_ROOT"] = (Join-Path $dir "card")
    $envs["B3NATIVE_MAX_VBLANKS"] = [string]$r.end_vblanks
    $envs["B3_PERF_LOG"] = (Join-Path $dir "perf.txt")
    $envs["B3_WINDOW_SHOTS"] = (@($r.shots) -join ",")
    $envs["B3NATIVE_RUN_UNFOCUSED"] = "1"   # (a click on this console must not hold the game)
    $a = @("--game", $Game, "--keys", $r.keys, "--record-pad", (Join-Path $dir "pads.txt"))
    if ($r.keys2) { $a += @("--keys2", $r.keys2) }
    if ($Offscreen) { $a += @("--offscreen", "--no-pace", "--render-args", ("--settings " + (Join-Path $user "pc_settings.ini"))) }
    else { $a += @("--pace-from", [string]$r.pace_from) }
    $argLine = ($a | ForEach-Object { Quote $_ }) -join " "
    $saved = @{}
    foreach ($k in $envs.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, $envs[$k], "Process") }
    Write-Host ("[{0}] {1} ..." -f $r.id, $r.title) -NoNewline
    $t0 = Get-Date
    $p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $exeDir -PassThru -NoNewWindow `
         -RedirectStandardOutput (Join-Path $dir "stdout.txt") -RedirectStandardError (Join-Path $dir "stderr.txt")
    $null = $p.Handle   # (Windows PowerShell: without the handle taken while it runs, ExitCode stays empty)
    $limit = 600   # seconds: a run that has not ended by then is stopped (its PID only)
    if (-not $p.WaitForExit($limit * 1000)) { $p.Kill(); $p.WaitForExit(); $killed = $true } else { $killed = $false }
    $secs = ((Get-Date) - $t0).TotalSeconds
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") }
    $log = Read-RunLog $dir
    [IO.File]::WriteAllText((Join-Path $dir "seconds.txt"), ("{0:N0}" -f $secs))

    $num = Measure-Run $dir $log

    # --- the run's verdict: it ended at its VBlank, no crash, every expected line ---
    $miss = @()
    foreach ($e in @($r.expect)) {
        $need = 1; $pat = $e
        if ($e -match '^(\d+)\*(.*)$') { $need = [int]$Matches[1]; $pat = $Matches[2] }
        if ([regex]::Matches($log, $pat).Count -lt $need) { $miss += $e }
    }
    $crash = [regex]::IsMatch($log, "(?m)^crash:")
    $ok = (-not $killed) -and ($p.ExitCode -eq 0) -and (-not $miss) -and (-not $crash)
    $why = @()
    if ($killed) { $why += "stopped after $limit s" } elseif ($p.ExitCode -ne 0) { $why += "exit code $($p.ExitCode)" }
    if ($crash) { $why += "crash" }
    if ($miss) { $why += "missing: " + ($miss -join " | ") }
    [IO.File]::WriteAllText((Join-Path $dir "verdict.txt"), ($(if ($ok) { "PASS" } else { "FAIL " + ($why -join "; ") })) + "`n")
    Write-Host (" {0} ({1:N0} s)" -f $(if ($ok) { "PASS" } else { "FAIL: " + ($why -join "; ") }), $secs)
    $rows += New-Row $r $(if ($ok) { "PASS" } else { "FAIL" }) $num ("{0:N0}" -f $secs)
}

$table = $rows | Format-Table -AutoSize | Out-String -Width 200
$head = "Burnout 3 native port: owner test runs, $stamp`nProgram: $Exe ($(if (Test-Path -LiteralPath (Join-Path $exeDir 'VERSION.txt')) { (Get-Content -LiteralPath (Join-Path $exeDir 'VERSION.txt') -TotalCount 1) } else { '?' }))`nScenarios made from $($scen.made_from)`nWindow: $(if ($Offscreen) { 'none (-Offscreen)' } else { 'yes' })`n"
[IO.File]::WriteAllText((Join-Path $resRoot "summary.txt"), $head + $table, $utf8)
Write-Host "`n$table"
# rcprep: the zip leaves out the card copies (your save) and shortens your home folder to ~ in every text file, as the
# game's bug reports do
$homes = @($env:USERPROFILE, $env:HOME) | Where-Object { $_ } | Select-Object -Unique
function Redact([string]$text) {
    foreach ($h in $homes) { $text = $text.Replace($h, "~").Replace($h.Replace('\', '/'), "~") }
    if ($env:USERNAME) { $text = [regex]::Replace($text, '(?i)([\\/]Users[\\/])' + [regex]::Escape($env:USERNAME) + '(?=[\\/])', '$1<user>') }
    return $text
}
$zip = Join-Path $here "test-results-$stamp.zip"
# (System.IO.Compression with "/" in the entry names: Compress-Archive in Windows PowerShell 5.1 writes "\", which other
# systems unpack as flat file names)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$za = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    $rootLen = (Resolve-Path -LiteralPath $resRoot).Path.TrimEnd('\').Length + 1
    foreach ($f in Get-ChildItem -LiteralPath $resRoot -Recurse -File) {
        $rel = $f.FullName.Substring($rootLen).Replace('\', '/')
        if ($rel -match '^[^/]+/card/') { continue }   # (the save copies)
        if ($f.Extension -in @(".txt", ".ini", ".log", ".json", ".csv")) {
            $e = $za.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
            $w = New-Object IO.StreamWriter($e.Open(), $utf8)
            try { $w.Write((Redact ([IO.File]::ReadAllText($f.FullName)))) } finally { $w.Dispose() }
        } else {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal)
        }
    }
} finally { $za.Dispose() }
Write-Host "Send this file: $zip"
Write-Host "(Motion p95/p99: how far the shown game time strays from even steps, in % of a step; held: frames that repeated.)"
