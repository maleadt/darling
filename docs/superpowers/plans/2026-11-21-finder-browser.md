# Finder.app, part 1: browsing, opening, reveal, sidebar and eject — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Darling ships a Finder: opening a folder or a volume (with `open`, through LaunchServices, or from DiskImageMounter) shows it in a Finder window, in icon or list view, with a sidebar of places and volumes. Double-clicking opens folders in place and files with their default application. `NSWorkspace` reveal requests and `open -R` show items selected in their folder. A disk image's volume can be ejected. Names with non-ASCII characters work everywhere.

**Architecture:**
- **libsystem_kernel (xnu):** guest path lookup becomes normalization-insensitive, as APFS is. When a path component is not found, the existing case-insensitive directory scan in `vchroot_userspace.c` also matches names that are canonically equivalent, so a decomposed name (what Foundation produces, on macOS too) finds a precomposed one (what Linux names usually are). This is the plan-7 prerequisite found in plan 6.
- **Finder.app** is a new in-tree ObjC app at `src/Finder`, installed as `/System/Library/CoreServices/Finder.app`. It declares Finder's Folder and Volume document types, so plan 6's LaunchServices lookup sends it folders and volumes.
  - It gets documents through `application:openURLs:`; cocotron delivers `odoc` there since plan 6.
  - Its windows are built from AppKit:
    - `NSCollectionView` with a flow layout (plan 5) for the icon view;
    - `NSOutlineView` for the list view;
    - an `NSSplitView` with a source-list `NSOutlineView` for the sidebar;
    - `NSToolbar` for back/forward and view switching.
  - Volumes and eject go through `NSFileManager`/`NSWorkspace` (plan 4).
  - Whatever AppKit gets wrong is fixed in cocotron, test first, never worked around in Finder.
- **`open -R`** reveals through `NSWorkspace activateFileViewerSelectingURLs:`, as `open(1)` documents.
- Roadmap row 7 is split. This plan is **7a**; **7b** is `.DS_Store` and styled windows; **7c** is file operations (copy/move by drag and drop and paste, new folder, rename, Trash, Get Info, progress and conflicts).

**Tech Stack:** C (libsystem_kernel emulation, freestanding), Python 3 (table generator, run by hand), Objective-C (Finder, AppKit through cocotron), shell test harnesses (xdotool on X11, wtype and swaymsg on Wayland).

**Spec:** `docs/superpowers/specs/2026-11-16-finder-app-design.md` (Goals 5–7, "NSWorkspace / Foundation", "NSCollectionView and drag & drop", "Reveal requests", "Apps: structure"); roadmap `docs/superpowers/plans/2026-11-16-finder-roadmap.md`, row 7.

## Global Constraints

- AGENTS.md governs everything:
  - clean room: never disassemble Apple binaries; the Macs are for black-box observation only;
  - no app-specific logic;
  - `QUIRK:` comments carry evidence and a test;
  - no debug leftovers;
  - every commit has a Provenance note;
  - fix at the right layer.
- Language: Finder is Objective-C only. Framework and system work is in the language of the component it lands in (libsystem_kernel emulation: freestanding C, no libc calls beyond what `vchroot_userspace.c` already declares).
- Bundle identity: `com.apple.finder`, `/System/Library/CoreServices/Finder.app`. Not `LSUIElement`. Document types as macOS 26.7.3 declares them: "Folder" (`CFBundleTypeOSTypes` `fold`) and "Volume" (`disk`), both `CFBundleTypeRole` Editor.
- Finder is not a permanently running process. It has no desktop, and it quits when its last window closes (spec, Non-goals).
- Non-goals here: no column/gallery view, Quick Look, tags, search, `.DS_Store` (7b), file operations (7c), and no writing of view settings.
- All artwork is our own. Icons follow AppKit's precedent: rendered from the Adwaita icon theme with `rsvg-convert`, and listed in a `COPYING.Adwaita` next to them.
- Build tree: `~/Projects/darling/build` (`-DCMAKE_INSTALL_PREFIX=$HOME/darling-finder`). Install only into `~/darling-finder`.
- After any install:
  - `sudo -n chown root:root ~/darling-finder/bin/darling && sudo -n chmod 4755 ~/darling-finder/bin/darling`;
  - `sudo -n /usr/bin/setcap cap_sys_admin+ep ~/darling-finder/libexec/darling/usr/sbin/fseventsd` (a CoreServices install drops it);
  - refresh the plain copy `~/darling-finder/rootless-bin/darling` (mode 755).
