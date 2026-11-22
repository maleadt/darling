# Finder.app, part 3: file operations — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finder can manage files. You can:
- copy and move items by drag and drop (icon view, list view, sidebar) and by Copy and Paste;
- create a folder;
- rename an item;
- move items to the Trash;
- see an item's Get Info panel.

A long copy shows progress and can be stopped. A name conflict asks Replace / Keep Both / Stop. The headline case works: dragging the app from a styled disk-image window onto its `Applications` link copies it into `/Applications`.

**Architecture:**
- **Finder, model (no UI):**
  - `FinderFileOperation` copies or moves items into a folder on a background queue.
    - Copies use `copyfile(3)`: a public libSystem API with byte progress and cancellation.
    - Moves within a volume use `-[NSFileManager moveItemAtURL:toURL:error:]`.
    - A move across volumes is a copy, then removing the source.
    - Every copy goes to a hidden temporary name in the destination and is renamed into place only when complete. So Stop, a failure or Replace never leaves half an item, or loses the item being replaced.
  - `FinderItemActions` does one-shot actions: new folder, rename with Finder's naming rules, and Trash through `-[NSWorkspace recycleURLs:completionHandler:]`.
- **Finder, UI:**
  - `FinderOperationQueue` runs operations, shows the progress panel after 1 s, and asks about conflicts.
  - `FinderInfoPanelController` is Get Info.
  - The window and view controllers gain:
    - the Edit menu, plus File > New Folder, Rename, Move to Trash and Get Info;
    - inline renaming;
    - drag source and drop target.
- **AppKit (cocotron):** view-based `NSTableView` and `NSOutlineView` have no modern drag-and-drop API today. Only the old `writeRowsWithIndexes:` path and a drop-above-row path exist, and `setDropItem:dropChildIndex:` is unimplemented. They gain the documented API, used by the list view and the sidebar.

**Tech Stack:** Objective-C (Finder, AppKit), `copyfile(3)`, the shell harness of `tests/finder.sh` with its existing X11 and Wayland input and screenshot helpers.

**Spec:** `docs/superpowers/specs/2026-11-16-finder-app-design.md`. The relevant parts are:
- Goals 4 and 6;
- Architecture ("file ops via NSFileManager / NSWorkspace");
- "NSCollectionView and drag & drop";
- the drop and copy bullets of "Styled-window behaviour";
- Testing.

The roadmap is `docs/superpowers/plans/2026-11-16-finder-roadmap.md`, row 7c.

## Global Constraints

- Everything in 7a's and 7b's Global Constraints still holds (`docs/superpowers/plans/2026-11-21-finder-browser.md`, `…-finder-styled-windows.md`):
  - AGENTS.md;
  - the build and install prefix, and the setuid, setcap and rootless-copy steps after installs;
  - `build/tmp` for temp files, never `/tmp`;
  - the private displays (`:97`, own sway);
  - all three modes;
  - commit trailers and a Provenance note;
  - `git add -f` under `tests/`, no `git add -A`;
  - push only, no pull requests.
- Branches: darling `finder-app`. cocotron gets `finder-fileops`, cut from its `finder-styled` HEAD (`8faf3bcb`). Any other submodule that needs a fix gets a `finder-fileops` branch cut from its `finder-styled` HEAD.
- From the spec:
  - Drop on a folder or a folder symlink (e.g. `Applications` → `/Applications`) copies into its target.
  - Across volumes the default is copy; within a volume it is move; ⌥ forces copy.
  - Conflicts ask Replace / Keep Both / Stop.
  - Copies longer than ~1 s show a progress window.
  - Get Info shows name, kind, size, dates and path.
- No app-, DMG- or name-specific logic. `Applications` is just a symlink to a folder.
- No `.DS_Store` writing: dropped items get no stored position, and there is no Put Back.
- Non-goals here (each a ruling below): undo, Duplicate (⌘D), aliases (⌘⌥-drag), Merge for folders, multi-item Rename, Cut, Get Info editing.

## Evidence for Finder behaviours

These are Finder UI conventions, our own app's design, not API contracts. They follow Apple's public Mac User Guide (paraphrased):
- "Rename files, folders, and disks on Mac" (`https://support.apple.com/guide/mac-help/mchlp1144/mac`):
  - select the item and press Return, type, then press Return;
  - a name can't contain a colon and can't start with a period;
  - changing an extension may stop the file opening in its app.
- "Organize files in folders on Mac" (`https://support.apple.com/guide/mac-help/mh26885/mac`):
  - ⌥-drag copies;
  - a conflict on folders with the same name offers Stop or Replace (and Merge, not done here).

The AppKit behaviours (the table and outline view drag-and-drop API, and `NSDraggingInfo` operation masks) come from Apple's API reference. They are pinned by cocotron tests that pass unchanged on 10.14.4 and 26.7.3. Finder's Automation is still refused over ssh on both Macs (26.7.3 times out; 10.14.4 now gives -610), so Finder itself can't be read back.

## Rulings in this plan

