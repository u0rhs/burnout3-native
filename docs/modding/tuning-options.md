# Tuning options: what each value does

The tuning console shows the game's own tuning variables: the values Criterion's designers tuned the game with (car
physics, boost and score rules, the rivals' driving, cameras, sound, rumble). This page says what each one does, in the
order the console shows them: tab, then section, then subsection. Find the row you are looking at under the same
headings here.

- **Opening the console**: PC SETTINGS > MODS > DEVELOPER > **DEVELOPER MODS** on, then start the game again.
  From then on **F9** opens the live overlay (in a race or anywhere in the game), and PC SETTINGS has a **DEVELOPER**
  tab with the same pages. How the console works, its keys, search and favourites: [tuning.md](tuning.md).
- **Changes are live**: a change is written at the next frame. The game uses it where it next reads the value: most are
  read every frame; some only when a car or a route is prepared, so they show from the next race (the entries say so).
- **Changes are recorded**: every change goes into the pad recording and the replay file with its frame, so a replay
  plays the same.
- **Nothing is saved** unless you use **SAVE AS PRESET** (the DEVELOPER tab): that writes your changes as a tuning
  preset mod, off until you turn it on in the launcher's Mods page. Start the game again and every value is the game's
  own.
- **Defaults never change the game.** With the console off, or on without changes, the game plays exactly as without
  it. A change does change gameplay: that is what the console is for.

Every variable and its registration (group, name, config file, type, range, the code that registers it) is listed in
[the tuning registry](file-formats/tuning-registry.md); its default comes from the disc's `DATA/VDB.XML`
([vdb.md](file-formats/vdb.md)). The game's default values are not given here: they are game data. Look at the value in
the console before you change it, and change it by a step or two to see what it does.

## Reading an entry

| Column | Meaning |
|---|---|
| Value | The variable's name exactly as the console shows it (the game's own spelling, typos included). |
| Type | `float`, `int` or `vector X Y Z` (three rows in the console); `×4` is an array of 4 (rows `[0]` .. `[3]`); a range (`0 .. 1`) is the registered range: the console never leaves it. |
| What it does | What the game does with it and what you notice when you change it. "Per car model": every car of that model has its own copy, the rivals' cars of that model included. |
| Evidence | **[C 0xADDR]**: read in the code that uses the value, at that function's address. **[L]**: likely, from the name and where it is used, not followed to its effect; also every "No effect: nothing reads it" (no reader was found). |
| # | The variable's row in [the tuning registry](file-formats/tuning-registry.md#every-registration). Two numbers: the code registers the same name in two places. |