- Temp files go in `~/Projects/darling/build/tmp`. Run every test with `TMPDIR` there (`build/tmp/helpers/mode.sh` sets it). Never use `/tmp`.
- GUI tests:
  - X11: a private Xvfb `:97` with xfwm4, started by you; `unset WAYLAND_DISPLAY SWAYSOCK`. Never `:99`, `:96`, `:98` or the user's desktop.
  - Wayland: the private headless sway (`XDG_RUNTIME_DIR=$HOME/Projects/darling/build/tmp/wl`, `SWAYSOCK=…/wl/sway-ipc.sock`, `WAYLAND_DISPLAY=…/wl/wayland-1`, config `…/wl/config`).
  - Export the display before the container starts (`darling shutdown` first); `build/tmp/t4/run.sh <mode> <x11|wayland> <script>` does both.
- Modes: every runtime test passes in rootless, non-root and root mode (`build/tmp/helpers/all-modes.sh`, which exits non-zero on a failure).
- Bundles registered by hand in a prefix stay registered while their directory exists. Remove scratch bundles you registered, or they can win LaunchServices lookups (plan 6 lost a run to this).
- Branches:
  - xnu: `finder-browser`, from `volume-attributes` HEAD;
  - cocotron: `finder-browser`, from `disk-image-mounter` HEAD;
  - darling: `finder-app`. It records the submodule bumps; never `git add -A` or `commit -a` (unrelated swift-cmark and zsh modifications are present). Files under `tests/` need `git add -f`.
- Push branches, open no pull requests.
- Commits end with:
  - `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
  - `Claude-Session: https://claude.ai/code/session_01CRFMixwsFg4yEYzqovZF1D`
- Do not touch the i386 `NSStringDrawingContext` regression; another agent owns it.

## Observed on macOS (the probes behind this plan)

Recorded on 10.10.10.152 (26.7.3, APFS) and 10.10.10.148 (10.14.4, APFS) while writing the plan. Task 1 writes the first part into `tests/path-normalization.md`, and Task 6 writes the second into `tests/finder.md`, as tier-2 evidence.

**Unicode normalization (`build/tmp/p7/nfd_probe.m`; the same on both):**
- `-[NSString fileSystemRepresentation]` of `ü.txt` (precomposed, U+00FC) is `75 cc 88 2e 74 78 74`: decomposed. Of the decomposed form it is the same.
- `+[NSURL fileURLWithPath:]` of the precomposed path gives `u%CC%88.txt` in `absoluteString`, and its `fileSystemRepresentation` is decomposed too.
- A file created with the precomposed bytes (`c3 bc`) is found by `access()` with the decomposed bytes, and by `fileExistsAtPath:` with either form.
- `contentsOfDirectoryAtPath:` returns the name as stored: precomposed, length 5.
- So Foundation decomposes on macOS as it does in Darling. The file system makes it work, because APFS is normalization-insensitive (Apple's APFS documentation says so; "Apple File System Guide", FAQ).

**Finder's `Info.plist` (macOS 26.7.3):**
- `CFBundleIdentifier` `com.apple.finder`, `CFBundlePackageType` `FNDR`, `NSPrincipalClass` `TApplication`, no `LSUIElement`.
- Document types include "Folder" (`fold`) and "Volume" (`disk`), both role Editor, with no `LSItemContentTypes`.

**Darling today:** `open -R` prints "This operation is not implemented yet" and fails.

## Rulings in this plan

1. **Row 7 is split into 7a, 7b and 7c** (see Architecture).
   - Why: each part is as large as plan 6, and they share little.
   - Cost if wrong: none; the roadmap row records the split.
2. **Non-ASCII names are fixed in libsystem_kernel's path lookup, not in Foundation.**
   - Foundation's decomposition matches macOS (observed), so changing it would break `fileSystemRepresentation` compatibility.
   - The difference is the file system: APFS is normalization-insensitive. Darling's lookup already emulates APFS's case-insensitivity on a miss, and normalization joins it there.
   - Rules:
     - The exact name is still tried first, so two names that differ only in normalization stay distinct when both exist.
     - The scan runs only when a component is not found, and compares canonical decompositions (NFD) of valid UTF-8.
     - It keeps today's ASCII-only case folding.
     - Invalid UTF-8 compares as today (ASCII case-insensitive bytes).
     - dyld keeps exact lookup, as it does for case.
   - The decomposition table is generated from the Unicode Character Database by a checked-in script, and the generated header records its Unicode version and licence (Unicode License v3, permissive).
   - Cost if wrong: a miss on a large directory scans it, which today's case-insensitive scan already does.
3. **An `odoc` of a folder or volume opens that folder's window; an `odoc` of anything else reveals it** (its folder's window, with the item selected).
   - The spec says every `odoc` is a reveal, but Goal 1 needs a volume opened by DiskImageMounter to show the volume's own window.
   - On macOS a reveal and an open carry the same event (`tests/volumes.md`), so the item's kind must decide.
   - Cost if wrong: `selectFile:` of a folder opens it instead of selecting it in its parent.
