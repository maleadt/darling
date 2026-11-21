# Finder.app, part 2: `.DS_Store` and styled disk-image windows — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A folder or volume whose `.DS_Store` describes its window shows that way in Finder: window size, toolbar and sidebar hidden, icon and text size, label position, background colour or picture, and each item at its stored position. This is the styled window of an app's disk image: a background picture with an arrow, the app on one side and an `Applications` link on the other. Items the Finder "invisible" flag marks are hidden.

**Architecture:**
- **CoreFoundation (`NSURL.m`):** `NSURLIsHiddenKey` honours the Finder invisible flag (`com.apple.FinderInfo`) and `UF_HIDDEN`, as observed on macOS. Setting it writes both, and fails with an error when nothing could be stored. Finder already reads this key (7a), so invisible items disappear without Finder code.
- **Finder.app** gets four new pieces:
  - `FinderDSStore` reads the `.DS_Store` container (buddy allocator, record B-tree) into records. It is hostile-input safe: on any inconsistency it rejects the whole file.
  - `FinderFolderStyle` turns a folder's records into view settings. It resolves the background picture's alias or bookmark to a path inside the folder's own volume, and nowhere else.
  - `FinderFreeformLayout` is an `NSCollectionViewLayout` subclass that places items at their stored centres (spec, "How icon views are drawn").
  - The window and icon-view controllers apply a style.
- Styles apply to **any** folder with a usable `.DS_Store`, not only to disk images. Anything keyed on DMGs is an anti-goal (spec).
- `.DS_Store` files are read, never written.

**Tech Stack:** Objective-C (Finder; Foundation/CoreFoundation), Python 3 (fixture writer, run by hand), shell harness (xdotool and ImageMagick on X11; swaymsg, grim and ImageMagick on Wayland), `hdiutil` on macOS 26.7.3 for the fixture image.

**Spec:** `docs/superpowers/specs/2026-11-16-finder-app-design.md`. The relevant parts are:
- Goal 3;
- "How icon views are drawn";
- "`.DS_Store` reading (in Finder)";
- "Styled-window behaviour" (except the drop and copy bullets, which belong to 7c);
- Testing.

The roadmap is `docs/superpowers/plans/2026-11-16-finder-roadmap.md`, row 7b.

## Global Constraints

- Everything in 7a's Global Constraints still holds (`docs/superpowers/plans/2026-11-21-finder-browser.md`):
  - AGENTS.md;
  - the build and install prefix, and the setuid, setcap and rootless-copy steps after installs;
  - `build/tmp` for temp files;
  - the private displays;
  - all three modes;
  - commit trailers;
  - `git add -f` for files under `tests/`;
  - no `git add -A`;
  - push only, no pull requests.
- Branches: darling `finder-app`. A submodule this plan changes gets a branch `finder-styled`, cut from its `finder-browser` HEAD: corefoundation, and cocotron if AppKit needs a fix.
- The `.DS_Store` format is undocumented by Apple. Implement it from public third-party descriptions only, paraphrased and written independently, with no copied code (spec; AGENTS.md §3/§4):
  - Wim Lewis's DSStoreFormat notes (the `Mac::Finder::DSStore` distribution);
  - Mark Mentovai's description;
  - the documentation of the MIT-licensed Python `ds_store` and `mac_alias` packages.

  Cite them in comments and commit Provenance.
- Hostile input: a disk image is untrusted. The reader never crashes, never reads out of bounds, and never follows a background path outside the folder's volume. Malformed data means a plain window.
- No app-, DMG- or picture-specific logic.
- Non-goals here:
  - drag-and-drop installing and file operations (7c);
  - writing view settings;
  - column and gallery views (a `.DS_Store` asking for one gets the icon view);
  - volume icons (`.VolumeIcon.icns`).

## Observed on macOS (the probes behind this plan)