1. **Copies go through `copyfile(3)`** (`COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_NOFOLLOW_SRC`, with a status callback) rather than `-[NSFileManager copyItemAtURL:toURL:error:]`.
   - Why: NSFileManager has no progress or cancellation, which the spec's progress window needs. `copyfile(3)` is the public libSystem API that Foundation's own copy calls in Darling.
   - Moves, folder creation and removal go through NSFileManager, and Trash through NSWorkspace, as the spec's architecture says.
2. **Staged copies:**
   - Each item is copied to `.<name>.finder-<random>` in the destination folder, then renamed (`rename(2)` via `moveItemAtURL`) to its final name.
   - Replace removes the existing item only after the staged copy completed, then renames.
   - Stop, cancel or a failure removes the staged item. Items already finished stay.
3. **Names:**
   - Keep Both appends ` 2`, ` 3`, … before the last extension (`a.txt` → `a 2.txt`; `a 2.txt` → `a 3.txt`; `x.tar.gz` → `x.tar 2.gz`; folder `f` → `f 2`).
   - Copying an item into its own folder names it `<name> copy`, then `<name> copy 2`, …, with no conflict question.
   - A new folder is `untitled folder`, then `untitled folder 2`, ….
   - A conflict is decided by asking the file system whether the name exists (`lstat`), so a precomposed and a decomposed name conflict, as on macOS (7a).
4. **Moves:**
   - Moving an item into the folder it is already in does nothing and succeeds.
   - Moving or copying a folder into itself or its own subfolder fails with an error before anything is written.
   - So does a Replace whose existing item is the source or contains it.
   - A move from a read-only volume becomes a copy, and a drop on a read-only volume is refused.
5. **Drop operation:**
   - Look at `-[NSDraggingInfo draggingSourceOperationMask]`. If it is exactly `NSDragOperationCopy` (⌥), copy; if exactly `NSDragOperationGeneric` (⌘), move.
   - Otherwise, move when every item is on the destination's volume (`NSURLVolumeIdentifierKey`), and copy if not.
   - Return `NSDragOperationNone` for:
     - a read-only destination;
     - a destination that is one of the dragged items or inside one;
     - a move onto the items' own folder;
     - anything that isn't file URLs.
   - Drop targets are a folder item, a symlink item whose target is a folder (resolved), the view's background (the window's folder), and a sidebar item.
   - Drops from other apps are accepted the same way.