4. **`CFBundlePackageType` is `APPL`, not macOS's `FNDR`.** LaunchServices' handler queries count only `APPL` bundles as applications.
   - Cost if wrong: an app reading Finder's package type sees `APPL`. Supporting `FNDR` is a LaunchServices change for later.
5. **`NSPrincipalClass` is `NSApplication`** (`TApplication` is Finder's private class).
6. **Launching and quitting:**
   - Launched without documents (`oapp`), or reopened (`rapp`) with no window, Finder opens a window on the home folder.
   - It quits when its last window closes (`applicationShouldTerminateAfterLastWindowClosed:` YES).
7. **One window per folder.** Opening or revealing into a folder that already has a window brings that window to the front, and selects the revealed items in it.
8. **Window content (our design; nothing could be observed over ssh):**
   - Hidden items are not shown: names starting with `.`, and items whose `NSURLIsHiddenKey` is YES.
   - Items are sorted by `NSURLLocalizedNameKey` with `localizedStandardCompare:`.
   - The window's title is `-[NSFileManager displayNameAtPath:]` of the folder. A new window is 800×500 points, cascaded.
   - Icon view: 64-point icons from `-[NSWorkspace iconForFile:]`, the name below in up to two lines.
   - List view columns: Name (icon and name), Date Modified, Size (`NSByteCountFormatter`, file style; `--` for folders), Kind (`NSURLLocalizedTypeDescriptionKey`). Folders expand in place. Sorted by Name ascending; clicking a column header sorts by it.
   - New windows use icon view. View › as Icons is ⌘1, as List is ⌘2, also from a segmented control in the toolbar. A window keeps its view while it navigates.
   - Cost if wrong: cosmetic.
9. **Sidebar (our design):**
   - "Favorites": Applications (`/Applications`), the home folder (labelled with `NSUserName()`), Desktop, Documents and Downloads. Only folders that exist are listed.
   - "Locations": `-[NSFileManager mountedVolumeURLsIncludingResourceValuesForKeys:options:]` with `NSVolumeEnumerationSkipHiddenVolumes`, labelled with `NSURLVolumeLocalizedNameKey`. Ejectable volumes (`NSURLVolumeIsEjectableKey`) get an eject button.
   - The list follows `NSWorkspaceDidMountNotification`/`DidUnmountNotification`.
   - Clicking an item shows it in the window.
   - Cost if wrong: cosmetic.
10. **Navigation:**
    - Double-click, or ⌘↓, on a folder or volume shows it in the same window; on anything else it opens with `-[NSWorkspace openURL:]`, i.e. the LaunchServices default application.
    - Back ⌘[, Forward ⌘], Enclosing Folder ⌘↑.
11. **Eject:**
    - File › Eject (⌘E) and the sidebar button eject with `-[NSWorkspace unmountAndEjectDeviceAtURL:error:]`.
    - The menu item ejects the volume selected in the sidebar, or the one the window is showing, and is disabled for non-ejectable volumes.
    - A failure shows an alert with the error.
    - When a volume unmounts, however that happens, every window showing a folder on it closes.
12. **`open -R <file>…`** calls `-[NSWorkspace activateFileViewerSelectingURLs:]` with all the files, as `open(1)` describes ("Reveals the file(s) in the Finder instead of opening them"). Its exit status is 1 if a file does not exist.

## Review Focus

1. **A folder that can't be read (no permission) or that disappears while its window shows.** Expected: an alert "The folder “<name>” can’t be opened." with the error's description, and no crash. A window whose folder disappears closes. Tests: Task 2 (unreadable folder), Task 6 (folder removed while shown).
2. **Names that are not plain ASCII:**
   - precomposed and decomposed accents, CJK, emoji;
   - a name with a newline;
   - a 255-byte name;
   - a name starting with `.` (hidden).

   Expected: shown as stored, opened and revealed correctly; hidden names are not shown. Tests: Task 1 (lookup), Task 2 (model), Task 6 (harness).
3. **A volume that unmounts from outside Finder (`hdiutil detach`) while its window and sidebar entry show.** Expected: its windows close and its sidebar entry goes, within 10 s. Test: Task 4.
4. **One reveal event with items in several folders, one of them missing.** Expected: one window per folder, the items of each selected together; the missing item is skipped; nothing crashes. Test: Task 5.
5. **A large folder (2000 files).** Expected: its window shows within 5 s and scrolls to the last item. Test: Task 6.

---

### Task 1: Path lookup is normalization-insensitive (libsystem_kernel)

**Files:**
- Create: `src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/name_equivalence.c`, `name_equivalence.h`
- Create: `src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/unicode_decomposition.h` (generated) and `src/external/xnu/darling/tools/gen-unicode-decomposition.py`
- Modify: `src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/vchroot_userspace.c` (the `strcasecmp` in the ENOENT scan), `src/external/xnu/darling/src/libsystem_kernel/emulation/CMakeLists.txt` (the new source)
- Test: `tests/src/path_normalization.c` (guest), `tests/path-normalization.md`
- Modify: `tests/disk-image-mounter.sh` (put the non-ASCII image name back into "odd path"), `tests/disk-image-mounter.md`, roadmap (the "Known gap" note becomes "fixed in plan 7a")

**Interfaces:**
- Produces: `bool darling_names_equivalent(const char* a, const char* b);`. It is true when the names are equal ignoring ASCII case, or, when both are valid UTF-8, when their canonical decompositions are equal ignoring ASCII case. The decomposition is full canonical decomposition, Hangul included, followed by canonical ordering of combining marks.
- The generator writes, for every code point with a canonical decomposition, its full decomposition, and the canonical combining class of every non-zero-class code point. Python's `unicodedata` provides both. Arrays are sorted for binary search. The header's comment names the Unicode version (`unicodedata.unidata_version`) and the licence.

- [ ] **Step 1: Write the failing test.** `tests/src/path_normalization.c <dir>` is plain C, so it builds for the guest and on macOS. It prints PASS/FAIL lines:
  - It creates `<dir>/ü.txt` with the precomposed bytes `c3 bc`, and `<dir>/Dïr/` (precomposed) with `é.txt` inside.
  - `access`, `stat` and `open` with the decomposed bytes (`u cc 88`) succeed, and `stat` gives the same `st_ino` as the precomposed path.
  - The decomposed path to the nested file (`Di cc 88 r/e cc 81.txt`) opens.
  - `open(decomposed, O_CREAT|O_EXCL)` fails with `EEXIST`.
  - `ü.txt` precomposed and `u ̈.txt` decomposed both created in a second directory (in that order, exact bytes) stay two files: `readdir` lists two entries, and each path opens its own file (write different contents, read them back).
  - `<dir>/v.txt` does not exist, and opening the decomposed `ü` under that name fails with `ENOENT`.
  - A name with invalid UTF-8 (`ff.txt`) is found by its exact bytes.
  - Run it on both Macs first: `build/tmp/mac-run.sh tests/src/path_normalization.c` (append a probe for the "two files" case; record what APFS does). Where APFS differs from the expectations above, the test follows APFS and the difference is ruled in the ledger.
- [ ] **Step 2: Run it in the guest to see it fail.**
  - Run: `build/tmp/helpers/src-test.sh rootless tests/src/path_normalization.c`
  - Expected: FAIL on the decomposed lookups (`ENOENT`).
- [ ] **Step 3: Implement.**
  - Write the generator and the generated header.
  - Write `darling_names_equivalent`: decode UTF-8, decompose, order the marks, then compare. Work on the stack (at most 4×255 code points per name), with no allocation; past that, fall back to byte comparison.
  - Replace the `strcasecmp` in the ENOENT scan with it.
  - The `TEST` build of `vchroot_userspace.c` keeps working.
- [ ] **Step 4: Run it to see it pass.**
  - Run `path_normalization` in all three modes, once with `<dir>` in the prefix (`$HOME/...` in the guest) and once on the host (`/Volumes/SystemRoot/...`): the lookup is the same code, but the two are different file systems.
  - Run `tests/disk-image-mounter.sh` with "odd path" back on `Dir With Späce/ünï & co.dmg`, X11 and Wayland, all three modes.
  - Expected: all PASS.
- [ ] **Step 5: Commit.**
  - xnu (`finder-browser`): "vchroot: path lookup is normalization-insensitive". Provenance: Apple's APFS documentation (normalization-insensitive names), black-box observation on macOS 10.14.4 and 26.7.3, Unicode Character Database via Python's `unicodedata` (Unicode License v3). Push.
  - darling: the bump, the tests, `tests/path-normalization.md` (the observations above, and how to run the test), the roadmap note.

### Task 2: Finder.app shows a folder in a window

**Files:**
- Create: `src/Finder/`:
  - `CMakeLists.txt` (as `src/DiskImageMounter/CMakeLists.txt`, COMPONENT gui);
  - `main.m`, `Info.plist` (Global Constraints, Rulings 4–5), `Finder.iconset/` (Adwaita `system-file-manager`), `COPYING.Adwaita`;
  - `FinderAppDelegate.h/.m`;
  - `FinderDirectory.h/.m` (the folder model);
  - `FinderWindowController.h/.m`;
  - `FinderIconViewController.h/.m`.
- Modify: `src/CMakeLists.txt` (`add_subdirectory(Finder)` next to `DiskImageMounter`)
- Modify: the tests that register a stand-in `com.apple.finder`. With the real Finder installed, the system's registration comes first and wins LaunchServices ties (plan 6). They are:
  - `tests/volumes.sh` "reveal" and "LSOpenApplication": the stand-in gets its own bundle ID, `org.darlinghq.test.fileviewer`, and the test sets the `NSFileViewer` default to it (`defaults write -g NSFileViewer …`, removed at the end). The spec names `NSFileViewer` as the documented way to replace the file viewer.
  - `tests/volumes.sh` "folder handler" and `tests/src/launchservices_folder_handler.m`: they expect `/System/Library/CoreServices/Finder.app` when it is installed, and the stand-in otherwise.
  - `tests/disk-image-mounter.sh`: the cases that waited for the stand-in's `odoc` wait for a Finder window titled `Unmount Test` instead, using `finder.sh`'s window helpers.
- Test: `tests/src/finder_directory.m`, `tests/src/finder_window.m` (in process), `tests/finder.sh` (X11 and Wayland harness, built like `tests/disk-image-mounter.sh`)

**Interfaces:**
- Produces:

```objc
// FinderDirectory.h: the visible contents of a folder (Ruling 8: no hidden items, sorted by localized name).
@interface FinderDirectory : NSObject
- (instancetype)initWithURL:(NSURL *)url;
@property (readonly) NSURL *URL;
@property (readonly, copy) NSArray<NSURL *> *items;   // after -reload:
- (BOOL)reload:(NSError **)error;                     // NO, and the error, if the folder can't be read
+ (NSString *)displayNameForURL:(NSURL *)url;         // -[NSFileManager displayNameAtPath:]
@end

// FinderWindowController.h
@interface FinderWindowController : NSWindowController
- (instancetype)initWithFolderURL:(NSURL *)folder;
@property (readonly) NSURL *folderURL;
- (void)showFolder:(NSURL *)folder;                        // navigates (history: Task 5)
- (void)selectItems:(NSArray<NSURL *> *)items;            // items not in the folder are ignored
@property (readonly) NSArray<NSURL *> *selectedItems;
@end

// FinderAppDelegate.h
@interface FinderAppDelegate : NSObject <NSApplicationDelegate>
- (FinderWindowController *)windowForFolder:(NSURL *)folder create:(BOOL)create; // Ruling 7
- (void)openURLs:(NSArray<NSURL *> *)urls;                  // Ruling 3; application:openURLs: calls it
@end
```

- `FinderIconViewController` owns the window's `NSCollectionView` (flow layout, item size 100×100, 64-point icon, name below). It reports the selection to the window controller.
- A folder that can't be read: `openURLs:` shows an alert, "The folder “<name>” can’t be opened.", with the error's `localizedDescription` as informative text, and opens no window.

- [ ] **Step 1: Write the failing tests.**
  - `finder_directory.m <dir>` (no display needed). It creates `b.txt`, `A.txt`, `a10.txt`, `a9.txt`, `.hidden`, `Ünï`, `日本.txt` and `x\ny` (newline), plus a file with `NSURLIsHiddenKey` set if the file system supports it (otherwise it prints SKIP). Checks:
    - `items` names are, in order, `A.txt a9.txt a10.txt b.txt x\ny Ünï 日本.txt` (localizedStandardCompare order; check the exact order on macOS first with the same program, and use what it gives);
    - no `.hidden`;
    - `reload:` of a missing folder returns NO with `NSFileReadNoSuchFileError` or a POSIX `ENOENT` underlying error;
    - of a mode-000 folder (skipped as root), NO.
  - `finder_window.m <dir>` (needs a display):
    - the window's title is the folder's display name;
    - the collection view has `items.count` items, whose labels are in order;
    - `selectItems:` of two items gives `selectedItems` equal to them, and the collection view's `selectionIndexPaths` match;
    - `selectItems:` of an item from another folder is ignored.
  - `tests/finder.sh`:
    - "open folder": `open <work>/F` gives a window titled `F` within 30 s;
    - "open twice": `open <work>/F` again leaves one window titled `F`;
    - "launch": `open -a Finder` gives a window titled with the home folder's display name;
    - "volume": `open unmount-test.dmg` (DiskImageMounter, plan 6) gives a window titled `Unmount Test`;
    - "unreadable": `open` of a mode-000 folder gives an alert window and no folder window (skipped in root mode);
    - "quit": closing every window with ⌘W ends the Finder process within 10 s.
- [ ] **Step 2: Run them to see them fail.** Expected: the in-process tests do not build; the harness finds no window.
- [ ] **Step 3: Implement.** Fix AppKit defects found on the way in cocotron (`finder-browser`), each with its own test in `src/external/cocotron/tests/`.
- [ ] **Step 4: Run them to see them pass,** in all three modes, X11 and Wayland. Then run the modified `tests/volumes.sh`, `tests/disk-image-mounter.sh` and `launchservices_folder_handler` with the real Finder installed, in all three modes; they pass.
  - Before the change they fail: the stand-in loses to the real Finder. Run them once, after installing Finder and before changing them, to see it.
- [ ] **Step 5: Commit** (darling `finder-app`): "Finder: show folders in windows". Provenance: the spec, Finder's `Info.plist` keys as observed on macOS 26.7.3 (the Observed section), Apple's NSApplicationDelegate and NSFileManager documentation. Push, with the cocotron bump if any.

### Task 3: List view, and switching views

**Files:**
- Create: `src/Finder/FinderListViewController.h/.m`
- Modify: `src/Finder/FinderWindowController.m` (view switching, toolbar segmented control, View menu), `src/Finder/main.m` or wherever the menu is built
- Test: `tests/src/finder_list_view.m` (in process), `tests/finder.sh` (case "views")

**Interfaces:**
- Produces: `@property (nonatomic) FinderViewStyle viewStyle;` on `FinderWindowController`, with `typedef NS_ENUM(NSInteger, FinderViewStyle) { FinderViewStyleIcons, FinderViewStyleList };`. Switching keeps the selection.
- `FinderListViewController` owns an `NSOutlineView` whose column identifiers are `name`, `modified`, `size` and `kind` (Ruling 8). Its children come from `FinderDirectory`, loaded when a row expands.

- [ ] **Step 1: Write the failing tests.**
  - `finder_list_view.m <dir>`. It creates `small` (10 bytes), `big` (2 MB, sparse), and folder `sub` with `inner.txt`, and sets the modification times with `utimes` to 2026-01-02 03:04 and 2026-02-03 04:05. Checks:
    - the column titles are "Name", "Date Modified", "Size", "Kind";
    - the rows are `big small sub`;
    - the size cells are "2 MB", "10 bytes" and "--" (whatever `NSByteCountFormatter` file style gives for 2097152 and 10 bytes on this system, computed by the test);
    - the date cells are those dates formatted with medium date and short time style;
    - the kind of `sub` is its `NSURLLocalizedTypeDescriptionKey`;
    - sorting by the size column descending gives `big small sub`, and ascending `sub small big` (folders' size is the smallest);
    - expanding `sub` shows `inner.txt` at level 1.
  - `finder_window.m`: `viewStyle = FinderViewStyleList` with two items selected keeps them selected; back to icons, still selected.
  - `finder.sh` "views": in a window of `F`, ⌘2 then ⌘1 leaves the window open and titled `F` (the in-process tests check the content).
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.** AppKit fixes as in Task 2.
- [ ] **Step 4: Run them to see them pass,** in all three modes, X11 and Wayland.
- [ ] **Step 5: Commit**: "Finder: list view". Push.

### Task 4: Sidebar, volumes and eject

**Files:**
- Create: `src/Finder/FinderSidebarController.h/.m`
- Modify: `src/Finder/FinderWindowController.m` (split view, sidebar), `src/Finder/FinderAppDelegate.m` (Eject menu item; closing a volume's windows on unmount)
- Test: `tests/src/finder_sidebar.m` (in process), `tests/finder.sh` (cases "eject", "detached outside")

**Interfaces:**
- Produces:

```objc
@interface FinderSidebarController : NSViewController
@property (readonly) NSArray<NSString *> *sectionTitles;          // @[@"Favorites", @"Locations"]
- (NSArray<NSURL *> *)itemsInSection:(NSInteger)section;
- (NSString *)titleForItem:(NSURL *)item;
- (BOOL)showsEjectButtonForItem:(NSURL *)item;
@property (copy) void (^showFolder)(NSURL *folder);                // set by the window controller
@end
```

- `FinderAppDelegate` observes `NSWorkspaceDidUnmountNotification` (`NSWorkspaceVolumeURLKey`) and closes every window whose folder is the volume or inside it.

- [ ] **Step 1: Write the failing tests.**
  - `finder_sidebar.m <image>`:
    - Favorites lists `/Applications` and the home folder, and Desktop, Documents and Downloads exactly when they exist (create `~/Downloads` for the test if it doesn't exist, and remove it after);
    - the home item's title is `NSUserName()`;
    - Locations lists the root volume;
    - after `hdiutil attach <image>` (spawned), "Unmount Test" appears in Locations within 10 s, with an eject button;
    - after `hdiutil detach`, it goes within 10 s.
  - `finder.sh`:
    - "eject": `open unmount-test.dmg`, the window `Unmount Test` appears, ⌘E closes it, and the volume is no longer mounted;
    - "detached outside" (Review Focus 3): `open unmount-test.dmg`, then `hdiutil detach` in the guest, and the window `Unmount Test` closes within 10 s.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them to see them pass,** in all three modes, X11 and Wayland.
- [ ] **Step 5: Commit**: "Finder: sidebar and eject". Push.

### Task 5: Navigation, opening files, reveal and `open -R`

**Files:**
- Modify: `src/Finder/FinderWindowController.m` (history, ⌘[ ⌘] ⌘↑ ⌘↓, double-click, toolbar back/forward), `src/Finder/FinderAppDelegate.m` (reveal grouping)
- Modify: `src/tools/open.m` (`-R`, Ruling 12)
- Test: `tests/src/finder_window.m` (navigation), `tests/src/finder_reveal.m` (in process), `tests/finder.sh` (cases "reveal", "reveal several", "open file", "open -R missing")

**Interfaces:**
- Produces: on `FinderWindowController`, `- (IBAction)goBack:(id)sender; - (IBAction)goForward:(id)sender; - (IBAction)goToEnclosingFolder:(id)sender; - (IBAction)openSelection:(id)sender;` and `@property (readonly) BOOL canGoBack, canGoForward;`.
- `FinderAppDelegate -openURLs:` groups the non-folder items by `URLByDeletingLastPathComponent` (standardized), in order of first appearance. For each group it calls `windowForFolder:create:YES`, then `selectItems:`. Items that don't exist are skipped.

- [ ] **Step 1: Write the failing tests.**
  - `finder_window.m`:
    - `showFolder:` of `sub`, then `goBack:` returns to the folder, and `goForward:` to `sub`;
    - `goToEnclosingFolder:` from `sub` shows the folder, with `sub` selected;
    - `canGoBack` is NO in a new window.
  - `finder_reveal.m <dir>` (in process, with a `FinderAppDelegate`). `openURLs:` of `@[a/x.txt, b/y.txt, a/z.txt, a/missing.txt, c]`, where `c` is a folder, gives:
    - windows for `a`, `b` and `c`;
    - `a`'s selection is `x.txt z.txt`;
    - `b`'s is `y.txt`;
    - `c`'s window shows `c` itself, with nothing selected.
  - `finder.sh`:
    - "reveal": `open -R <work>/F/one.txt` gives a window `F` (the in-process test checks the selection);
    - "reveal several": `open -R` of files in `F` and `G` gives windows `F` and `G`;
    - "open -R missing": `open -R <work>/nope` exits 1 and no window appears;
    - "open file": a stand-in editor (ae-receiver registered for the extension `fndtest`) is set up; `open -R F/doc.fndtest`, then ⌘↓ in window `F`, and the stand-in gets `odoc` for `doc.fndtest`.
- [ ] **Step 2: Run them to see them fail.** `open -R` fails with "not implemented".
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them to see them pass,** in all three modes, X11 and Wayland.
- [ ] **Step 5: Commit**: "Finder: navigation, opening and reveal; open -R". Provenance adds `open(1)`. Push.

### Task 6: The flows the tests above leave out, and the record

**Files:**
- Modify: `tests/finder.sh` (cases "odd names", "large folder", "folder removed")
- Create: `tests/finder.md`: how to run it, the cases, and the observations above (the Finder `Info.plist` part).
- Modify: `docs/superpowers/plans/2026-11-16-finder-roadmap.md`: row 7 becomes three rows:
  - 7a: "Finder.app: browsing, icon/list views, sidebar, open, reveal, eject; normalization-insensitive lookup", repos xnu, cocotron (if changed), darling (Finder, tools); done: `2026-11-21-finder-browser.md`;
  - 7b: ".DS_Store reader, styled disk-image windows";
  - 7c: "file operations: drag & drop and paste copy/move, new folder, rename, Trash, Get Info, progress and conflicts".
  Plan 8 depends on 6 and 7a–7c.

- [ ] **Step 1: Write the cases.**
  - "odd names" (Review Focus 2): a folder `Ünï 日本 😀` containing a file whose name has a newline, and one with a 255-byte name. `open` of the folder gives its window; `open -R` of each file gives that window.
  - "large folder" (Review Focus 5): `open` of a folder of 2000 empty files shows its window within 5 s of the event arriving (measure from `open`'s return), and End/⌘↓ on the last item does not crash. The in-process `finder_window.m` adds the same folder: 2000 items, and `scrollToItemsAtIndexPaths:` the last one makes it visible.
  - "folder removed" (Review Focus 1): `open F2`, then `rm -r F2` in the guest. The window closes within 10 s (Finder watches its folders with FSEvents, or when a reload fails; implement in `FinderWindowController` if the test fails).
- [ ] **Step 2: Run them.** All three modes, X11 and Wayland. If one fails, fix the code of Tasks 2–5 test-first; the failing case is the test.
- [ ] **Step 3: Run the whole set**, in all three modes:
  - `TESTS="tests/finder.sh tests/disk-image-mounter.sh tests/volumes.sh tests/dmg-attach.sh tests/dmg-license.sh tests/diskarbitration.sh" build/tmp/helpers/all-modes.sh`;
  - `python3 tests/launchservices-folder-handler.py` and `python3 tests/launchservices-extension-query.py`;
  - the cocotron tests touched by AppKit fixes.

  Expected: no FAIL other than ones recorded as pre-existing (`uti_conformance`'s `txt`/`public.image` check; the Wayland "titlebar" drag check; the i386 `NSStringDrawingContext` size check).
- [ ] **Step 4: Commit** (darling `finder-app`): "tests: Finder's remaining flows; roadmap: plan 7a done". Push.
