# Open a Disk Image from the Linux Desktop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Double-clicking a `.dmg` in a Linux file manager mounts it in Darling (after its licence agreement) and opens
its volume's window in Finder: plan 8 of the Finder roadmap.

**Architecture:** The launcher gets a generic `darling open <file>...` that maps each host path into the guest
(`/Volumes/SystemRoot/<real path>`, as it already does for program paths) and runs the guest's `/usr/bin/open` on
them; LaunchServices, DiskImageMounter and Finder do the rest, as built in plans 6–7c. A `.desktop` entry for
`application/x-apple-diskimage`, a shared-mime-info package and hicolor icons, installed from CMake in the `gui`
component, make the Linux desktop use it. An end-to-end test drives the whole flow from the host side.

**Tech Stack:** C (launcher), CMake, freedesktop Desktop Entry and shared-mime-info XML, Python 3 (host test,
fixture generator), bash (GUI test, X11 and Wayland).

**Spec:** `docs/superpowers/specs/2026-11-16-finder-app-design.md` (sections "Linux desktop integration",
"Testing", "Left to the builder"). Roadmap: `docs/superpowers/plans/2026-11-16-finder-roadmap.md`, row 8.

## Global Constraints

- AGENTS.md governs: documented contracts only, no app-specific hacks, `QUIRK:` comments with evidence and a test,
  no debug leftovers, a **Provenance** note in every commit message.
- Spec: the launcher feature "must not be Finder- or DMG-specific"; "Exact CLI shape is the builder's call".
- Spec: "A `.desktop` file declaring `MimeType=application/x-apple-diskimage;` that uses it, plus a shared-mime-info
  entry for `*.dmg` (installed so it does not conflict with a desktop that already defines the type)."
- Spec: "Packaging: install from CMake; add to the Debian/RPM file lists of the GUI package."
- Spec: "All three launcher modes (root, rootless, non-root) must support mount, eject and the end-to-end flow."
- Spec end-to-end: "open from the host side → agreement → mount → Finder window layout → copy into a temporary
  destination → eject. GUI checks follow the darling-skills X11/Wayland test guides."
- Roadmap common rules: build in `~/Projects/darling/build`, install with `make install` into `~/darling-finder`
  only; re-apply `chown root:root` + `chmod 4755` to `~/darling-finder/bin/darling` after every install and refresh
  the non-setuid copy `rootless-bin/darling`; containers `~/.darling-finder` (rootless), `~/.darling-finder-nr`
  (non-root), `~/.darling-finder-root` (root); push the branch, open no pull request.
