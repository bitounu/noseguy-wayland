# Gitea Release Workflow Design

**Date:** 2026-04-14  
**Project:** noseguy-wayland  

---

## Goal

Automate building and releasing `noseguy-wayland` and `libnoseguy-plugin.so` to Gitea Releases whenever a version tag is pushed.

---

## Trigger

- File: `.gitea/workflows/release.yml`
- Event: `push` to refs matching `refs/tags/v*`
- Examples: `v0.1.0`, `v1.2.3`
- Nothing else triggers this workflow

---

## Approach

Single workflow file with three sequential jobs (Option A). Both artifacts are built in one `meson` invocation so there is nothing to parallelize. Simplicity preferred over structure.

---

## Jobs

### 1. `test`

- Runner: `archlinux:latest`
- Install dependencies via `pacman -Syu --noconfirm`
  - `wayland`, `wayland-protocols`, `cairo`, `pango`, `meson`, `ninja`, `gcc`, `pkg-config`, `wlr-protocols`
- Run `meson setup build`
- Run `meson test -C build`
- Fails fast: if any test fails, subsequent jobs do not run

### 2. `build`

- `needs: [test]`
- Runner: `archlinux:latest`
- Fresh container — reinstall dependencies
- Run `meson setup build -Dplugin=true`
- Run `ninja -C build`
- Upload artifacts: `build/noseguy-wayland`, `build/libnoseguy-plugin.so`

### 3. `release`

- `needs: [build]`
- Runner: `archlinux:latest`
- Download artifacts from `build` job
- Extract tag name from `GITHUB_REF` → `VERSION` (e.g. `v0.1.0`)
- Assemble staging directory:
  - `noseguy-wayland` (binary)
  - `libnoseguy-plugin.so` (plugin shared library)
  - `noseguy-images/` (sprite PNGs)
  - `INSTALL.md` (installation instructions)
- Pack: `noseguy-${VERSION}-x86_64.tar.gz`
- Create Gitea release via API (`curl`) using built-in `GITEA_TOKEN` secret
  - Title: tag name (e.g. `v0.1.0`)
  - Body: empty (editable manually after release)
- Upload tarball as release asset

---

## Release Tarball Contents

```
noseguy-v0.1.0-x86_64.tar.gz
├── noseguy-wayland
├── libnoseguy-plugin.so
├── noseguy-images/
│   └── *.png
└── INSTALL.md
```

---

## INSTALL.md

A static file committed once to the repo root. The workflow copies it into the tarball at release time — it is not generated dynamically.

Contents:

- How to extract the tarball
- How to install the binary (`noseguy-wayland`) and sprites
- Basic usage examples (standalone, with sprites, with fortune)
- For `libnoseguy-plugin.so`: a link to the repository for the full plugin ABI documentation

The full `README.md` is not included in the tarball — `INSTALL.md` is the concise end-user guide.

---

## Secrets & Permissions

- `GITEA_TOKEN`: built-in Gitea Actions secret, used for API calls to create the release and upload assets
- No additional secrets required

---

## Files Created

| Path | Description |
|------|-------------|
| `.gitea/workflows/release.yml` | The workflow definition |
| `INSTALL.md` | Installation guide included in tarball |