Recorded on 10.10.10.152 (26.7.3) and 10.10.10.148 (10.14.4) while writing the plan, using `build/tmp/p7b/hidden_probe.m`. Both Macs gave the same results. Task 1 writes them into the corefoundation test's header as tier-2 evidence.

**`NSURLIsHiddenKey`:**
- A plain file reads NO.
- A file or a folder whose `com.apple.FinderInfo` has `kIsInvisible` (byte 8 = `0x40`, Finder flags `0x4000` big-endian) reads YES.
- A `FinderInfo` with only `kExtensionIsHidden` (`0x0010`) reads NO.
- A file with `UF_HIDDEN` (`chflags`) reads YES.
- `setResourceValue:@YES forKey:NSURLIsHiddenKey` succeeds. Afterwards `st_flags` has `UF_HIDDEN`, and `FinderInfo` is 32 bytes, all zero except byte 8 = `0x40`.

**Darling today:**
- Only a leading `.` reads YES.
- `chflags` fails with ENOTSUP.
- `setResourceValue:` returns YES and stores nothing.
- `setxattr` of `com.apple.FinderInfo` works.

**Finder's own `.DS_Store` files** (read-only copies in `build/tmp/p7b/ref/`, never committed):
- 26.7.3's `/Applications/.DS_Store` is 6148 bytes, starts `00 00 00 01 'Bud1'`, and holds only `lg1S`, `ph1S`, `modD` and `moDD` records.
- The ones on 10.14.4 are empty.
- No Finder-written store with a background picture or `Iloc` positions was available. Those parts rest on the public descriptions and on synthetic fixtures.

## Rulings in this plan

1. **The invisible flag is fixed in CoreFoundation**, where `NSURLIsHiddenKey` is computed, not in Finder.
   - Getting: YES for a name starting with `.`, a `UF_HIDDEN` `st_flags`, or `kIsInvisible` in `FinderInfo`.
   - Setting YES:
     - set `kIsInvisible` in `FinderInfo`, preserving its other 31 bytes, or create a zeroed 32-byte one;
     - then try `UF_HIDDEN`;
     - succeed if either was stored, otherwise return NO with the `chflags` errno as an `NSPOSIXErrorDomain` error.
   - Setting NO clears both.