- Temporary files in `~/Projects/darling/build/tmp`, never `/tmp`. GUI tests only on the private Xvfb `:97` and the
  private sway (`build/tmp/wl`).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01CRFMixwsFg4yEYzqovZF1D`. Never `git add -A`/`commit -a`;
  files under `tests/` need `git add -f`.

## Decisions this plan makes (the spec leaves them to the builder)

- CLI: `darling open <file>...`. Every argument is a host path, mapped as program paths are (`realpath`, then
  `/Volumes/SystemRoot` in front; `/` itself maps to `/Volumes/SystemRoot`). `open` options (`-a`, `-R`, …) are not
  passed through: in `darling shell open …` they already work. A file in the working directory named `open` can no
  longer be run as `darling open`, just as one named `shell` can't (use `darling ./open`).
- A missing file prints `The file <argument> does not exist.` (the wording of macOS `open`) and exits 1; no arguments
  print `Usage: darling open <file>...` and exit 1. Both before anything is spawned in the guest.
- Desktop file id `org.darlinghq.Darling.DiskImageMounter` (reverse DNS, as the Desktop Entry spec recommends), name
  "Disk Image Mounter (Darling)", `NoDisplay=true` (it only makes sense with a file, so it stays out of the
  applications menu and appears under "Open With"), `Exec` naming the installed launcher by absolute path.
- Installing does not make it the default handler for disk images (that is the user's or the distribution's choice);
  the README shows the `xdg-mime default` command.
- The MIME package file is `org.darlinghq.Darling.DiskImageMounter.xml` and defines the same type freedesktop.org's
  database already does, with the same English comment and glob, so the merged database is unchanged where the type
  exists and complete where it doesn't. No maintainer scripts: debhelper and RPM file triggers refresh the MIME,
  desktop and icon caches.
- Icons: the existing `DiskImageMounter.iconset` PNGs (Adwaita-derived, LGPL-3 / CC-BY-SA-3.0, see
  `src/DiskImageMounter/COPYING.Adwaita`) at 16, 32, 128, 256 and 512 px into `share/icons/hicolor/<N>x<N>/apps/`.
- Packaging: the new files go into CMake component `gui`; `debian/darling-gui.install` (`gui/usr /`) and the RPM
  `%files gui -f build/files.gui.txt` list that component's whole install tree, so no list needs editing. The
  Debian `copyright` file gains a stanza for the icon images.
- End-to-end fixture: `styled-sla-test.dmg`, the styled image of `make-styled-fixture.py` (same volume "Styled Test")
  with the English-only licence agreement of `make-sla-fixture.py … 5000 one` embedded, built on the macOS 26.7.3 Mac.
- The GUI helpers at the top of `tests/finder.sh` move to `tests/finder-lib.sh`, sourced by `finder.sh` and the new
  test, instead of being copied.

## Review Focus

1. Host paths with spaces, single quotes, `$`, `&`, a newline and non-ASCII characters → the guest gets the same name
   (unit test cases in Task 1; the end-to-end image lives at such a path in Task 3).
2. The Darling container is not running and there is no terminal when the desktop launches the entry (first
   double-click of a session) → the licence window still appears (Task 3, "cold start": `darling shutdown`, then the
   entry launched with `setsid` and stdin from `/dev/null`).
3. A relative path, a symbolic link to the image, `/`, a dangling link, a missing file and no arguments → each has the
   result the decisions above give (Task 1 unit test; Task 3 "command line" case).
4. A host whose shared-mime-info already defines `application/x-apple-diskimage` → `*.dmg` still maps to it once and
   freedesktop.org's translated comments survive (Task 2 test); a host without it → our file alone defines it.
5. Several images selected and opened at once (`%F` with two files) → each is mounted and gets its window (Task 3,
   "two files").

---

### Task 1: `darling open <file>...` in the launcher

**Files:**
- Create: `src/startup/launcher-path.h`, `src/startup/launcher-path.c`
- Create: `tests/launcher/path.c`
- Modify: `src/startup/darling.c` (`main`'s subcommand dispatch near line 545, `resolveProgramArgument` at 609,
  its early call for the rootless mode at 527, `showHelp` at 1333)
- Modify: `src/startup/CMakeLists.txt` (add `launcher-path.c` to `darling`; add test `launcher_path`)
- Modify: `tests/launcher/README.md`, `README.md` (Usage: "Opening files from the Linux desktop")

**Interfaces:**
- Produces: `char *launcherGuestPath(const char *hostPath);` — malloc'd guest path, or NULL with `errno` set
  (from `realpath`). Used by `resolveProgramArgument` for program paths and for every `open` argument.
- Produces: the command `darling open <file>...` (Task 2's `Exec` and Task 3's test run it).

- [ ] **Step 1: Write the failing unit test `tests/launcher/path.c`**

  It makes a scratch directory under `$TMPDIR` (the ctest environment sets it to the build tree) and checks, printing
  PASS/FAIL per case and exiting non-zero on any failure:
  - `launcherGuestPath("<dir>/a.dmg")` → `"/Volumes/SystemRoot<real dir>/a.dmg"` (`<real dir>` = `realpath(dir)`)
  - after `chdir(dir)`: `launcherGuestPath("a.dmg")` and `launcherGuestPath("./sub/../a.dmg")` → the same
  - a symbolic link `link.dmg -> a.dmg` → the path of `a.dmg`
  - `launcherGuestPath("/")` → `"/Volumes/SystemRoot"`
  - names `"Späce & 'q' $x.dmg"` and `"new\nline.dmg"` and one with the byte `0xff` → returned byte for byte
  - `"missing.dmg"` → NULL, `errno == ENOENT`; a dangling link → NULL, `ENOENT`

- [ ] **Step 2: Register it and watch it fail**

  `add_executable(launcher_path_test ${CMAKE_SOURCE_DIR}/tests/launcher/path.c launcher-path.c)` with the include
  directory, `add_test(NAME launcher_path COMMAND launcher_path_test)` (follow the neighbouring launcher tests).
  Run: `cd build && make launcher_path_test && ctest -R launcher_path --output-on-failure`
  Expected: build failure, `launcher-path.c` missing / `launcherGuestPath` undefined.

- [ ] **Step 3: Implement `launcherGuestPath` in `src/startup/launcher-path.c`**

  `realpath`, then `SYSTEM_ROOT` (from `darling-config.h`) prepended; `/` maps to `SYSTEM_ROOT` alone.

- [ ] **Step 4: Run the unit test**

  Run: `cd build && make launcher_path_test && ctest -R launcher_path --output-on-failure`
  Expected: `100% tests passed`.

- [ ] **Step 5: The `open` subcommand in `darling.c`**

  - `resolveProgramArgument` uses `launcherGuestPath`; for `argv[1] == "open"` it maps `argv[2..argc-1]` in place,
    printing `Usage: darling open <file>...` (no arguments) or `The file <argument> does not exist.` (stderr, the
    argument as given) and returning false. It already runs before the rootless join and after the root-mode join;
    `open` takes the same path.
  - `main`: `open` spawns `spawnBinary("/usr/bin/open", argv)` with `argv[0] = "open"` followed by the mapped paths
    (no shell: arguments reach the guest untouched).
  - `showHelp`: add `\t%s open <file>...` and one sentence: opens host files in Darling, as `open` does there.
  - `README.md` Usage: a "Opening files from the Linux desktop" subsection: `darling open ~/Downloads/App.dmg`, what
    happens, and `xdg-mime default org.darlinghq.Darling.DiskImageMounter.desktop application/x-apple-diskimage` to
    make it the default. `tests/launcher/README.md`: the `launcher_path` test.

- [ ] **Step 6: Build, install, check the errors in the guest**

  `make -C build -j$(nproc) && make -C build install`, then the setuid and `rootless-bin/darling` refresh. In each of
  the three modes (setuid root with `DPREFIX=~/.darling-finder-root`; `rootless-bin/darling` with
  `DARLING_ROOTLESS=1 DPREFIX=~/.darling-finder`; `DARLING_NONROOT=1 DPREFIX=~/.darling-finder-nr` with
  `rootless-bin/darling`):
  Run: `timeout -s KILL 120 darling open build/tmp/p8/missing.dmg; echo rc=$?` and
  `timeout -s KILL 120 darling open; echo rc=$?`
  Expected: `The file build/tmp/p8/missing.dmg does not exist.` rc=1; `Usage: darling open <file>...` rc=1.
  (The working case is Task 3's, which needs a display.)

- [ ] **Step 7: Commit**

  `git add src/startup/launcher-path.[ch] src/startup/darling.c src/startup/CMakeLists.txt README.md` and
  `git add -f tests/launcher/path.c tests/launcher/README.md`;
  message `launcher: darling open <file>... opens host files in Darling` with a Provenance note (macOS `open(1)`
  manual for the message wording; no restricted material).

### Task 2: Desktop entry, MIME type and icons, installed in the GUI package

**Files:**
- Create: `src/DiskImageMounter/linux/org.darlinghq.Darling.DiskImageMounter.desktop.in`
- Create: `src/DiskImageMounter/linux/org.darlinghq.Darling.DiskImageMounter.xml`
- Modify: `src/DiskImageMounter/CMakeLists.txt` (configure and install, `COMPONENT gui`)
- Modify: `debian/copyright` (stanza for `src/DiskImageMounter/DiskImageMounter.iconset/*`)
- Create: `tests/desktop-integration.py`; register it as ctest `desktop_integration` (when Python 3 is found) in
  `src/DiskImageMounter/CMakeLists.txt`

**Interfaces:**
- Consumes: `darling open <file>...` (Task 1).
- Produces, installed under `${CMAKE_INSTALL_PREFIX}/share`:
  `applications/org.darlinghq.Darling.DiskImageMounter.desktop`,
  `mime/packages/org.darlinghq.Darling.DiskImageMounter.xml`,
  `icons/hicolor/{16x16,32x32,128x128,256x256,512x512}/apps/org.darlinghq.Darling.DiskImageMounter.png`.
  Task 3 launches the installed `.desktop`.

- [ ] **Step 1: Write the failing test `tests/desktop-integration.py`**

  `desktop-integration.py <build dir> <install prefix>` (unittest). It installs only DiskImageMounter's directory:
  `DESTDIR=<tmp> cmake -DCOMPONENT=gui -P <build>/src/DiskImageMounter/cmake_install.cmake` (tmp under the build
  tree), then:
  - `test_files`: the three kinds of files above exist under `<tmp><prefix>/share`, each icon PNG is N×N (read from
    its IHDR).
  - `test_desktop_entry`: `desktop-file-validate` passes (skip if missing); `Type=Application`,
    `MimeType=application/x-apple-diskimage;`, `NoDisplay=true`, `Terminal=false`,
    `Icon=org.darlinghq.Darling.DiskImageMounter`, and `Exec` split per the Desktop Entry quoting rules is
    `[<prefix>/bin/darling, open, %F]`.
  - `test_mime_alone`: `update-mime-database` on a scratch `mime/` holding only our package → `globs2` has
    `*.dmg` → `application/x-apple-diskimage`.
  - `test_mime_with_freedesktop`: the same with the host's `/usr/share/mime/packages/freedesktop.org.xml` copied in
    (skip if absent) → `globs2` has the `*.dmg` line exactly once, and `application/x-apple-diskimage.xml` still has
    a `xml:lang="de"` comment.
  - `test_handler`: with `XDG_DATA_HOME` and `XDG_DATA_DIRS` pointing only at the scratch `share` (after
    `update-desktop-database` on its `applications`), `gio mime application/x-apple-diskimage` lists
    `org.darlinghq.Darling.DiskImageMounter.desktop` (skip if `gio` is missing).

- [ ] **Step 2: Watch it fail**

  Run: `cd build && cmake . && ctest -R desktop_integration --output-on-failure`
  Expected: `test_files` fails (no `.desktop` installed); the others fail or error for the same reason.

- [ ] **Step 3: Write the two files and the install rules**

  `.desktop.in`: `[Desktop Entry]`, `Type=Application`, `Name=Disk Image Mounter (Darling)`,
  `Comment=Open macOS disk images in Darling`, `Exec="@CMAKE_INSTALL_PREFIX@/bin/darling" open %F`,
  `Icon=org.darlinghq.Darling.DiskImageMounter`, `MimeType=application/x-apple-diskimage;`, `NoDisplay=true`,
  `Terminal=false`, `Categories=System;`. `configure_file(... @ONLY)` into the build tree.
  `.xml`: shared-mime-info `mime-info` with `application/x-apple-diskimage`, `<comment>Apple disk image</comment>`,
  `<glob pattern="*.dmg"/>`. Install rules, all `COMPONENT gui`: the desktop file to `share/applications`, the XML
  to `share/mime/packages`, `icon_<N>x<N>.png` renamed to `org.darlinghq.Darling.DiskImageMounter.png` in
  `share/icons/hicolor/<N>x<N>/apps`. `debian/copyright`: `Files: src/DiskImageMounter/DiskImageMounter.iconset/*`
  with the copyright and licences that `COPYING.Adwaita` names.

- [ ] **Step 4: Run the test**

  Run: `cd build && cmake . && ctest -R desktop_integration --output-on-failure`
  Expected: passes (a skip only where the host lacks the tool named above).

- [ ] **Step 5: Packaging check**

  Run: `python3 tests/debian-packaging.py` (needs debhelper; record SKIP if absent) and
  `grep -n 'gui/usr' debian/darling-gui.install; grep -n 'files gui ' rpm/SPECS/darling.spec`
  Expected: the Debian test passes; `gui/usr /` and `%files gui -f build/files.gui.txt` are present (the component
  carries the new files into both packages).

- [ ] **Step 6: Commit**

  `git add src/DiskImageMounter/linux src/DiskImageMounter/CMakeLists.txt debian/copyright`,
  `git add -f tests/desktop-integration.py`; message `DiskImageMounter: desktop entry, MIME type and icons for the
  Linux desktop` with a Provenance note (freedesktop.org Desktop Entry and Shared MIME-info specifications; the
  existing Adwaita-derived icons).

### Task 3: End to end — from the Linux desktop to an ejected volume

**Files:**
- Modify: `tests/fixtures/make-styled-fixture.py` (optional `--license <resources.xml>`)
- Create: `tests/fixtures/styled-sla-test.dmg` (built on the 26.7.3 Mac)
- Create: `tests/finder-lib.sh` (the helpers now at the top of `tests/finder.sh`)
- Modify: `tests/finder.sh` (sources `finder-lib.sh`; no case changes)
- Create: `tests/open-disk-image.sh`, `tests/open-disk-image.md`
- Modify: `docs/superpowers/plans/2026-11-16-finder-roadmap.md` (row 8: done)

**Interfaces:**
- Consumes: the installed `share/applications/org.darlinghq.Darling.DiskImageMounter.desktop` (Task 2), whose `Exec`
  runs `darling open %F` (Task 1).
- Produces: `tests/finder-lib.sh`, sourced after `set -u`, defining what `finder.sh` defines today (`work`, `gw`,
  `display`, `pass`/`fail`/`skip`, `guest`, `rc`, `outputs`, `find_windows`, `windows`, `window`, `window_gone`,
  `key`, `click`, `shot`, `pointer`, `at`, `place`, `press`, `drag`, `typed`, `waitfor`, `item_x`, `client_size`,
  `pixel`, `near`, the EXIT trap); `find_windows`/`windows`/`window`/`window_gone` take the application name from
  `$app` (default `Finder`).

- [ ] **Step 1: The fixture**

  `make-styled-fixture.py <out.dmg> [--license <resources.xml>]`: with `--license`, after `hdiutil create` it runs
  `hdiutil udifrez <out.dmg> -xml <resources.xml>` (as `tests/dmg-license.md` describes for the sla fixtures). On
  `lubos@10.10.10.152`: `make-sla-fixture.py sla-one.xml 5000 one` and
  `make-styled-fixture.py styled-sla-test.dmg --license sla-one.xml`; copy it back. Check it on macOS:
  `hdiutil imageinfo styled-sla-test.dmg | grep -i 'Software License Agreement'` → `true`; in Darling,
  `printf 'y\n' | hdiutil attach` shows "SLA TEST AGREEMENT (English)" and mounts "Styled Test". Document the new
  fixture in `tests/dmg-license.md`'s table.

- [ ] **Step 2: Move the helpers to `tests/finder-lib.sh`**

  Lines 12–246 of `finder.sh` (setup through `item_x`) and the later `client_size`/`pixel`/`near` move as they are,
  `find_windows` gaining `$app` (X11 `--class "$app"`, Wayland `app_id == app`). `finder.sh` keeps `here` and does
  `. "$here/finder-lib.sh"`.
  Run: `finder.sh` in all three modes on X11 and on Wayland (the usual private displays, `VIRTUAL_POINTER` set).
  Expected: the same PASS lines as before the move, no FAIL.

- [ ] **Step 3: Write `tests/open-disk-image.sh` and run it to see it fail**

  Host-side test, header like `finder.sh`'s (same requirements), sourcing `finder-lib.sh`. Runs `darling` from
  `PATH`, so the caller chooses the mode. The image is copied to
  `$work/Dir With Späce/Ünï & 'q' \$x.dmg`. The installed entry is read from
  `$DARLING_ROOT/share/applications/org.darlinghq.Darling.DiskImageMounter.desktop`; the case first checks that its
  `Exec` program is `$DARLING_ROOT/bin/darling`, then launches a copy in `$work` whose `Exec` program is
  `$(command -v darling)` (so a rootless or non-root run doesn't start the setuid launcher). Cases, in order:
  - **cold start**: `darling shutdown`; `setsid -w gio launch <entry copy> <image> </dev/null >/dev/null 2>&1 &`;
    a DiskImageMounter window (`app=DiskImageMounter`) titled with the image's file name appears within 180 s.
  - **agreement**: Return in it → a Finder window "Styled Test" within 60 s; `mount` in the guest lists
    `/Volumes/Styled Test`.
  - **window layout**: its client area is 640 wide and 400–440 high, and the pixel near the bottom left is within 8 of
    (47 111 159) (as `finder.sh`'s "styled window" checks).
  - **copy**: `darling open "$work/dest"` gives a window "dest"; dragging Styled App.app (160, 200 from the styled
    window's top left) into it copies the bundle; `diff -r` in the guest is clean.
  - **eject**: Command-E in "Styled Test" → the window closes within 15 s and the mount is gone.
  - **two files**: `darling open "$work/unmount-test.dmg" "$work/rel"` → windows "Unmount Test" and "rel".
  - **command line**: `cd "$work" && darling open rel2` → window "rel2" (relative path); `darling open` of a symbolic
    link to `unmount-test.dmg` while attached → the window "Unmount Test" (no second mount); `darling open
    "$work/missing.dmg"` → rc 1 and `The file … does not exist.`, no new window; `darling open` → rc 1, usage line.
  Ends by quitting Finder and detaching what it attached (`hdiutil detach -quiet`, no `rm` in the guest).
  Tasks 1–2 are installed by now, so to see it fail, run it once with `DARLING_ROOT` set to a scratch directory
  that has no `share/applications`.
  Expected: "cold start" FAILs naming the missing entry, and the run exits non-zero.

- [ ] **Step 4: Run it in every mode**

  With Tasks 1–2 installed: the three modes × X11 (`:97`) and Wayland (private sway, `VIRTUAL_POINTER` set).
  Expected: all PASS in all six runs. A Mesa llvmpipe exit crash (rc 139) is ignored and the run repeated.

- [ ] **Step 5: Document and finish the roadmap**

  `tests/open-disk-image.md`: how to run it, the six runs and their result, what each case shows. Roadmap row 8:
  `done: 2026-11-23-open-from-linux-desktop.md`, with any open items found.

- [ ] **Step 6: Commit**

  `git add -f tests/finder-lib.sh tests/finder.sh tests/open-disk-image.sh tests/open-disk-image.md
  tests/fixtures/make-styled-fixture.py tests/fixtures/styled-sla-test.dmg tests/dmg-license.md` and
  `git add docs/superpowers/plans/2026-11-16-finder-roadmap.md`; message `tests: open a disk image from the Linux
  desktop, end to end; roadmap: plan 8 done` with a Provenance note (fixture built with `hdiutil` on macOS 26.7.3;
  black-box only). Push `finder-app`.
