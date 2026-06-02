# ved — Vim-like Editor for U-Boot

A modal, vim-inspired text editor that runs natively inside U-Boot 2023.10. It operates entirely within the bootloader environment — no kernel, no OS, no external dependencies — using U-Boot's built-in filesystem API and UART console for all I/O.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [File Structure](#file-structure)
- [Integration](#integration)
- [Build Configuration](#build-configuration)
- [Usage](#usage)
- [Key Bindings](#key-bindings)
- [Ex Commands](#ex-commands)
- [Design Decisions](#design-decisions)
- [Constraints and Limits](#constraints-and-limits)
- [Terminal Requirements](#terminal-requirements)
- [Customization](#customization)

---

## Overview

`ved` adds a `ved` command to the U-Boot shell that opens a text file from any supported filesystem for interactive editing. The interface is deliberately vim-like: it has Normal, Insert, Command, and Search modes with standard vim keybindings, a status bar showing the current mode and cursor position, and a command line for ex-style commands such as `:w`, `:q`, and `:e`.

**Primary use cases:**

- Editing boot configuration files (e.g. `uEnv.txt`, `extlinux.conf`, `config.txt`) directly from the bootloader without booting a full OS
- Modifying kernel command-line arguments stored on a FAT or ext4 partition
- Emergency recovery editing when no OS is accessible
- Factory provisioning workflows that need to write device-specific files to storage

---

## Architecture

The editor is split into four translation units with a shared header, each with a single responsibility:

```
┌──────────────────────────────────────────────────────┐
│                   U-Boot Shell                        │
│                  ved <args>                           │
└────────────────────┬─────────────────────────────────┘
                     │
┌────────────────────▼─────────────────────────────────┐
│                  cmd_ved.c                            │
│  • U_BOOT_CMD registration                           │
│  • ANSI escape sequence input parser (ved_read_key)  │
│  • Modal dispatch loop (Normal / Insert / Command /  │
│    Search)                                           │
│  • Ex command interpreter (:w :q :e :N ...)          │
└──────┬────────────────┬──────────────────┬───────────┘
       │                │                  │
┌──────▼──────┐  ┌──────▼──────┐  ┌───────▼──────┐
│ cmd_ved_    │  │ cmd_ved_    │  │ cmd_ved_     │
│ fs.c        │  │ edit.c      │  │ render.c     │
│             │  │             │  │              │
│ Filesystem  │  │ All in-     │  │ ANSI terminal│
│ abstraction │  │ memory      │  │ rendering    │
│             │  │ editing     │  │              │
│ fs_set_blk_ │  │ operations: │  │ Line drawing │
│ dev()       │  │ insert,     │  │ Status bar   │
│ fs_read()   │  │ delete,     │  │ Command line │
│ fs_write()  │  │ yank, put,  │  │ Cursor       │
│             │  │ search,     │  │ positioning  │
│ Supports:   │  │ scroll,     │  │              │
│ FAT, ext4,  │  │ word motion │  │              │
│ squashfs    │  │             │  │              │
└─────────────┘  └─────────────┘  └──────────────┘
       │
┌──────▼──────────────────────────────────────────────┐
│              U-Boot FS API (include/fs.h)            │
│         FAT · ext4 · squashfs · FS_TYPE_ANY          │
└─────────────────────────────────────────────────────┘
```

### State Model

All editor state lives in a single `struct ved_state` instance (`g_ved`) allocated statically in BSS. There is no heap allocation at runtime. The structure holds:

- **Line buffer** — `lines[4096][512]`, a flat 2D array of fixed-width rows
- **Line length array** — `line_len[4096]`, length of each row (avoids `strlen` on every operation)
- **Cursor** — `cur_row`, `cur_col`, `scroll_top`
- **Mode** — one of `MODE_NORMAL`, `MODE_INSERT`, `MODE_COMMAND`, `MODE_SEARCH`
- **Dirty flag** — set on any modification, cleared on successful save
- **Yank register** — single-line yank buffer (`yank_line`)
- **Search buffer** — last `/` pattern, reused by `n`/`N`
- **FS descriptor** — `struct ved_fs` holding interface type, device/partition string, and filesystem type

### Filesystem Layer

`ved_fs_probe()` calls `fs_set_blk_dev()` to validate the device and partition at startup. All subsequent load and save operations call `fs_set_blk_dev()` again before each `fs_read()` or `fs_write()`, because U-Boot's FS layer does not retain the active device across command boundaries.

File content is read into and written from `CONFIG_SYS_LOAD_ADDR` via `map_sysmem()`. This is the same address used by `tftp`, `load`, and similar commands, so care should be taken not to run those commands while `ved` holds data in RAM (though in practice `ved` is interactive and blocking, so this is not an issue during normal use).

### Input Parsing

`ved_read_key()` reads one logical keypress and returns an `int`. ASCII characters are returned as-is (values 0x00–0xFF). Extended keys (arrows, Page Up/Down, Delete, Home, End) are parsed from their ANSI escape sequences and returned as values above 0xFF, defined as constants in `cmd_ved.h`. This allows all key handling to use a single `switch` statement without nested escape-sequence state machines in the mode handlers.

### Rendering

Every iteration of the main loop calls `ved_render()`, which redraws the entire visible area. Cursor hiding (`\033[?25l`) at the start and showing (`\033[?25h`) at the end prevents flicker. Each line is preceded by a 4-digit dimmed line number followed by one space (5 columns total), leaving `term_cols - 5` columns for content. Control characters below 0x20 are rendered as `^X` in reverse video rather than being sent raw to the terminal. The status bar occupies the second-to-last row and the command/message line occupies the last row.

---

## File Structure

| File | Purpose |
|---|---|
| `cmd_ved.h` | Shared definitions: limits, mode constants, key constants, ANSI macros, `struct ved_fs`, `struct ved_state` |
| `cmd_ved.c` | `U_BOOT_CMD` entry point, input loop, mode handlers, ex command interpreter |
| `cmd_ved_fs.c` | Filesystem probe, file load, file save |
| `cmd_ved_render.c` | Terminal rendering: lines, status bar, command line, cursor positioning |
| `cmd_ved_edit.c` | All editing primitives: motion, insert, delete, yank, put, search |
| `Makefile` | `obj-$(CONFIG_CMD_VED)` build rule |
| `Kconfig` | `CONFIG_CMD_VED` option with dependency on `FS_FAT` or `FS_EXT4` |

---

## Integration

Copy all `.c`, `.h`, `Makefile`, and `Kconfig` files into `cmd/` inside the U-Boot source tree:

```bash
cp cmd_ved.h cmd_ved.c cmd_ved_fs.c cmd_ved_render.c cmd_ved_edit.c \
   Makefile Kconfig  /path/to/u-boot/cmd/
```

Add the build rule to `cmd/Makefile` (one line):

```makefile
obj-$(CONFIG_CMD_VED) += cmd_ved.o cmd_ved_fs.o cmd_ved_render.o cmd_ved_edit.o
```

Add the Kconfig entry to `cmd/Kconfig` (append the contents of the provided `Kconfig` file, or paste it under another relevant `CMD_` entry in that file).

---

## Build Configuration

Enable the required options in your board's defconfig or via `make menuconfig`:

```kconfig
# Required
CONFIG_CMD_VED=y

# At least one filesystem driver must be enabled
CONFIG_FS_FAT=y        # for FAT12/16/32/exFAT partitions
CONFIG_FS_EXT4=y       # for ext2/3/4 partitions

# Generic FS command infrastructure (usually already enabled)
CONFIG_CMD_FS_GENERIC=y
```

For write support on ext4, also ensure:

```kconfig
CONFIG_EXT4_WRITE=y
```

FAT write support is included by default when `CONFIG_FS_FAT=y` is set.

---

## Usage

```
=> ved <iftype> <dev:part> <filename> [fstype]
```

| Argument | Description | Examples |
|---|---|---|
| `iftype` | Block device interface type | `mmc`, `usb`, `virtio`, `nvme`, `sata` |
| `dev:part` | Device index and partition number | `0:1`, `1:2`, `0:3` |
| `filename` | Absolute path to file on the partition | `/boot/uEnv.txt`, `/config.txt` |
| `fstype` | Filesystem type (optional — auto-detected if omitted) | `fat`, `ext4` |

**Examples:**

```bash
# Edit uEnv.txt on the first partition of the first MMC device (auto-detect FS)
=> ved mmc 0:1 /boot/uEnv.txt

# Edit a config file on a USB drive, explicitly specifying FAT
=> ved usb 0:1 /config.txt fat

# Edit extlinux.conf on an ext4 partition of a VirtIO block device
=> ved virtio 0:1 /boot/extlinux/extlinux.conf ext4

# Create a new file (ved opens an empty buffer if the file does not exist)
=> ved mmc 0:1 /new-file.txt
```

If the specified file does not exist, `ved` opens an empty buffer. Writing with `:w` creates the file.

---

## Key Bindings

### Normal Mode

**Motion**

| Key | Action |
|---|---|
| `h` or `←` | Move left one character |
| `l` or `→` | Move right one character |
| `k` or `↑` | Move up one line |
| `j` or `↓` | Move down one line |
| `w` | Jump forward to the start of the next word |
| `b` | Jump backward to the start of the previous word |
| `0` or `Home` | Move to the beginning of the line |
| `$` or `End` | Move to the end of the line |
| `gg` | Jump to the first line of the file |
| `G` | Jump to the last line of the file |
| `{N}G` | Jump to line N (e.g. `42G` goes to line 42) |
| `Page Up` | Scroll up one full screen |
| `Page Down` | Scroll down one full screen |
| `Ctrl+U` | Scroll up half a screen |

**Entering Insert Mode**

| Key | Action |
|---|---|
| `i` | Insert before the cursor |
| `I` | Insert at the beginning of the line |
| `a` | Insert after the cursor |
| `A` | Insert at the end of the line |
| `o` | Open a new line below and enter Insert mode |
| `O` | Open a new line above and enter Insert mode |

**Editing (Normal Mode)**

| Key | Action |
|---|---|
| `x` or `Delete` | Delete the character under the cursor |
| `X` | Delete the character before the cursor |
| `r{c}` | Replace the character under the cursor with `c` |
| `dd` | Delete the current line (also copies it to the yank register) |
| `{N}dd` | Delete N lines |
| `D` | Delete from the cursor to the end of the line |
| `d$` | Same as `D` |
| `yy` or `Y` | Yank (copy) the current line |
| `p` | Put (paste) the yanked line below the current line |

**Search**

| Key | Action |
|---|---|
| `/{pattern}` then `Enter` | Search forward for pattern |
| `n` | Repeat the last search forward |
| `N` | Repeat the last search backward |

**Other**

| Key | Action |
|---|---|
| `:` | Enter Command mode |
| `Ctrl+S` | Save the file immediately |
| `Ctrl+C` | Quit (only if no unsaved changes) |

---

### Insert Mode

| Key | Action |
|---|---|
| `Esc` | Return to Normal mode |
| Any printable character | Insert at cursor position |
| `Enter` | Insert a newline, splitting the current line |
| `Backspace` | Delete the character before the cursor (merges lines at column 0) |
| `Ctrl+U` | Delete from the cursor back to the beginning of the line |
| `↑` `↓` `←` `→` | Move cursor (without leaving Insert mode) |
| `Home` | Move to beginning of line |
| `End` | Move to end of line |

---

### Command Mode

Entered by pressing `:` in Normal mode. Type the command and press `Enter` to execute, or `Esc` to cancel.

---

## Ex Commands

| Command | Action |
|---|---|
| `:w` | Save the current file |
| `:w {filename}` | Save to a different filename (also changes the active filename) |
| `:q` | Quit (refused if there are unsaved changes) |
| `:q!` | Quit unconditionally, discarding unsaved changes |
| `:wq` | Save and quit |
| `:wq!` | Save and quit unconditionally |
| `:x` | Save and quit (same as `:wq`) |
| `:{N}` | Jump to line N |
| `:$` | Jump to the last line |
| `:e {filename}` | Open a different file (refused if current buffer has unsaved changes) |
| `:e! {filename}` | Open a different file, discarding unsaved changes |

---

## Design Decisions

**No heap allocation.** The entire `struct ved_state` (approximately 2.1 MB for the line buffer at maximum capacity) is allocated statically in BSS. This avoids fragmentation of the U-Boot malloc pool and eliminates the possibility of allocation failure mid-edit. In practice, boards that run U-Boot typically have sufficient RAM to hold this structure alongside normal bootloader use.

**`CONFIG_SYS_LOAD_ADDR` as the I/O buffer.** Rather than a separate static buffer for file I/O, the load address is reused. This is conventional in U-Boot and keeps BSS footprint lower. The implication is that the file is fully read into RAM before parsing, and fully serialized to RAM before writing.

**`fs_set_blk_dev()` called before every I/O operation.** U-Boot's filesystem layer does not preserve the active device between calls — any other command that runs `fs_set_blk_dev()` will silently change it. Calling it again just before `fs_read` and `fs_write` ensures correctness even if another command somehow runs (which cannot happen during a blocking interactive session, but is defensive nonetheless).

**Extended keys mapped above 0xFF.** The `int` return value of `ved_read_key()` uses values above `0xFF` for arrow keys and function keys. This allows all key dispatch to use a single flat `switch` statement with integer constant cases, which is valid in C and avoids needing a lookup table or secondary dispatch.

**Single-line yank register.** Only one line can be yanked at a time. Multi-line yank/put is not implemented. This is a deliberate simplification — the editor is intended for configuration file editing, not general text processing.

**Line-number gutter.** Lines are displayed with a 4-digit right-aligned line number and a space (5 columns) on the left, using dim ANSI rendering. This uses 5 columns of the terminal width but significantly aids navigation, which is the primary concern when editing boot configuration files.

---

## Constraints and Limits

| Limit | Value | Defined in |
|---|---|---|
| Maximum lines per file | 4096 | `VED_MAX_LINES` in `cmd_ved.h` |
| Maximum characters per line | 511 + NUL | `VED_MAX_LINE_LEN` in `cmd_ved.h` |
| Maximum filename length | 255 + NUL | `VED_MAX_FILENAME` in `cmd_ved.h` |
| Maximum file size (read) | ~2 MB | `VED_FILE_BUF_SIZE = VED_MAX_LINES * VED_MAX_LINE_LEN` |
| Command buffer length | 127 + NUL | `VED_CMD_BUF_SIZE` in `cmd_ved.h` |
| Yank register | 1 line | `yank_line` in `struct ved_state` |
| Terminal size (default) | 24 rows × 80 columns | `TERM_ROWS`, `TERM_COLS` in `cmd_ved.h` |

Lines exceeding `VED_MAX_LINE_LEN - 1` characters are silently truncated on load. Files exceeding `VED_MAX_LINES - 1` lines are truncated at that boundary. Binary files are not supported — bytes below `0x20` are rendered as `^X` and cannot be inserted in Insert mode (the input handler ignores values below `0x20` other than recognized control sequences).

---

## Terminal Requirements

The serial console application on the host must:

- Support **ANSI/VT100 escape sequences** for cursor movement, line clearing, and color/attribute codes
- Send standard **VT100 escape sequences for arrow keys** (`ESC [ A` through `ESC [ D`)
- Transmit **Backspace as `0x7F` (DEL)** or `0x08 (BS)` — both are handled

Recommended terminal emulators and serial clients:

- **minicom** — works out of the box with default settings
- **picocom** — works out of the box
- **PuTTY** — works; ensure "Terminal" → "Keyboard" → "The Backspace key" is set to "Control-H" or "VT100+"
- **screen** — works with a standard `TERM=vt100` or `TERM=xterm` environment

The terminal size defaults to **24 rows × 80 columns**. To adjust for a larger terminal, change `TERM_ROWS` and `TERM_COLS` in `cmd_ved.h` and rebuild.

---

## Customization

**Changing terminal dimensions** — edit `TERM_ROWS` and `TERM_COLS` in `cmd_ved.h`. The editor allocates `term_rows - 2` rows for text (the bottom two rows are reserved for the status bar and command line).

**Changing the load address** — `VED_LOAD_ADDR` defaults to `CONFIG_SYS_LOAD_ADDR`. Override it in `cmd_ved.h` if this conflicts with other uses of that address on your board.

**Reducing memory footprint** — lower `VED_MAX_LINES` and/or `VED_MAX_LINE_LEN` in `cmd_ved.h`. The line buffer is `VED_MAX_LINES × VED_MAX_LINE_LEN` bytes in BSS, so halving either constant halves that contribution. For a board that only edits short config files, `VED_MAX_LINES=256` and `VED_MAX_LINE_LEN=256` reduces the BSS cost from ~2 MB to ~64 KB.

**Adding filesystem types** — add a new `if (!strcmp(...))` branch returning the appropriate `FS_TYPE_*` constant to `ved_fs_str_to_type()` in `cmd_ved_fs.c`