2. **The reader rejects the whole file on any structural problem:**
   - a block address outside the file;
   - a node visited twice;
   - a depth over 32;
   - a record count over 100000;
   - an unknown data type (its length can't be known);
   - a length running past its block;
   - a file over 16 MB.

   Known structure with an unexpected value (an `Iloc` that isn't 16 bytes, a `bwsp` that isn't a dictionary) only drops that setting.
3. **Records are matched to items by canonical equivalence of the names.** HFS+ stores decomposed names, and Foundation also decomposes (7a). The match is case-sensitive, because Finder writes the name it shows.
4. **What a style changes, and when:**
   - Window size (from `bwsp` `WindowBounds`; the origin is ignored) and toolbar/sidebar visibility apply only when a new window opens on that folder.
   - View style, icon size, text size, label position, background and positions apply whenever the folder is shown, also on navigating into it.
   - A folder without a usable style gets 7a's defaults back.
5. **Clamps:**
   - window content between 300×200 and the screen's visible frame;
   - icon size 16…512 points (default 64);
   - text size 10…16 points (default 12).
6. **Items without a position go below the lowest positioned item**, in a grid with the flow layout's spacing, in name order. Every item is reachable by scrolling. The content size is the union of item frames plus a 20-point margin, and at least the visible area.
7. **The background picture** is drawn at its natural size (pixels at 72 dpi as points), anchored top-left, and scrolls with the items. If it can't be found or loaded, the background colour is used if one is given, otherwise the default.
   - Resolution:
     - alias v2/v3 tag 18 (volume-relative POSIX path), else tag 2 (Carbon path without its volume name, with `:` turned into `/`);
     - bookmark: path components (`0x1004`) minus the volume path (`0x2002`) components;
     - the result is joined to the folder's volume URL (`NSURLVolumeURLKey`).
   - Rejected: an empty or `..` component, a path that is outside the volume after `realpath`, anything that isn't a regular file.
8. **The view menu gains "Show/Hide Toolbar" and "Show/Hide Sidebar"** (⌥⌘T and ⌃⌘S, as on macOS), so a styled window's chrome can be brought back.
9. **Older records:**
   - `icvo` (`icv4`: icon size, label position `botm`/`rght`) and `BKGD` (`DefB`, `ClrB` r/g/b, `PctB` with the alias in the folder's `pict` record) are read when `icvp` is absent;
   - `vstl` `Nlsv` (or `vSrn` with `vstl` absent and `lsvp` present) means the list view; anything else means icons.
10. **Test fixtures are project-made.**
    - `tests/fixtures/dsstore.py` is our own writer, from the same public descriptions.
    - Small `.DS_Store` fixtures are committed under `tests/fixtures/dsstore/`.
    - `styled-test.dmg` is built on macOS 26.7.3 by `tests/fixtures/make-styled-fixture.py` with `hdiutil`.
    - Writer and reader share one author's reading of the format. To guard against a shared misreading, two Finder-written references are checked:
      - Finder's own `/Applications/.DS_Store` from 26.7.3 (local copy, not committed);
      - a styled folder's store that Finder writes on .152 under `osascript` (Task 2, Step 6). The Macs are dedicated dev machines, and any probing is allowed (user, 2026-11-21).
    - Task 5 also opens the fixture image in macOS Finder, and reads back what Finder shows.

## Review Focus

1. **Hostile `.DS_Store`:** truncated at any byte, a B-tree that cycles, block addresses out of range, huge counts or lengths, known codes with the wrong type, a `bwsp` that isn't a property list. Expected: no crash, no out-of-bounds read, a plain window. Tests:
   - Task 2: malformed fixtures, every truncation, 2000 seeded mutations;
   - Task 3: wrong-typed records.
2. **A background reference that escapes or fails:**
   - `/../../etc/passwd`;
   - a symlink inside the volume that points outside it;
   - a path to a missing file, a folder, or a file that isn't an image.

   Expected: no picture; the colour is used if given; the rest of the style still applies. Test: Task 3.
3. **Positions that don't fit:** negative, far outside the window, overlapping, items without a position, positions for names that don't exist. Expected: every visible item is shown and reachable by scrolling; nothing is placed at a negative origin. Test: Task 4.
4. **Names whose normalization differs between record and disk** (a decomposed record against a precomposed listing, and the reverse), and names with emoji or CJK. Expected: matched. Tests: Tasks 3 and 4.
5. **Absurd window settings:** `WindowBounds` of `{{0, 0}, {0, 0}}`, `{{-500, -500}, {100000, 100000}}`, or a string that doesn't parse; icon size 0 or 4096. Expected: clamped as in Ruling 5, never an unusable window. Test: Task 4.

---

### Task 1: `NSURLIsHiddenKey` honours the invisible flag (CoreFoundation)

**Files:**
- Modify: `src/external/corefoundation/NSURL.m` (the `NSURLIsHiddenKey` getter near line 807; the setter; reuse the `FinderInfo` helpers at 413–510)
- Test: `src/external/corefoundation/tests/URLIsHidden.m` (new; follows `tests/URLLocalizedTypeDescription.m`)

**Interfaces:**
- Produces: `NSURLIsHiddenKey` behaviour per Ruling 1. Task 2's `FinderDirectory` hides those items with no change.

- [ ] **Step 1: Write the failing test** `URLIsHidden.m <new dir>`, with these checks (exact cases from the probe above):
  - plain NO;
  - `.dot` YES;
  - `FinderInfo` byte 8 `0x40` on a file YES, and on a folder YES;
  - `kExtensionIsHidden` only NO;
  - `setResourceValue:@YES` succeeds and reads back YES, with `FinderInfo` byte 8 now `0x40` and the other bytes unchanged (pre-set byte 9 to `0x10` first and check it stays);
  - `setResourceValue:@NO` reads back NO and clears byte 8's `0x40`;
  - `UF_HIDDEN` set by `chflags` reads YES where `chflags` works. Skip that check, with the reason, when `chflags` fails with ENOTSUP.
- [ ] **Step 2: Run it.** Guest: `build/tmp/helpers/src-test.sh rootless src/external/corefoundation/tests/URLIsHidden.m`. Expected: FAIL on the `FinderInfo` and set checks. Run it on both Macs (`build/tmp/mac-run.sh`). Expected: ALL PASSED.
- [ ] **Step 3: Implement Ruling 1** in `NSURL.m`. Getter: also `lstat` `st_flags & UF_HIDDEN`, and `FinderInfo` Finder flags `& 0x4000`. Setter: as ruled.
- [ ] **Step 4: Run it** in all three modes, plus `tests/URLLocalizedTypeDescription.m` and 7a's `tests/src/finder_directory.m`. Expected: PASS. Rebuild targets `CoreFoundation_x86_64 CoreFoundation_i386`.
- [ ] **Step 5: Commit** corefoundation (branch `finder-styled`): "NSURL: NSURLIsHiddenKey honours the Finder invisible flag and UF_HIDDEN". Include Provenance (Apple's `NSURLIsHiddenKey` documentation; black-box on both Macs). Push. Then commit darling: the submodule bump.

### Task 2: `FinderDSStore` reads a `.DS_Store`

**Files:**
- Create: `src/Finder/FinderDSStore.h`, `src/Finder/FinderDSStore.m`; add to `src/Finder/CMakeLists.txt`
- Create: `tests/fixtures/dsstore.py` (writer module), `tests/fixtures/make-dsstore-fixtures.py`, `tests/fixtures/dsstore/*.DS_Store` (generated, committed)
- Test: `tests/src/finder_dsstore.m`

**Interfaces:**
- Produces:

```objc
@interface FinderDSStoreRecord : NSObject
@property (readonly) NSString *name;      // as stored
@property (readonly) FourCharCode code;   // e.g. 'Iloc'
@property (readonly) FourCharCode type;   // 'long' 'shor' 'bool' 'blob' 'type' 'ustr' 'comp' 'dutc'
@property (readonly) id value;            // NSNumber (long shor bool comp dutc), NSData (blob),
                                          // NSString (ustr; 'type' as its four characters)
@end

@interface FinderDSStore : NSObject
+ (nullable instancetype)storeWithData:(NSData *)data;          // nil when malformed (Ruling 2)
+ (nullable instancetype)storeForFolder:(NSURL *)folder;         // reads <folder>/.DS_Store, at most 16 MB
@property (readonly) NSArray<FinderDSStoreRecord *> *records;    // in tree order
- (nullable FinderDSStoreRecord *)recordForName:(NSString *)name code:(FourCharCode)code; // Ruling 3
@end
```

- `dsstore.py` API (used again in Tasks 3–5):
  - `write_store(records) -> bytes`, where records are `(name, code, type, value)` tuples. The writer sorts them as Finder does: by name case-insensitively, then by code.
  - Helpers: `iloc(x, y)`, `bwsp(dict)`, `icvp(dict)`, `alias_v2(volume_name, volume_relative_path)`, `bookmark(volume_path, path)`.

- [ ] **Step 1: Write `dsstore.py` and `make-dsstore-fixtures.py`.** Generate these fixtures into `tests/fixtures/dsstore/`:
  - `all-types.DS_Store`: one record of each of the 8 types, with names `a`, `ü` (decomposed), `日本`, `😀` and `.`;
  - `multi-level.DS_Store`: 3000 `Iloc` records forcing internal nodes, with page size and node layout per the descriptions;
  - `empty.DS_Store`: no records;
  - malformed variants:
    - `cycle` (an internal node pointing at itself);
    - `block-out-of-range`;
    - `bad-type` (type `'zzzz'`);
    - `huge-count` (`0xffffffff` records);
    - `long-name` (name length past the block);
    - `bad-magic`.

  Document the layout in the script's docstring, in our own words, citing the descriptions.
- [ ] **Step 2: Write the failing test** `finder_dsstore.m <fixture dir> [<extra store>...]`:
  - `all-types`: 8 records, in writer order, with values equal to what the generator wrote (the expected values are repeated in the test as literals);
  - `recordForName:@"ü" code:'type'` and the precomposed `@"ü"` find the same record;
  - `multi-level`: 3000 records, and `recordForName:@"item 2999.txt" code:'Iloc'` gives a 16-byte blob decoding to (2999, 2999 * 2);
  - `empty`: 0 records;
  - each malformed fixture: `storeWithData:` returns nil;
  - robustness: every truncation of `all-types` and `multi-level` (all lengths), and 2000 mutations (`srand48(7)`: flip 1–8 random bytes) of each, give nil or a store, and never crash. With AddressSanitizer unavailable in the guest, the bounds checks are the code's; the test proves only "no crash";
  - extra stores (the reference copy): parse to non-nil, and the test prints each record's name and code.
- [ ] **Step 3: Run it.** Expected: build failure (no `FinderDSStore`).
- [ ] **Step 4: Implement `FinderDSStore`** from the descriptions, with Ruling 2's limits:
  - every read is checked against the block it is in;
  - visited nodes go in an `NSMutableIndexSet`;
  - names are UTF-16BE.
- [ ] **Step 5: Run it** in the guest (all three modes) and on both Macs, passing the fixtures with `MAC_EXTRA`. Expected: PASS. In the guest, also pass `build/tmp/p7b/ref/152-Applications.DS_Store` as an extra store. Expected: non-nil, with exactly the records `lg1S`, `ph1S`, `modD` and `moDD`. Ledger the printed list.
- [ ] **Step 6: A Finder-written reference.** On .152, make a styled folder in a `mktemp -d` folder with `osascript` driving Finder:
  - background picture `.background/bg.png`;
  - icon size 96;
  - two items positioned, at (100, 150) and (400, 150);
  - window bounds 500×300;
  - toolbar hidden.

  Copy back its `.DS_Store`, and remove the folder. Run `finder_dsstore.m` on it as an extra store. Ledger its records. The copy stays in `build/tmp`. Task 3's test gets it as an optional argument.
- [ ] **Step 7: Commit:** "Finder: read .DS_Store files", with Provenance naming the public descriptions and stating that no code was copied. Push.

### Task 3: `FinderFolderStyle`: view settings, positions, background

**Files:**
- Create: `src/Finder/FinderFolderStyle.h`, `src/Finder/FinderFolderStyle.m`; CMakeLists
- Modify: `tests/fixtures/make-dsstore-fixtures.py` (style fixtures)
- Test: `tests/src/finder_folder_style.m`

**Interfaces:**
- Consumes: `FinderDSStore`, `FinderViewStyle` (7a, `FinderWindowController.h`).
- Produces:

```objc
@interface FinderFolderStyle : NSObject
+ (nullable instancetype)styleForFolder:(NSURL *)folder;                      // volume from NSURLVolumeURLKey
+ (nullable instancetype)styleForFolder:(NSURL *)folder volume:(NSURL *)volume; // nil: no usable .DS_Store
@property (readonly) NSSize windowContentSize;      // NSZeroSize when bwsp gives none (Ruling 5 clamps are Task 4's)
@property (readonly) BOOL showsToolbar, showsSidebar;          // default YES
@property (readonly) FinderViewStyle viewStyle;                // Ruling 9
@property (readonly) CGFloat iconSize, textSize;              // clamped, Ruling 5
@property (readonly) BOOL labelOnBottom;                      // default YES
@property (readonly, nullable) NSColor *backgroundColor;
@property (readonly, nullable) NSURL *backgroundImageURL;     // Ruling 7; nil when rejected
- (BOOL)getCenter:(NSPoint *)center forItemNamed:(NSString *)name; // Iloc, top-left origin; Ruling 3
@end
```

- [ ] **Step 1: Add style fixtures** to `make-dsstore-fixtures.py`:
  - `styled.DS_Store`:
    - `.` `bwsp`: `WindowBounds` `{{200, 120}, {640, 400}}`, `ShowToolbar` NO, `ShowSidebar` NO;
    - `.` `icvp`: `iconSize` 128, `textSize` 13, `labelOnBottom` YES, `backgroundType` 2, `backgroundImageAlias` = `alias_v2("Styled Test", "/.background/background.png")`;
    - `.` `vSrn` 1, `.` `vstl` `icnv`;
    - `Iloc` for `Styled App.app` (160, 200), `Applications` (480, 200), and `Ünï 😀.txt` decomposed (320, 330);
  - `styled-bookmark.DS_Store`: the same, but the picture is `bookmark("/Volumes/Styled Test", "/Volumes/Styled Test/.background/background.png")`;
  - `colour.DS_Store`: `backgroundType` 1, colour (0.2, 0.4, 0.6);
  - `legacy.DS_Store`: `icvo` `icv4` size 72, label `rght`; `BKGD` `ClrB` (0x3333, 0x6666, 0x9999);
  - `list.DS_Store`: `vstl` `Nlsv`;
  - escape fixtures: the alias path `/../../etc/passwd`, and `/.background/link.png` (the test makes `link.png` a symlink to `/etc/passwd`);
  - `wrong-types.DS_Store`: `Iloc` as `long`, `bwsp` blob that is a plist array, `icvp` `iconSize` as a string.
- [ ] **Step 2: Write the failing test** `finder_folder_style.m <fixture dir> [<finder-made store>]`. Each case copies a fixture into a fresh folder under a temp "volume" directory, which holds `.background/background.png` (a 2×2 PNG the test writes):
  - `styled`: content size 640×400; no toolbar or sidebar; icons; icon 128; text 13; label at the bottom; the picture URL is `<volume>/.background/background.png`;
  - centres (160, 200) and (480, 200); `Ünï 😀.txt` found by its precomposed name;
  - no centre for `Nope.txt`;
  - `styled-bookmark`: the same picture URL;
  - `colour`: the colour within 1/255;
  - `legacy`: icon 72, label on the right, the colour (0.2, 0.4, 0.6);
  - `list`: `FinderViewStyleList`;
  - both escape fixtures, a missing picture, and a picture that is a folder: picture nil, the rest of the style intact;
  - `wrong-types`: a non-nil style with defaults (content size zero, toolbar YES, icon 64);
  - a folder without `.DS_Store`, or with `empty.DS_Store`: nil;
  - with the optional Finder-made store: icon 96, centres (100, 150) and (400, 150), content 500×300, toolbar NO.
- [ ] **Step 3: Run it.** Expected: build failure.
- [ ] **Step 4: Implement `FinderFolderStyle`** (Rulings 3, 5, 7 and 9):
  - `bwsp` and `icvp` are binary plists read with `NSPropertyListSerialization`;
  - `WindowBounds` goes through `NSSizeFromString` of its size part;
  - alias and bookmark parsing are private functions in this file, bounds-checked like the reader.
- [ ] **Step 5: Run it** in all three modes and on both Macs. Expected: PASS.
- [ ] **Step 6: Commit:** "Finder: folder view settings from .DS_Store". Push.

### Task 4: The styled window

**Files:**
- Create: `src/Finder/FinderFreeformLayout.h`, `src/Finder/FinderFreeformLayout.m`; CMakeLists
- Modify: `src/Finder/FinderIconViewController.{h,m}` (style: layout, item size, label, background), `src/Finder/FinderWindowController.m` (apply on open and on navigation; View menu toggles via `FinderAppDelegate.m`)
- Test: `tests/src/finder_styled_window.m`; `tests/src/finder_window.m` (still passes)

**Interfaces:**
- Consumes: `FinderFolderStyle`.
- Produces:

```objc
@interface FinderFreeformLayout : NSCollectionViewLayout
- (instancetype)initWithItemSize:(NSSize)itemSize;
@property (copy) NSArray<NSValue *> *centers;   // per item, index-aligned [NSValue valueWithPoint:];
                                                // NSMakePoint(NAN, NAN) means no position (Ruling 6)
@end
// FinderIconViewController
- (void)applyStyle:(nullable FinderFolderStyle *)style;  // nil: the flow layout and 7a's defaults
// FinderWindowController
- (IBAction)toggleSidebar:(id)sender;
```

- [ ] **Step 1: Write the failing test** `finder_styled_window.m <fixture dir>`. It builds a folder with `styled.DS_Store`, `Styled App.app/` (an empty folder bundle), an `Applications` symlink to `/Applications`, `Ünï 😀.txt` (precomposed on disk), `Unplaced.txt`, and `.background/background.png`. It opens a `FinderWindowController` on it. The folder's volume is the guest's `/`, so the picture doesn't resolve here: Task 3 tests resolution, and Task 5's GUI case checks the picture's pixels. Assert:
  - content size 640×400;
  - toolbar not visible;
  - sidebar hidden (its split subview collapsed or width 0);
  - item frames centred at (160, 200), (480, 200) and (320, 330), ±1 point;
  - `Unplaced.txt` at y > 330 + 64, and x ≥ 0;
  - item view icon size 128, label font 13 points;
  - `toggleSidebar:` shows the sidebar;
  - `showFolder:` of a plain subfolder gives the flow layout and icon size 64, and Back gives the freeform layout again;
  - Review Focus 3: a store with centres (-500, -500), (100000, 100), two at the same point, and one for a missing name gives every item a frame with origin ≥ 0, inside `collectionViewContentSize`;
  - Review Focus 5: `WindowBounds` `{{0, 0}, {0, 0}}` gives 300×200; `{{-500, -500}, {100000, 100000}}` gives at most the screen's visible frame; garbage gives the default 800×500; icon size 0 gives 16; 4096 gives 512.
- [ ] **Step 2: Run it.** Expected: FAIL (no style applied). Also run on both Macs (MAC_EXTRA with the Finder sources). Expected: PASS once implemented, which makes it the check that our layout subclass follows AppKit's contract.
- [ ] **Step 3: Implement:**
  - the layout, per Ruling 6, with `prepareLayout`, `collectionViewContentSize`, `layoutAttributesForElementsInRect:`, `layoutAttributesForItemAtIndexPath:`, and `shouldInvalidateLayoutForBoundsChange:` YES;
  - `applyStyle:`;
  - the window controller applies the style on open (Ruling 4) and on every display of a folder;
  - the menu items (Ruling 8).

  Any AppKit gap is fixed in cocotron (branch `finder-styled`), test first.
- [ ] **Step 4: Run** `finder_styled_window.m`, `finder_window.m`, `finder_list_view.m`, `finder_sidebar.m` and `finder_reveal.m` in all three modes, and on Wayland in-process, plus both Macs. Expected: PASS.
- [ ] **Step 5: Commit:** "Finder: styled windows (positions, icon size, background, hidden chrome)". Push.

### Task 5: The styled disk image, end to end, and the record

**Files:**
- Create: `tests/fixtures/make-styled-fixture.py`, `tests/fixtures/styled-test.dmg`
- Create: `tests/src/finder_ls.m` (`finder_ls <folder>`: prints `FinderDirectory`'s item names, one per line)
- Modify: `tests/finder.sh` (cases "styled window", "styled hidden item"), `tests/finder.md`, `docs/superpowers/plans/2026-11-16-finder-roadmap.md` (row 7b done)

- [ ] **Step 1: Write `make-styled-fixture.py`** (run on .152; usage `make-styled-fixture.py <out.dmg>`). Using `dsstore.py`, it builds the volume "Styled Test":
  - `Styled App.app` (`Contents/Info.plist` only, `CFBundlePackageType` `APPL`);
  - `Applications` → `/Applications`;
  - `.background/background.png`, a 640×400 PNG written with `zlib`/`struct`: solid `#2F6F9F` with a `#F0F0F0` arrow band from x 260 to 380 at y 190–210;
  - `Hidden Notes.txt` with `FinderInfo` `kIsInvisible` (`xattr -wx`);
  - the `.DS_Store` of `styled.DS_Store` (Task 3).

  Image: `hdiutil create -fs HFS+ -volname "Styled Test" -srcfolder <dir> -format UDZO`. If darling-dmg can't read UDZO, use the format of `unmount-test.dmg`, and record that.
- [ ] **Step 2: Check the fixture against macOS Finder** (black-box, tier 2). On .152:
  - `hdiutil attach` the image;
  - open its volume in Finder with `osascript`;
  - read back through Finder's AppleScript interface:
    - the window's `bounds`;
    - `toolbar visible`;
    - `sidebar width`;
    - the icon view options' `icon size` and `text size`;
    - each item's `position`;
    - whether `Hidden Notes.txt` is listed among the window's items;
  - close the window and detach.

  Expected: they match the fixture's values. In particular, a `position` equal to the `Iloc` value confirms that `Iloc` is the centre, in top-left coordinates. Any difference is a finding about our reading of the format: fix Tasks 2–4 test-first, and ledger it. Record the read-back in `tests/finder.md`.
- [ ] **Step 3: Write the cases:**
  - "styled window": `open styled-test.dmg`, through DiskImageMounter.
    - A Finder window "Styled Test" appears within 60 s.
    - Its client width is 640 (X11 `xdotool getwindowgeometry`; Wayland: the sway `rect`) and its height is 400…440.
    - The pixel 20 points in from the left and 20 up from the bottom of the window image (`shot`, then ImageMagick `convert <png> -format '%[pixel:p{x,y}]' info:`) is `#2F6F9F` within 8 per channel.
    - The arrow band pixel at the window's content (320, 200) is light (each channel > 200).
  - "styled hidden item": with the image attached, the harness builds `finder_ls` (`build`, with `src/Finder/FinderDirectory.m`) and runs it on `/Volumes/Styled Test`. Its output has `Styled App.app` and `Applications`, and no `Hidden Notes.txt`, `.background` or `.DS_Store`.
  - Detach at the end.
- [ ] **Step 4: Run** `gui-modes.sh tests/finder.sh` (all three modes, X11 and Wayland). Expected: no FAIL. Fix code test-first if one fails.
- [ ] **Step 5: Write the record:**
  - `tests/finder.md` gets:
    - the styled-window cases and fixtures;
    - the observations above (`NSURLIsHiddenKey`; the reference stores);
    - the public sources for the format;
    - the rulings that shape behaviour (4–9).
  - The roadmap row 7b becomes `done: 2026-11-21-finder-styled-windows.md`.
- [ ] **Step 6: Run the whole set** in all three modes, from 7a's Task 6 Step 3 plus:
  - `finder_dsstore.m`;
  - `finder_folder_style.m`;
  - `finder_styled_window.m`;
  - `URLIsHidden.m`;
  - any cocotron tests added.

  Expected: no FAIL other than the pre-existing ones recorded in 7a.
- [ ] **Step 7: Commit:** "tests: styled disk-image window; roadmap: plan 7b done". Push.