**About [C] on this page.** Here [C] means the code the port runs: the game's C in `gen/native`, generated from the
decomp. Most of the functions read are the decomp's "nonmatching" drafts, which the
[file-format pages' tags](file-formats/index.md#evidence-tags) rate at most [L] on their own: those pages describe the
disc's files and the original program. This page describes the game as the port plays it, and that is this code: what
it does with a value is what you see when you change it.

Per-car values (HANDLING, ENGINE & SPEED, DAMAGE & CRASHES' car rows, the race cameras, the engine sounds) have a
subsection per car model in the console, the model you drive first, marked `(your car)`; with only one model in the
section the console shows its rows directly ("Steering - <car> (your car)"). They are described once here.

## YOUR CAR

The car you are driving now. These are not tuning variables: they are the car's own state, read at each frame and
changed at the next one. Outside a race the rows show `-` and do nothing. Changes here are recorded like the others but
are never part of SAVE AS PRESET. Details: [tuning.md](tuning.md#the-car-page).

| Row | What it does |
|---|---|
| Speed | The speedometer's value now, in the HUD's units (mph or km/h; also m/s). Read only. |
| Boost | The boost in the bar now, in boost units. Left / Right change it by a twentieth of the bar; it never goes above the bar's size. |
| Boost bar size | The bar's size now, in boost units. It grows as the bar gains extra segments (see BOOST & SCORE > Boost bar). Read only. |
| Boost use rate | Boost units used per second while you boost. Lower: a full bar lasts longer. The game sets it again when the bar changes size, so a change lasts until then. |
| Health | The car's health, 100 % full. Road Rage (offline and online) uses it: each crash or hit takes some away, by the game's Road Rage damage table, and at 0 your car is out [C 0x00395FC0 0x0038C120]. Below 10 % the car smokes and sparks, in any mode [C 0x00260F30]. No other mode was found to take health away [L]. |
| Repair | Sets the health back to full. It does not straighten the bent body. |
| Fill boost | Fills the boost bar. |
| Freeze damage | On: the health never goes down (it is written back every frame); a repair or a new race still raises it. |
| Infinite boost | On: the game's own infinite-boost switch, set again every frame (the game turns it off at a crash). The bar stays full while you boost. |

Below these rows, **this model's tuning**: eleven of the per-car values of the car you drive, so you need not look for
them in their tabs. Each is the same variable as in its tab (Enter goes there), and each is described there:

| Row | The variable |
|---|---|
| Top speed in boost | [ENGINE & SPEED > Speed & braking](#speed--braking): Max Speed In Boost |
| Acceleration | [ENGINE & SPEED > Speed & braking](#speed--braking): Accel Multiplier |
| Engine torque | [ENGINE & SPEED > Engine](#engine): Torque |
| Boost kick | [ENGINE & SPEED > Boost kick](#boost-kick): Boost Kick Torque |
| Brakes | [ENGINE & SPEED > Speed & braking](#speed--braking): Braking Factor |
| Drag | [ENGINE & SPEED > Speed & braking](#speed--braking): Drag Coef |
| Downforce (grip) | [HANDLING > Downforce & in the air](#downforce--in-the-air): Downforce Coef |
| Mass (kg) | [HANDLING > Mass](#mass): Mass (Kg) |
| Steering response | [HANDLING > Steering](#steering): Response |
| Drift turn rate | [HANDLING > Drift](#drift): Turn Rate |
| Wall hit penalty | [DAMAGE & CRASHES > Wall hits](#wall-hits): Wall Collsion Penalty |

## MOST USED

Three lists, no values of its own: your **favourites** (F on a row, Triangle on a controller), the values you **changed
last** (newest first, up to 12), and a **starter list** of values worth trying first: Max Speed In Boost, Accel
Multiplier, Torque, Mass (Kg), Response, Turn Rate (your car's), Boost Rate (Boost Units per Second), Boost Earning
Multiplier, Boost value per Near Miss (Boost units), Boost value per Drift metre (Boost units), Takedown Boost value
(Boost units), Top speed mps (AI RIVALS > Car), Camera Offset (CAMERA > Follow, your car's) and the bloom's Intensity
(VISUALS > Bloom). Each is described in its own tab below.

## HANDLING

All values here are **per car model** (`VehiclePhysics/<car>.cfg`): every car of that model on the road uses them, the
rivals' cars of the same model included. Most are copied into the car when the race loads, so a change made during a
race shows from the next race; the console keeps your change for the session and applies it when the next race registers
the car. Where a value is read every frame instead, its entry says so. Speeds in the code are m/s unless an entry says
mph. The traffic models' mass and suspension have the same names and are in TRAFFIC.

### Downforce & in the air

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Downforce Coef | float | Extra pull-down while the car is in the air (all four wheels off the ground): each physics step adds a downward force of mass x (10 + speed in mph x this value / 10). Bigger: the car drops sooner and flies less far off jumps at speed; it has no effect on the ground (it is not grip). | [C 0x001C1B20] | 128 |
| Min Drift Angle In Air | float | In degrees (stored as its sine). When the car lands after more than 0.1 s in the air and its direction of travel is turned sideways from its heading by more than this angle, it lands straight into a drift (left or right by the side). Bigger: you have to land more crossed-up to get a drift out of a jump. | [C 0x001C1B20] | 134 |
| In Air Corkscrew Damping | float | A 0..1 factor. In the air, each physics step removes this fraction of the car's spin about its own length (the barrel roll / corkscrew). 0: an airborne car keeps rolling; 1: the roll stops at once. | [C 0x001C1B20] | 135 |

### Steering

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Min Angle (degrees) | float | In degrees. The smallest steering lock of the front wheels (reached at high speed). Each frame the lock is `Max Velocity - 1.4 x (speed in m/s - Min Velocity)`, clamped between Min Angle and Max Angle; the stick or the AI's steering is then multiplied by it. Bigger: the car turns more sharply at top speed. | [C 0x001C0F00] | 154 |
| Max Angle (degrees) | float | In degrees. The largest steering lock (the clamp's upper end, reached at low speed: see Min Angle for the formula). Unlike most car values it is read again from the tuning value every frame, so a console change shows at once. It is also the lock the car returns to after the being-hit reaction (DAMAGE & CRASHES > REACTION TO BEING HIT). | [C 0x001C0F00] | 155 |
| Min Velocity | float | In m/s. The speed where the steering lock starts to fall: the lock is `Max Velocity - 1.4 x (speed - Min Velocity)` degrees, clamped to [Min Angle, Max Angle], so it shrinks by 1.4 degrees per m/s above this speed. Bigger: full lock is kept to a higher speed. | [C 0x001C0F00] | 156 |
| Max Velocity | float | Despite the name it acts as an angle: the steering lock (degrees) the car has at the speed Min Velocity, before the 1.4 degrees per m/s fall-off and the [Min Angle, Max Angle] clamp. Bigger: more lock at every speed until the clamp. Read again from the tuning value every frame (a change shows at once; the being-hit reaction replaces it for a while with REACTION TO BEING HIT > Steering Max Velocity). | [C 0x001C0F00] | 157 |
| Response | float | How fast the steering follows the stick: the steering input (-1..1) may change by at most this much per frame. Bigger: snappier steering; smaller: the wheels turn in slowly. | [C 0x001C0F00] | 158 |
| Drag Coef | float | Speed lost by steering: while not drifting, the drag on the car becomes `speed^2 x (Drag Coef + steer^2 x this)`, so holding full lock costs speed in proportion to this value. Bigger: the car scrubs more speed in corners. Read again from the tuning value every frame (a change shows at once), except during the being-hit reaction, which uses REACTION TO BEING HIT > Steering Drag Coef instead. | [C 0x001C1B20 0x001C0F00] | 159 |

### Drift

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Slide Max | float | About 0..1. The drift's "slide" share at a small drift angle: in a drift the sideways force on each wheel is a blend of the scripted drift push (this share) and normal tyre grip (the rest), and the drift turning torque is scaled by (1 - 0.75 x share). The share starts at Slide Max when a drift begins and falls toward Slide Min as the drift angle reaches the Max Drift Angle limit (braking adds to it, throttle above half takes away). Bigger: the car slides wider and holds the drift with less grip. | [C 0x001C0F00 0x001C1B20] | 160 |
| Slide Min | float | About 0..1. The lowest drift "slide" share (see Slide Max): the share the drift settles to at the widest drift angle, and the floor of its clamp. Bigger: the car keeps sliding even when it is fully sideways (less grip comes back). | [C 0x001C0F00 0x001C1B20] | 161 |
| Turn Momentum Slow | float | The cap on the car's turning (yaw) rate at about 110 mph: between 60 and 110 mph the cap blends from Turn Momentum Fast (at 60) to this value (at 110); when the car turns faster than the cap, the extra turning torque is cut to 10%. Below 60 mph there is no real cap. Note the code: above 110 mph the cap jumps back to Turn Momentum Fast, so this value only acts in the 60..110 mph band. | [C 0x001C1B20] | 162 |
| Turn Momentum Fast | float | The cap on the car's turning (yaw) rate at 60 mph and at every speed above 110 mph (see Turn Momentum Slow for the 60..110 blend). Smaller: the car cannot rotate as quickly at speed (less twitchy, wider lines); bigger: quicker direction changes. Despite the names, this is the value that limits turning at top speed. | [C 0x001C1B20] | 163 |
| Auto Drift Delay | float | In seconds (an offset). The automatic drift: when you hold the stick almost fully over (more than 0.9) with some throttle above 43 m/s (about 96 mph), a timer runs, and the car breaks into a drift by itself once it passes `this + (1.5 - throttle) - (speed - 43) / 25` (at least 0.2 s). Bigger: you must hold full lock longer before the car drifts on its own. | [C 0x001C0F00] | 164 |
| Turn Rate | float | How hard the steering turns the car while it is drifting: the drift's turning torque is 10000 x this x steer^2 (steering into the drift) or 5000 x this x steer^2 (steering the other way), times (1 - 0.75 x slide share). Bigger: the car rotates faster in a drift and the drift line can be tightened more. | [C 0x001C1B20] | 165 |
| Min Drift Speed | float | In mph. The brake-tap drift: a drift starts (with steering beyond 0.1) when the brake is pressed beyond a threshold that is 1.0 (full brake, i.e. never by a tap) at this speed and falls linearly to Max Speed Pressure at 130 mph (never below 0.1). Bigger: you need more speed before a brake tap throws the car into a drift. Throttle and brake both above 0.2 also start a drift at any speed. Not in one game mode (mode 3 in the code; which one is not known). | [C 0x001C0F00] | 166 |
| Max Speed Pressure | float | A 0..1 brake-pedal amount. The brake pressure needed to start a drift at 130 mph (see Min Drift Speed: the threshold falls from full brake at Min Drift Speed to this value at 130 mph and goes on falling above it). Smaller: lighter brake taps start drifts at high speed. | [C 0x001C0F00] | 167 |
| Max Drift Angle | float | In degrees (stored as its cosine). The drift angle limit: the slide share falls toward Slide Min as the car's sideways travel approaches it, and past it the drift's turning torque is replaced by a fixed push back toward straight. The code compares the cosine of this value with the sine of the car's slip angle, so the actual limit is reached at a slip of 90 degrees minus this value: a BIGGER value gives a SMALLER maximum drift angle. | [C 0x001C0F00 0x001C1B20] | 168 |

### Suspension Front

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Attatch Height | float | In metres in the body's frame. Where the racing car's front springs attach (copied to both front wheels); with Spring Length it sets the front ride height and where the wheel sits under the body. | [C 0x00252530 0x001CC900] | 111 |
| Spring Length | float | In metres. The front springs' rest length: the spring force is Spring Force x (length - compression); the wheel's travel runs from 0.25 (bump stop: a hard impulse; hard front-wheel bumps dent the player's car) to 0.75 of it. Bigger: more travel and a taller front. | [C 0x00252530 0x001CC900] | 112 |
| Spring Force | float | The front springs' stiffness (force per metre of compression). Bigger: a stiffer front that dives less under braking and rides higher; smaller: a soft front that bottoms out on landings. | [C 0x00252530 0x001CC900] | 113 |
| Spring Damping | float | The front dampers: a force of this x the compression speed, added to the spring. Bigger: the front settles quickly after bumps and landings; smaller: it keeps bouncing. | [C 0x00252530 0x001CC900] | 114 |

### Suspension Rear

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Attatch Height | float | In metres in the body's frame. Where the racing car's rear springs attach (copied to every rear wheel, including the extra axles of six-wheel vehicles); with Spring Length it sets the rear ride height. | [C 0x00252530 0x001CC900] | 115 |
| Spring Length | float | In metres. The rear springs' rest length (travel from 0.25 to 0.75 of it, a hard bump stop below). Bigger: more travel and a taller rear. | [C 0x00252530 0x001CC900] | 116 |
| Spring Force | float | The rear springs' stiffness (force per metre of compression). Bigger: the rear squats less under acceleration and bounces more. | [C 0x00252530 0x001CC900] | 117 |
| Spring Damping | float | The rear dampers (force per unit of compression speed). Bigger: the rear settles quickly after bumps. | [C 0x00252530 0x001CC900] | 118 |

### Body roll

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Brake Force Height | float | In metres. The height above the car's centre at which the brake and coasting (engine braking) force is applied. The higher the point, the more the nose dives when you brake. 0: braking does not pitch the car. | [C 0x001C1B20] | 119 |
| Accel Force Height | float | In metres. The height above the car's centre at which the tyres' forward/backward road forces (acceleration traction and wheel braking) are applied. Higher: more squat (nose up) when accelerating and more pitch under wheel braking. Not used during a boost start. | [C 0x001C1B20] | 120 |
| Steer Force Height | float | In metres. The height above each wheel's contact point at which the tyres' sideways (cornering) forces are applied, so it sets how much the body leans in corners: higher = more body roll. The low-speed model uses half of it as its lever too. | [C 0x001C1B20 0x001C38F0] | 121 |
| Drift Force Height | float | In metres. Like Steer Force Height, but for the sideways forces while drifting (and only while wheels on both sides touch the ground): higher = the body leans more in a drift. | [C 0x001C1B20] | 122 |

### Differential (LSD)

Despite the console's title, `LSDM` is not a limited-slip differential: the code uses these values for its **low-speed
driving model**, the simpler tyre model a car switches to below Speed Limit.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Speed Limit | float | In mph. Below this speed (and when not on a boost start), the car is driven by a simpler slow-speed tyre model instead of the full one; the wheels' slip effects use the same threshold, and the AI's steering changes method below it. Bigger: the simple model is used up to a higher speed (pulling away and tight manoeuvres feel different). | [C 0x001C1B20 0x001BF170 0x001450C0] | 123 |
| Steering Angle | float | In degrees. The steering lock used by the Low Speed Driving model (below LSD Speed Limit): the front wheels turn by this x the steering input. Bigger: tighter turns when crawling or pulling away. | [C 0x001C38F0] | 124 |
| Torque 1 | float | In the Low Speed Driving model, a yaw term of `Torque 2 x -(yaw rate) x (Torque 1 - /steer/)`: with the steering below Torque 1 it damps the car's rotation (straightens it), above Torque 1 it adds to it. So Torque 1 is the steering amount (0..1) where damping turns into help. | [C 0x001C38F0] | 125 |
| Torque 2 | float | The strength (gain) of the Low Speed Driving yaw term described under Torque 1: bigger = the car's rotation at low speed is damped (or helped) more strongly. | [C 0x001C38F0] | 126 |

### Mass

The front end registers Mass and Max Speed In Boost a second time for the car shown on the car-select screens (the
second row): it has the same key as the racing value, so a change to either is the same change.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Mass (Kg) | float | In kg. The racing car's mass. It sets the weight on each wheel (and so the tyre grip), how much the engine's and brakes' forces accelerate it (heavier: slower to speed up and to stop), the suspension's bump impulses and, in car-to-car contacts, how much it shoves other cars and is shoved (takedowns). In Crash mode, heavy vehicles (HEVY... models) triple their mass while crashing and read it back from this value afterwards. Player and AI cars of the model | [C 0x00252530 0x001C1B20 0x001CC900 0x001C5430] | 110 |
| Mass (Kg) | float | The same Mass, registered again by the front end's car viewer for the car shown on the car-select screens. Nothing in the viewer was found to read it, so it changes nothing you see there; it shares its key with the racing value, so a change here is a change of that value too. | [L] | 702 |

## ENGINE & SPEED

All values here are **per car model** (`VehiclePhysics/<car>.cfg`): every car of that model on the road uses them, the
rivals' cars of the same model included. Most are copied into the car when the race loads, so a change made during a
race shows from the next race; the console keeps your change for the session and applies it when the next race registers
the car. Where a value is read every frame instead, its entry says so. Speeds in the code are m/s unless an entry says
mph. The car's gearbox is automatic: the engine values set when it shifts.

### Speed & braking

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Drag Coef | float | Air drag: each step the car feels a force of `this x speed^2` (m/s) against its travel. Bigger: lower top speed and slower acceleration at high speed. It is switched off while boosting below Max Speed In Boost (in top gear, or with the revs below Change Up RPM), so it mostly limits the non-boost top speed. | [C 0x001C1B20] | 127 |
| Accel Multiplier | float | The throttle is the accelerator pedal x this, capped at full. Above 1: part pedal already gives full throttle; below 1: the car never gets full throttle (less acceleration and a lower top speed). Applies to the player's car, the AI's cars (in races) and online cars of the model; from the next race. | [C 0x001BD020 0x001D2790 0x00268790] | 130 |
| Braking Factor | float | The brakes' strength: the brake force is `20000 x this x brake pedal x (speed + 1) / 70` against the travel, applied at Brake Force Height (so it also sets how much the nose dives). Bigger: shorter stops. | [C 0x001C1B20] | 131 |
| Engine Braking Factor | float | The coasting force when you are off the throttle (and not drifting): `(speed + 1) x this / 70` along the car's travel, summed with the brake force (which carries the opposite sign), so its size sets how quickly the car slows when you lift off (its sign as the file gives it). | [C 0x001C1B20] | 132 |
| Max Speed In Boost | float | In mph. The car's absolute top speed for engine drive: above it the engine gives no drive force at all (boost or not). Below it, while boosting, the air drag is switched off and the Boost Kick applies, so this is the speed a boost carries the car to. It also scales the engine sound's volume tail-off, the wheelspin effect and how fast an off-screen AI car's speed may change. | [C 0x001C0F00 0x001C1B20 0x001C6F90 0x002ED3E0] | 133 |
| Max Speed In Boost | float | The same Max Speed In Boost, registered again by the front end's car viewer for the car shown on the car-select screens. Nothing in the viewer was found to read it; it shares its key with the racing value, so a change here is a change of that value too. | [L] | 703 |

### Engine

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Torque | float | The engine's torque: the drive the gearbox delivers is `Final Gear x gear ratio x torque curve (0.5..1) x throttle x this` (with a random +-5% wobble each frame). Bigger: stronger acceleration in every gear. | [C 0x001C6F90] | 136 |
| Peak Torque Revs | float | In rpm (converted to rad/s on copy). The torque curve's peak: below these revs the torque rises linearly from 50% at 0 rpm to 100%; from here to Fall Off Torque Revs it is 100%. Bigger: weaker pull at low revs (after shifts and from low speed). | [C 0x00252530 0x001C6F90] | 137 |
| Fall Off Torque Revs | float | In rpm. Above these revs the torque falls linearly to 75% at Max RPM. Bigger: full torque is kept to higher revs. | [C 0x00252530 0x001C6F90] | 138 |
| Idle RPM | float | In rpm. The idle revs: the engine speed does not fall below about this; when it drops under it (or the car nearly stops) with no throttle, the gearbox drops to neutral. Also affects the engine sound through the engine speed (not traced further). | [C 0x001C6F90] | 139 |
| Max RPM | float | In rpm. The rev limiter: at it the drive turns into engine braking and the revs drop back (the limit is re-drawn a little below it at random, which gives the limiter stutter); it is raised 10% while boosting and lowered 20% in reverse. The torque curve ends at 75% here. | [C 0x001C6F90 0x001C7880] | 140 |
| Change Down RPM | float | In rpm. The automatic gearbox shifts down (from 2nd gear and above) when the revs fall below this with less than half throttle, or below 75% of it at any throttle. Bigger: earlier downshifts. | [C 0x001C6F90] | 141 |
| Change Up RPM | float | In rpm. The automatic gearbox shifts up when the revs pass this (not above top gear, and not within a moment of the last shift). Bigger: the car holds each gear longer. While boosting below Max Speed In Boost, air drag is off only in top gear or below these revs. | [C 0x001C6F90 0x001C1B20] | 142 |

### Gear ratios

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Reverse | float | The reverse gear's ratio: it multiplies the engine revs and the drive in reverse (reverse uses its own torque curve that falls to 0 at the rev limit, and a limiter 20% lower). | [C 0x001C6F90] | 143 |
| Neutral | float | The neutral "ratio": it multiplies the drive in neutral (the car is in neutral at the start line and when the revs fall to idle), so anything but 0 lets the engine push the car in neutral. In neutral the revs follow the throttle, not the wheels. | [C 0x001C6F90] | 144 |
| 1st Gear | float | First gear's ratio: the engine revs are wheel speed x ratio x Final Gear, and the drive is multiplied by the same ratio. Bigger: more pull and an earlier shift (lower top speed in the gear). The gear count is the number of positive ratios from 1st on (at most 6): a ratio of 0 ends the gearbox there. | [C 0x001C6F90 0x001C7880] | 145 |
| 2nd Gear | float | Second gear's ratio (revs = wheel speed x ratio x Final Gear; drive x ratio). Bigger: more pull, shorter gear. 0 makes a one-speed gearbox. | [C 0x001C6F90 0x001C7880] | 146 |
| 3rd Gear | float | Third gear's ratio (revs = wheel speed x ratio x Final Gear; drive x ratio). 0 ends the gearbox at 2nd. | [C 0x001C6F90 0x001C7880] | 147 |
| 4th Gear | float | Fourth gear's ratio (revs = wheel speed x ratio x Final Gear; drive x ratio). 0 ends the gearbox at 3rd. | [C 0x001C6F90 0x001C7880] | 148 |
| 5th Gear | float | Fifth gear's ratio (revs = wheel speed x ratio x Final Gear; drive x ratio). 0 ends the gearbox at 4th. | [C 0x001C6F90 0x001C7880] | 149 |
| 6th Gear | float | Sixth gear's ratio (revs = wheel speed x ratio x Final Gear; drive x ratio); 0 makes a five-speed gearbox. Top gear matters for boost: air drag stays on in a boost unless the car is in top gear or below Change Up RPM. | [C 0x001C6F90 0x001C7880] | 150 |
| Final Gear | float | The final drive: it multiplies every gear's ratio, both for the engine revs and for the drive. Bigger: quicker acceleration and earlier shifts in every gear, lower speed per gear. | [C 0x001C6F90] | 151 |

### Boost kick

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost Kick Torque | float | The extra shove at the start of a boost: while boosting below Max Speed In Boost the torque curve is replaced by 1 + this, fading linearly to 1 over Boost Kick Time seconds from the boost's start. Bigger: a harder kick when you hit boost. | [C 0x001C6F90] | 152 |
| Boost Kick Time | float | In seconds. How long the boost kick (Boost Kick Torque) takes to fade after a boost starts. Bigger: the extra shove lasts longer. | [C 0x001C6F90] | 153 |

## BOOST & SCORE

One set for the whole game (`Score.cfg`), read when the game uses it, so a change works at once. **Boost units** are the
units of the boost bar (BOOST BAR > Boost Bar sizes gives the bar's size in them). Every boost reward is multiplied by
Boost Earning Multiplier and by the game mode's own factor; Crash mode earns no boost at all. **BP** (burnout points)
are the points the game awards for moves, shown as bonus events; "modes that score race BP" excludes Road Rage, which
has its own takedown points.

### Boost bar

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Minimum Boost Time (seconds) | float | The shortest boost, in seconds: once a boost starts it goes on for at least this long, even if you let go of the button. It also sets how much boost you need to start one: the bar must hold Boost Rate x this time. Bigger = longer forced boosts and more boost needed to start. | [C 0x0014E3A0 0x0014DEB0 0x0014DFF0] | 2 |
| Minimum Boost Recovery Time (seconds) | float | The cool-down after a boost ends, in seconds: `StartBurn` refuses a new boost until this long after the last one stopped. Bigger means a longer wait before you can boost again; 0 lets you re-boost straight away. | [C 0x0014DEF0] | 3 |
| Boost Bar sizes (Boost Units) | float ×4, 0 .. 720 | The boost bar's capacity in boost units, per number of extra bar segments (index 0 = the starting bar, 1..3 = after one, two or three extensions). Each race starts at index 0; a takedown in a race or Road Rage adds a segment (`AddSegment`, up to 3); a crash removes one (or empties the bar and goes back to index 0). Earned boost is capped at the current size. Entry 0 also sets the boost swapped by a slam (entry 0 / (segments + 1), `GetSegmentSizeInBurn`), and entry 3 is the "full" level the boost sound and the boost screen effect are scaled against. The car's own bar-size setting passed to `CB3Burn::Prepare` is ignored. | [C 0x0014DFF0 0x0014E1C0 0x0014E060 0x0014E500 0x002ED3E0 0x0030B190] | 4 |
| Boost Earning Multiplier | float ×4, 0 .. 10 | A factor on boost gained and lost, per number of extra bar segments (same index as Boost Bar sizes). `AddBurn` multiplies every boost reward (near miss, drift, takedown...) by it (and by the game mode's own factor), and `RemoveBurn` multiplies boost lost to slams and crashes by it. Bigger fills (and drains) the bar faster with that bar size. Boost earning is off entirely in Crash mode. | [C 0x0014E2D0 0x0014E250 0x0014DFF0] | 5 |
| Boost Rate (Boost Units per Second) | float ×4, 0 .. 720 | How fast the bar empties while boosting, in boost units per second, per number of extra bar segments (same index as Boost Bar sizes). A full bar lasts size / rate seconds of boost. Bigger means shorter boosts. It also sets the amount needed to start a boost (with Minimum Boost Time). | [C 0x0014E3A0 0x0014DFF0] | 6 |

### Boost earned

#### Air

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Minimum Air Distance (Metres) | float | How far you must fly, in metres travelled while airborne, before a jump pays any boost. Below it a jump gives no boost; once past it, the whole distance so far is paid at once and then each further metre. | [C 0x00157FA0] | 12 |
| Boost value per Air metre (Boost units) | float | Boost per metre flown, in boost units (times the Boost Earning Multiplier). Paid while you are in the air once you pass Minimum Air Distance. Bigger means more boost from jumps. | [C 0x00157FA0] | 13 |

#### Oncoming

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Minimum Oncoming Speed (MPH) | float | The slowest you can go, in mph, and still score oncoming (driving in the lanes of oncoming traffic). Below it the oncoming run stops (and is scored). | [C 0x00157D50] | 14 |
| Minimum Oncoming Distance (Metres) | float | How far you must drive in the oncoming lanes, in metres, before it pays any boost. After that the whole distance so far is paid, then each further metre. | [C 0x00157D50] | 15 |
| Boost value per Oncoming metre (Boost units) | float | Boost per metre driven in the oncoming lanes, in boost units (times the Boost Earning Multiplier), once past the minimum distance. | [C 0x00157D50] | 16 |

#### Drift

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Minimum Drift Speed (MPH) | float | The slowest you can go, in mph, and still score a drift (powerslide). Below it the drift ends and is scored. | [C 0x00157B00] | 17 |
| Minimum Drift Distance (Metres) | float | How far you must drift, in metres, before it pays any boost. After that the whole distance so far is paid, then each further metre. | [C 0x00157B00] | 18 |
| Boost value per Drift metre (Boost units) | float | Boost per metre drifted, in boost units (times the Boost Earning Multiplier), once past the minimum drift distance. | [C 0x00157B00] | 19 |

#### Near Miss

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Near Miss Distance (Metres) | float | How close you must pass a traffic car to score a near miss, in metres: the gap between the two cars' bounding boxes seen from above (and within 4 m in height). Bigger makes near misses easier and more frequent. | [C 0x001586D0] | 20 |
| Minimum Near Miss Speed (MPH) | float | The slowest you can go, in mph, for a pass to count as a near miss. Slower passes score nothing. | [C 0x001598F0] | 21 |
| Minimum Near Miss Chain Speed (MPH) | float | The speed, in mph, you must stay above to keep a near-miss chain going. Dropping below it ends the chain, which is then scored (the Near Miss category). | [C 0x001598F0] | 22 |
| Boost value per Near Miss (Boost units) | float | Boost given for each near miss, in boost units (times the Boost Earning Multiplier). Each traffic car counts once. | [C 0x001598F0] | 23 |
| Near Miss Chain Time (Seconds) | float | The longest gap allowed between near misses in a chain, in seconds. If no new near miss comes within this time the chain ends and is scored. The boost bar's near-miss message also uses it to time out. | [C 0x001598F0 0x001A45F0] | 24 |

#### Crash Escape

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Crash Escape Radius | float | How close a crash must come, in metres (seen from above), to start a Crash Escape: a wrecked racer or a crashing traffic car within this distance, in front of you (within about 60 degrees of your direction of travel). Bigger means more Crash Escapes. | [C 0x00158260] | 25 |
| Time Between Crashing Near Misses (Seconds) | float | How long, in seconds, you must be clear of that crash after being within the radius before the Crash Escape is awarded. | [C 0x00159390] | 26 |
| Minimum Time Clear After Crash Escape | float | After a Crash Escape from a crash (or after being caught in it), the same crash cannot give another one for this many seconds. | [C 0x00159390] | 27 |
| Minimum Crash Escape Speed (MPH) | float | The slowest you can go, in mph, to earn Crash Escapes. Below it every pending escape is cancelled. | [C 0x00159390] | 28 |
| Boost value per Crash Escape (Boost units) | float | Boost given for each Crash Escape, in boost units (times the Boost Earning Multiplier). | [C 0x00159390] | 29 |

#### Aggressive

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost value lost or gained per Slam (Boost units) | float | Despite the name, not the boost a slam swaps: a slam moves one bar segment of boost (Boost Bar sizes[0] / (segments + 1)) from the victim to the attacker. This value is only added to two slam totals on the score that nothing reads, so changing it has no visible effect. | [C 0x00155580] | 30 |
| Boost value for rubbing (Boost units) | float | Boost per second while rubbing (sustained side contact with a rival racer), in boost units (times the Boost Earning Multiplier). Paid each frame once the contact has lasted the minimum time. | [C 0x00159DF0] | 31 |
| Min Contact Time For Rubbing (Seconds) | float | How long, in seconds, you must stay in contact with a rival before it counts as rubbing and starts paying boost. Shorter contacts give nothing. | [C 0x00159DF0] | 32 |
| Trading Paint Boost value (Boost units) | float | Boost given for trading paint (a light hit with a rival), in boost units (times the Boost Earning Multiplier). Both cars get it, except for a rear-end shunt where only the hitter does. | [C 0x00155910] | 33 |

#### Boost Start

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost Start Boost value (Boost units) | float | Boost given for a boost start (the boost launch at the start of a race), in boost units (times the Boost Earning Multiplier). | [C 0x00156E20] | 34 |

#### Takedowns

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Takedown Boost value (Boost units) | float | Boost given for each takedown, in boost units (times the Boost Earning Multiplier), in races, Road Rage (which uses the race code) and online races. The same takedown also adds a boost bar segment. | [C 0x003899E0 0x00391F10] | 35 |
| Maximum Crash Wait Time (Seconds) | float | How long after a slam or paint trade, in seconds, your crash still counts as a takedown for the car that hit you. The same window decides "You Were Lucky"/"Denied": hitting traffic without crashing within this time after being hit. | [C 0x00157410 0x00157060 0x0015B000] | 36 |
| Maximum Crash Wait Time - No Slam (Seconds) | float | How long, in seconds, the rubbing contact timer is kept after the contact ends. A crash within this time can still count as a takedown for the car that rubbed you; after it the timer resets. | [C 0x00159DF0] | 37 |
| Minimum Collide Time to enable Takedown - No Slam (Seconds) | float | How long a rival must have rubbed you, in seconds, for your crash to count as that rival's takedown (only for cars within 160 m) when there was no slam. Smaller makes non-slam takedowns easier. | [C 0x00157410] | 38 |
| Race Car Clear Wait Time (Seconds) | float | How long, in seconds, the attacker must stay uncrashed after the victim crashes before the takedown is given. If you crash too within this time, the takedown is lost. | [C 0x001578E0] | 39 |
| Time window for getting a double takedown(Seconds) | float | The window for chaining takedowns into a Double/Triple... Takedown, in seconds: each takedown within it adds to the count and restarts the window. Bigger makes multi-takedowns easier. | [C 0x00368680] | 40 |
| Time window for getting a takedown spree (Seconds) | float | The window for a Takedown Spree, in seconds: each takedown within it adds to the spree count and restarts the window. | [C 0x00368680] | 41 |
| Time window for getting a double revenge takedown (Seconds) | float | No effect: nothing reads it. | [L] | 42 |

#### Props

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Prop Hit Chain time (Seconds) | float | The longest gap between prop hits (signs, barriers and other roadside objects) in a chain, in seconds. If no new hit comes within it, the chain count goes back to 0 (it multiplies Prop Hit BP). | [C 0x0015B000 0x00156EF0] | 43 |
| Prop Hit Boost value (Boost units) | float | Boost given for each prop hit, in boost units (times the Boost Earning Multiplier). Not given while crashed. | [C 0x00156EF0] | 44 |

#### Lap

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Perfect Lap Boost value (Boost units) | float | Boost given at the end of a Perfect Lap (a lap with no crash), in boost units (times the Boost Earning Multiplier). | [C 0x0015A1F0] | 45 |
| Burnout Lap Boost value (Boost units) | float | Boost given at the end of a Burnout Lap (a whole lap spent boosting without stopping), in boost units (times the Boost Earning Multiplier). | [C 0x0015A1F0 0x0015B000] | 46 |

#### Crashing

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Ratio of Boost Lost After Crash | float, 0 .. 1 | No effect: nothing reads it. The boost lost in a crash comes from the race rules instead: the bar loses one segment, or is emptied and set back to its starting size (CB3OfflineSingleRaceLogic::OnCrashStart 0x00389AB0). | [L] | 47 |

### Burnout points

#### Driving

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Prop Hit BP | int | Burnout Points (BP) for a prop hit, multiplied by the number of hits in the current chain. Counted only in modes that score race BP (not Road Rage). No on-screen bonus event. | [C 0x00156EF0] | 48 |
| Rubbing BP | int | No effect: nothing reads it. Rubbing scores Rubbing Category BP instead. | [L] | 49 |
| Tradin' Paint BP | int | BP for trading paint with a rival, plus Burning Slam Extra BP if you were boosting. Repeated attacks on the same car can add the game's own combo points on top. Counted only in modes that score race BP. | [C 0x00368D20 0x00367F40] | 50 |
| Boost Start BP | int | BP for a boost start at the start of a race. Shown as a bonus event; counted only in modes that score race BP. | [C 0x00156E20] | 51 |
| Crash Escape BP | int | No effect: nothing reads it. The Crash Escape bonus uses fixed points in code instead: 25, or 50 within 15 m, or 100 within 5 m (CB3ScoreBonus::OnCrashEscape 0x00368530). | [L] | 52 |
| Perfect Lap BP | int | BP for a Perfect Lap (a lap with no crash), given at the end of the lap with a bonus event. Counted only in modes that score race BP. | [C 0x0015A1F0] | 54 |
| Burnout Lap BP | int | BP for a Burnout Lap (a whole lap spent boosting without stopping), given at the end of the lap with a bonus event. Counted only in modes that score race BP. | [C 0x0015A1F0] | 62 |
| Near Miss BP | int | BP for each near miss, multiplied by its place in the chain (1st ×1, 2nd ×2, ...). Counted only in modes that score race BP. | [C 0x001598F0] | 63 |
| Leading Race At End Of Lap BP | int | BP for being in 1st place when you finish a lap (not the last lap), in races with more than one car. Shown as a bonus event. | [C 0x0015A1F0] | 64 |
| Take Race Position BP | int ×5 | BP for moving up to a new best race position, one entry per place: index 0 = taking 1st, 1 = taking 2nd, ... 4 = taking 5th. Given once per new best place, only with rivals in the race, with a bonus event; counted only in modes that score race BP. | [C 0x0015B000] | 74 |

#### Takedowns & slams

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Burning Slam Extra BP | int | BP added to a slam's or paint trade's BP when the attacker is boosting (a "burning" slam). | [C 0x00368CB0 0x00368D20] | 53 |
| Takedown BP | int | The base BP for a takedown (signature takedowns use a fixed 1500 from code instead). Double/spree/revenge bonuses are added on top. Counted in race modes and Road Rage. | [C 0x00368680] | 55 |
| Revenge Takedown Bonus BP | int | BP added to a takedown when the victim is your revenge rival (a car that took you down before), with the Revenge bonus event. Counted in race modes and Road Rage. | [C 0x00368680] | 56 |
| Psyche Out BP | int | BP for a Psyche Out takedown (the rival crashes on its own while under your pressure), used in place of Takedown BP. Counted in race modes and Road Rage. | [C 0x00368680] | 57 |
| Aftertouch Takedown BP | int | BP for an Aftertouch takedown (taking a rival out with your wrecked car after a crash), used in place of Takedown BP. Counted in race modes and Road Rage. | [C 0x00368680] | 58 |
| No Mercy BP | int | No effect: nothing reads it. | [L] | 59 |
| Denied (You Were Lucky) BP | int | BP for "You Were Lucky": you get hit by a rival, then clip traffic without crashing within the takedown crash window. Counted only in modes that score race BP. | [C 0x0015B000 0x00157060] | 60 |
| Denied (Takedown Denied) BP | int | BP for "Takedown Denied": the rival's takedown attempt failed (same trigger as You Were Lucky). The rival gets the "Denied" event, and the BP are added to the surviving car's score. Counted only in modes that score race BP. | [C 0x0015B000 0x00368510] | 61 |
| Slam Type BP | int ×4 | BP for a slam, per slam type from `CalculateSlamType`: 0 = side or other angle, 1 = rear-end shunt (same heading, rival straight ahead), 2 and 3 = T-bone from either side. Plus Burning Slam Extra BP if you were boosting. | [C 0x00368CB0 0x00156320] | 66 |
| Super Slam Type BP | int ×4 | BP for a slam (same 4 slam types as Slam Type BP), used instead of Slam Type BP when the victim was already crashing, was hit within 3 s of an earlier event on its score, or was going under 70 mph. | [C 0x00368CB0 0x00155580] | 67 |
| Double Takedown BP | int ×4 | BP added for chained takedowns within the double-takedown window: index 0 = 2nd takedown (Double), 1 = 3rd, 2 = 4th, 3 = 5th. A 6th reads one entry past the array, which is the first Takedown Spree entry. Added on top of the takedown's BP, with its bonus event. | [C 0x00368680] | 75 |
| Takedown Spree BP | int ×4 | BP added for takedowns within the spree window: index 0 = 2nd takedown of the spree, 1 = 3rd, 2 = 4th, 3 = 5th and later. Added on top of the takedown's BP. | [C 0x00368680] | 76 |
| Double Revenge BP | int ×2 | No effect: nothing reads it. | [L] | 77 |
| Road Rage Takedown BP | int ×8 | Road Rage only: BP for each takedown, per medal stage: index 0 = no medal yet, 1 and 2 = after the first and second medal, 3..7 = after the third medal, rising with each takedown past its target (capped at 7). Shown with the takedown event; the medal events come from the same code. | [C 0x0038C120] | 78 |

#### By category

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Air Category BP | int ×4 | BP for a jump, per category level reached (index 0..3, set by Air Category Minima). Given when you land, with the jump's bonus event; counted only in modes that score race BP. | [C 0x00157FA0] | 65 |
| Oncoming Category BP | int ×4 | BP for an oncoming run, per category level reached (index 0..3, set by Oncoming Category Minima). Given when the run ends; counted only in modes that score race BP. | [C 0x00157D50] | 68 |
| Drift Category BP | int ×4 | BP for a drift, per category level reached (index 0..3, set by Drift Category Minima). Given when the drift ends; counted only in modes that score race BP. | [C 0x00157B00] | 69 |
| Tailgate Category BP | int ×3 | BP for tailgating, per category level reached (index 0..2, set by Tailgate Category Minima). Given when the tailgate ends, with its bonus event. | [C 0x00368D60 0x00158EB0] | 70 |
| Rubbing Category BP | int ×3 | BP for rubbing, per category level reached (index 0..2, set by Rubbing Category Minima). Given when the contact ends; the rival gets the same BP. | [C 0x00368E40 0x00159DF0] | 71 |
| Grinding Category BP | int ×3 | BP for grinding against a rival, per category level reached (index 0..2, set by Grinding Category Minima). Given when the grind ends. | [C 0x00368DF0 0x001576B0] | 72 |
| Near Miss Category BP | int ×4 | BP for a near-miss chain, per category level reached (index 0..3, set by Near Miss Category Minima chain counts). Given when the chain ends, on top of the per-near-miss BP. | [C 0x001598F0] | 73 |

### BP Categories

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Air Category Minima (Metres) | float ×4 | The jump distances, in metres, needed for Air category levels 0..3: a jump reaches the highest level whose minimum it passes, which picks its Air Category BP. Bigger means longer jumps are needed. | [C 0x0015C340 0x00157FA0] | 79 |
| Oncoming Category Minima (Metres) | float ×4 | The oncoming distances, in metres, needed for Oncoming category levels 0..3 (these pick Oncoming Category BP). | [C 0x0015C340 0x00157D50] | 80 |
| Drift Category Minima (Metres) | float ×4 | The drift distances, in metres, needed for Drift category levels 0..3 (these pick Drift Category BP). | [C 0x0015C340 0x00157B00] | 81 |
| Tailgate Category Minima (Seconds) | float ×3 | The tailgating times, in seconds, needed for Tailgate category levels 0..2 (these pick Tailgate Category BP). | [C 0x0015C340 0x00158EB0] | 82 |
| Rubbing Category Minima (Seconds) | float ×3 | The rubbing times, in seconds, needed for Rubbing category levels 0..2 (these pick Rubbing Category BP). | [C 0x0015C340 0x00159DF0] | 83 |
| Grinding Category Minima (Seconds) | float ×3 | The grinding times, in seconds, needed for Grinding category levels 0..2 (these pick Grinding Category BP). Reaching the top level after 1 s or more of grinding makes an AI rival crash (`StartRaceCarCrashing`), so the last entry also decides when a grind takes a computer car out. | [C 0x0015C340 0x001576B0] | 84 |
| Near Miss Category Minima (Counts) | float ×4 | The number of near misses in one chain needed for Near Miss category levels 0..3 (these pick Near Miss Category BP). | [C 0x0015C340 0x001598F0] | 85 |

### Tailgating

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Max distance behind for tailgating (Metres) | float | The farthest a rival can be, in metres, for you to tailgate it (and it must be more than 1.5 m away and doing at least 60 mph); the nearest such rival is the target. Bigger makes tailgating easier. | [C 0x00158EB0] | 86 |
| Min distance behind for psyche-out when tailgating (Metres) | float | No effect: nothing reads it. | [L] | 87 |
| Cone angle behind for tailgating (Degrees) | float | How far off the rival's line you can be, in degrees: the angle between the rival's heading and the line from your car to it must be at most this. Bigger allows tailgating from further to the side. | [C 0x00158EB0] | 88 |
| Extra boost per second | float | Boost per second while tailgating, in boost units (times the Boost Earning Multiplier), paid each frame once the minimum tailgating time has passed. | [C 0x00158EB0] | 89 |
| Minimum tailgating time to count as tailgating (Seconds) | float | How long, in seconds, you must sit behind the same rival before it counts as tailgating (and starts paying boost and scoring). | [C 0x00158EB0] | 90 |

### Grinding

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Minimum time to grind before scoring (Seconds) | float | No effect: nothing reads it. Grinding pays from the first frame of contact. | [L] | 91 |
| Extra boost per second | float | Boost per second while grinding against a rival, in boost units (times the Boost Earning Multiplier), paid each frame of contact. | [C 0x001576B0] | 92 |

### Distance Travelled

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Distance Travelled Medals (Miles) | float ×3 | No effect: nothing reads it. | [L] | 93 |

## DAMAGE & CRASHES

Two per-car-model sections (`VehiclePhysics/<car>.cfg`, copied when the race loads: from the next race) and the crash
action director's air ram (`Physics/cad.cfg`, one set, read at each crash).

### Wall hits

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Wall Collsion Penalty | float | A 0..1 factor. When the car scrapes or hits a wall while driving (not crashing), its velocity is multiplied by this on every physics step it touches the wall (plus a small extra loss when it meets the wall at more than 45 degrees). 1: walls cost no speed; smaller: more speed lost. Only human-driven cars use it: the AI's cars always keep 0.99. | [C 0x001C5430] | 129 |

### Reaction to being hit

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Steer Away Time (s) | float | In seconds. After the car is slammed by another car, for this long its steering is forced to full lock (away, in the direction the game picks from the hit) and its lock set to Steering Max Angle below; then the lock blends back to normal until Total Out-Of-Control Time. A side-swipe (trading paint) uses 0.6 x the times. Bigger: the car is thrown off line for longer. | [C 0x001C0F00] | 169 |
| Total Out-Of-Control Time (s) | float | In seconds. The whole being-hit window: within this time of being slammed the steering limits are replaced by the reaction values (Steering Max Velocity, Steering Drag Coef) and the lock blends back from Steering Max Angle to normal (side-swipes: 0.6 x). The AI also uses it: an AI car slammed by a human driver within this time holds or flips its steering away. Bigger: a longer loss of control after hits. | [C 0x001C0F00 0x001D17D0] | 170 |
| Steering Max Angle (degrees) | float | In degrees. The steering lock while the car is being thrown by a hit (the Steer Away Time), blending back to the normal Max Angle over the rest of the out-of-control window (0.8 x for a side-swipe). Bigger: the hit car swerves harder. | [C 0x001C0F00] | 171 |
| Steering Max Velocity | float | Replaces STEERING > Max Velocity (the lock at Min Velocity, before the speed fall-off) for the whole out-of-control window after a hit. Smaller: the hit car steers less at speed while it recovers. | [C 0x001C0F00] | 172 |
| Steering Drag Coef | float | Replaces STEERING > Drag Coef (the speed lost by steering) for the whole out-of-control window after a hit. Bigger: a hit car loses more speed while it swerves. | [C 0x001C0F00 0x001C1B20] | 173 |

### Crash action (air ram)

The **air ram** is the upward kick a race car gets the moment its crash starts: scaled by the car's mass and pushed at a
corner or side of the body, it lifts, flips and spins the car (with an air-ram sound). Which factor applies depends on
how the crash happened. Every race-car crash uses it (yours and the rivals', any mode, takedowns included); traffic and
aftertouch do not.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Air Ram Front Corner | float, 0 .. 1 | The air-ram strength (0..1) for most car-against-car crashes: the front and back "quadrant" kinds, where neither car hit with a corner and the other car is not a heavy vehicle (picked partly at random and by whether the car was over 130 mph). Bigger = the car is thrown higher and tumbles harder. | [C 0x001AF450 0x001B0E10 0x001CC580] | 94 |
| Air Ram Back Corner | float, 0 .. 1 | The air-ram strength (0..1) when the crashing car hits a heavy vehicle (bus, truck) side-on (45..135 degrees apart), and for the rear-quadrant crash kinds; the kick is at the back of the car. | [C 0x001AF450 0x001B0E10 0x001CC580] | 95 |
| Air Ram Side | float, 0 .. 1 | The air-ram strength (0..1) when both cars hit each other with a corner, and for one random front-quadrant crash kind; the kick is at the struck side of the car. | [C 0x001AF450 0x001B0E10 0x001CC580] | 96 |
| Air Ram Front | float, 0 .. 1 | The air-ram strength (0..1) for a straight front kick. Only the crash director's debug mode reads it, and the game never turns that on: no effect in play. | [C 0x001B0E10] | 97 |
| Air Ram Back | float, 0 .. 1 | The air-ram strength (0..1) for a straight rear kick. Only the crash director's debug mode reads it: no effect in play. | [C 0x001B0E10] | 98 |
| Air Ram World | float, 0 .. 1 | The air-ram strength (0..1) when a car crashes into a wall or other scenery (no other vehicle), and only when it hit nearly head-on: a kick at the back that tips the car over its nose. | [C 0x001AF450 0x001CC580] | 99 |
| Air Ram Low Speed Multiplier | float, 0 .. 2 | A factor (0..2) on the air ram when the crashing car is under 100 mph, for the quadrant crash kinds only (not the heavy-vehicle side hit, the corner-to-corner hit or the wall crash). Below 1 = slow crashes are calmer; above 1 = they launch harder. (Your own car under 120 mph also skips the air ram one time in four.) | [C 0x001AF450] | 100 |

## AI RIVALS

One set for every rival in every mode (`AI/defaults.cfg`), read while they drive: a change works at once. The code sets
a default before the disc's value replaces it (the registry lists those code defaults). A rival's **aggression** is its
own 0..1 value; many attack values below are scaled by it. Speeds are m/s unless the name says mph.

### Aggressive driving

When and how rivals attack: pick a target, speed-match it, block it and slam it. It matters only in events with
aggressive rivals (races, Road Rage and the like).

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Min. aggression before we start attacking | float, 0 .. 1 | Minimum aggression (the rival's 0..1 aggression value) needed before it looks for a car to attack: below it, `FindTarget` never picks a target, so the rival never chases, blocks, slams or speed-matches. 0 = every rival may attack. Only matters in events with aggressive rivals (races, Road Rage and similar). | [C 0x0032FF80] | 223 |
| Min. time to wait between attacks (seconds) | float | No effect: nothing reads it. The cool-down after an attack runs from 0 (not from this value) to the Max. time below, by the rival's aggression. | [L] | 224 |
| Max. time to wait between attacks (seconds) | float | Cool-down in seconds after a rival stops an attack before it may look for a new target: `StopAttacking` sets the wait to aggression x this value (so calm rivals re-attack almost at once and the most aggressive wait the full time; the minimum value is ignored). Only matters when rivals attack (races, Road Rage). | [C 0x0032F550] | 225 |
| Max. distance apart to begin attacking when ahead of target (meters) | float | Outside Road Rage: a rival only takes a car as its attack target when it is at most this many metres ahead of it along the track. Bigger = rivals that are in front of you will drop back to attack from further away. Road Rage ignores this window. | [C 0x0032FF80] | 226 |
| Max. distance apart to begin attacking when behind of target (meters) | float | Outside Road Rage: a rival only takes a car as its attack target when it is at most this many metres behind it along the track. Bigger = rivals start hunting you from further back. Road Rage ignores this window. | [C 0x0032FF80] | 227 |
| Min. target speed to consider attacking (mph) | float | Minimum speed in mph of the target car for an attack: below it a rival does not pick the target (races only), cannot start a slam, and gives up approaching (`MoveToTarget` stops the attack). Higher = rivals only attack you at high speed; 0 = they attack even when you are slow. | [C 0x0032FF80 0x0032FD90 0x0032F840] | 228 |
| How much slower than the player to drive while speed matching to attack position | float | Speed matching (outside Road Rage): when the rival's target is behind it by more than the speed-matching distance (AI/Driver "How close the target car must be..."), the rival drives at this fraction of its matching speed (but not below its normal wanted speed - 20 m/s) so you catch up. Smaller = rivals ahead of you brake harder to wait for you. Only matters while a rival has an attack target. | [C 0x00144180] | 229 |
| How far in front to boost when speed matching the player | float | Road Rage only (AI mode 1): a rival that started ahead of its target boosts at full AI top speed once it is more than this many metres ahead (minus the aggression term below). Only matters while a rival has an attack target. | [C 0x00144180] | 230 |
| Extra distance in front to boost relative to aggression (meters) | float | Road Rage only: added to the boost-in-front distance in proportion to the rival's aggression (threshold = base + aggression x this, so the default negative value lets aggressive rivals boost away sooner). Only matters while a rival has an attack target. | [C 0x00144180] | 231 |
| How long to wait at the start of the race before doing aggressive driving (seconds) | float | Seconds after the start of an event during which rivals do not attack: `Prepare` puts the aggression behaviour in its waiting state for this long (-1 = never attack). Bigger = a calmer start. Only matters in events with aggressive rivals. | [C 0x00331B30] | 232 |
| How long after hitting something to disable immunity (seconds) | float | Seconds after a rival last touched a player's car during which it can be wrecked normally. A rival that is attacking (not blocking) and has not touched a player for longer than this is made immune to fatal traffic and wall crashes. Bigger = rivals stay crashable longer after contact with you. Followed in the code; not checked in play. | [C 0x00330370 0x001D1130] | 233 |

#### Slam

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Min time to try and block you for (seconds) | float | Shortest time in seconds a rival keeps blocking you (driving just in front of you to stop you passing); the actual time is interpolated from this to the maximum by the rival's aggression. Only matters when rivals attack (races, Road Rage). | [C 0x00330370] | 234 |
| Max time to try and block you for (seconds) | float | Longest time in seconds a rival keeps blocking you; the most aggressive rivals block for this long. Only matters when rivals attack. | [C 0x00330370] | 235 |
| Max distance ahead to start blocking you (meters) | float | How far ahead of you (metres, plus the rival's car length) a rival may be and still start or keep blocking you; also scales where it aims (15..30 m ahead of your car, by how far ahead it is in that window). Bigger = rivals block from further ahead. Only matters when rivals attack. | [C 0x0032FF80 0x00330370] | 236 |
| Preferred car separation when getting into slam position (meters) | float | No effect: nothing reads it. | [L] | 237 |
| Max. time to try and get into slamming position (seconds) | float | Stored as the time limit of the "move into attack position" state (set on entering it), but that state's code never tests its time limit; it ends only when the rival can slam, can block or gives up. No visible effect found. | [C 0x00330370] | 238 |
| Max. distance between cars, when ahead (meters) | float | A slam can start only when the rival is level with you: at most this many metres ahead of you along the track (or less than half a car length behind). Bigger = rivals slam you from further ahead. Only matters when rivals attack. | [C 0x0032FD90] | 239 |
| Max. difference in speeds (MPH) | float | A slam can start only when your speed is at most this many mph above the rival's (if you are much faster it will not try). Bigger = rivals try to slam even while you pull away. Only matters when rivals attack. | [C 0x0032FD90] | 240 |
| Steer out distance (meters) | float | Before a slam from close alongside (under 2.5 m apart, or 1 m when the attack starts from block/target pick-up), the rival first steers away from you to a point this many metres to its side, to make room for the hit. Bigger = a wider wind-up before the slam. | [C 0x00330370] | 241 |
| Steer out time (seconds) | float | Seconds the rival spends steering out before it slams (then it switches to the slam), and also how long it steers away after a slam is broken off. Only matters when rivals attack. | [C 0x00330370 0x0032F550] | 242 |
| Slam time (seconds) | float | Seconds a slam lasts: the rival steers into the point just ahead of your car for this long, then stops attacking (the slam also ends early if you change sides or it can no longer slam). Bigger = longer, more persistent side-swipes. | [C 0x00330370 0x0032FB30] | 243 |
| Max. cos angle off lane to stop attack | float | Lane-alignment limit for slamming: the cosine of the angle between the rival's heading and the lane direction must be at least this, or it cannot (start or keep) a slam. Closer to 1 = rivals only slam on straight, well-aligned driving; lower = they slam even at an angle. | [C 0x0032FD90] | 244 |
| How soon after starting to slam is the AI car committed and can't stop | float, 0 .. 1 | The slam code compares it with how much of the slam time has passed, but never uses the result: no effect. | [C 0x0032FB30] | 245 |

#### Reactions

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| How long a rubbed AI car goes blind for (seconds) | float | Seconds a rival's avoidance goes blind after it was rubbed by another car: during that time its avoidance map is left empty (no traffic, no other racers, no no-go zones), so it drives straight on without swerving. (It is also blind for 2 s after being slammed, fixed in code.) Bigger = rivals stay dazed and hit traffic more after you rub them. | [C 0x00140850] | 246 |

### Car

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Out of range speed decrease rate | float | For a rival that is out of range (far from every player and moved along the AI lanes instead of being driven by physics): the most its speed may drop per update, in m/s, when it is faster than its wanted speed; within this step it is set straight to the wanted speed. Smaller = out-of-sight rivals slow down gradually instead of at once. | [C 0x001423E0] | 174 |
| Max desDir angle change OOR | float | For an out-of-range rival: the most its heading may turn per update, in degrees, toward the wanted direction. Smaller = smoother but slower turning of off-screen rivals along the road. | [C 0x001423E0] | 175 |
| Angle you want min spd at | float | Heading error (degrees between where the rival points and where it wants to go) at which its wanted speed falls to the AI minimum speed; between 0 and this angle the speed falls off linearly. Smaller = rivals slow down a lot for small steering corrections; bigger = they keep speed through bends. | [C 0x00144E70] | 176 |
| Top speed mps | float | AI top speed in m/s: the cap on the speed a rival aims for from its driving profile (`GetCurrentMaxSpeed`), the speed it uses when told to boost (speed matching, chasing), and +45 of it is the catch-up limit. Bigger = faster rivals (they are still limited by their car's physics); smaller = slower rivals. Applies to every AI racer in every mode. | [C 0x001446B0 0x00145A10 0x00143FA0 0x00144970] | 177 |
| Min speed mps | float | AI minimum speed in m/s: the floor for the speed a rival aims for (from its profile and when slowing for steering), and the base of its speed after being reset onto the road. Bigger = rivals never slow much (they take corners faster and crash more). | [C 0x00144810 0x00144E70 0x00143B10] | 178 |
| How much carAt affects steering | float | How much the rival's nose direction counts, against its direction of travel, when it works out the steering angle to its target (0 = only the direction it is moving, 1 = only where the nose points). Changes how rivals steer while sliding. | [C 0x001450C0] | 179 |
| Angle at which drift is started | float | The rival starts a drift into a corner when the steering angle it needs exceeds this many degrees (or when it is about to reach the apex, see "Time until we hit the apex..."). Smaller = rivals drift more often. | [C 0x00145700] | 180 |
| Max lock at 180 x degrees | float | Steering gain of the AI car: the steering input is the wanted steering angle x this / 180, clamped to full lock, so full lock is reached at 180/this degrees (30 degrees with the code default). Bigger = sharper, twitchier steering; smaller = lazier steering. | [C 0x001D17D0 0x001D1FD0] | 181 |
| Drift Max lock at 180 x degrees | float | The same steering gain while the AI car is drifting (full lock at 180/this degrees). Bigger = harder steering in drifts. | [C 0x001D17D0] | 182 |

### Arbitrator

The arbitrator decides, each frame, whether a rival follows its racing line or the avoidance path around traffic. "Risk"
is the avoidance map's danger figure for a path.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Risk threshold | float | Arbitrator (decides each frame whether the rival follows its racing line or the avoidance path), on straights: if the racing-line target's risk is more than this above the risk of the avoidance path, the rival switches to avoiding. Bigger = rivals hold their racing line and hit more traffic; smaller = they swerve more readily. | [C 0x0019D6D0] | 183 |
| Total risk threshold | float | As "Risk threshold", for the second (total) risk figure of the racing-line target versus the avoidance path, on straights. Bigger = rivals keep their line through more risk. | [C 0x0019D6D0] | 184 |
| Current risk threshold | float | On straights: if the risk at the rival's current position is above this, it uses the avoidance path at once (before the racing line is even checked). Smaller = rivals react sooner to things right in front of them. | [C 0x0019D6D0] | 185 |
| Corner Risk threshold | float | As "Risk threshold" but used while the rival is in a corner (turning in or through the corner). | [C 0x0019D6D0] | 186 |
| Total corner risk threshold | float | As "Total risk threshold" but used while the rival is in a corner. | [C 0x0019D6D0] | 187 |
| Current corner risk threshold | float | As "Current risk threshold" but used while the rival is in a corner. | [C 0x0019D6D0] | 188 |

### Avoidance

Each rival keeps an **avoidance map** of what lies ahead: traffic, other racers, wrecks and no-go lane areas (road
edges, closed lanes), each marked with a time and a distance to it. These values decide what goes into the map and how
the rival brakes and steers around it.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| AVOID: LookAhead dist racecars | float | Avoidance: other racers (not crashing) farther than this many metres from the rival are left out of its avoidance map, so it does not steer around them. Bigger = rivals give other cars (you included) a wider berth earlier; your own attack target is never avoided. | [C 0x0013F090] | 189 |
| AVOID: Soft No Go offset time | float | Avoidance: the time (seconds) written into the avoidance map for undriveable "no-go" lane areas on the segments ahead; smaller = treated as more urgent, so rivals keep further away from road edges and off-limits lanes. Grows per segment by "Extra softNoGo offset time for future". | [C 0x0013DF60] | 190 |
| AVOID: Soft No Go offset distance | float | Avoidance: the distance (metres) written into the avoidance map for no-go lane areas on the segments ahead; under 10/20/30 m it also triggers the avoidance speed caps, and it sets how far ahead the rival aims when swerving. Grows per segment by "Extra softNoGo offset dist for future". | [C 0x0013DF60] | 191 |
| Extra softNoGo offset dist for future | float | Avoidance: extra distance (metres) added to the soft no-go distance for each segment further ahead (up to 9 segments), so far no-go areas matter less than near ones. | [C 0x0013DF60] | 192 |
| Extra softNoGo offset time for future | float | Avoidance: extra time (seconds) added to the soft no-go time for each segment further ahead (up to 9 segments), so far no-go areas matter less than near ones. | [C 0x0013DF60] | 193 |
| Hard No Go offset time | float | Avoidance: the time (seconds) written for no-go lane areas in the segment the rival is in now (0 = "right now", the most urgent). Bigger = rivals tolerate being next to an edge. | [C 0x0013DF60] | 194 |
| Hard No Go offset distance | float | Avoidance: the distance (metres) written for no-go lane areas in the segment the rival is in now; with a large value they do not trigger the 10/20/30 m speed caps. | [C 0x0013DF60] | 195 |
| Distance to discard fatally colliding racecar | float | Avoidance: a crashing (wrecked) racer is added to the rival's avoidance map only when it is within this many metres. Bigger = rivals start dodging wrecks earlier. | [C 0x0013F090] | 196 |
| Vert dist to discard traffic and racecars | float | Avoidance: traffic, racers and crashing traffic more than this many metres above or below the rival are ignored (another level of road: bridges, flyovers). | [C 0x0013F090 0x0013EED0 0x0013E920] | 197 |
| dt between start and end vehicle | float | Avoidance: the time span (seconds) over which an obstacle's predicted path is marked in the map (from its position to its position after this time, centred on the predicted meeting time). Bigger = rivals react to cars' future positions over a longer window. | [C 0x0013F090 0x0013E920] | 198 |
| Speed when car is <10m away | float | Avoidance speed cap (m/s, assumed: it replaces the normal target speed) while swerving and the nearest obstacle in the middle of the rival's path is closer than 10 m. Smaller = rivals brake harder for traffic. | [C 0x00140A30] | 199 |
| Speed when car is <20m away | float | Avoidance speed cap (m/s) while the nearest obstacle in the rival's path is 10..20 m away. | [C 0x00140A30] | 200 |
| Speed when car is <30m away | float | Avoidance speed cap (m/s) while the nearest obstacle in the rival's path is 20..30 m away. | [C 0x00140A30] | 201 |
| Speed when risk is >0.95 | float | Avoidance speed cap (m/s) when the obstacles are farther than 30 m but the risk across the rival's width (0..1, from traffic, wrecks and no-go areas due within the map's time window) is above 0.95. | [C 0x00140A30] | 202 |
| Speed when risk is >0.9 | float | Avoidance speed cap (m/s) when the risk across the rival's width is above 0.9 (up to 0.95); at or below 0.9 the rival keeps its normal speed. | [C 0x00140A30] | 203 |
| Steering factor big=>extreme | float | Avoidance steering: when swerving into a gap, the rival aims 2 x (clear distance / this) metres ahead (at least 5 m). Bigger = a shorter aim point, so sharper, more extreme swerves. | [C 0x00140A30] | 204 |
| Aggression time variation factor | float | Avoidance: added (times a fixed 1.0 in the code, so it acts as a second offset) to the predicted meeting time of other racers in the map. Bigger = rivals treat other cars as less urgent and drive closer to them. | [C 0x0013F090] | 205 |
| Aggression dist variation factor | float | Avoidance: added (times a fixed 1.0, so another offset) to the distance figure of other racers in the map. Bigger = rivals treat other cars as further away. | [C 0x0013F090] | 206 |
| Aggression time variation offset | float | Avoidance: added to the predicted meeting time of other (non-crashing) racers in the map, together with the time factor above. Bigger = rivals are less afraid of other racers (more bumping). | [C 0x0013F090] | 207 |
| Aggression dist variation offset | float | Avoidance: added to the distance figure of other (non-crashing) racers in the map, together with the distance factor above. Bigger = less avoidance of other racers. | [C 0x0013F090] | 208 |

### Target

How a rival takes corners: its target points (turn-in, apex, exit) along the lane, when it brakes and when it drifts.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Dist to brake speed factor big=>fast | float | Braking for corners: before a corner, the rival's target speed is the corner's speed + (metres to the corner start x this). Bigger = it brakes later and harder (enters faster); smaller = it slows earlier. | [C 0x0014D280] | 209 |
| Drift speed threshold | float | A rival only drifts a corner when it is going faster than this (m/s) and faster than the corner's speed. Bigger = rivals drift only at high speed. | [C 0x0014D420] | 210 |
| Dist to update target pos | float | The rival counts its current corner target point as reached (and moves on to the next one: turn-in, then apex, then exit) when it is within this many metres of it. | [C 0x0014D420] | 211 |
| Perp dist to update target pos | float | The rival also counts the target point as reached when its along-the-lane distance to it is within this many metres (it has drawn level with the point). Bigger = it moves to the next point earlier. | [C 0x0014D420] | 212 |
| Corner radius to give max offset pos | float | How far the corner target points sit from the lane edge: offset = clamp(corner value / this, 0..1) x the car's width, so corners with a value at or above this get the full offset and tight ones hug the edge. Bigger = rivals cut closer to the edge on more corners. | [C 0x0014B010] | 213 |
| Time until we hit the apex at which to start drifting (seconds) | float | The rival also starts a drift when the time to reach the apex point (distance / speed, seconds) is below this. 0 = only the steering-angle test starts drifts. | [C 0x00145700] | 214 |
| Number of segments to extend the start of any corner | int | Number of lane segments by which the start (and end) of every corner zone is pushed out, for corners taken without drifting: the rival treats the corner as starting earlier (brakes and turns in sooner). | [C 0x0014B4B0] | 215 |
| Max. number of extra segments to extend the corner based on your speed | int | Extra segments added to the non-drifting corner zone in proportion to speed (full amount at 150 mph). Bigger = faster rivals start corners earlier. | [C 0x0014B4B0] | 216 |
| Number of segments to extend the start of any corner (when drifting) | int | As "Number of segments to extend the start of any corner", for corners the rival will drift (corner speed below its approach speed). | [C 0x0014B4B0] | 217 |
| Max. number of extra segments to extend the corner based on your speed (when drifting) | int | As "Max. number of extra segments...", for corners the rival will drift. | [C 0x0014B4B0] | 218 |
| How far away from the true apex to move the apex point of a corner when drifting (meters) | float | When drifting a corner, the apex/edge target point is moved this many metres further from the lane edge. Bigger = wider drifting lines. | [C 0x0014A980] | 219 |

### Driver

Speed matching: how a rival that is attacking holds its position beside or in front of its target.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| How close the target car must be to start proper speed matching (meters) | float | Speed matching outside Road Rage: when the gap along the track to the rival's target is larger than this (metres), the rival doubles its matching speed to catch up (target ahead) or slows (target behind, see "How much slower..."); within it, it matches the target's speed plus the gap. Only matters while a rival has an attack target. | [C 0x00144180] | 220 |
| Maximum distance from player to start doing sticky speed matching (metres) | float | "Sticky" speed matching (the rival holds your speed exactly, within 1 m/s, to stay beside you): allowed when the along-track gap is within 3 + aggression x (this - 3) metres. Road Rage uses a fixed 40 m instead. Only matters while a rival has an attack target. | [C 0x0032F0D0] | 221 |
| Maximum diffence in speeds for sticky speed matching (MPH) | float | Sticky speed matching outside Road Rage needs the two cars' speeds within this many mph (and you above 100 mph; 60 mph in Road Rage). Bigger = rivals lock on to you more easily. | [C 0x0032F0D0] | 222 |

## TRAFFIC

The traffic models' physics (per traffic model, from its `VehiclePhysics/<model>.cfg`) and the traffic's sounds. A
traffic vehicle drives on rails until something hits it; only then does it get full physics, with the mass and
suspension below copied in: a change applies to the next traffic vehicle you hit. The console lists one subsection per
traffic model on the road.

### Suspension Front

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Attatch Height | float | Per traffic model, metres in the body's frame: where the front springs attach; with Spring Length it sets the ride height. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 102 |
| Spring Length | float | Per traffic model, metres: the front springs' rest length; the wheel travels from 0.25 to 0.75 of it. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 103 |
| Spring Force | float | Per traffic model: the front springs' stiffness (force per metre of compression). Bigger = a stiffer, higher front that bounces more in impacts. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 104 |
| Spring Damping | float | Per traffic model: the front dampers (force per unit of compression speed). Bigger = less bouncing after bumps and landings. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 105 |

### Suspension Rear

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Attatch Height | float | Per traffic model, metres in the body's frame: where the rear springs attach; with Spring Length it sets the ride height. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 106 |
| Spring Length | float | Per traffic model, metres: the rear springs' rest length; the wheel travels from 0.25 to 0.75 of it. Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 107 |
| Spring Force | float | Per traffic model: the rear springs' stiffness (force per metre of compression). Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 108 |
| Spring Damping | float | Per traffic model: the rear dampers (force per unit of compression speed). Applies to the next traffic vehicle you hit. | [C 0x00252050 0x001CC900] | 109 |

### Mass

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Mass (Kg) | float | Per traffic model, kg. The traffic vehicle's mass, copied into it when a hit knocks it into full physics (the next traffic vehicle you hit uses a change). Heavier traffic is pushed less and hits back harder. Racing cars use HANDLING > Mass instead. | [C 0x00252050 0x001CC900] | 101 |

### Traffic sounds

#### General

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Bus volume | float | Volume (linear factor) of the bus's driving-past loop, as Lorry volume. 0 silences it. | [C 0x00278F20 0x002795F0] | 247 |
| Lorry volume | float | Volume (linear factor) of the articulated lorry's (cab and trailer) driving-past loop. Each traffic vehicle copies it when it appears; each frame the loop plays at this x min(speed / 40 m/s, 1) x the distance fade, so it gets louder as the vehicle speeds up. 0 silences it. | [C 0x00278F20 0x002795F0] | 248 |
| Van volume | float | Volume (linear factor) of the van's and rigid lorry's driving-past loop, as Lorry volume. 0 silences it. | [C 0x00278F20 0x002795F0] | 249 |
| Car volume | float | Volume (linear factor) of the car's driving-past loop, as Lorry volume. The code also uses it as the horn volume of cars, vans and rigid lorries (where Car Horn Volume would be expected), so lowering it quietens their horns too. | [C 0x00278F20 0x002795F0 0x00278B20] | 250 |
| Bus Pitch | float | Pitch factor (1 = the sample's recorded pitch) of the bus driving-past loop of each traffic vehicle; multiplied by the Doppler pitch (TRAFFIC SOUNDS > DOPPLER) and the sample rate every frame. Higher = higher-pitched traffic. | [C 0x00278F20 0x002795F0] | 251 |
| Lorry Pitch | float | Pitch factor (1 = the sample's recorded pitch) of the articulated-lorry cab and trailer driving-past loop of each traffic vehicle; multiplied by the Doppler pitch (TRAFFIC SOUNDS > DOPPLER) and the sample rate every frame. Higher = higher-pitched traffic. | [C 0x00278F20 0x002795F0] | 252 |
| Van Pitch | float | Pitch factor (1 = the sample's recorded pitch) of the van and rigid lorry driving-past loop of each traffic vehicle; multiplied by the Doppler pitch (TRAFFIC SOUNDS > DOPPLER) and the sample rate every frame. Higher = higher-pitched traffic. | [C 0x00278F20 0x002795F0] | 253 |
| Car Pitch | float | Pitch factor (1 = the sample's recorded pitch) of the car driving-past loop of each traffic vehicle; multiplied by the Doppler pitch (TRAFFIC SOUNDS > DOPPLER) and the sample rate every frame. Higher = higher-pitched traffic. Also used for trams and the monorail, whose loop volume is fixed at 0 (silent). | [C 0x00278F20 0x002795F0] | 254 |
| Bus Engine Pitch | float | Pitch of the separate engine loop that only buses have: the voice pitch is this x min(vehicle speed / 10 m/s, 1), never below 0.5, x the sample rate (no Doppler on the engine loop). Higher = higher-revving engine. | [C 0x00278F20 0x002795F0] | 255 |
| Bus Engine Volume | float | Volume (linear factor) of the separate engine loop that only buses have: this x min(vehicle speed / 40 m/s, 1) x the distance fade, with a small fixed floor. 0 makes the engine loop near-silent. | [C 0x00278F20 0x002795F0] | 256 |
| Lorry Engine Pitch | float | Pitch of the separate engine loop that only articulated-lorry cabs have: the voice pitch is this x min(vehicle speed / 10 m/s, 1), never below 0.5, x the sample rate (no Doppler on the engine loop). Higher = higher-revving engine. | [C 0x00278F20 0x002795F0] | 257 |
| Lorry Engine Volume | float | Volume (linear factor) of the separate engine loop that only articulated-lorry cabs have: this x min(vehicle speed / 40 m/s, 1) x the distance fade, with a small fixed floor. 0 makes the engine loop near-silent. | [C 0x00278F20 0x002795F0] | 258 |
| Horn Min On Time | int | Shortest time, in frames (traffic sound updates), that a blipping traffic horn stays ON; each blip lasts Min + random(Max - Min). Only about 1 in 5 horning vehicles blip this way (the rest hold a steady horn or follow a fixed pattern table). Horn blips use 8-bit signed countdowns: keep it at 127 or below (larger values wrap negative and switch at once) and keep Max above Min (the code takes a random value modulo Max - Min). | [C 0x002795F0] | 273 |
| Horn Min Off Time | int | Shortest silent gap, in frames, between blips of a blipping traffic horn (also the first gap after the horn starts); each gap is Min + random(Max - Min). Only the randomly-blipping vehicles (about 1 in 5) use it. Horn blips use 8-bit signed countdowns: keep it at 127 or below (larger values wrap negative and switch at once) and keep Max above Min (the code takes a random value modulo Max - Min). | [C 0x002795F0] | 274 |
| Horn Max On Time | int | Longest ON time, in frames, of a blipping traffic horn (see Horn Min On Time). Horn blips use 8-bit signed countdowns: keep it at 127 or below (larger values wrap negative and switch at once) and keep Max above Min (the code takes a random value modulo Max - Min). | [C 0x002795F0] | 275 |
| Horn Max Off Time | int | Longest silent gap, in frames, between blips of a blipping traffic horn (see Horn Min Off Time). Horn blips use 8-bit signed countdowns: keep it at 127 or below (larger values wrap negative and switch at once) and keep Max above Min (the code takes a random value modulo Max - Min). | [C 0x002795F0] | 276 |
| Horn Speed of Sound | float | The speed of sound (m/s) in the traffic horns' own Doppler formula: horn pitch = c / (c - closing speed) x sample rate, where each vehicle gets c = this x a random 0.5..1.5 when it spawns. Smaller = stronger pitch rise and drop as you pass a horning vehicle; larger = flatter horns. | [C 0x00278F20 0x00278B20] | 277 |
| Horn Max Distance | float | Maximum audible distance (metres) given to the traffic horn voice: past it the horn fades to silence. Bigger = horns carry further. It does not change when a horn starts (that is fixed: an oncoming vehicle within 50 m). | [C 0x00278D20 0x00278B20] | 278 |
| Horn Pitch Variance | float | Random spread of each traffic vehicle's horn pitch: at spawn the vehicle picks a pitch factor between 1 - this and 1 + this (0 = every horn of a kind at the same pitch; 0.2 = up to +/-20%). | [C 0x00278F20 0x00278B20] | 279 |
| Truck Horn Volume | float | Horn volume (linear factor) of buses and articulated-lorry cabs. 0 stops those vehicles horning at all (StartHorn skips a vehicle whose horn volume is 0). The horn's volume also grows with your boost level (x 0.25..1). | [C 0x00278F20 0x00278D20 0x00278B20] | 280 |
| Car Horn Volume | float | No effect: nothing reads it. Car, van and rigid-lorry horns take their volume from `Car volume` instead (the code reads that field where this one would be expected). | [L] | 281 |
| Horn Linear Scalar | float | Shapes the horn's fade with distance: horn level = max(t x this, t^2) where t = 1 - distance/100 m (t^4 instead of t^2 while boosting hard). 0 = a quick quadratic fade; larger = horns stay louder further away. Horns are silent while your car is under 15 m/s. | [C 0x002795F0] | 282 |

#### Crashing Skids / Large

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Large Crashing Traffic Min Speed | float | Speed (m/s) of big crashed traffic (lorries, articulated cabs, buses) at or below which its tyre-skid sound uses the Min volume and pitch; the sound plays while a hit traffic vehicle is sliding. Keep it below Max Speed (equal values divide by zero). | [C 0x0028FF30] | 283 |
| Large Crashing Traffic Max Speed | float | Speed (m/s) of big crashed traffic (lorries, articulated cabs, buses) at or above which its skid sound reaches the Max volume and pitch; between Min and Max Speed both are interpolated linearly. | [C 0x0028FF30] | 284 |
| Large Crashing Traffic Min Vol | float | Skid-sound volume (linear factor) of big crashed traffic (lorries, articulated cabs, buses) at Min Speed or slower (also scaled by the skid's fade in/out). | [C 0x0028FF30] | 285 |
| Large Crashing Traffic Max Vol | float | Skid-sound volume (linear factor) of big crashed traffic (lorries, articulated cabs, buses) at Max Speed or faster. | [C 0x0028FF30] | 286 |
| Large Crashing Traffic Min Pitch | float | Skid-sound pitch factor of big crashed traffic (lorries, articulated cabs, buses) at Min Speed or slower (x a random per-skid variation, x up to 1.25 while fading in, x the sample rate). | [C 0x0028FF30] | 287 |
| Large Crashing Traffic Max Pitch | float | Skid-sound pitch factor of big crashed traffic (lorries, articulated cabs, buses) at Max Speed or faster. | [C 0x0028FF30] | 288 |

#### Crashing Skids / Small

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Small Crashing Traffic Min Speed | float | Speed (m/s) of small crashed traffic (cars, vans) at or below which its tyre-skid sound uses the Min volume and pitch; the sound plays while a hit traffic vehicle is sliding. Keep it below Max Speed (equal values divide by zero). | [C 0x0028FF30] | 289 |
| Small Crashing Traffic Max Speed | float | Speed (m/s) of small crashed traffic (cars, vans) at or above which its skid sound reaches the Max volume and pitch; between Min and Max Speed both are interpolated linearly. | [C 0x0028FF30] | 290 |
| Small Crashing Traffic Min Vol | float | Skid-sound volume (linear factor) of small crashed traffic (cars, vans) at Min Speed or slower (also scaled by the skid's fade in/out). | [C 0x0028FF30] | 291 |
| Small Crashing Traffic Max Vol | float | Skid-sound volume (linear factor) of small crashed traffic (cars, vans) at Max Speed or faster. | [C 0x0028FF30] | 292 |
| Small Crashing Traffic Min Pitch | float | Skid-sound pitch factor of small crashed traffic (cars, vans) at Min Speed or slower (x a random per-skid variation, x up to 1.25 while fading in, x the sample rate). | [C 0x0028FF30] | 293 |
| Small Crashing Traffic Max Pitch | float | Skid-sound pitch factor of small crashed traffic (cars, vans) at Max Speed or faster. | [C 0x0028FF30] | 294 |

#### Doppler

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Point Distances | float ×4 | Four signed distances (metres) that place the Doppler curve's points, in descending order: index 0..3 run from far moving-apart (positive) through just passed / just arriving to far approaching (negative; the distance is negated while the vehicle closes on you). The traffic loop's pitch is interpolated between the two points around the current distance and held at the end values beyond them. | [C 0x002F4BA0 0x00278830] | 295 |
| Point Scalar | float ×4 | Per Doppler point (index 0..3, as Point Distances): weight of a speed term added to the pitch, (0.5 x your boost level + relative speed / 150 m/s) / 2. 0 at a point = no boost/speed effect there; larger = more pitch shift with speed and boost. | [C 0x002F4BA0 0x00278830] | 296 |
| Base Pitch | float ×4 | Per Doppler point (index 0..3, as Point Distances): the pitch factor of a traffic vehicle's driving-past loop at that distance (x a mild speed term (1360 - relative speed)/1360). Setting the approaching points above the moving-apart points gives the classic rising-then-falling pass-by; final pitch is clamped to at most 2. Horns and bus/lorry engine loops have their own pitch and do not use this curve. | [C 0x002F4BA0 0x00278830] | 297 |

#### L train

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Max distance | float | Maximum audible distance (metres) of the L train's rail and squeak sounds (Chicago-style elevated train): beyond it they fade to silence. | [C 0x002795F0] | 298 |
| Min distance | float | Minimum distance (metres) of the L train's rail and squeak sounds: closer than this they play at full volume; between Min and Max distance they fade out. | [C 0x002795F0] | 299 |
| Max pitch gain | float | Upper limit of the slow wobble of the L train rail loop's pitch: each frame the pitch factor moves by Pitch gain step towards this, then turns back down to Min pitch gain. | [C 0x002795F0] | 300 |
| Min pitch gain | float | Lower limit of the slow wobble of the L train rail loop's pitch (see Max pitch gain). | [C 0x002795F0] | 301 |
| Pitch gain step | float | How much the L train rail loop's pitch factor moves per frame between Min and Max pitch gain. Bigger = a faster wobble; 0 = steady pitch. | [C 0x002795F0] | 302 |
| Squeak base pitch | float | Lowest pitch factor of an L train wheel squeak; each squeak picks base + random(0..range). Only every third carriage squeaks. | [C 0x002795F0] | 303 |
| Squeak pitch range | float | Random pitch spread added to Squeak base pitch for each L train squeak (0 = every squeak at the base pitch). | [C 0x002795F0] | 304 |
| Length of squeak (frames) | int | Minimum length of an L train squeak, in frames; each squeak lasts this + random(0..variation-1). The countdown is 8-bit, so keep the total below 256. | [C 0x002795F0] | 305 |
| Frames between squeaks | int | Minimum gap between an L train carriage's squeaks, in frames; each gap is this + random(0..variation-1); a carriage's first squeak comes after a random part of it. Keep the total below 256 (8-bit countdown). | [C 0x00278F20 0x002795F0] | 306 |
| Squeak duration variation (frames) | int | Random extra length (frames) added to each L train squeak. Keep it at least 1: the code takes a random value modulo it. | [C 0x002795F0] | 307 |
| Time between squeaks variation (frames) | int | Random extra gap (frames) added between L train squeaks. Keep it at least 1 (random value modulo it). | [C 0x002795F0] | 308 |
| Rails volume | float | Volume (linear factor) of the L train's rail-rumble loop on each carriage. 0 silences the rails. | [C 0x002795F0] | 309 |
| Squeak base volume | float | Lowest volume (linear factor) of an L train squeak; each squeak picks base + random(0..range). | [C 0x00278F20 0x002795F0] | 310 |
| Squeak volume range | float | Random volume spread added to Squeak base volume for each L train squeak. | [C 0x002795F0] | 311 |

## CAMERA

The race cameras of each car model (`Camera/<car>.cfg`, registered for the cars players drive: the console shows your
car's) and the shared crash and shake values (`Camera/Camera.cfg`). Vectors are X (right), Y (up), Z (forward) in the
car's axes, in metres.

### Bumper

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Camera Offset | vector X Y Z | The bumper camera's position, in metres in the car's axes (X right, Y up, Z forward) from the car's origin. The camera sits rigidly there, looking along the car (90 degrees of field of view, widened by the boost effect). | [C 0x002E2730] | 647 |

### Look Back

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Camera Offset | vector X Y Z | The look-back camera's position, in metres in the car's axes (the code adds 0.25 m to Y), facing backwards. The look-back button uses it in both the chase and the bumper view. | [C 0x002E2730] | 648 |

### Follow

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Camera Offset | vector X Y Z | The chase camera's position from the Focus Offset point, in metres: X sideways, Y up, Z along the car (negative = behind). The offset swings with the camera's lagging pitch and yaw, so the camera moves round the car in turns. More negative Z = further behind, bigger Y = higher. | [C 0x002E3440] | 649 |
| Focus Offset | vector X Y Z | The point on the car (metres, car axes) the chase camera is placed around: the camera sits at this point + the rotated Camera Offset. It does not aim at it, so raising it raises the whole camera and moves the car down the screen. | [C 0x002E3440] | 650 |
| Spring Coeff | vector X Y Z | How stiffly the chase camera follows. X = pitch: each frame the camera keeps (1 - X) of its pitch lag (scaled by speed up to 50 mph), so a bigger X follows hills faster. Y = yaw: each frame it keeps (1 - Y) of the angle to the car's heading; 1 = locked behind the car, smaller = it swings out more in turns and drifts. Z is not used. | [C 0x002E3440] | 651 |
| Down Angle | float | The chase camera's downward tilt, in degrees, turned about its own axis without moving it: bigger = the view looks further down and the car sits higher on screen. | [C 0x002E3440] | 652 |

### Crash camera

The crash "handheld camera" values are registered but read by nothing.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Spring Coeff | vector X Y Z | No effect: nothing reads it. The game's crash and shake camera effects are fixed in code. | [L] | 613 |
| Damping | vector X Y Z | No effect: nothing reads it. The game's crash and shake camera effects are fixed in code. | [L] | 614 |
| Safe FOV Horiz | float | No effect: nothing reads it. The game's crash and shake camera effects are fixed in code. | [L] | 615 |
| Safe FOV Vert | float | No effect: nothing reads it. The game's crash and shake camera effects are fixed in code. | [L] | 616 |

### Shake effects

Camera shake springs (spring coefficient, damping, kick force; a position and an angular set) for five events: Burnout,
Shunt (you hit a car), Shunted (a car hits you), Slam and Shake. **None of them is read** [L]: the game's camera shakes
are fixed damped waves in its camera code [C 0x0032D200], so these 30 values change nothing.

#### Burnout / Position

| Value | # |
|---|---|
| Spring Coeff | 617 |
| Damping | 618 |
| Force | 619 |

#### Burnout / Angular

| Value | # |
|---|---|
| Spring Coeff | 620 |
| Damping | 621 |
| Force | 622 |

#### Shunt / Position

| Value | # |
|---|---|
| Spring Coeff | 623 |
| Damping | 624 |
| Force | 625 |

#### Shunt / Angular

| Value | # |
|---|---|
| Spring Coeff | 626 |
| Damping | 627 |
| Force | 628 |

#### Shunted / Position

| Value | # |
|---|---|
| Spring Coeff | 629 |
| Damping | 630 |
| Force | 631 |

#### Shunted / Angular

| Value | # |
|---|---|
| Spring Coeff | 632 |
| Damping | 633 |
| Force | 634 |

#### Slam / Position

| Value | # |
|---|---|
| Spring Coeff | 635 |
| Damping | 636 |
| Force | 637 |

#### Slam / Angular

| Value | # |
|---|---|
| Spring Coeff | 638 |
| Damping | 639 |
| Force | 640 |

#### Shake / Position

| Value | # |
|---|---|
| Spring Coeff | 641 |
| Damping | 642 |
| Force | 643 |

#### Shake / Angular

| Value | # |
|---|---|
| Spring Coeff | 644 |
| Damping | 645 |
| Force | 646 |

## AUDIO

Volumes are linear factors (1 = the sample as recorded, 0 = silent, 2 = twice the amplitude) and pitches are
playback-rate factors (1 = the recorded pitch, 2 = an octave up), unless an entry says otherwise. Many of the game's
sound values are registered but read by nothing: their entries say "No effect". Traffic sounds are in TRAFFIC, road
noise per surface in SURFACES.

### Skids

The tyre squeal of every race car (`Sound/Skids.cfg`, one set, read every frame). Each side of the car has an
emitter playing three layered samples. The rows directly in this section shape the **skid input** the samples follow:
the slide (wheels turning slower than the road: sliding, drifting, locked brakes) and the wheelspin (wheels turning
faster than the road).

The subsections are the three samples' **graphs**, each of two points mapping the skid input to an output: below the X
of point 0 the output is its Y, above the X of point 1 it is that point's Y, a straight line in between (keep X0 below
X1) [C 0x0028E2E0 0x0028E120]. Slip is the slide graph, Spin the wheelspin graph:

| Value | What it does |
|---|---|
| Sample N Slip / Spin Volume Input Point 0, 1 (X) | The skid input (0 .. Maximum Slip, or 0 .. Maximum Spin) at the graph's two points. |
| Sample N Slip / Spin Volume Output Point 0, 1 (Y) | The sample's volume (linear factor) at those points. A result near 0 stops the sample; the three samples' graphs crossfade them as the slide grows. |
| Sample N Slip / Spin Frequency Input Point 0, 1 (X) | The same for the pitch graph (the `Variant1` subsections, the only frequency variant registered). |
| Sample N Slip / Spin Frequency Output Point 0, 1 (Y) | A frequency offset in Hz added to the sample's own rate at those points: positive = the squeal rises in pitch. |

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Maximum Slip | float | Cap on the skid input in the slide direction (wheels turning slower than the road: sliding, drifting, locked brakes), per wheel and for each side's two-wheel sum. It sets the top of the range the samples' Slip graphs see. Your (and every race car's) tyre squeal. | [C 0x0028E2E0] | 422 |
| Maximum Spin | float | Cap on the skid input in the wheelspin direction (wheels turning faster than the road), per wheel and per side; the top of the range the samples' Spin graphs see. | [C 0x0028E2E0] | 423 |
| Emitter Distance AT | float | How far forward (metres, along the car's heading) the two tyre-skid emitters sit from the car's centre. Mostly changes where the squeal seems to come from. | [C 0x0028EA10] | 424 |
| Emitter Distance RIGHT | float | How far out to each side (metres) the left and right tyre-skid emitters sit from the car's centre; there is one emitter (3 samples) per side. | [C 0x0028EA10] | 425 |
| SlipSpin Lag Min | float | Fastest drop per frame of the smoothed skid value (negative number): the change towards a lower value (less slide / more wheelspin) is limited to this. Closer to 0 = squeal lingers and fades slowly; more negative = it cuts off quicker. | [C 0x0028E2E0] | 426 |
| SlipSpin Lag Max | float | Fastest rise per frame of the smoothed skid value: the change towards more slide is limited to this. Smaller = squeal builds up gradually; larger = instant squeal. | [C 0x0028E2E0] | 427 |
| Wheel Mode 'Driving' Multipler | float | Multiplies the skid input of a wheel while it is in the 'Driving' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 428 |
| Wheel Mode 'StartLine' Multipler | float | Multiplies the skid input of a wheel while it is in the 'StartLine' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 429 |
| Wheel Mode 'Donuts' Multipler | float | Multiplies the skid input of a wheel while it is in the 'Donuts' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 430 |
| Wheel Mode 'Drift' Multipler | float | Multiplies the skid input of a wheel while it is in the 'Drift' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 431 |
| Wheel Mode 'Skid' Multipler | float | Multiplies the skid input of a wheel while it is in the 'Skid' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 432 |
| Wheel Mode 'LockUp' Multipler | float | Multiplies the skid input of a wheel while it is in the 'LockUp' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 433 |
| Wheel Mode 'OutOfControl' Multipler | float | Multiplies the skid input of a wheel while it is in the 'OutOfControl' wheel state (index into the per-mode table by the wheel's mode). 0 = no skid sound in that state; above 1 = louder/stronger squeal. | [C 0x0028E2E0] | 434 |
| Left Pitch Offset | float | Fixed frequency offset, in Hz, added to the left emitter's skid samples (on top of the samples' frequency graphs), so the two sides don't sound identical. | [C 0x0028E2E0] | 435 |
| Right Pitch Offset | float | Fixed frequency offset, in Hz, added to the right emitter's skid samples. | [C 0x0028E2E0] | 436 |
| Crash Volume Multipler | float | Volume factor applied to a car's skid sounds while that car is crashing (wrecked and tumbling: `StartCrashing` sets the flag the skid update reads). 0 = silent skids during crashes. | [C 0x0028E2E0 0x001519F0] | 437 |
| Slip Speed Multipler | float | Scales the slide input by the car's speed: when a wheel slides, its slip (road speed minus wheel rim speed, m/s, x the wheel-mode multiplier) is multiplied by speed x this. Bigger = squeal grows faster with speed; does not affect wheelspin. | [C 0x0028E2E0] | 438 |

#### Sample1

| Value | # |
|---|---|
| Sample 1 Slip Volume Input Point  0 (X) | 439 |
| Sample 1 Slip Volume Output Point 0 (Y) | 440 |
| Sample 1 Slip Volume Input Point  1 (X) | 441 |
| Sample 1 Slip Volume Output Point 1 (Y) | 442 |
| Sample 1 Spin Volume Input Point  0 (X) | 447 |
| Sample 1 Spin Volume Output Point 0 (Y) | 448 |
| Sample 1 Spin Volume Input Point  1 (X) | 449 |
| Sample 1 Spin Volume Output Point 1 (Y) | 450 |

#### Sample1 / Variant1

| Value | # |
|---|---|
| Sample 1 Slip Frequency Input Point  0 (X) | 443 |
| Sample 1 Slip Frequency Output Point 0 (Y) | 444 |
| Sample 1 Slip Frequency Input Point  1 (X) | 445 |
| Sample 1 Slip Frequency Output Point 1 (Y) | 446 |
| Sample 1 Spin Frequency Input Point  0 (X) | 451 |
| Sample 1 Spin Frequency Output Point 0 (Y) | 452 |
| Sample 1 Spin Frequency Input Point  1 (X) | 453 |
| Sample 1 Spin Frequency Output Point 1 (Y) | 454 |

#### Sample2

| Value | # |
|---|---|
| Sample 2 Slip Volume Input Point  0 (X) | 455 |
| Sample 2 Slip Volume Output Point 0 (Y) | 456 |
| Sample 2 Slip Volume Input Point  1 (X) | 457 |
| Sample 2 Slip Volume Output Point 1 (Y) | 458 |
| Sample 2 Spin Volume Input Point  0 (X) | 463 |
| Sample 2 Spin Volume Output Point 0 (Y) | 464 |
| Sample 2 Spin Volume Input Point  1 (X) | 465 |
| Sample 2 Spin Volume Output Point 1 (Y) | 466 |

#### Sample2 / Variant1

| Value | # |
|---|---|
| Sample 2 Slip Frequency Input Point  0 (X) | 459 |
| Sample 2 Slip Frequency Output Point 0 (Y) | 460 |
| Sample 2 Slip Frequency Input Point  1 (X) | 461 |
| Sample 2 Slip Frequency Output Point 1 (Y) | 462 |
| Sample 2 Spin Frequency Input Point  0 (X) | 467 |
| Sample 2 Spin Frequency Output Point 0 (Y) | 468 |
| Sample 2 Spin Frequency Input Point  1 (X) | 469 |
| Sample 2 Spin Frequency Output Point 1 (Y) | 470 |

#### Sample3

| Value | # |
|---|---|
| Sample 3 Slip Volume Input Point  0 (X) | 471 |
| Sample 3 Slip Volume Output Point 0 (Y) | 472 |
| Sample 3 Slip Volume Input Point  1 (X) | 473 |
| Sample 3 Slip Volume Output Point 1 (Y) | 474 |
| Sample 3 Spin Volume Input Point  0 (X) | 479 |
| Sample 3 Spin Volume Output Point 0 (Y) | 480 |
| Sample 3 Spin Volume Input Point  1 (X) | 481 |
| Sample 3 Spin Volume Output Point 1 (Y) | 482 |

#### Sample3 / Variant1

| Value | # |
|---|---|
| Sample 3 Slip Frequency Input Point  0 (X) | 475 |
| Sample 3 Slip Frequency Output Point 0 (Y) | 476 |
| Sample 3 Slip Frequency Input Point  1 (X) | 477 |
| Sample 3 Slip Frequency Output Point 1 (Y) | 478 |
| Sample 3 Spin Frequency Input Point  0 (X) | 483 |
| Sample 3 Spin Frequency Output Point 0 (Y) | 484 |
| Sample 3 Spin Frequency Input Point  1 (X) | 485 |
| Sample 3 Spin Frequency Output Point 1 (Y) | 486 |

### Props hit

The sound of a car hitting a roadside prop (cones, boxes, signposts, barriers: `Sound/Tracks/<route>.cfg`, per
route) or an object shed from a truck's load (barrels, crates, boats, hay, cable drums, containers, logs:
`Sound/Payload.cfg`). Read at each hit. Every prop has the same four values:

| Value | What it does | Evidence |
|---|---|---|
| Min Impulse | Roadside props: the weakest hit (collision impulse) that makes a sound; weaker hits are silent. Shed loads: the hit strength that plays at Min Volume (weaker hits, above a fixed floor, still play at Min Volume); for the Log only, hits at or below it are silent. Keep it below Max Impulse. | [C 0x0027F5E0 0x0027F8D0] |
| Max Impulse | The hit strength at which the sound reaches Max Volume; between Min and Max Impulse the volume is a straight blend. | [C 0x0027F5E0 0x0027F8D0] |
| Min Volume | Volume (linear factor) of a hit at Min Impulse. Roadside props: the volume is also lowered at random by up to a fixed fraction, and halved for hits found by the effects' own collision pass; at most one sound per prop type every 4 updates. Shed loads: a small random pitch change per hit; at most one hit sound every 4 frames. | [C 0x0027F5E0 0x0027F8D0] |
| Max Volume | Volume (linear factor) of a hit at Max Impulse or harder. | [C 0x0027F5E0 0x0027F8D0] |

The first seven subsections (Cone to Metal Barrier) are the per-route roadside props, the other eight the shed loads.

#### Cone

| Value | # |
|---|---|
| Max Impulse | 312 |
| Min Impulse | 313 |
| Max Volume | 314 |
| Min Volume | 315 |

#### Card Box

| Value | # |
|---|---|
| Max Impulse | 316 |
| Min Impulse | 317 |
| Max Volume | 318 |
| Min Volume | 319 |

#### Metal Box

| Value | # |
|---|---|
| Max Impulse | 320 |
| Min Impulse | 321 |
| Max Volume | 322 |
| Min Volume | 323 |

#### Woodbox

| Value | # |
|---|---|
| Max Impulse | 324 |
| Min Impulse | 325 |
| Max Volume | 326 |
| Min Volume | 327 |

#### Signpost

| Value | # |
|---|---|
| Max Impulse | 328 |
| Min Impulse | 329 |
| Max Volume | 330 |
| Min Volume | 331 |

#### Barrier

| Value | # |
|---|---|
| Max Impulse | 332 |
| Min Impulse | 333 |
| Max Volume | 334 |
| Min Volume | 335 |

#### Metal Barrier

| Value | # |
|---|---|
| Max Impulse | 336 |
| Min Impulse | 337 |
| Max Volume | 338 |
| Min Volume | 339 |

#### Barrel

| Value | # |
|---|---|
| Max Impulse | 340 |
| Min Impulse | 341 |
| Max Volume | 342 |
| Min Volume | 343 |

#### Crate

| Value | # |
|---|---|
| Max Impulse | 344 |
| Min Impulse | 345 |
| Max Volume | 346 |
| Min Volume | 347 |

#### Boat

| Value | # |
|---|---|
| Max Impulse | 348 |
| Min Impulse | 349 |
| Max Volume | 350 |
| Min Volume | 351 |

#### Hay Bale

| Value | # |
|---|---|
| Max Impulse | 352 |
| Min Impulse | 353 |
| Max Volume | 354 |
| Min Volume | 355 |

#### Hay Drum

| Value | # |
|---|---|
| Max Impulse | 356 |
| Min Impulse | 357 |
| Max Volume | 358 |
| Min Volume | 359 |

#### Cable Drum

| Value | # |
|---|---|
| Max Impulse | 360 |
| Min Impulse | 361 |
| Max Volume | 362 |
| Min Volume | 363 |

#### Container

| Value | # |
|---|---|
| Max Impulse | 364 |
| Min Impulse | 365 |
| Max Volume | 366 |
| Min Volume | 367 |

#### Log

| Value | # |
|---|---|
| Max Impulse | 368 |
| Min Impulse | 369 |
| Max Volume | 370 |
| Min Volume | 371 |

### HUD

#### General

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Popup Message Volume | float | Volume (linear factor) of the general pop-up chime played when a HUD message pops up on the boost bar (`GeneralPopup`, called from the boost bar's message update). | [C 0x00285BC0] | 372 |
| Position Message Volume | float | No effect: nothing reads it. The position sounds play at fixed volumes. | [L] | 373 |
| Lap Message Volume | float | No effect: nothing reads it. The lap sounds play at fixed volumes. | [L] | 374 |
| Touch Message Volume | float | No effect: nothing reads it. The touch sounds play at fixed volumes. | [L] | 375 |
| Slam Message Volume | float | No effect: nothing reads it. The slam sounds play at fixed volumes. | [L] | 376 |
| Slammed Message Volume | float | No effect: nothing reads it. The slammed sounds play at fixed volumes. | [L] | 377 |
| Taken down Message Volume | float | Volume (linear factor) of the Takedown message sound: it plays twice, at this volume and at half of it, pitched up in steps for double, triple and bigger multi-takedowns. | [C 0x00285C00] | 378 |
| Taken out Message Volume | float | No effect: nothing reads it. The taken-out sounds play at fixed volumes. | [L] | 379 |
| Crash Escape Message Volume | float | No effect: nothing reads it. The crash-escape sounds play at fixed volumes. | [L] | 380 |
| Crash Bonus Message Volume | float | No effect: nothing reads it. The crash-bonus sounds play at fixed volumes. | [L] | 381 |
| Lost Boost Segment Gain | float | Volume (linear factor) of the blip when a boost-bar segment is lost (called from the boost bar update). | [C 0x00285A60] | 382 |
| Gained Boost Segment Gain | float | Volume (linear factor) of the blip when a boost-bar segment is gained; the code starts the sample twice at once, so it sounds about twice as loud as the same number on Lost Boost Segment Gain. | [C 0x00285B00] | 383 |

#### Aggressive Driving

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| 3d Shunt Vol | float | Volume (linear factor) of the 3D shunt sound (the lighter of the two rival-contact kinds `CB3Score::OnSlam` reports, positioned at the car). Only when a human-driven car is involved, halved when the human is the one hit, and at most once per 30 frames. | [C 0x00286AD0] | 384 |
| 3d Slam Vol | float | Volume (linear factor) of the 3D slam sound (a hard aggressive hit on a rival car, positioned at the car; it also briefly ducks other sound). Only when a human-driven car is involved, halved when the human is the one slammed, at most once per 30 frames. | [C 0x00286970] | 385 |

### Boost

The first fourteen rows are the **boost pass-by**: the whoosh when you pass a traffic vehicle (`Sound/Boost.cfg`; full
volume only while boosting, about 9 % otherwise). Its speed blend runs from the Min to the Max values over the closing
speed, but as coded it works on a negative (closing) speed and so stays at its Max end: the Max Pitch and Max Volume are
what you hear [C 0x00278020]. The rows from Boost Ready Volume on are the boost sounds of the cars (`Sound/Misc.cfg`);
most of them are read by nothing.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Min Speed | float | The closing speed on a traffic vehicle, in m/s, needed before a pass-by can trigger; higher = only faster passes whoosh. It is also the low end of the pitch/volume speed blend. As coded the blend stays at its Max end (see above). | [C 0x00278020] | 259 |
| Max Speed | float | The top of the pitch/volume speed blend, in m/s. As coded the blend stays at its Max end (see above), so this changes little or nothing. | [C 0x00278020] | 260 |
| Min Pitch | float | Pitch multiplier of the sample (1.0 = recorded pitch) at the Min Speed end of the blend; a random -0.1..+0.1 is added, then the result is scaled by Centre/Edge Pitch. As coded the blend stays at its Max end (see above). | [C 0x00278020] | 261 |
| Max Pitch | float | Pitch multiplier at the Max Speed end of the blend (normally the one heard); higher = a higher, faster whoosh. A random -0.1..+0.1 is added, then Centre/Edge Pitch scale it. | [C 0x00278020] | 262 |
| Sample Midpoint | float | The time, in seconds, from the start of the sample to its peak. The trigger point is placed that far ahead of the traffic vehicle (closing speed x this / pitch, plus half the Car Length) so the peak lands as you draw level; bigger = the sample starts earlier. | [C 0x00278020] | 263 |
| Car Length | float | Assumed length of a traffic vehicle in metres; half of it is added to the trigger lead distance, so a bigger value starts the whoosh earlier. | [C 0x00278020] | 264 |
| DampTime | int | No effect: nothing reads it. | [L] | 265 |
| Min Volume | float | Linear volume at the Min Speed end of the blend. The final volume is this blend x the boost factor (max(0.09, your boost ramp)) x Big/Little Pass Scalar x how square-on the pass is x 2. As coded the blend stays at its Max end (see above). | [C 0x00278020] | 266 |
| Max Volume | float | Linear volume at the Max Speed end of the blend (normally the one heard); bigger = louder whooshes. Multiplied by the boost factor, Big/Little Pass Scalar and pass geometry. | [C 0x00278020] | 267 |
| Plane Width | float | Scales the 'centred pass' zone: the game compares (how directly you pass the vehicle, squared) x this value with your car's sideways angle to the trigger point; a wider zone = more passes count as centred (louder, nearer Centre Pitch), 0 or very small = pass-bys go silent. | [C 0x00278020] | 268 |
| Centre Pitch | float | Pitch multiplier for a dead-centre pass; blended toward Edge Pitch as the pass gets less centred. | [C 0x00278020] | 269 |
| Edge Pitch | float | Pitch multiplier for a pass at the edge of the pass zone (blended with Centre Pitch by how centred the pass is). | [C 0x00278020] | 270 |
| Little Pass Scalar | float | Volume multiplier for passing small traffic (cars and vans, traffic categories 1-2; the `bstpss` samples). | [C 0x00278020] | 271 |
| Big Pass Scalar | float | Volume multiplier for passing large traffic (lorries, buses and other categories; the `bstpsl` samples). L-trains (category 7) never play a pass-by. | [C 0x00278020] | 272 |
| Boost Ready Volume | float | No effect: nothing reads it. | [L] | 414 |
| Boost In Volume | float | No effect: nothing reads it. Your boost-start sound uses fixed built-in levels. | [L] | 415 |
| Boost Out Volume | float | No effect: nothing reads it. Your boost-end sound's volume is fixed: 0.25 + 0.25 x the boost left. | [L] | 416 |
| Boost Chain Volume | float | No effect: nothing reads it. | [L] | 417 |
| Boost Loop Volume | float | No audible effect: the only code that touches it scales it once when the race's sounds are prepared; no sound is played at this volume. | [C 0x0028DBB0] | 418 |
| AI Boost In Volume | float | Volume (linear) of the boost-start sound for online opponents' cars (race-car type 2, CB3NetworkCar), played each time one starts a boost. Computer rivals (type 1) get no boost-start sound, so offline this has no effect; online only. | [C 0x0028D300] | 419 |
| AI Boost Out Volume | float | Volume (linear) of the boost-end sound for every car that is not a local player: computer rivals and online opponents, played when their boost runs out. | [C 0x0028D1A0] | 420 |
| AI Boost Loop Volume | float | No effect: nothing reads it. | [L] | 421 |

### Engine sound model

Wobbles in the engine sound's revs, for every car's engine (`Sound/Esm.cfg`): an **upshift wobble** after each gear
change (the first three rows) and an **off-road wobble**, a slow random wave while at least two wheels are on loose
surfaces (gravel, dirt). Several rows are read by nothing.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Rpm Modulation Max Amplitude | float | Engine-rpm wobble after an upshift: the peak rpm swing added to the engine-sound rpm (in rpm). After each upshift the engine note wobbles by up to this much and fades out over the modulation time; bigger = a more pronounced 'bounce' on gear changes. | [C 0x00288770] | 386 |
| Oscillator period | float | Upshift rpm wobble speed: the wobble is sin(frame count / this), so the value is frames per radian (about 6.3 x this frames per cycle); bigger = slower wobble. | [C 0x00288770] | 387 |
| Total Modulation Time (seconds) | float | Upshift rpm wobble length, in seconds: each upshift starts a timer of 0.5..1.0 x this (random), and the wobble fades linearly to nothing over it; bigger = longer wobble. | [C 0x00288770] | 388 |
| Time Till Full Air Saturation | float | No effect: nothing reads it. | [L] | 389 |
| Full Air Saturation Scalar | float | No effect: nothing reads it. | [L] | 390 |
| Time to modulate on landing | float | No effect: nothing reads it. | [L] | 391 |
| Time Till Full Offroad Saturation | float | Seconds off-road before the wobble reaches full depth (it ramps up linearly over this time and resets as soon as you are back on road); bigger = slower build-up. All cars' engines. | [C 0x00288480] | 392 |
| Offroad Oscillator period | float | No effect: nothing reads it. | [L] | 393 |
| Offroad Max Amplitude | float | The peak rpm swing (in rpm) at full depth; scaled by the time ramp and by speed. Bigger = a rougher-sounding engine off-road; 0 turns it off. | [C 0x00288480] | 394 |
| Offroad Modulation Start Speed (percent of top) | float | The speed, as a fraction of the car's top speed (0.3 = 30%), at the low end of the speed clamp. The depth factor is speed / (End - Start) with speed clamped between Start and End, so it grows with speed and is never zero: raising Start makes slow off-road driving wobble more. | [C 0x00288480] | 395 |
| Offroad Modulation End Speed (percent of top) | float | The speed, as a fraction of the car's top speed, at the high end of the speed clamp; above it the depth stops growing (see Start Speed). | [C 0x00288480] | 396 |
| Offroad Modulation Period Min | float | Shortest wobble period, in frames per radian (the wave is sin(frames / period)); each new period is picked at random between Min and Max. Smaller = faster wobble. | [C 0x00288480 0x00288960] | 397 |
| Offroad Modulation Period Max | float | Longest wobble period, in frames per radian (random between Min and Max for each new wave). Bigger = slower, lazier wobble. | [C 0x00288480 0x00288960] | 398 |
| Offroad Lerp time | float | Seconds to cross-fade from the old wobble period to a newly picked one; at the end a new random period is chosen, so this is also how often the wobble changes speed. | [C 0x00288480 0x00288960] | 399 |

### Mix

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost Volume | float | No effect: nothing reads it. Boost sounds use the separate per-sound levels (most of which are also unused) or fixed values. | [L] | 413 |

#### Vehicle Volume Tailoff

How your engine gets quieter near top speed (the players' cars only, both split-screen players), and the master gain of
every race car's engine.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Start Speed (Percent of MaxSpeed) | float | The speed, as a fraction of the car's top speed, where the engine starts to get quieter; below it the engine plays at full volume. Between Start and End the volume falls linearly to End Volume. | [C 0x002ED3E0] | 487 |
| End Speed (Percent of MaxSpeed) | float | The speed, as a fraction of the car's top speed, where the engine reaches End Volume. | [C 0x002ED3E0] | 488 |
| End Volume | float | The engine volume multiplier at and above End Speed (1 = no tail-off; lower = quieter engine at top speed). Separately, while boosting the engine is ducked further by up to half, by the boost left. | [C 0x002ED3E0] | 489 |
| Race Car Master Gain | float | Master gain for every race car's engine sound (your car, rivals and online opponents): multiplied with each car model's Car Volume. Linear; 0 silences all engines. | [C 0x002ED3E0] | 490 |
| High Detail ESM 2 player gain | float | No effect: nothing reads it (no separate split-screen engine gain exists in the code). | [L] | 491 |
| Low Detail ESM 2 player gain | float | No effect: nothing reads it (no separate split-screen engine gain exists in the code). Despite the names around it, the tail-off's End Volume is the row above. | [L] | 492 |

### Rivals' engines

Per car model (`VehicleAudioAI/<car>.cfg`): the engine of a rival driving that model.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Car Volume | float | The engine-sound gain (linear) of this car when a rival drives it; multiplied by Race Car Master Gain each frame. Bigger = a louder rival engine. | [C 0x002ED3E0] | 401 |

### Your car's engine

Per car model (`VehicleAudio/<car>.cfg`), heard only on the car you drive. Most rows are the turbo **dump valve** (the
blow-off when the revs drop sharply: lifting off, shifting); the engine sound follows its own **tracked revs**, which
lag behind the engine's.

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Car Volume | float | The engine-sound gain (linear) of this car when you drive it; multiplied by Race Car Master Gain and the speed tail-off each frame. | [C 0x002ED3E0] | 402 |
| Wave Attack Offset | int | Start offset into the dump-valve sample (whole number, in the sound system's position units). The sample starts at (1 - dump strength) x this, so weaker dumps skip the attack and play only the tail; 0 = always play from the start. | [C 0x0028FB20] | 403 |
| Rpm to Turbo Falloff Lag | float | The largest per-frame fall (a negative rpm step) of the turbo model's tracked rpm in normal driving; the tracked rpm lags behind the engine and sets the turbo spool level. | [C 0x0028FB20] | 404 |
| Rpm to Turbo Rising Lag | float | The largest per-frame rise of the tracked rpm (rpm per frame); smaller = the turbo spools up more slowly, so early lifts give weaker dumps. | [C 0x0028FB20] | 405 |
| Rpm to Turbo Min Rpm | float | Tracked rpm where turbo spool starts (spool 0); spool rises smoothly to 1 at Rpm to Turbo Max Rpm Volume. | [C 0x0028FB20] | 406 |
| Rpm to Turbo Max Rpm Volume | float | Tracked rpm where turbo spool reaches full (1). Despite the name it is an rpm, not a volume; lower = full-strength dumps from lower revs. | [C 0x0028FB20] | 407 |
| Dump Valve Kick-in Min Falloff | float | The per-frame rpm drop (negative) that fires the dump-valve sample. Lifting off the throttle or shifting reads as rpm 0, a big drop, so lifting fires it; a value nearer 0 makes it fire on smaller drops. | [C 0x0028FB20] | 408 |
| Dump Valve Kick-in Max Falloff | float | The rpm drop (negative) that counts as a full-strength dump. Strength = turbo spool x (drop / this), with the drop clamped between the two Kick-in values. | [C 0x0028FB20] | 409 |
| Dump Valve Volume Min | float | Linear volume of the dump-valve sample at strength 0 (blended to Volume Max by dump strength); a result of 0 plays nothing. | [C 0x0028FB20] | 410 |
| Dump Valve Volume Max | float | Linear volume of the dump-valve sample at full strength. | [C 0x0028FB20] | 411 |
| Gear Change Volume | float | Linear volume of the gear-change sample, played each time the gear number goes up (upshifts only). | [C 0x0028BF90] | 412 |

### Front End

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Gain | float | Volume of the front end's music/jingle streams (tracks from `movie.rws` and `tracks\_femain.rws`) that menu pages start: stream volume = this x the louder of the game's two volume settings (percent). Front end only. | [C 0x00130EE0] | 1 |
| Intro Gain | float | Car-unlock screens (shown after an event unlocks a car): volume of the intro stream (track 0x21 of `movie.rws`), played while the page's movie flag is clear. Front end only. | [C 0x004546B0] | 704 |
| Movie Gain | float | Car-unlock screens: volume of track 1 of `movie.rws`, played instead of the intro stream when the page's movie flag is set. Front end only. | [C 0x004546B0] | 705 |

### Voice

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Master Sfx Volume | float | Master volume of the game's whole sound mixer (CGtSoundManager::Update each frame: this x the current fade level). Not speech or commentary: Voice.cfg configures the sound-voice system. Every race start forces it to 0 and fades it back to 1.0 over 10 frames (CB3SoundGameModeManager), so an edited value lasts only until the next race starts. | [C 0x0028B160 0x0028ADD0] | 400 |

## SURFACES

The road noise under your tyres, per surface the wheel is on (`Sound/Surface.cfg`, one set, read every frame: a
change is heard at once). Each wheel side plays two layers, a left- and a right-wheel voice each: the shared
**tarmac roll** loop (the rows whose names start with `Tarmac`, on every surface) and **the surface's own sample**
(the rows without it). Both blend from their Min values at Min Speed to their Max values at Max Speed. Only the
players' cars make road noise; the rivals' cars have none.

Every surface section has the same twelve values:

| Value | What it does | Evidence |
|---|---|---|
| Tarmac Min Speed, Min Speed | The car speed, in m/s, where the blend starts: at or below it the layer plays at its Min Vol and Min Pitch. | [C 0x0028C0F0] |
| Tarmac Max Speed, Max Speed | The speed, in m/s, where the layer reaches its Max Vol and Max Pitch (a straight blend from Min Speed). | [C 0x0028C0F0] |
| Tarmac Min Vol, Min Vol | Volume (linear factor) at or below Min Speed; 0 = silent. On loose surfaces the volume is raised up to 5x while the tyres slide. | [C 0x0028C0F0] |
| Tarmac Max Vol, Max Vol | Volume at or above Max Speed: bigger = louder road noise at speed (also raised while sliding on loose surfaces). | [C 0x0028C0F0] |
| Tarmac Min Pitch, Min Pitch | Pitch (1 = as recorded) at or below Min Speed. One voice of the pair plays 0.24 higher; sliding on a loose surface lowers the pitch to as little as 0.85x. | [C 0x0028C0F0] |
| Tarmac Max Pitch, Max Pitch | Pitch at or above Max Speed: bigger = a higher, faster roar at speed. | [C 0x0028C0F0] |

Three of the ten sets are rarely or never heard: **None** is used only for surface IDs the game's surface-to-sound
table leaves unmapped (a wheel in the air, or on surface 0, makes no road noise at all), and **Offline Tarmac** and
**Offline Gravel** are never selected by that table, so their values change nothing [C 0x0028C0F0 0x002ED690].

### None

| Value | # |
|---|---|
| Tarmac Min Speed | 493 |
| Tarmac Max Speed | 494 |
| Tarmac Min Vol | 495 |
| Tarmac Max Vol | 496 |
| Tarmac Min Pitch | 497 |
| Tarmac Max Pitch | 498 |
| Min Speed | 553 |
| Max Speed | 554 |
| Min Vol | 555 |
| Max Vol | 556 |
| Min Pitch | 557 |
| Max Pitch | 558 |

### Tarmac

| Value | # |
|---|---|
| Tarmac Min Speed | 499 |
| Tarmac Max Speed | 500 |
| Tarmac Min Vol | 501 |
| Tarmac Max Vol | 502 |
| Tarmac Min Pitch | 503 |
| Tarmac Max Pitch | 504 |
| Min Speed | 559 |
| Max Speed | 560 |
| Min Vol | 561 |
| Max Vol | 562 |
| Min Pitch | 563 |
| Max Pitch | 564 |

### Concrete

| Value | # |
|---|---|
| Tarmac Min Speed | 505 |
| Tarmac Max Speed | 506 |
| Tarmac Min Vol | 507 |
| Tarmac Max Vol | 508 |
| Tarmac Min Pitch | 509 |
| Tarmac Max Pitch | 510 |
| Min Speed | 565 |
| Max Speed | 566 |
| Min Vol | 567 |
| Max Vol | 568 |
| Min Pitch | 569 |
| Max Pitch | 570 |

### Offline Tarmac

| Value | # |
|---|---|
| Tarmac Min Speed | 511 |
| Tarmac Max Speed | 512 |
| Tarmac Min Vol | 513 |
| Tarmac Max Vol | 514 |
| Tarmac Min Pitch | 515 |
| Tarmac Max Pitch | 516 |
| Min Speed | 571 |
| Max Speed | 572 |
| Min Vol | 573 |
| Max Vol | 574 |
| Min Pitch | 575 |
| Max Pitch | 576 |

### Gravel

| Value | # |
|---|---|
| Tarmac Min Speed | 517 |
| Tarmac Max Speed | 518 |
| Tarmac Min Vol | 519 |
| Tarmac Max Vol | 520 |
| Tarmac Min Pitch | 521 |
| Tarmac Max Pitch | 522 |
| Min Speed | 577 |
| Max Speed | 578 |
| Min Vol | 579 |
| Max Vol | 580 |
| Min Pitch | 581 |
| Max Pitch | 582 |

### Offline Gravel

| Value | # |
|---|---|
| Tarmac Min Speed | 523 |
| Tarmac Max Speed | 524 |
| Tarmac Min Vol | 525 |
| Tarmac Max Vol | 526 |
| Tarmac Min Pitch | 527 |
| Tarmac Max Pitch | 528 |
| Min Speed | 583 |
| Max Speed | 584 |
| Min Vol | 585 |
| Max Vol | 586 |
| Min Pitch | 587 |
| Max Pitch | 588 |

### Pavement

| Value | # |
|---|---|
| Tarmac Min Speed | 529 |
| Tarmac Max Speed | 530 |
| Tarmac Min Vol | 531 |
| Tarmac Max Vol | 532 |
| Tarmac Min Pitch | 533 |
| Tarmac Max Pitch | 534 |
| Min Speed | 589 |
| Max Speed | 590 |
| Min Vol | 591 |
| Max Vol | 592 |
| Min Pitch | 593 |
| Max Pitch | 594 |

### Snow

| Value | # |
|---|---|
| Tarmac Min Speed | 535 |
| Tarmac Max Speed | 536 |
| Tarmac Min Vol | 537 |
| Tarmac Max Vol | 538 |
| Tarmac Min Pitch | 539 |
| Tarmac Max Pitch | 540 |
| Min Speed | 595 |
| Max Speed | 596 |
| Min Vol | 597 |
| Max Vol | 598 |
| Min Pitch | 599 |
| Max Pitch | 600 |

### Metal

| Value | # |
|---|---|
| Tarmac Min Speed | 541 |
| Tarmac Max Speed | 542 |
| Tarmac Min Vol | 543 |
| Tarmac Max Vol | 544 |
| Tarmac Min Pitch | 545 |
| Tarmac Max Pitch | 546 |
| Min Speed | 601 |
| Max Speed | 602 |
| Min Vol | 603 |
| Max Vol | 604 |
| Min Pitch | 605 |
| Max Pitch | 606 |

### Wood

| Value | # |
|---|---|
| Tarmac Min Speed | 547 |
| Tarmac Max Speed | 548 |
| Tarmac Min Vol | 549 |
| Tarmac Max Vol | 550 |
| Tarmac Min Pitch | 551 |
| Tarmac Max Pitch | 552 |
| Min Speed | 607 |
| Max Speed | 608 |
| Min Vol | 609 |
| Max Vol | 610 |
| Min Pitch | 611 |
| Max Pitch | 612 |

## VISUALS

The bloom per route and the cars' paint and glass values.

### Bloom

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Threshold | float, 0 .. 1 | Per route: the bloom's brightness cut-off, 0..1 of full brightness: only the parts of the picture brighter than this glow. Lower = more of the scene glows; higher = only the brightest lights. Read every frame, so a change shows at once; the PC renderer draws its bloom from the same value (PC SETTINGS BLOOM on, the default). | [C 0x0030A9D0 0x002659C0] | 653 |
| Intensity | float, 0 .. 1 | Per route: the bloom's strength: the blurred glow is added to the picture times this (1 = the full glow, 0 = none). Read every frame; the PC renderer uses the same value (BLOOM on). | [C 0x0030A9D0 0x002659C0] | 654 |

### Car paint & glass

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Reflectivity | float ×8, 0 .. 1 | The paint's reflectivity (0..1), 8 entries (likely one per paint colour). Per car model (`VehicleMaterial/<car>.cfg`). No effect found: nothing that draws the cars reads it, on the PS2 or in the PC renderer. | [L] | 7 |
| Glossiness | float ×8, 0 .. 1 | The paint's glossiness (0..1), 8 entries (likely one per paint colour). Per car model (`VehicleMaterial/<car>.cfg`). No effect found: nothing that draws the cars reads it, on the PS2 or in the PC renderer. | [L] | 8 |
| Specularity | float ×8, 0 .. 1 | The paint's specular highlight (0..1), 8 entries (likely one per paint colour). Per car model (`VehicleMaterial/<car>.cfg`). No effect found: nothing that draws the cars reads it, on the PS2 or in the PC renderer. | [L] | 9 |
| GlossinessGlass | float, 0 .. 1 | The window glass's glossiness (0..1). Per car model (`VehicleMaterial/<car>.cfg`). No effect found: nothing that draws the cars reads it, on the PS2 or in the PC renderer. | [L] | 10 |
| SpecularityGlass | float, 0 .. 1 | The window glass's specular highlight (0..1). Per car model (`VehicleMaterial/<car>.cfg`). No effect found: nothing that draws the cars reads it, on the PS2 or in the PC renderer. | [L] | 11 |

## RUMBLE

Pad vibration (`Input/rumble.cfg`, one set, read when a jolt fires: a change works at once). On PC you feel it only with
PC SETTINGS > CONTROLS > FEEDBACK > **RUMBLE** on (off by default; RUMBLE STRENGTH scales it, and the game's
VIBRATION option in Driver Details still applies). The game's large motor drives the pad's low-frequency motor, its
small motor the high-frequency one.

Most entries are **jolts** of four values [C 0x0022C2F0 0x0022C050 0x0022C340]:

- **jolt type**: 0 = the small motor, a plain on/off buzz for the jolt's time (amount and fade ignored); 1 = the large
  motor; any other value = no jolt;
- **Jolt Amount**: the large motor's strength, 0..1. The game triples it and caps it before the motor, so about 0.33
  already gives full speed. A new jolt replaces a running one only if it is stronger;
- **Jolt time**: how long the jolt lasts, in seconds;
- **Fade time**: the last part of the jolt, in seconds, in which the large motor drops to about a third of its strength.

### Car

#### Boost

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost jolt type (0=samll,1=large) | int | The motor of the jolt you feel at a Boost Start (the launch on the starting line; ordinary boosting has no rumble): 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x001BD6A0 0x0022C2F0] | 655 |
| Boost Jolt Amount (0.0 to 1.0) | float | Despite its name, how long the Boost Start jolt lasts, in seconds: the code passes it where the duration goes and Boost Jolt time where the strength goes. | [C 0x001BD6A0 0x0022C050] | 656 |
| Boost Jolt time (s) | float | Despite its name, the Boost Start jolt's strength (0..1); see Boost Jolt Amount. | [C 0x001BD6A0 0x0022C050] | 657 |
| Boost Fade time (s) | float | The fade of the Boost Start jolt, in seconds (large motor only). | [C 0x001BD6A0 0x0022C340] | 658 |

#### BoostStart

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Boost start Accelerator Rumble (0.0 to 1.0) | float | The large motor's steady level (0..1) while you rev on the starting line in the countdown, before the car is primed for a Boost Start. | [C 0x001BD6A0 0x001C0F00] | 659 |
| Boost start Brakes Released Rumble (0.0 to 1.0) | float | The large motor's steady level (0..1) once the car is primed for a Boost Start (revving in the window 1 to 0.5 s before GO), until you lift off for the launch. | [C 0x001BD6A0 0x001C0F00] | 660 |

#### Chain

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Chain Boost jolt type (0=samll,1=large) | int | No effect: nothing reads it (the game has no chain-boost jolt). | [L] | 661 |
| Chain Jolt Amount (0.0 to 1.0) | float | No effect: nothing reads it. | [L] | 662 |
| Chain Jolt time (s) | float | No effect: nothing reads it. | [L] | 663 |
| Chain Fade time (s) | float | No effect: nothing reads it. | [L] | 664 |

#### Kerb

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Kerb jolt type (0=samll,1=large) | int | The motor of the jolt when a wheel touches a kerb above 20 mph: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x001BD6A0 0x0022C2F0] | 665 |
| Kerb Jolt Amount (0.0 to 1.0) | float | No effect: the kerb jolt never reads it; Kerb Jolt time sets its strength as well. | [L] | 666 |
| Kerb Jolt time (s) | float | The kerb jolt's duration in seconds and also its strength (the code passes it twice): a longer kerb rumble is a stronger one. | [C 0x001BD6A0 0x0022C050] | 667 |
| Kerb Fade time (s) | float | No effect: never read (the kerb jolt's fade is a fixed value). | [L] | 668 |

#### Surface

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Wheel Skid rumble (0.0 to 1.0) | float | Rumble level (0..1) for a wheel in a skid, scaled by speed. It goes only to a USB force-feedback steering wheel, which the PC port does not drive: no effect on PC. | [C 0x001BD6A0 0x00442900] | 669 |
| Wheel Drift rumble (0.0 to 1.0) | float | Rumble level (0..1) for a wheel in a drift, scaled by speed. Force-feedback steering wheels only: no effect on PC. | [C 0x001BD6A0 0x00442900] | 670 |
| Racing Line Dirt rumble (0.0 to 1.0) | float | Rumble level (0..1) for wheels on the racing line's dirt surfaces, scaled by speed. Force-feedback steering wheels only: no effect on PC. | [C 0x001BD6A0 0x00442900] | 671 |
| Off-line Dirt rumble (0.0 to 1.0) | float | Rumble level (0..1) for wheels on off-line dirt (the rougher surfaces), scaled by speed. Force-feedback steering wheels only: no effect on PC. | [C 0x001BD6A0 0x00442900] | 672 |

#### Air

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Big Air jolt type (0=samll,1=large) | int | The motor for the jolt when you land a scored jump above 20 mph: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x00157FA0 0x0022C2F0 0x0022C050 0x0022C340] | 673 |
| Big Air Base Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). | [C 0x00157FA0 0x0022C2F0 0x0022C050 0x0022C340] | 674 |
| Big Air Jolt time (s) | float | How long the landing jolt lasts, per level of the jump, in seconds: the jolt lasts this x the jump's rating, so bigger jumps rumble longer. | [C 0x00157FA0 0x0022C340] | 675 |
| Big Air Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x00157FA0 0x0022C2F0 0x0022C050 0x0022C340] | 676 |

### Collisions

#### Crash

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Crash jolt type (0=samll,1=large) | int | The motor for the jolt when your car starts crashing: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x001BFC70 0x0022C2F0 0x0022C050 0x0022C340] | 677 |
| Crash Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). | [C 0x001BFC70 0x0022C2F0 0x0022C050 0x0022C340] | 678 |
| Crash Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x001BFC70 0x0022C2F0 0x0022C050 0x0022C340] | 679 |
| Crash Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x001BFC70 0x0022C2F0 0x0022C050 0x0022C340] | 680 |

#### CarVCar

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Car v. Car jolt type (0=samll,1=large) | int | The motor for the jolt when two racing cars hit each other (both players feel it): 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x0025BBC0 0x0022C2F0 0x0022C050 0x0022C340] | 681 |
| Car v. Car Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). The strength used is this x (closing speed / 20 m/s) squared, capped at 1. | [C 0x0025BBC0 0x0022C2F0 0x0022C050 0x0022C340] | 682 |
| Car v. Car Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x0025BBC0 0x0022C2F0 0x0022C050 0x0022C340] | 683 |
| Car v. Car Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x0025BBC0 0x0022C2F0 0x0022C050 0x0022C340] | 684 |

#### CarVTraffic

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Car v. Traffic jolt type (0=samll,1=large) | int | The motor for the jolt when your car hits traffic while not crashing: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 685 |
| Car v. Traffic Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). The strength used is this x (impact speed / 20 m/s) squared, capped at 1. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 686 |
| Car v. Traffic Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 687 |
| Car v. Traffic Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 688 |
| Crashing Car v. Car jolt type (0=samll,1=large) | int | The motor for the jolt when your car hits traffic while it is already crashing (despite the name: traffic, not a car): 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 689 |
| Crashing Car v. Car Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). Its strength is figured as for Car v. Traffic. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 690 |
| Crashing Car v. Car Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 691 |
| Crashing Car v. Car Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x0025B450 0x0022C2F0 0x0022C050 0x0022C340] | 692 |

#### CarVWorld

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Car v. World jolt type (0=samll,1=large) | int | The motor for the jolt when your car hits or scrapes a wall or other scenery above 20 mph while not crashing: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 693 |
| Car v. World Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). The strength used grows with the speed into the wall (glancing scrapes rumble less). | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 694 |
| Car v. World Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 695 |
| Car v. World Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 696 |
| Crashing Car v. World jolt type (0=samll,1=large) | int | The motor for the jolt when your car hits scenery while it is already crashing: 0 = the small motor, 1 = the large motor, anything else = no jolt. | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 697 |
| Crashing Car v. World Jolt Amount (0.0 to 1.0) | float | The jolt's strength (0..1). | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 698 |
| Crashing Car v. World Jolt time (s) | float | How long the jolt lasts, in seconds. | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 699 |
| Crashing Car v. World Fade time (s) | float | The jolt's fade, in seconds (large motor only). | [C 0x002594C0 0x0022C2F0 0x0022C050 0x0022C340] | 700 |

### Menu

| Value | Type | What it does | Evidence | # |
|---|---|---|---|---|
| Menu test Jolt time (s) | float | No effect: nothing reads it. (The launcher's own rumble test is separate.) | [L] | 701 |

## OTHER

Variables no rule of the console places, under their own group. Every registration the game's code makes has a place
in the tabs above, so this tab stays empty; a variable registered by code the registry does not list would show here.