6. **Conflict questions:**
   - Asked on the main thread while the worker waits.
   - The alert says: "An item named “<name>” already exists in this location. Do you want to replace it with the one you're <copying|moving>?"
   - Buttons: Keep Both, Stop, Replace (Replace is the default).
   - With more items to go, an "Apply to All" checkbox (the alert's suppression button) is shown.
   - Stop ends the operation; items done so far stay.
7. **Errors end the operation:**
   - The error alert names the item and shows the `NSError`'s description, with one OK button.
   - There is no Skip; the cost is that one unreadable file stops a large copy.
8. **Progress window:**
   - An `NSPanel` per operation, shown only if the operation is still running 1.0 s after it started.
   - It shows "Copying “<name>” to “<folder>”", or "Copying <n> items to “<folder>”" (Moving likewise).
   - Below that, a determinate `NSProgressIndicator` and "<done> of <total>" from `NSByteCountFormatter`, plus a Stop button that cancels.
   - The total is measured before copying starts, with `lstat` sizes of every file.
9. **Rename:**
   - Return on a single selected item, or File > Rename, starts inline editing of its label: the icon-view item's text field, or the list view's name cell. The name is selected without its extension.
   - Return or clicking elsewhere commits; Escape cancels.
   - Refused, with an alert and the old name kept:
     - an empty or whitespace-only name;
     - one containing `:` or `/`;
     - one starting with `.`;
     - one that another item in the folder has.

     The `/` case differs from macOS, which stores it as `:`. The cost: a name with a slash is refused.
   - Changing the extension asks "Are you sure you want to change the extension from “.<old>” to “.<new>”?" with "Keep .<old>" and "Use .<new>".
   - Renaming a volume is not offered.
10. **Commands and keys:**
    - Edit: Copy ⌘C (file URLs of the selection onto the general pasteboard with `writeObjects:`), Paste ⌘V (copies the pasteboard's file URLs into the window's folder), "Move Item Here" ⌥⌘V (the alternate of Paste: moves), and Select All ⌘A.
    - File: New Folder ⇧⌘N (created, selected, then renaming starts), Get Info ⌘I, Rename, and Move to Trash ⌘⌫.
    - Items are validated: nothing selected, or a read-only folder, disables what can't apply.
    - Trash uses `recycleURLs:completionHandler:`. A failure (a volume without a Trash, a read-only image) shows its error.
11. **Get Info:**
    - A panel per item, titled "<name> Info", repeated ⌘I brings an open one forward. Its rows:
      - Name;
      - Kind (`NSURLLocalizedTypeDescriptionKey`);
      - Size: a file shows "<formatted> (<n> bytes)"; a folder shows "<formatted> for <n> items" once it is counted, recursively off the main thread, and "Calculating…" before;
      - Where (the parent's path);
      - Created and Modified (medium date, short time).
    - Read-only. A selection of several items opens a panel each.
12. **Out of scope** (none is in the spec):
    - Undo, Duplicate, aliases, Merge, Cut, Put Back and multi-item Rename.
    - Dragging to the Linux desktop. The existing `DarlingFileURIList` bridge is used only if it already works, and is not tested here.

## Review Focus

1. **A copy that fails or is stopped halfway:** a folder holding an unreadable file, a cancel during a large file, a full or read-only destination. Expected:
   - no item at the final name;
   - no staged `.…finder-…` item left behind;
   - items finished earlier remain;
   - an error or nothing (Stop) is reported.

   Test: Task 1.
2. **Self-referential transfers:** a folder dropped into itself or its subfolder; Replace where the existing item is the source or its ancestor; moving an item onto its own folder. Expected: refused up front (or a no-op for the last), nothing removed. Tests: Tasks 1 and 6.
3. **Symlinks:**
   - a symlink is copied as a link;
   - a dangling symlink copies;
   - a drop on a symlink to a folder goes into its target;
   - a drop on a symlink to a file, or on a dangling one, is refused.

   Tests: Tasks 1 and 6.
4. **Names:** conflicts between precomposed and decomposed names; Keep Both on `a 2.txt`, `x.tar.gz` and extensionless folders; emoji and CJK names; renames to `.x`, `a:b`, `""` or an existing name. Expected: as in Rulings 3 and 9. Tests: Tasks 1 and 3.
5. **Read-only and foreign places:** a drop on a disk-image volume, Trash on a read-only image, moving out of a read-only image, pasting URLs that aren't files or no longer exist. Expected:
   - refused, or an alert;
   - a move out of an image becomes a copy;
   - missing items are skipped (nothing to do means no operation);
   - no crash.

   Tests: Tasks 1, 4 and 6.

---

### Task 1: The file-operation engine

**Files:**
- Create: `src/Finder/FinderFileOperation.h`, `src/Finder/FinderFileOperation.m`
- Modify: `src/Finder/CMakeLists.txt` (source list)
- Test: `tests/src/finder_file_operation.m` (no display)

**Interfaces:**
- Produces:
```objc
typedef NS_ENUM(NSInteger, FinderTransferKind) { FinderTransferCopy, FinderTransferMove };
typedef NS_ENUM(NSInteger, FinderConflictChoice) { FinderConflictReplace, FinderConflictKeepBoth, FinderConflictStop };

@interface FinderFileOperation : NSObject
- (instancetype)initWithKind:(FinderTransferKind)kind items:(NSArray<NSURL *> *)items destination:(NSURL *)folder;
@property (readonly) FinderTransferKind kind;      // after -start, Move from a read-only volume reads Copy
@property (readonly) NSArray<NSURL *> *items;
@property (readonly) NSURL *destination;
// All three are called on the main queue. resolveConflict may set *applyToAll; it is not called again then.
@property (copy) FinderConflictChoice (^resolveConflict)(NSURL *source, NSURL *existing, BOOL *applyToAll);
@property (copy) void (^progress)(int64_t doneBytes, int64_t totalBytes, NSURL *current);
// results: the items at their final URLs, in item order. error: nil, or NSCocoaErrorDomain NSUserCancelledError
// after Stop or -cancel, or the failure (NSFilePathErrorKey set to the item).
@property (copy) void (^completion)(NSArray<NSURL *> *results, NSError *error);
- (void)start;    // once; the work runs on a private serial queue
- (void)cancel;   // any thread
@property (readonly, getter=isFinished) BOOL finished;
// Rulings 3 and 5
+ (NSURL *)keepBothURLForItem:(NSURL *)item inFolder:(NSURL *)folder;
+ (NSURL *)copyURLForItem:(NSURL *)item inFolder:(NSURL *)folder;   // "<name> copy", "<name> copy 2", …
+ (FinderTransferKind)defaultKindForItems:(NSArray<NSURL *> *)items destination:(NSURL *)folder;
// nil if the transfer may go ahead; otherwise why not (Ruling 4)
+ (NSError *)refusalForItems:(NSArray<NSURL *> *)items destination:(NSURL *)folder kind:(FinderTransferKind)kind;
@end
```

- [ ] **Step 1: Write `tests/src/finder_file_operation.m`.** It uses the `expect()` helper as the other Finder tests do, takes a new directory as `ARGS`, and runs a main run loop until each operation's completion. Its cases:
  - `copy one file`: `a.txt` (5 bytes) copied into `dst/`. `dst/a.txt` has the same bytes, the source remains, results = `[dst/a.txt]`, the error is nil, and the last progress call has done == total == 5.
  - `copy a tree`: a folder with a subfolder, a symlink to `../x` and a dangling symlink. The copy has the same tree; the links are links with the same targets (`destinationOfSymbolicLinkAtPath:`).
  - `move within a volume`: same inode afterwards (`NSURLFileResourceIdentifierKey` equal), the source is gone.
  - `move into its own folder is a no-op`: success, results = `[the item]`.
  - `conflict replace`: `dst/a.txt` exists with other contents. `resolveConflict` returns Replace; `dst/a.txt` has the new bytes.
  - `conflict keep both`: the result is `dst/a 2.txt`, and both exist.
  - `conflict stop`: the error is `NSUserCancelledError`; `dst/a.txt` is unchanged; no `dst/.a.txt.finder-*`.
  - `apply to all`: three conflicting items, `*applyToAll = YES` on the first call. `resolveConflict` is called once and all three are kept both.
  - `normalization conflict`: `dst/ü.txt` precomposed and a decomposed source `ü.txt`. `resolveConflict` is called.
  - `copy into own folder`: `a.txt` copied into its folder gives `a copy.txt`, then `a copy 2.txt`, with no conflict call.
  - `keep both names`: `keepBothURLForItem:` gives `a 2.txt` for `a.txt`, `a 3.txt` when `a 2.txt` exists, `x.tar 2.gz`, `f 2` (folder), and `😀 2.txt`. `copyURLForItem:` gives `a copy.txt`.
  - `folder into itself`: `refusalForItems:@[f] destination:f/sub kind:Copy` is non-nil. Running the operation reports that error, and nothing is created in `f/sub`.
  - `replace own ancestor`: copying `p/q/x` into `p` where `p/q` is the existing item for a source named `q` (an item `q` dragged from inside the existing `q`). Replace gives an error, and `p/q` still exists.
  - `unreadable file in a folder`: folder `t` with `t/ok` and `t/no` (mode 000; skipped with a SKIP line when running as root, since root can read it). The error has `NSFilePathErrorKey`; `dst/t` doesn't exist; no staged item.
  - `cancel mid-file`: a 64 MB file, `-cancel` from the first progress call. Result `NSUserCancelledError`; no `dst/big`, no staged item.
  - `default kind`: same volume → Move. For other volumes the test attaches a disk image given as its second argument (`tests/fixtures/unmount-test.dmg`, guest path; `ARGS2` below means this image in every task) and SKIPs without one. Across volumes → Copy. A move out of the read-only image reads `kind == Copy` after `-start`, and the source remains.
- [ ] **Step 2: Run it.** Run: `build/tmp/helpers/src-test.sh rootless tests/src/finder_file_operation.m`. Expected: compile failure (no `FinderFileOperation.h`).
- [ ] **Step 3: Implement `FinderFileOperation`** per Rulings 1–4. Notes:
  - Measure the total with `lstat` over an `NSDirectoryEnumerator` that doesn't follow links.
  - The `copyfile` callback adds bytes on `COPYFILE_PROGRESS` and returns `COPYFILE_QUIT` once cancelled.
  - The staged name uses `arc4random_uniform`.
  - Conflicts: `dispatch_sync` to the main queue for `resolveConflict`, or use a semaphore with `dispatch_async`. A main-thread caller must never deadlock: `-start` always works off the main thread.
  - Throttle progress calls to at most 20 a second, plus a final one.
- [ ] **Step 4: Run it on all three modes, and on both Macs** (`build/tmp/mac-run.sh tests/src/finder_file_operation.m`; the test needs only Foundation and the Finder file). Expected: every PASS, the root mode's unreadable case SKIPped. A Mac failure is a fact about the test's expectation or our engine, not about macOS. Fix the cause, and ledger a ruling if the expectation changes.
- [ ] **Step 5: Commit** "Finder: copy and move engine (staged copies, conflicts, progress)", with a Provenance note citing `copyfile(3)` and the NSFileManager reference.

### Task 2: Progress and conflict UI

**Files:**
- Create: `src/Finder/FinderOperationQueue.h`, `src/Finder/FinderOperationQueue.m`
- Test: `tests/src/finder_operation_ui.m` (needs a display)

**Interfaces:**
- Consumes: `FinderFileOperation` (Task 1).
- Produces:
```objc
@interface FinderOperationQueue : NSObject
+ (instancetype)sharedQueue;
// Refused transfers (refusalForItems:…) show an alert attached to window (or app-modal when nil) and return nil.
- (FinderFileOperation *)transferItems:(NSArray<NSURL *> *)items to:(NSURL *)folder kind:(FinderTransferKind)kind
                                window:(NSWindow *)window;
@property (readonly) NSArray<FinderFileOperation *> *runningOperations;
@property NSTimeInterval progressDelay;            // 1.0; tests lower it
- (NSPanel *)progressPanelForOperation:(FinderFileOperation *)operation;   // nil until shown
// Tests answer alerts through this instead of a click: given the alert, returns the button index (0-based) to press
// and whether to tick the suppression button. nil (the default) runs the alert for the user.
@property (copy) NSInteger (^alertResponder)(NSAlert *alert, BOOL *suppress);
@end
```

- [ ] **Step 1: Write `tests/src/finder_operation_ui.m`.** Cases:
  - `a quick copy shows no panel`: a 1-byte copy with `progressDelay` 1.0 has completed and never had a panel.
  - `a long copy shows a panel`: a 256 MB file with `progressDelay` 0.05. While running, the panel exists, is visible, titles "Copying “big” to “dst”", has a determinate indicator whose `doubleValue` rises, and a Stop button. Pressing Stop (`performClick:`) ends with `dst/big` absent, and the panel closes.
  - `conflict alert`: the `alertResponder` sees the message text from Ruling 6, buttons "Replace", "Keep Both", "Stop" (in the order `NSAlert` shows them, Replace first as the default), and no suppression button for one item. Returning Keep Both gives `a 2.txt`.
  - `apply to all shown for several`: three conflicting items; the suppression button is shown and titled "Apply to All". Ticking it answers the rest.
  - `refused transfer`: a folder into itself gives an alert, a nil operation, and nothing written.
  - `error alert`: the unreadable-file copy (non-root) shows one alert whose informative text contains the item's name.
- [ ] **Step 2: Run it.** Run: `DISPLAY=:97 build/tmp/helpers/src-test.sh rootless tests/src/finder_operation_ui.m`. Expected: compile failure.
- [ ] **Step 3: Implement `FinderOperationQueue`** per Rulings 6–8. The panel is built in code. Alerts attach as sheets to the operation's window when it is still open, otherwise they are app-modal. While a sheet is up, the worker waits. The queue keeps operations until completion and then drops them.
- [ ] **Step 4: Run it** in all three modes, on X11 `:97` and on the own sway. Run it on both Macs too: the test is Finder code over AppKit, so Mac runs check that the AppKit calls behave as the test expects. Expected: all PASS.
- [ ] **Step 5: Commit** "Finder: progress panel and conflict questions".

### Task 3: New folder, rename rules, Trash

**Files:**
- Create: `src/Finder/FinderItemActions.h`, `src/Finder/FinderItemActions.m`
- Test: `tests/src/finder_item_actions.m` (no display, but links AppKit for NSWorkspace)

**Interfaces:**
- Produces:
```objc
@interface FinderItemActions : NSObject
+ (NSURL *)createFolderIn:(NSURL *)folder error:(NSError **)error;   // "untitled folder", "untitled folder 2", …
// nil if name may be used for item; otherwise an NSError whose localizedDescription is the alert text (Ruling 9)
+ (NSError *)problemWithName:(NSString *)name forItem:(NSURL *)item;
// YES if the extension changes, with the old and new extensions (no dot)
+ (BOOL)name:(NSString *)name changesExtensionOf:(NSURL *)item from:(NSString **)old to:(NSString **)new;
+ (NSURL *)renameItem:(NSURL *)item to:(NSString *)name error:(NSError **)error;
// completion on the main queue with NSWorkspace's results
+ (void)trashItems:(NSArray<NSURL *> *)items completion:(void (^)(NSDictionary<NSURL *, NSURL *> *trashed, NSError *error))completion;
@end
```

- [ ] **Step 1: Write `tests/src/finder_item_actions.m`.** Cases:
  - `untitled folder` then `untitled folder 2`, both directories.
  - `problemWithName:`:
    - nil for `b.txt`;
    - non-nil for `""`, `"  "`, `"a:b"`, `"a/b"`, `".x"`, and for `"c.txt"` when `c.txt` exists;
    - nil for the item's own name;
    - nil for a case-only change (`A.txt` for `a.txt`; a case-only rename must work on a case-insensitive FS);
    - non-nil for a name that differs only in normalization from another existing item.
  - `changesExtension`: `a.txt` → `a.md` gives YES (`txt`, `md`); `a.txt` → `b.txt` gives NO; a folder never asks.
  - `rename`: the result is the new URL and the old one is gone; a case-only rename leaves the new case on disk (`contentsOfDirectoryAtPath:`).
  - `trash`: an item in the test folder is trashed. `trashed[item]` is in a `.Trash` and the item is gone. For an item on the read-only image (`ARGS2`, else SKIP), the error is non-nil and the item remains.
- [ ] **Step 2: Run it.** Expected: compile failure.
- [ ] **Step 3: Implement `FinderItemActions`.** The texts:
  - a taken name: "The name “<name>” is already taken. Please choose a different name.";
  - a dot: "Names that begin with a dot “.” are reserved for the system.";
  - `:` or `/`: "The name “<name>” can't be used." with "Try using a name with fewer characters, or with no punctuation marks.";
  - an empty name: "Please enter a name.".

  Renames use `moveItemAtURL:toURL:error:`. A case-only rename goes through a staged name, since `rename(2)` to a case variant is a no-op on case-insensitive volumes.
- [ ] **Step 4: Run** in all three modes and on both Macs. Expected: all PASS.
- [ ] **Step 5: Commit** "Finder: new folder, rename rules and Trash actions".

### Task 4: Commands: Edit menu, New Folder, Rename, Move to Trash

**Files:**
- Modify: `src/Finder/FinderAppDelegate.m` (menus), `src/Finder/FinderWindowController.{h,m}`, `src/Finder/FinderIconViewController.{h,m}`, `src/Finder/FinderListViewController.{h,m}`
- Test: `tests/src/finder_commands.m` (needs a display)

**Interfaces:**
- Consumes: Tasks 2 and 3.
- Produces (window controller):
```objc
- (IBAction)copy:(id)sender;               // Edit > Copy
- (IBAction)paste:(id)sender;              // Edit > Paste: copy into the window's folder
- (IBAction)moveItemHere:(id)sender;       // ⌥⌘V
- (IBAction)selectAll:(id)sender;
- (IBAction)newFolder:(id)sender;          // ⇧⌘N
- (IBAction)rename:(id)sender;             // and Return with one item selected
- (IBAction)moveToTrash:(id)sender;        // ⌘⌫
- (IBAction)getInfo:(id)sender;            // ⌘I (Task 5 fills it)
- (void)beginRenamingItem:(NSURL *)item;   // inline editing in the current view
@property (readonly) NSURL *renamingItem;  // nil when not editing
```
- View controllers each get `- (void)beginEditingItem:(NSURL *)item` and a `void (^renameItem)(NSURL *item, NSString *name)` callback, called on commit. They don't validate; the window controller does, through `FinderItemActions`, and restores the label on refusal.

- [ ] **Step 1: Write `tests/src/finder_commands.m`** (window on a new folder; `alertResponder` answers alerts). Cases:
  - `copy writes file URLs`: select `a`, `b`, then `copy:`. `[[NSPasteboard generalPasteboard] readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}]` gives both.
  - `paste copies`: into a second window's folder. Both appear there after the operation completes and the FSEvents reload; the sources remain.
  - `move item here moves`: the sources go.
  - `paste nothing usable`: a string on the pasteboard, or URLs of deleted files. `validateMenuItem:` disables Paste for the string; the deleted files start no operation.
  - `new folder`: `untitled folder` appears, is the selection, and `renamingItem` is it.
  - `rename commits`: `beginRenamingItem:` on `a.txt`. The field editor's text set to `c.txt`, then `insertNewline:`, gives `c.txt` on disk, selected.
  - `rename refused`: `.x` gives an alert with the dot text, and the label still reads `a.txt`.
  - `rename extension`: `a.txt` → `a.md` asks with "Keep .txt"/"Use .md"; Keep leaves `a.txt`.
  - `rename cancel`: `cancelOperation:` leaves the name.
  - `rename in list view`: the same commit in the list view.
  - `trash`: `moveToTrash:` on `b`; `b` leaves the window.
  - `menu validation`: with nothing selected, Copy, Rename, Move to Trash and Get Info are disabled. In a window on the read-only image (`ARGS2`, else SKIP), Paste, New Folder, Rename and Move to Trash are disabled.
- [ ] **Step 2: Run it.** Expected: FAIL / compile failure on the new selectors.
- [ ] **Step 3: Implement.**
  - Menus per Ruling 10. "Move Item Here" is Paste's alternate (`alternate = YES`, ⌥⌘V).
  - Return goes through `keyDown:` in the views, mapped to `rename:` when one item is selected. Open stays ⌘O/⌘↓ and double-click; if 7a's views bind Return to anything, that binding moves to Rename.
  - Inline editing: make the item's `NSTextField` editable and first responder. Select the name without its extension via the field editor's `setSelectedRange:`. End editing through the control's delegate (`controlTextDidEndEditing:`; the `NSTextMovement` key tells Return from other ends). Escape is `cancelOperation:` → restore.
  - Any AppKit defect this meets (the field editor in a collection view item or a view-based outline cell) is fixed in cocotron, with a cocotron test that passes on both Macs, per AGENTS.md §2. Ledger it.
- [ ] **Step 4: Run** in all three modes, on X11 and Wayland. Run on both Macs as well (the window classes build there, as `finder_window.m` does). Expected: all PASS.
- [ ] **Step 5: Commit** "Finder: Edit menu, New Folder, Rename and Move to Trash".

### Task 5: Get Info

**Files:**
- Create: `src/Finder/FinderInfoPanelController.h`, `src/Finder/FinderInfoPanelController.m`
- Modify: `FinderWindowController.m` (`getInfo:`)
- Test: `tests/src/finder_get_info.m` (needs a display)

**Interfaces:**
```objc
@interface FinderInfoPanelController : NSWindowController
+ (FinderInfoPanelController *)showInfoForItem:(NSURL *)item;   // one per item; brings an existing one forward
@property (readonly) NSURL *item;
// The row's value as shown: name, kind, size, where, created, modified
- (NSString *)valueForRow:(NSString *)row;
@property (readonly, getter=isCounting) BOOL counting;          // a folder's size is still being counted
@end
```

- [ ] **Step 1: Write the test.**
  - A 1234-byte file:
    - title "f.txt Info";
    - `name` "f.txt";
    - `kind` equals the file's `NSURLLocalizedTypeDescriptionKey`;
    - `size` equals `[NSByteCountFormatter stringFromByteCount:1234 countStyle:NSByteCountFormatterCountStyleFile]` + " (1,234 bytes)" (grouping from `NSNumberFormatter` decimal style);
    - `where` is the parent's path;
    - `modified` equals the medium/short `NSDateFormatter` string of the modification date.
  - A folder of 3 files totalling 3000 bytes in a subfolder: "Calculating…" first, then (after `counting` goes NO) the formatted 3000 bytes + " for 4 items" (the subfolder counts as an item).
  - Calling twice gives the same controller.
  - Closing the panel releases it (a weak reference goes nil after the run loop turns).
- [ ] **Step 2: Run it.** Expected: compile failure.
- [ ] **Step 3: Implement** per Ruling 11: a small panel of label pairs (`NSGridView` if cocotron has it, else stacked text fields; the builder's call). Count with an enumerator on a global queue, cancellable when the panel closes.
- [ ] **Step 4: Run** in all three modes, X11 and Wayland, and on both Macs. Expected: all PASS.
- [ ] **Step 5: Commit** "Finder: Get Info panel".

### Task 6: Drag and drop in the icon view

**Files:**
- Modify: `src/Finder/FinderIconViewController.{h,m}`, `src/Finder/FinderWindowController.m`
- Create: `src/Finder/FinderDrop.h`, `src/Finder/FinderDrop.m`: the drop decision shared with Task 7.
- Test: `tests/src/finder_drop.m` (needs a display)

**Interfaces:**
```objc
// Ruling 5. The folder a drop on item lands in (the item if a folder, a symlink's folder target), or nil if the
// item can't take drops.
NSURL *FinderDropFolderForItem(NSURL *item);
// The file URLs on the pasteboard, or nil when there are none.
NSArray<NSURL *> *FinderDraggedFileURLs(NSPasteboard *pasteboard);
NSDragOperation FinderDropOperation(NSArray<NSURL *> *items, NSURL *folder, NSDragOperation sourceMask);
```
- `FinderIconViewController` gets `@property (copy) NSURL *folderURL` (the background's target) and a `void (^dropItems)(NSArray<NSURL *> *items, NSURL *folder, FinderTransferKind kind)` callback. The window controller hands it to `FinderOperationQueue`.

- [ ] **Step 1: Write `tests/src/finder_drop.m`.** The drop cases drive the collection view delegate methods (`collectionView:validateDrop:proposedIndexPath:dropOperation:` and `…acceptDrop:…`) with a test `NSDraggingInfo` object. It holds a pasteboard and a source mask, and stands in for the dragging system, which can't be driven in process. The real drag is covered by Task 8. Cases:
  - `FinderDropFolderForItem`:
    - a folder → itself;
    - a symlink to a folder → the resolved target;
    - a file → nil;
    - a symlink to a file → nil;
    - a dangling symlink → nil.
  - `FinderDropOperation`:
    - same volume with `NSDragOperationEvery` → Move;
    - mask Copy → Copy;
    - mask Generic → Move;
    - the image volume to the test folder (`ARGS2`) → Copy;
    - onto the image → None;
    - a folder onto itself or its subfolder → None;
    - a move onto the items' own folder → None;
    - a copy onto it → Copy (which gives "copy" names).
  - `FinderDraggedFileURLs`: nil for a string-only pasteboard.
  - `validate`:
    - a proposed drop on a folder item stays On;
    - a drop on a file item is retargeted to the background (`*proposedDropOperation` set to On with a nil index path, as the collection view drop-on-background contract says);
    - the result matches `FinderDropOperation`.
  - `accept`: dropping `a` (Copy mask) on folder item `F` gives `F/a`, and on the background of another window's folder gives a copy there.
  - `drag source`: `collectionView:pasteboardWriterForItemAtIndexPath:` returns the item's file URL. `collectionView:canDragItemsAtIndexPaths:withEvent:` is YES.
  - `drop on a symlink item`: a `Link` → `../T` item. Accepting gives `T/a`.
- [ ] **Step 2: Run it.** Expected: compile failure.
- [ ] **Step 3: Implement.**
  - The view registers for `NSPasteboardTypeFileURL`.
  - The source operation mask is Copy | Move | Generic for both local and external drags.
  - After a move whose source window isn't the destination, both windows reload through FSEvents, as today. The styled (freeform) layout's drop target is the item under the point or the background, as in the flow layout.
- [ ] **Step 4: Run** in all three modes on X11 and Wayland, and on both Macs. Expected: all PASS.
- [ ] **Step 5: Commit** "Finder: drag and drop in the icon view".

### Task 7: AppKit table/outline drag and drop; list view and sidebar drops

**Files:**
- cocotron (`finder-fileops`): `AppKit/NSTableView.m`, `AppKit/NSOutlineView.m`, plus their headers if the declarations are missing; tests `tests/appkit-tableview-drag.m` and `tests/appkit-outlineview-drag.m`.
- darling: `src/Finder/FinderListViewController.m`, `src/Finder/FinderSidebarController.m`; test `tests/src/finder_list_drop.m`; the cocotron bump.

**Interfaces (Apple's documented API, implemented in AppKit):**
- `NSTableViewDataSource`:
  - `tableView:pasteboardWriterForRow:`, which is preferred over `tableView:writeRowsWithIndexes:toPasteboard:` when both exist;
  - `tableView:validateDrop:proposedRow:proposedDropOperation:` and `tableView:acceptDrop:row:dropOperation:`, with On and Above both proposed by position (On over the middle of a row, Above near its edges);
  - `-setDropRow:dropOperation:` retargets.
- `NSOutlineViewDataSource`:
  - `outlineView:pasteboardWriterForItem:`;
  - `outlineView:validateDrop:proposedItem:proposedChildIndex:`;
  - `outlineView:acceptDrop:item:childIndex:`.

  `-setDropItem:dropChildIndex:` retargets; a drop on an item is index `NSOutlineViewDropOnItemIndex` (-1). The table methods are not sent to an outline view's data source.
- The dragging-session callbacks `draggingSession:endedAtPoint:operation:` are forwarded to both data sources (`tableView:draggingSession:endedAtPoint:operation:` and the outline equivalent).
- `-[NSTableView setDraggingSourceOperationMask:forLocal:]`.

- [ ] **Step 1: Write the cocotron tests** (they run on both Macs with `build/tmp/mac-run.sh` and in the guest with `src-test.sh`). Destination side: build a view-based table and outline in a window and send `draggingEntered:`/`draggingUpdated:`/`performDragOperation:` with a test `NSDraggingInfo` at chosen points. Assert:
  - the proposed row or item and the drop operation for a point over a row's middle (On) and near its edge (Above);
  - for a point below the last row: row = count, Above (outline: the root item, index = top-level count);
  - a retarget via `setDropRow:`/`setDropItem:` changes what `acceptDrop` receives;
  - `validateDrop` returning None means `performDragOperation:` doesn't call `acceptDrop`.

  Source side: with only `pasteboardWriterForRow:` implemented, a synthesized mouse-down plus drag (as cocotron's collection-view drag test does) begins a session whose items carry the writers' URLs. Record the expected values from the Mac runs first, then on Darling.
- [ ] **Step 2: Run them in the guest.** Expected: FAIL (methods not called).
- [ ] **Step 3: Implement in cocotron.** Keep the existing old-API path for data sources that only have it. Commit on `finder-fileops` with Provenance (API reference, black-box on both Macs) and push.
- [ ] **Step 4: Write `tests/src/finder_list_drop.m`.**
  - List view: drag source URLs; a drop on a folder row → into it; a drop on a file row → retargeted to the folder (`setDropItem:nil dropChildIndex:NSOutlineViewDropOnItemIndex`); a drop in an expanded folder's children → into that folder; operations via `FinderDropOperation`.
  - Sidebar: a drop on Applications → into `/Applications`. The test uses a sidebar whose favourites are given by the test, if the sidebar has no such hook; add `- (instancetype)initWithFavorites:(NSArray<NSURL *> *)favorites` only if needed, and ledger it. A drop on a section title → None; on a read-only volume → None.
- [ ] **Step 5: Implement** the list-view and sidebar data-source methods over `FinderDrop`. Bump cocotron in darling.
- [ ] **Step 6: Run** the cocotron tests and `finder_list_drop.m` in all three modes, X11 and Wayland; the cocotron tests and `finder_list_drop.m` on both Macs. Also run cocotron's existing table, outline and collection-view tests in the guest to check nothing regressed. Expected: all PASS.
- [ ] **Step 7: Commit** "Finder: drag and drop in the list view and sidebar; bump cocotron".

### Task 8: End to end, records

**Files:**
- Modify: `tests/finder.sh` (new cases), `tests/finder.md` (record), `docs/superpowers/plans/2026-11-16-finder-roadmap.md` (row 7c)
- Fixture: a test-made folder, no new committed image unless needed.

- [ ] **Step 1: Add `finder.sh` cases** (X11 and Wayland, all three modes; real pointer drags with the harness's existing input tools):
  - `install from a disk image`: attach `styled-test.dmg`, open a Finder window on `<work>/dest`, and drag the image's app item from its styled window into it. It is copied (another, read-only volume), `diff -r` against the image's copy matches, and the image is then ejected. The real `/Applications` is never written.
  - `drop on a folder link`: the test makes a folder `kit` with a small bundle (a few files and a symlink inside) and a symlink `Install` → `<work>/dest2`, opens `kit`, and ⌥-drags the bundle onto `Install`. `<work>/dest2/<bundle>` matches and the source remains.
  - `option-drag copies within a volume`: ⌥-drag `a` onto folder `F` in the same window; `a` remains, and `F/a` exists. A plain drag moves.
  - `paste`: ⌘C in one window, ⌘V in another.
  - `new folder and rename`: ⇧⌘N, type `Neu`, Return → `Neu` exists.
  - `trash`: ⌘⌫ → the item is in `~/.Trash`.
  - `progress`: a 512 MB file copied by drag shows a window titled "Copy"-like (by its text, found with the window search the harness uses) within 3 s, and pressing its Stop leaves no file.

  No `rm` in guest scripts: everything lives in `$work` or the test's own temporary folders, which the harness already leaves.
- [ ] **Step 2: Run** `build/tmp/helpers/gui-modes.sh tests/finder.sh` and the full Finder test set (`all-modes.sh` with `TESTS=` every `finder_*.m` plus the cocotron tests). Expected: all PASS on X11 and Wayland in every mode. Any flake must be rerun twice and recorded.
- [ ] **Step 3: Records.**
  - `tests/finder.md` gets a "File operations" section: each test, what it checks, and its Mac status.
  - The roadmap row 7c becomes "done: `2026-11-22-finder-file-operations.md`", plus any open item.
- [ ] **Step 4: Commit** "tests: Finder file operations end to end; roadmap: plan 7c done", then push darling `finder-app` and cocotron `finder-fileops`.
