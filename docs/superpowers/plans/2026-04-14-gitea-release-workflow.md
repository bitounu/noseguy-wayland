# Gitea Release Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Gitea Actions workflow that runs tests, builds both artifacts, and publishes a release tarball whenever a `v*` tag is pushed.

**Architecture:** Single workflow file with three sequential jobs (`test` → `build` → `release`). Both artifacts are produced in one Meson invocation (`-Dplugin=true`). A static `INSTALL.md` is committed to the repo and copied into the tarball at release time.

**Tech Stack:** Gitea Actions, Arch Linux Docker container (`archlinux:latest`), Meson, Ninja, GCC, curl (Gitea API)

---

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `INSTALL.md` | End-user installation guide, included in every release tarball |
| Create | `.gitea/workflows/release.yml` | Gitea Actions workflow definition |

---

## Task 1: Create INSTALL.md

**Files:**
- Create: `INSTALL.md`

- [ ] **Step 1: Write INSTALL.md**

Create `INSTALL.md` at the repo root with the following content (replace `<REPO_URL>` with your actual Gitea repository URL before committing):

```markdown
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
```

- [ ] **Step 2: Replace `<REPO_URL>` with the actual Gitea repository URL**

Edit the last line of `INSTALL.md` — replace `<REPO_URL>` with your Gitea repo URL, e.g.:

```
repository: https://gitea.example.com/youruser/noseguy-wayland
```

- [ ] **Step 3: Commit**

```bash
git add INSTALL.md
git commit -m "docs: add INSTALL.md for release tarballs"
```

---

## Task 2: Create the Gitea Actions workflow

**Files:**
- Create: `.gitea/workflows/release.yml`

- [ ] **Step 1: Create the workflows directory**

```bash
mkdir -p .gitea/workflows
```

- [ ] **Step 2: Write the workflow file**

Create `.gitea/workflows/release.yml` with the following content:

```yaml
name: Release

on:
  push:
    tags:
      - 'v*'

jobs:
  test:
    runs-on: ubuntu-latest
    container:
      image: archlinux:latest
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          pacman -Syu --noconfirm
          pacman -S --noconfirm \
            wayland wayland-protocols \
            cairo pango \
            meson ninja gcc pkg-config

      - name: Configure
        run: meson setup build

      - name: Run tests
        run: meson test -C build --print-errorlogs

  build:
    needs: test
    runs-on: ubuntu-latest
    container:
      image: archlinux:latest
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          pacman -Syu --noconfirm
          pacman -S --noconfirm \
            wayland wayland-protocols \
            cairo pango \
            meson ninja gcc pkg-config

      - name: Build (app + plugin)
        run: |
          meson setup build -Dplugin=true
          ninja -C build

      - name: Upload binaries
        uses: actions/upload-artifact@v4
        with:
          name: binaries
          path: |
            build/noseguy-wayland
            build/libnoseguy-plugin.so

  release:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Download binaries
        uses: actions/download-artifact@v4
        with:
          name: binaries
          path: artifacts/

      - name: Set version
        run: echo "VERSION=${GITHUB_REF_NAME}" >> "$GITHUB_ENV"

      - name: Assemble tarball
        run: |
          STAGING="noseguy-${VERSION}-x86_64"
          mkdir -p "${STAGING}/noseguy-images"
          cp artifacts/noseguy-wayland        "${STAGING}/"
          cp artifacts/libnoseguy-plugin.so   "${STAGING}/"
          cp noseguy-images/*.png             "${STAGING}/noseguy-images/"
          cp INSTALL.md                       "${STAGING}/"
          chmod +x "${STAGING}/noseguy-wayland"
          tar -czf "noseguy-${VERSION}-x86_64.tar.gz" "${STAGING}"

      - name: Create Gitea release
        id: create_release
        run: |
          RELEASE_ID=$(curl -s -X POST \
            -H "Authorization: token ${{ secrets.GITHUB_TOKEN }}" \
            -H "Content-Type: application/json" \
            "${GITHUB_SERVER_URL}/api/v1/repos/${GITHUB_REPOSITORY}/releases" \
            -d "{\"tag_name\":\"${VERSION}\",\"name\":\"${VERSION}\"}" \
            | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
          echo "release_id=${RELEASE_ID}" >> "$GITHUB_OUTPUT"

      - name: Upload release asset
        run: |
          ASSET="noseguy-${VERSION}-x86_64.tar.gz"
          curl -s -X POST \
            -H "Authorization: token ${{ secrets.GITHUB_TOKEN }}" \
            -H "Content-Type: application/octet-stream" \
            "${GITHUB_SERVER_URL}/api/v1/repos/${GITHUB_REPOSITORY}/releases/${{ steps.create_release.outputs.release_id }}/assets?name=${ASSET}" \
            --data-binary @"${ASSET}"
```

- [ ] **Step 3: Commit**

```bash
git add .gitea/workflows/release.yml
git commit -m "ci: add Gitea release workflow (test → build → release on v* tags)"
```

---

## Task 3: Verify the workflow

- [ ] **Step 1: Push commits to Gitea**

```bash
git push origin master
```

- [ ] **Step 2: Create and push a test tag**

```bash
git tag v0.1.0
git push origin v0.1.0
```

- [ ] **Step 3: Check the workflow run in Gitea**

Open your Gitea repo → **Actions** tab. You should see a workflow run named `Release` triggered by the `v0.1.0` tag.

Expected progression:
1. `test` job runs — both `test-anim` and `test-text` pass
2. `build` job runs — `noseguy-wayland` and `libnoseguy-plugin.so` are produced and uploaded as artifacts
3. `release` job runs — tarball `noseguy-v0.1.0-x86_64.tar.gz` is created and attached to the release

- [ ] **Step 4: Verify the release**

Open your Gitea repo → **Releases** tab. You should see a release named `v0.1.0` with one asset:

```
noseguy-v0.1.0-x86_64.tar.gz
```

- [ ] **Step 5: Verify the tarball contents**

```bash
curl -LO <release-asset-url>
tar -tzf noseguy-v0.1.0-x86_64.tar.gz
```

Expected output:
```
noseguy-v0.1.0-x86_64/
noseguy-v0.1.0-x86_64/noseguy-wayland
noseguy-v0.1.0-x86_64/libnoseguy-plugin.so
noseguy-v0.1.0-x86_64/noseguy-images/
noseguy-v0.1.0-x86_64/noseguy-images/nose-f1.png
noseguy-v0.1.0-x86_64/noseguy-images/nose-f2.png
... (all sprites)
noseguy-v0.1.0-x86_64/INSTALL.md
```
