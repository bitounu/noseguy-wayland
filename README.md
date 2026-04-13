# Noseguy Wayland — Installation & Sway Integration Guide

## Dependencies

Install the required libraries before building:

```bash
# Arch Linux
sudo pacman -S wayland wayland-protocols cairo pango \
               wlr-protocols meson ninja gcc pkg-config

# Debian / Ubuntu
sudo apt install libwayland-dev wayland-protocols libcairo2-dev \
                 libpango1.0-dev meson ninja-build gcc pkg-config
```

---

## Building

### Standalone screensaver only

```bash
git clone <repo-url> noseguy
cd noseguy
meson setup build
ninja -C build
```

### With the plugin shared library

```bash
meson setup build -Dplugin=true
ninja -C build
```

---

## Installing

### System-wide

```bash
sudo ninja -C build install
```

Installs:
- `/usr/local/bin/noseguy-wayland`
- `/usr/local/lib/libnoseguy-plugin.so` (if built with `-Dplugin=true`)

### Per-user (no root required)

```bash
meson setup build --prefix="$HOME/.local"
ninja -C build install
```

Installs to `~/.local/bin/` and `~/.local/lib/`.  
Make sure `~/.local/bin` is in your `$PATH`.

### Sprites

Copy the sprite images to a permanent location:

```bash
mkdir -p ~/.local/share/noseguy
cp noseguy-images/*.png ~/.local/share/noseguy/
```

---

## Running standalone

```bash
# Vector character (no sprites needed)
noseguy-wayland

# With sprites
noseguy-wayland --sprites-dir ~/.local/share/noseguy

# With fortune quotes
noseguy-wayland --sprites-dir ~/.local/share/noseguy \
                --text-command "fortune -s"

# Custom appearance
noseguy-wayland \
  --sprites-dir ~/.local/share/noseguy \
  --text-command "fortune -s" \
  --reading-wpm 150 \
  --bg-color '#1a1a2e' \
  --bubble-color '#ffffffee' \
  --bubble-border-color '#5d4037cc' \
  --bubble-border-width 2 \
  --font-color '#1a1a1a'
```

Press **Escape** to quit.

---

## Sway integration

### swayidle — screensaver on idle

Install swayidle:

```bash
sudo pacman -S swayidle   # Arch
sudo apt install swayidle # Debian
```

Add to your Sway config (`~/.config/sway/config`):

```
exec swayidle -w \
  timeout 300 'noseguy-wayland --sprites-dir ~/.local/share/noseguy --text-command "fortune -s"' \
       resume 'pkill noseguy-wayland' \
  timeout 600 'swaymsg "output * dpms off"' \
       resume 'swaymsg "output * dpms on"' \
  before-sleep 'pkill noseguy-wayland'
```

Timings:
- **300 s** (5 min) — noseguy appears
- **600 s** (10 min) — monitors turn off
- **before-sleep** — noseguy is killed before suspend

Reload Sway config after editing:

```
swaymsg reload
```

### swayidle + swaylock — screensaver then lock screen

```
exec swayidle -w \
  timeout 300  'noseguy-wayland --sprites-dir ~/.local/share/noseguy --text-command "fortune -s"' \
       resume  'pkill noseguy-wayland' \
  timeout 360  'pkill noseguy-wayland; swaylock --screenshots --effect-blur 7x5' \
  timeout 600  'swaymsg "output * dpms off"' \
       resume  'swaymsg "output * dpms on"' \
  before-sleep 'pkill noseguy-wayland; swaylock --screenshots --effect-blur 7x5'
```

> **Note:** swaylock-effects is required for `--screenshots --effect-blur`.  
> Plain `swaylock` works without those flags.

### Manual keybinding to start/stop

Add to `~/.config/sway/config`:

```
bindsym $mod+Shift+s exec noseguy-wayland \
    --sprites-dir ~/.local/share/noseguy \
    --text-command "fortune -s"

bindsym $mod+Shift+q exec pkill noseguy-wayland
```

---

## Using noseguy-wayland with swaylock

### How Wayland layers interact

Wayland layer-shell defines four stacking layers (bottom to top):

```
OVERLAY  ← swaylock always renders here (grabs exclusive input)
TOP      ← noseguy-wayland renders here
BOTTOM
BACKGROUND
```

Because swaylock sits on `OVERLAY` and noseguy sits on `TOP`, swaylock
always covers noseguy completely. Noseguy therefore acts as a **screensaver
before the lock screen activates**, not as a background inside it.

### Recommended workflow: screensaver → lock

This is the standard pattern under Sway. noseguy runs for a minute before the
lock screen appears, giving the user a visible idle indicator:

```
exec swayidle -w \
  timeout 300  'noseguy-wayland --sprites-dir ~/.local/share/noseguy --text-command "fortune -s"' \
       resume  'pkill noseguy-wayland' \
  timeout 360  'pkill -TERM noseguy-wayland; swaylock -f' \
  timeout 600  'swaymsg "output * dpms off"' \
       resume  'swaymsg "output * dpms on"' \
  before-sleep 'pkill -TERM noseguy-wayland; swaylock -f'
```

Timeline:
- **5 min idle** — noseguy walks across the screen
- **6 min idle** — noseguy is killed, swaylock takes over the screen
- **10 min idle** — monitors go dark
- **Wake / resume** — monitors come back, swaylock still active until password

### noseguy-lock — combined lock helper script

For a `mod+l` keybinding that kills any running noseguy, optionally takes a
blurred screenshot, and locks immediately, save this as
`~/.local/bin/noseguy-lock` and make it executable:

```bash
#!/bin/bash
# noseguy-lock — kill screensaver and lock with optional blurred screenshot

SPRITES="$HOME/.local/share/noseguy"
TMPIMG="/tmp/swaylock-bg.png"

pkill -TERM noseguy-wayland 2>/dev/null
sleep 0.1

# Take a screenshot and blur it (requires grim + imagemagick or swaylock-effects)
if command -v grim &>/dev/null && command -v convert &>/dev/null; then
    grim "$TMPIMG" && convert "$TMPIMG" -blur 0x8 "$TMPIMG"
    swaylock -f -i "$TMPIMG"
elif swaylock --help 2>&1 | grep -q effect-blur; then
    # swaylock-effects installed
    swaylock -f --screenshots --effect-blur 7x5
else
    swaylock -f -c 000000
fi
```

```bash
chmod +x ~/.local/bin/noseguy-lock
```

Add to `~/.config/sway/config`:

```
bindsym $mod+l exec noseguy-lock
```

### swaylock-effects — one-shot custom effects

`swaylock-effects` supports `--effect-custom <path>` which loads a `.so` and
calls one of these functions **once** to process the background image:

```c
/* process the whole frame at once */
void swaylock_effect(uint32_t *data, int width, int height, int scale);

/* or process pixel by pixel */
uint32_t swaylock_pixel(uint32_t pix, int x, int y, int width, int height);
```

This is a **static image effect**, not an animation loop. It is not compatible
with `libnoseguy-plugin.so`. The noseguy plugin uses a different ABI designed
for continuous rendering (`screensaver_init` / `screensaver_render` /
`screensaver_destroy`).

---

## Plugin shared library

`libnoseguy-plugin.so` is intended for **custom Wayland lock screens, display
managers, or any application** that wants to embed the noseguy animation inside
its own rendering loop. It is not a swaylock-effects plugin.

The exported ABI:

```c
void screensaver_init(int width, int height);    /* call once at start / resize */
void screensaver_render(cairo_t *cr, double dt); /* call each frame             */
void screensaver_destroy(void);                  /* call on exit / cleanup      */
```

A host must supply a `cairo_t` backed by an image surface for each frame and
handle presenting it to the screen itself (via Wayland SHM, EGL, or another
mechanism).

Configuration is passed via environment variables — no recompilation needed:

| Variable                | Default          | Description                              |
|-------------------------|------------------|------------------------------------------|
| `NOSEGUY_SPRITES_DIR`   | *(vector art)*   | Directory containing `nose-*.png`        |
| `NOSEGUY_TEXT_COMMAND`  | *(built-in)*     | Shell command that prints one quote      |
| `NOSEGUY_TEXT_FILE`     | *(built-in)*     | Text file with one quote per line        |
| `NOSEGUY_READING_WPM`   | `200`            | Reading speed (words per minute)         |
| `NOSEGUY_FONT`          | `Sans`           | Pango font name                          |
| `NOSEGUY_FONT_SIZE`     | `18`             | Font size in pixels                      |
| `NOSEGUY_FONT_COLOR`    | `#0d0d0d`        | Speech bubble text color `#rrggbb`       |
| `NOSEGUY_BG_COLOR`      | `#000000`        | Background fill color `#rrggbb`          |
| `NOSEGUY_BUBBLE_COLOR`  | `#ffffffee`      | Bubble fill color `#rrggbbaa`            |
| `NOSEGUY_BUBBLE_BORDER` | `#2e2e2ed9`      | Bubble border color `#rrggbbaa`          |
| `NOSEGUY_BUBBLE_WIDTH`  | `1.5`            | Bubble border line width in pixels       |

### Loading the plugin at runtime

```c
#include <dlfcn.h>
#include <cairo/cairo.h>

void *lib = dlopen("libnoseguy-plugin.so", RTLD_LAZY);

void (*init)   (int, int)            = dlsym(lib, "screensaver_init");
void (*render) (cairo_t *, double)   = dlsym(lib, "screensaver_render");
void (*destroy)(void)                = dlsym(lib, "screensaver_destroy");

init(width, height);

/* animation loop */
while (running) {
    render(cr, dt);          /* dt = seconds since last frame */
    present_frame();
}

destroy();
dlclose(lib);
```

---

## CLI options reference

| Option                        | Default      | Description                            |
|-------------------------------|--------------|----------------------------------------|
| `--sprites-dir <path>`        | *(vector)*   | Directory with `nose-*.png` sprites    |
| `--text-command <cmd>`        | built-in     | Shell command for quotes               |
| `--text-file <path>`          | built-in     | Text file, one quote per line          |
| `--stdin`                     |              | Read quotes from stdin                 |
| `--interval <seconds>`        | `30`         | Seconds between text fetches           |
| `--fps <n>`                   | `30`         | Target frame rate                      |
| `--reading-wpm <n>`           | `200`        | Reading speed in words per minute      |
| `--font <name>`               | `Sans`       | Pango font name                        |
| `--font-size <px>`            | `18`         | Font size in pixels                    |
| `--font-color <#rrggbb>`      | `#0d0d0d`    | Speech bubble text color               |
| `--bg-color <#rrggbb>`        | `#000000`    | Background color                       |
| `--bubble-color <#rrggbbaa>`  | `#ffffffee`  | Bubble fill color (supports alpha)     |
| `--bubble-border-color <…>`   | `#2e2e2ed9`  | Bubble border color (supports alpha)   |
| `--bubble-border-width <px>`  | `1.5`        | Bubble border line width               |

Press **Escape** to quit when running interactively.
