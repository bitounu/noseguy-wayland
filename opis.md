# noseguy-wayland

Wayland-native reimplementation of the classic `noseguy` screensaver from XScreenSaver, designed to work as an animated background for swaylock via swaylock-plugin.

## Goals

- Recreate the behavior and visuals of the original `noseguy`
- Run natively on Wayland (no X11 / XWayland dependency)
- Integrate with swaylock-plugin as an animated lockscreen background
- Display text from external sources (e.g. `fortune`)
- Be lightweight and dependency-minimal

## Non-Goals

- Implementing a full screen locker (handled by swaylock-plugin)
- Supporting X11
- Pixel-perfect reproduction of legacy code

## Architecture Overview

swaylock-plugin → runs → noseguy-wayland (animation program)

## Core Components

### 1. Wayland Layer Surface

Use wlr-layer-shell:
- fullscreen surface
- background layer
- one surface per output

### 2. Rendering Engine

Recommended:
- cairo + pango

Responsibilities:
- draw character
- draw speech bubble
- render text
- animate (~30 FPS)

### 3. Animation System

State machine:

IDLE → THINKING → TALKING → WAIT → IDLE

### 4. Text Provider

Command:
--text-command "fortune -s"

File:
--text-file file.txt

STDIN:
echo "hi" | noseguy-wayland --stdin

### 5. CLI

noseguy-wayland [OPTIONS]

--text-command <cmd>
--text-file <path>
--stdin
--fps <n>
--font <name>
--font-size <px>
--interval <seconds>

## Integration

swaylock-plugin --command 'noseguy-wayland --text-command "fortune -s"'

Example with swayidle:

swayidle -w \
  timeout 300 'swaylock-plugin --command "noseguy-wayland --text-command fortune -s"' \
  timeout 600 'swaymsg "output * dpms off"' \
  resume 'swaymsg "output * dpms on"'

## Rendering Details

### Character

- simple 2D drawing
- animated nose, eyes, mouth

### Speech Bubble

- rounded rectangle
- tail pointing to character
- text inside

### Layout

Character: bottom-center  
Bubble: above character

## Dependencies

- wayland-client
- wlr-layer-shell
- cairo
- pango

## Build

meson + ninja

## Main Loop

init_wayland()
create_layer_surface()
init_renderer()

while running:
    if time_for_new_text:
        text = get_text()

    update_animation()
    draw_frame()
    commit_surface()

## Security

- no authentication
- no input handling
- locking handled externally

## Summary

Wayland-native animated noseguy for swaylock background.
