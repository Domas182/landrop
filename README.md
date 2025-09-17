# landrop (Terminal File Transfer)

Minimal TCP client/server for sending a single file over the network from the terminal. Shows live progress and speed, with a tiny length‑prefixed protocol.

## Build
- Native: `make` (outputs to `bin/native/`)
- ARM64: `make arm64` (uses `aarch64-linux-gnu-gcc` if installed)
- Custom: `make ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu-`
- Clean: `make clean`

## Usage
1) Start the server (receiver):
```
bin/native/landropd -p 9000 [or whatever port you like] -d /tmp/recv [-o]
```
- `-p`: TCP port to listen on
- `-d`: destination directory for received files
- `-o`: overwrite existing files (default: fail if exists)

2) Send from the client (sender):
```
bin/native/landrop -h 127.0.0.1 -p 9000 -f /path/to/file [-n remote_name]
bin/native/landrop -h 127.0.0.1 -p 9000 -d /path/to/dir
```
- `-h`: server host or IP
- `-p`: server port
- `-f`: path to local file (regular file)
- `-n`: optional remote filename (sanitized)
- `-d`: send all files in directory (recursively)

Both sides print a progress bar with percent, speed, and bytes transferred.

## Protocol (brief)
- Header: `"LFT1"` + 8‑byte filesize (big‑endian) + 2‑byte name length + filename bytes
- Body: file content
- Reply: 1‑byte status (0=success)

## Notes
- Filenames are sanitized to avoid problems 
- Single‑threaded; handles one connection at a time.
- Cross‑compiles for aarch64 with a suitable toolchain (personal use-case).

When using `-d`, each file is sent as a separate upload. Relative paths are included in the remote filename but slashes are sanitized, so files are flattened on the receiver side. Consider `-o` on the server to overwrite existing files.
