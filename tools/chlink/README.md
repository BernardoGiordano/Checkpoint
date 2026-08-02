# chlink

Companion PC CLI for Checkpoint's wireless save transfer. Speaks the exact
protocol implemented by the console targets (HTTP/1.1, multipart upload with a
4-digit PIN token, store-mode ZIP packaging), acting as sender or receiver.

Stdlib-only Go, `CGO_ENABLED=0`; cross-compiles to Linux/macOS/Windows with
`make release`.

## Build

```bash
cd tools/chlink
make            # -> ./chlink
make test       # unit + round-trip tests
make release    # -> dist/chlink-{linux,darwin}-{amd64,arm64}, dist/chlink-windows-amd64.exe
```

## Usage

```
chlink send <path> --to <ip[:port]> --pin <PIN> [flags]
chlink receive [flags]
chlink info --to <ip[:port]>
chlink zip <folder> [-o out.zip] [--store|--deflate]
chlink unzip <archive.zip> [-o dir]
```

Global flags: `--port` (default 8000), `--verbose`, `--json`, `--timeout`
(default 30s).

### Send (PC → console)

Start Receive on the console, then:

```bash
chlink send ./my-backup-folder --to 192.168.1.42 --pin 1234
```

The console PIN is random per receive unless one is pinned in
Settings > Network (3DS) / Connectivity (Switch) > Receive PIN, which is what
makes a `--pin` baked into a script work run after run.

`<path>` may be a folder (packaged as a console-compatible store-mode zip; a
folder holding exactly one file and no subfolders is sent raw, like the
console does), a single file (raw), or an existing `.zip` (sent as-is).

When the path sits inside a Checkpoint SD layout
(`.../Checkpoint/{saves,extdata}/<title folder>/<backup folder>`), the type,
title name/id and backup name are inferred automatically; `--title-id`,
`--title-name`, `--type`, `--backup-name` override. `--yes` skips the confirm
prompt; `--force` overrides the receiver's size cap check (consoles currently
cap uploads at 32 MiB).

### Receive (console → PC)

```bash
chlink receive                 # auto-generates a PIN, prints IPs + PIN
chlink receive --pin 1234 --out ~/backups --once
```

Backups land under `<out>/<saves|extdata>/<titleId titleName>/<backupName>/`
(`--flat` to skip the tree). `--keep-zip` keeps the received archive,
`--no-extract` stores it without extracting, `--once` exits after the first
successful upload.

### zip / unzip

Offline helpers using the same store-mode writer/extractor. `chlink zip` is
console-compatible by default; `--deflate` produces smaller, PC-only archives.
