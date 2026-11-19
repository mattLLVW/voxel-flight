# Voxel Flight

**An endless, procedurally generated flight sim for the Waveshare ESP32-C6 1.9" Touch,
built as a native ESP-IDF project.**

> **Vibe coded with Claude.** The original project was written by
> [Claude](https://claude.ai) (Anthropic's AI) through conversation, as a stress test of how far an
> **ESP32-C3 SuperMini** and a **0.6" 64×64 SSD1357 OLED** can be pushed. I described what I wanted
> and gave feedback from watching the screen; Claude wrote and tuned the code. This port keeps the
> renderer but targets the integrated C6, SH8601 display and touch controller through ESP-IDF.

![Flying into a waterfront city at sunset as the lights come on](media/sunset-city.gif)

**▶ Watch the 30-minute demo on YouTube** (click the picture):

[![Voxel Flight – 30 minute demo on YouTube](https://img.youtube.com/vi/XE_0soNe3UQ/hqdefault.jpg)](https://www.youtube.com/watch?v=XE_0soNe3UQ)

Pick one of three planes, then watch it fly itself third-person over a world that's generated
as you go: patchwork farmland, lakes and woods, snowy mountains, sandstone canyons, coastline,
villages, and skyscraper cities with suspension bridges. The day turns from sunrise to sunset to
moonlit night with city lights coming on window by window. Every boot is a new world.

| | | |
|:-:|:-:|:-:|
| ![Swooping between skyscrapers](media/city-swoop.gif) | ![Loop the loop](media/loop.gif) | ![Snowy mountains](media/mountains.gif) |
| Down a boulevard between the towers | The Racer loops the loop | Carving past the mountains |
| ![Sunrise over the water](media/sunrise.gif) | ![Plane select screen](media/plane-select.gif) | ![A city at night](media/night-city.gif) |
| Sunrise over the coast | Choosing a plane | A city at night under the moon |

*These are real frames from the original 64×64 build (captured over USB, see
[Recording videos](#recording-videos)). The C6 port renders a wider 128×68 game frame and scales
it across the 320×170 display.*

Highlights: a voxel-space ray-marched renderer with smooth shading, a 256×256 streaming map,
five blended biomes, cities with individual buildings and lit windows, 3D suspension bridges,
a 3D layer of blocky clouds, sun and moon (with phases) crossing the sky, an autopilot that
follows valleys, threads boulevards and visits hot air balloons, aerobatics (loops and barrel
rolls), wingtip trails, a terrain-draped shadow, and DMA display output - all in integer maths
on a chip with no floating point unit.

---

## Files

| File | What it is |
|---|---|
| `voxel-flight.ino` | The rendering and simulation engine, included as C++ by the ESP-IDF entry point. |
| `main/board_bsp.cpp` | Waveshare display, DMA output, I²C touch controller and BOOT-button setup. |
| `main/main.cpp` | ESP-IDF `app_main` entry point and FreeRTOS frame loop. |
| `flight_types.h` | Shared structs and enums (biomes, materials, plane models, bridges, day light, balloons). |
| `tools/capture.ps1`, `tools/frames.py` | Record the screen over USB and turn it into GIFs (see [Recording videos](#recording-videos)). |
| `media/` | The GIFs above. |
| `README.md` | This file. |
| `LICENSE` | MIT: use it, change it, share it. |

---

## Hardware

| Part | Notes |
|---|---|
| **Waveshare ESP32-C6 1.9" Touch** | Integrated ESP32-C6, 170×320 SH8601 display and I²C touch controller. |
| USB cable | Powers and flashes the board. |

No wiring is required. The BSP uses the pinout from the supplied Waveshare example: display
MOSI/CLK/CS/DC/RST on GPIO 4/5/7/6/14, and touch SDA/SCL on GPIO 18/8. The touch controller is
read at address `0x15`. On the selection screen either a touch or the physical BOOT button (GPIO9)
chooses a plane.

## Flight controls

The onboard QMI8658 gyroscope and accelerometer provide tilt controls, with the board held in
landscape:

- Tilt left/right to steer.
- Tilt the top of the board up/down to climb or descend.
- Hold the board still once the plane is chosen: the neutral position and the gyroscope's zero
  offset are calibrated when the world has been generated.
- Press BOOT at any time to re-centre the controls on the current hold.
- About 3 seconds at neutral hands the plane back to the autopilot.

The gyro rate is integrated for smooth, lag-free angles and a complementary filter lets gravity
from the accelerometer slowly correct its drift (ignored while the board is being shaken). A dead
zone prevents drift while holding the board normally. The terrain-clearance planner stays active
in manual flight, so a downward command cannot fly below the safety clearance. If a tilt goes the
wrong way, flip `INVERT_STEER` / `INVERT_CLIMB` in `main/board_bsp.cpp`; set `DEBUG_TILT` to log
the angles over serial.

## Building and uploading

Use ESP-IDF 6.1 or newer. The checked-in `sdkconfig.defaults` selects ESP32-C6 and the 8 MB flash.
Open this directory as an ESP-IDF project, or build from an ESP-IDF shell:

```sh
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

No Arduino libraries or component-registry packages are required. The build currently links at
about 79% of DIRAM, including 12.8 KB of DMA strip buffers, leaving about 93 KB headroom for
ESP-IDF services and the running application.

## Recording videos

The GIFs above are the board's own frames, sent to the PC over the USB cable.

1. Set `CAPTURE_FRAMES = true` near the top of the sketch and upload. Each 128×68 RGB565 frame is
   also sent over USB.
2. Record (Windows PowerShell, from this folder; change the port):
   `powershell -ExecutionPolicy Bypass -File tools\capture.ps1 -Port COM4 -Seconds 60 -Out capture.bin`
3. Make a GIF (needs Python and `pip install pillow`):
   `python tools\frames.py capture.bin --gif clip.gif --start 20 --length 8 --scale 5`
   (`--list` says how long the capture is, `--png folder` writes single frames, `--step 2`
   halves the frame rate and the file size.)

Set `CAPTURE_FRAMES` back to `false` for normal use. Tip: `FORCE_PLANE` and `START_TIME`
(further down) pick the plane and the time of day, e.g. `START_TIME = 0.70` to arrive at a city
at sunset.

---

## The selection screen

After a reset, each plane spins on a turntable under a soft spotlight for 3.5 s. Its name is at
the top, three dots show which of the three it is, and three stat bars show **SPD** (speed),
**AGI** (agility) and **ALT** (cruising height).

- **Touch the screen** or press **BOOT** while a plane is on screen to fly that one.
- If you don't press anything, it picks one at random after showing all three.

The chosen plane's name flashes, then the world is generated (about 0.75 s) and the flight starts.

| Plane | Look | Handling |
|---|---|---|
| **CUB** | Yellow high-wing taildragger, struts, black stripe, propeller | Slow (0.45 cells/frame), gentle turns, flies low |
| **RACER** | Red and white low-wing aerobatic plane, bubble canopy, wheel spats | Quicker (0.62), turns tightest and banks hardest (up to ~55°), hugs the ground. **Loops the loop** every 20–40 s |
| **JET** | White business jet, swept wings with winglets, rear engines with glowing exhausts, T-tail | Fastest (0.75), wide gentle turns, cruises high. **Barrel rolls** every 20–40 s |

To skip the screen while testing, set `FORCE_PLANE` to 0, 1 or 2.

## What you'll see in flight

- **The plane.** Drawn anti-aliased at double resolution, so it glides by fractions of a pixel.
  Wing shading changes smoothly with bank, and its colours dim with the daylight. At dusk it
  shows wingtip lights and a strobe.
- **Wingtip trails.** Faint white streaks behind the wingtips. Invisible in level flight, they
  strengthen the harder the plane banks and fade as they trail away.
- **Aerobatics.** The Racer loops the loop and the Jet does barrel rolls, when there's room
  ahead. The plane first levels its wings and flies straight for a moment, and the camera eases
  back and up so the whole figure fits on screen. The wingtip trails draw the figure in the sky.
  The figure is an offset from the plane's straight path and only ever goes up from it, so the
  terrain clearance still holds. Sub-parts of the plane hide each other correctly upside down
  (the plane drawing has a depth buffer).
- **The shadow.** Drawn onto the exact ground surface, so it drapes over hills, fields and
  rooftops and is hidden behind hills. It is darker when low, fades with height, and only shows
  by day.
- **The landscape.** Five biomes in large regions with wide, gentle borders:

  | Biome | Look |
  |---|---|
  | farmland | Gentle hills; wheat, ploughed, crop, pasture and hay fields with hedgerows and trees; lanes; copses; villages and cities |
  | lakeland | Rolling hills, lakes with sandy shores, woods, villages and cities |
  | mountains | Broad ridged ranges with snow above height 140, rock on steep slopes, forest low down |
  | canyon | Stepped sandstone plateaus in muted red layers, cut by river canyons |
  | coast | Sea with islands, beaches, cliffs, villages and waterfront cities |

- **Cities**, sized to the planes (a wingspan is 6 cells):
  - **Layout.** At most one per 256×256 area, 64–86 cells in radius, built on flattened ground.
    Blocks are 24 cells with 2-cell streets. A pair of 3-cell avenues crosses at the centre, each
    running down a 19-cell boulevard (paving, lawns, a row of trees each side) with the buildings
    kept back, so there's an open corridor between the towers. Outside the city an avenue carries
    on as a one-cell lane.
  - **Separate buildings.** Each block holds one, two or four lots, and each lot one building
    standing back 2–4 cells from its edges, with paving or lawn around it. Every building has
    its own random height, wall material (pale stone, blue glass, smoked glass, brick) and shape
    (plain box, setback, tower on a wide podium, the odd mast with a blinking red light). Roofs
    are flat, square and a different colour from the walls, and buildings are drawn with crisp
    edges.
  - **Heights.** Downtown up to ~140 height units, mid-rise around it, low-rise (some with tiled
    roofs) at the edge. Most buildings are modest and a few soar. Parks and empty plazas here
    and there. Where the land meets the sea or a lake, the city stops at a waterfront.
  - **Facades.** Floor lines by day; lit windows at night. At dusk the lights come on one by one
    across the city (each building, window band and street lamp has its own moment), go off the
    same way at dawn, and which windows are lit changes now and then.
- **Bridges.** Where an avenue runs into water, a bridge carries it across, as wide as the avenue,
  with approach ramps sloping down to the ground at each end (up to 40 cells). The far end is
  always real land (sandbars with more water beyond are crossed).
  Long spans are suspension bridges (red or steel-grey towers, main cables, hangers); short ones
  sit on piers. At night they have deck lamps and red lights on the towers. They are real 3D
  objects with a depth test, so you see the water under them and hills or towers in front hide
  them.
- **Country roads.** Farmland lanes (a grid every 64 cells) and city avenues running out into
  the countryside keep to a smooth level of their own: slightly higher ground is cut away (a
  cutting), much higher ground means a tunnel (the hill stays, with a dark tunnel mouth at each
  end), and small dips are filled in (an embankment).
- **Villages.** At most one per 64×64 block away from cities: lanes every 8 cells with the odd
  street lamp, and small cottages (about one per 7×7 square) standing apart in big gardens with
  trees. A third of them have warm lit windows at night.
- **City visits.** The flight starts 200 cells from a city, heading for it. About once a minute
  afterwards, the plane looks for a city ahead (within two 256-cell areas) and steers to it. If
  the city has a bridge it can reach on the way, it first flies to a point just beyond the
  bridge's far end and then comes in along the avenue, right over the bridge. Otherwise it lines
  up with the nearest avenue end. Either way it flies in down the boulevard, between the
  skyscrapers (its terrain check looks 3.5 and 7 cells to each side, and the corridor is 9 each
  side).
- **Hot air balloon.** A rare sighting: first chance after about a minute, then every 2.5–5
  minutes by day (not over high mountains), a balloon appears ~190 cells ahead, 20–50 cells off
  to one side, drifting on the wind 45–75 above the ground. Striped envelope in one of four
  colour schemes, shaded, with ropes and a basket; hills hide it and it fades with distance.
  - **Flyby:** about half the time the plane goes over for a closer look, passing 16 cells to
    one side at the balloon's height (holding its course once close). About a third of those,
    it then carries on out a way, turns round and comes back for a second pass head on (circling
    it would keep the balloon off to the side, out of view).
  - The autopilot climbs over a balloon if it's ever in the way otherwise.
- **Day and night.** A full day takes 5 minutes, and nights pass twice as fast. Boot starts at
  about 7 am.
  - **Day:** blue sky, a layer of blocky 3D clouds overhead and light haze in the distance. The
    clouds are slabs of blocks high above everything, like Minecraft's: you see their flat
    undersides and, across gaps, their side faces in perspective; nearby ones loom overhead and
    far ones shrink towards the horizon. They drift slowly and are lit pink at sunset.
  - **Sun and moon:** they go all the way round the sky once a day, opposite each other: each
    rises in the same direction, climbs to near the top of the screen, and sets on the far side,
    sinking behind the hills. Their height is scaled to the visible sky so the whole arc can be
    seen whenever you're facing them. The sun is a soft disc. The moon is white with a soft edge and goes through its phases (new, crescent, quarter, gibbous,
    full and back) once every 8 days, about 40 minutes; it starts at a random phase, printed at
    boot. The dark part shows the sky through it.
  - **Night:** a dark navy sky with stars and the moon; the ground stays clearly visible in
    blue moonlight and fades into a dark navy haze in the distance; dim moonlit clouds; town
    lights.
- **Autopilot.** Every input is low-pass filtered, so the flight stays smooth.
  - **Height:** it looks 120 cells ahead along its current turn, across a swath 16 cells wide.
    Anything far away only needs the height it could climb by the time it gets there, so a tower
    ahead starts a gentle climb early. It also never climbs slower than the rate that clears
    the worst obstacle ahead, so it is over a roof well before reaching it. Bridges count, as
    tall as their towers. It adds a clearance that varies over minutes, and eases down more
    slowly than it climbs.
  - **Steering:** it steers towards the lower side, so it follows valleys and canyons, and
    wanders gently.

## How it works

- **Streaming map.** The terrain lives in a 256×256 ring buffer: world cell (x, y) is stored at
  `[y & 255][x & 255]`. The buffer window follows a point 50 cells ahead of the camera, and new
  rows and columns are generated in front as old ones drop off behind. It makes one line per
  frame, or two if it falls more than 26 lines behind, which keeps frame times even. Each noise
  field remembers its last lattice corners, because neighbouring cells usually share them, and
  the city/village lookups remember their last block.
- **Terrain.** Integer value noise in five octaves. A 512-cell biome field picks the biome; the
  outer 112/256 of each band blends with the neighbouring biome along a smoothstep. Farmland is
  the exception: it stays pure farmland right to its edge, and the neighbouring biome takes the
  whole transition over its half of the band, easing to exactly farmland height at the border
  (so there's no mountainous farmland). The biome map and the broadest hills are smooth noise
  evaluated at every cell (`bigField`); they used to be read at coarse coordinates, which made
  the biome blend jump every 16 cells and showed as walls where biomes meet. Canyon
  terraces fade out towards other biomes, and town ground is flattened to the blended height, so
  there are no walls at biome borders. Town flattening (`settleGround`) fades out at the edge,
  on high ground and at the water (where hills are carved down to a beach) instead of stopping
  dead, which used to leave vertical walls. Fields, roads and buildings set their material directly.
- **Colours.** Each cell stores `material << 3 | light`. A 16-level fog palette is rebuilt one
  level per frame as the time of day changes. The haze follows an eased curve, so the middle
  distance stays clear and only the far edge fades fully (fully eased at night, half by day).
- **Renderer.** Voxel-space ray march: about 130 distance steps, one sample per screen column per
  step, drawn front to back.
  - **Smoothing (within 45 cells):** heights and slope shading are interpolated between cells,
    and the shading is dithered between its 8 levels. The material is picked by dithering
    between the four nearest cells, so field and shore edges dissolve instead of showing square
    steps. Natural slopes up to 16 units per cell stay smooth ramps; buildings (any material
    from `M_ROOF` on) and steeper steps stay crisp, so roofs are square.
  - **Per-sample extras:** tower facades get floors or windows, and the shadow is tested
    against the plane's footprint.
  - **Depth buffer:** the depth of every terrain pixel goes into a buffer for the bridges.
- **Bridges.** Each city's four avenues are traced outwards with the terrain function, one avenue
  per frame, long before they come into view. The scan looks for land, then water at least 14
  cells wide, then land again. Bridges are drawn as depth-tested triangles and lines in fixed
  point.
- **Planes.** Each plane is data: a list of 3D points and a list of primitives (triangles, quads,
  lines, dots, bank-shaded wing quads, a see-through propeller disc, engine glows), plus its
  colours and handling numbers. To add or change a plane, edit `PLANES` and its `*_V` / `*_C` /
  `*_P` arrays.
- **Sky.** The gradient goes in the pixels left over after the terrain. It eases from the
  horizon colour to the overhead colour along an S-curve over 36 rows (`SKY_GRAD_ROWS`), is
  looked up in quarter rows (smooth when the horizon tilts) and is dithered between neighbouring
  16-bit colours, so there's no visible band. Soft sun and moon are sub-pixel positioned.
- **Clouds.** A repeating 64×64 map of cloud blocks (8×8 cells each, heights 292–300, above
  anywhere the camera goes) made from tiling noise once per world. It's drawn with an
  upside-down version of the terrain ray march, about 40–60 distance steps out to 640 cells,
  starting at the first distance that reaches the screen. Each column draws a block's underside
  as it passes under it, and its side face when it reaches it across a gap. Span ends are
  blended by their fraction of a pixel so edges glide. Beyond 200 cells a blurred copy of the
  map gives soft coverage instead of flickering blocks, and the layer fades out with distance.
- **Bridges list.** Up to 10 bridges are remembered (out-of-range ones are kept, not deleted, so
  a city scanned early still has its bridge when you arrive).

## Serial monitor (115200 baud)

- The chosen plane, the start city, world seed, biome band order, and initial map time.
- Every 2 s:
  - The in-game time.
  - Average time per frame for sim, map generation, drawing and SPI blit; the worst frame; and
    frames that ran late.
  - Biome, altitude, ground height and clearance.
  - The drawing time split into terrain, sky and bridges/trails/plane, and the lowest gap
    between the plane and the ground under it in those 2 s.
- `>> entering <biome>`, `>> heading for a city N cells away`, `>> over the city`,
  `>> bridge, N cells long`, `>> loop the loop`, `>> barrel roll`.

The 128×68 simulation frame is nearest-neighbour scaled to 320×170 in the display BSP. It is sent
at 40 MHz as 10-row strips using two DMA buffers, keeping the transfer-buffer cost to 12.8 KB.
The timing report runs against a 33 ms target and remains useful for checking rendering speed on
the C6.

---

## Things to tweak

All of these are named constants in `voxel-flight.ino`, near the top or at the start of the
section named.

| Constant | Default | Effect |
|---|---|---|
| `PLANES` (planes) | | Each plane's speed, turn rate, bank, cruising height, camera and stat bars |
| `FORCE_PLANE` / `SHOW_FRAMES` | -1 / 105 | Skip the selection screen with a fixed plane; how long each plane is shown |
| `DAY_SECONDS` / `START_TIME` | 300 / 0.29 | Length of a day; time at boot (0 midnight, 0.25 dawn, 0.5 noon, 0.75 dusk). Use 0.8 to see night quickly. |
| `NIGHT_LIGHT` | 100, 110, 150 | Moonlight on the ground. Higher is brighter. |
| `NIGHT_HAZE` / `NIGHT_SKY` / `NIGHT_CLOUD` | | Night haze at the horizon (distant ground fades into it), sky overhead, cloud colour |
| `LOOP_FRAMES` / `LOOP_R` / `ROLL_FRAMES` / `ROLL_R` (autopilot) | 100 / 8 / 72 / 3 | Length and size of the Racer's loop and the Jet's barrel roll; `scheduleTrick(600, 1200)` sets how often |
| `CITY_CHANCE` / `VILLAGE_CHANCE` (terrain) | 120,0,0,150,150 / 90,0,0,150,80 | Chance out of 256 of a city per 256-cell area / a village per 64-cell block, by the biome at its centre (lakeland, mountains, canyon, farmland, coast) |
| `CITY_BLOCK` / `CITY_STREET` / `AVENUE_HALF` / `BOULEVARD_HALF` | 24 / 2 / 1 / 9 | City block size, street width, avenue half-width, half-width of the open corridor along the avenues |
| `nextBalloon` / `BALLOON_R` (hot air balloon) | 1800 / 4.5 | Frames until the first balloon chance; envelope radius in cells. The flyby odds (55%, then 33% for a circle) are in `updateBalloon` |
| `CRISP_STEP` (terrain renderer) | 16 | Height steps between cells bigger than this are drawn sharp; smaller ones as smooth slopes |
| `BRIDGE_HW` / `SUSPENSION_MIN` (bridges) | 4 / 30 | Bridge deck half-width; length above which a bridge gets towers and cables |
| `CITY_SEARCH_FRAMES` | 1800 | How often the autopilot looks for a city to visit (frames, 30 per second) |
| `SMOOTH_DIST` (terrain renderer) | 45 | How far out terrain heights, shading and material edges are smoothed |
| `BIOME_BLEND` | 112 | Width of the soft border between biomes (out of 256 per band, max 128) |
| `HORIZON_Y` | 21 | Horizon row; lower = looking more level |
| `ZFAR` / `FOG_START` | 100 / 0.45 | View distance (must stay well under 128); where the haze begins |
| `SEA` | 40 | Water level |
| `MAT_RGB` / `MAT_GLOW` | | Day colour and night glow of every material |

## Design rules (carried over from spore-cell)

- **Keep the display-independent renderer separate from board setup.** `voxel-flight.ino` owns
  simulation and RGB565 pixels; `main/board_bsp.cpp` owns the Waveshare hardware.
- **New shared structs go in `flight_types.h`.** Keep forward declarations for engine functions
  that are called before their definitions because ESP-IDF compiles the included `.ino` as C++.
- **No floating point in per-pixel or per-cell loops.** The C6 has no FPU. The 3D bridge
  triangles and lines first used floats per row and per pixel, and cost up to 40 ms a frame; in
  fixed point they cost 2–4 ms.
- **Keep motion smooth.** Filter every autopilot input; use soft floors instead of clamps; draw
  moving things with sub-pixel positions and soft edges; spread work evenly across frames.
- `constrain()` didn't clamp floats reliably; use `clampf` / `clampi`.

## Ideas for later

- Roads joining towns, rivers through the farmland, a lighthouse on the coast.
- Rain or snow, and a landing light beam at night.
- Fly the plane yourself with buttons or a joystick.

---

## History

| Date | Version |
|---|---|
| 2026-11-18 | First version: streaming voxel terrain, five fantasy biomes, autopilot jet, night sky |
| 2026-11-18 | Realistic rework: Cub-style plane, farmland/lakeland/mountains/canyon/coast, towns and cities, day/night cycle, speed-ups |
| 2026-11-18 | Smoothness pass: no turbulence; filtered autopilot; smooth terrain; anti-aliased plane; soft clouds; terrain-hugging shadow; gentler mountains; more cities |
| 2026-11-18 | Plane selection screen (Cub, Racer, Jet) with BOOT-button pick; big in-proportion skyscraper cities; suspension bridges; smoothed shading and dithered material edges; cumulus clouds; wingtip trails; autopilot clears bridges and side buildings |
| 2026-11-18 | Racer loops, Jet barrel rolls; depth-buffered plane drawing; propeller disc is a true 3D circle (no grey blob under the Racer on the selection screen); cities rebuilt as separate varied buildings with square roofs; autopilot plans climbs 130 cells ahead with a guaranteed climb rate; no walls at biome borders; Minecraft-style block clouds; brighter moonlit nights with a navy haze; city lights come on one by one |
| 2026-11-18 | 3D cloud layer (undersides and side faces in perspective); bridges no longer deleted as soon as they're found, and the plane routes over them on city visits; lower cruising height and eased haze curve; sparse villages with cottages in big gardens |
| 2026-11-18 | DMA display transfer (frames 13–23 ms); country roads with cuttings, tunnels and embankments; no more walls where town flattening stopped; smooth dithered sky gradient; bridge approach ramps; moon phases; farmland stays flat at biome borders |
| 2026-11-18 | Narrower roads (3-cell avenues, 2-cell streets, 1-cell lanes beyond the city); boulevards the plane flies down between towers; more open plazas; rare hot air balloons |
| 2026-11-18 | Balloons appear more reliably and closer to the route; balloon flybys (a pass alongside, sometimes a turn back for a second pass) |
| 2026-11-18 | Sun and moon rise, cross and set within the visible sky; smooth biome map (no walls at biome borders); integer balloon drawing |
| 2026-11-18 | USB frame capture and GIF tools; fixed: dark bar under the plane during loops (plane buffer read past its cleared area), black star specks at dawn, slow bridges at night, overlapping selection-screen labels; tidier suspension cables |
