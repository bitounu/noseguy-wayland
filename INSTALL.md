# Installing noseguy-wayland

## Extract the release archive

```bash
tar -xzf noseguy-<VERSION>-x86_64.tar.gz
cd noseguy-<VERSION>-x86_64
```

## Install the binary

```bash
sudo cp noseguy-wayland /usr/local/bin/
```

Or per-user (no root required):

```bash
cp noseguy-wayland ~/.local/bin/
```

Make sure `~/.local/bin` is in your `$PATH`.

## Install sprites

```bash
mkdir -p ~/.local/share/noseguy
cp noseguy-images/*.png ~/.local/share/noseguy/
```

## Run

```bash
# Vector character — no sprites needed
noseguy-wayland

# With sprites
noseguy-wayland --sprites-dir ~/.local/share/noseguy

# With fortune quotes
noseguy-wayland --sprites-dir ~/.local/share/noseguy --text-command "fortune -s"
```

Press **Escape** to quit.

## Plugin (libnoseguy-plugin.so)

`libnoseguy-plugin.so` is a shared library for embedding the noseguy animation
in custom Wayland lock screens or display managers.

For installation instructions and the full plugin ABI documentation, see the
repository: <REPO_URL>
