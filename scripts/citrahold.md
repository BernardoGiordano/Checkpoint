# Citrahold sync

`citrahold` is a 3DS universal Checkpoint script for synchronizing save and
extdata backups with the official Citrahold service or another compatible
server. It keeps the account token, server selection, optional settings, Game
ID mappings, and cached remote Game IDs in encrypted Checkpoint state.

The script prints its embedded revision label before any other startup output.

## Install

Copy `citrahold.c` to:

```text
/3ds/Checkpoint/scripts/universal/citrahold.c
```

Restart Checkpoint after replacing the file. From Checkpoint, press SELECT and
choose **citrahold**.

## First-run setup

On the first run:

1. Choose **Official Citrahold** or **Custom Citrahold server**.
2. Enter either a full token or a shorthand token.
3. If the entered value is shorter than 16 characters, the script exchanges it
   through `/getToken` and uses the returned full token.
4. The resulting full token is verified through `/getUserID`.
5. Only after verification succeeds is the encrypted vault written.
6. Optionally add a passphrase to the encrypted state.

Official mode uses `https://api.citrahold.com`. Custom mode asks for a server
URL and removes trailing slashes before making requests.

Cancelling setup exits without committing a new configuration. Network,
response-validation, authentication, or state-write failures are reported as
failures rather than being treated as a successful setup.

After setup, the selected mode and token are reused on later runs. Use
**Configuration** to change them.

### Reauthentication

**Configuration → Authenticate active server** is for a full Citrahold token.
It does not perform the shorthand-token exchange used during first-run setup.
The candidate token is verified before it replaces the saved token. If
verification or the subsequent vault write fails, the previous token remains
the saved token.

## Encrypted state and local files

The main state file is:

```text
/3ds/Checkpoint/config/citrahold.vault
```

The vault is encrypted and sealed to the console. Without a passphrase, an SD
card or copied `config/` folder cannot reveal it and it cannot be moved to
another console. Other homebrew running on the same console can reproduce the
device-derived key, however, so only the optional passphrase is a security
boundary. The Configuration menu can set, change, or remove that passphrase.

During a state replacement, the script uses these sibling files temporarily:

```text
/3ds/Checkpoint/config/citrahold.vault.tmp
/3ds/Checkpoint/config/citrahold.vault.old
```

It writes and closes the new vault before replacing the active one, and keeps
the previous vault recoverable while the replacement is installed. Startup
removes stale temporary files and restores the `.old` copy if the final vault
is missing.

While preparing an upload, the script creates:

```text
/3ds/Checkpoint/config/citrahold-upload-payload.json
```

This file contains the plaintext, Base64-encoded upload request. The script
removes it after a successful transfer, a transfer failure, or Hold-B
cancellation. Startup also removes a stale copy after a power loss or an abort
before the upload begins.

Ordinary milestones are sent to Checkpoint's script log. **Debug logging**
also writes non-secret diagnostic lines to:

```text
/3ds/Checkpoint/logs/citrahold/citrahold.log
```

Credentials and save contents are not written to that log.

## Game IDs and mappings

Create a mapping before uploading or downloading. Open **Manage Game IDs** to:

- link a remote Game ID to a Checkpoint save or extdata title;
- unlink an existing mapping; or
- refresh the save and extdata Game ID lists from the server.

The limits are separate concepts:

- There are at most **16 local title mappings total**, shared by saves and
  extdata.
- The encrypted remote cache retains up to **32 save Game IDs** and **32
  extdata Game IDs**, with separate capacities.
- A Game ID can be entered manually, but it must be a single safe value: it
  cannot contain `/`, `\`, `"`, a newline, a carriage return, `|`, or
  `..`, and it must be shorter than 128 characters.

The 32-per-type values are client cache limits, not a statement about how many
Game IDs Citrahold permits on an account. If the server returns more than the
client cache can retain, the script reports that some IDs were not saved. An
unassigned cached ID must be linked to a compatible Checkpoint title before it
can be used for a transfer.

## Uploads

Choose **Upload**, select **Upload save backup** or **Upload extdata backup**,
then select a mapped title and one of its local Checkpoint backups.

Before writing the payload, the script queries the remote Game ID list and the
last-updated time when available. It shows the remote-copy status and asks for
confirmation before continuing. If the pre-upload query is unavailable, the
script identifies the remote state as unavailable and still asks whether to
continue.

The upload request is streamed from the temporary SD-card payload. Its JSON
shape is:

```json
{
  "token": "...",
  "game": "user-defined-game-id",
  "multi": [
    ["user-defined-game-id/path/file.bin", "BASE64_DATA"],
    [
      "user-defined-game-id/subdirectory/citraholdDirectoryDummy",
      "citraholdDirectoryDummy"
    ]
  ]
}
```

Files are Base64-encoded as they are written. Empty directories are represented
by the `citraholdDirectoryDummy` marker. HTTP 200 and HTTP 201 are accepted as
successful upload responses; other statuses are reported as a rejected upload.

**Delete after upload** is disabled by default. When enabled, a successful
upload presents the exact local backup path and asks for confirmation before
recursively removing it. The option can be toggled under **Configuration**.

The upload uses Checkpoint's existing `web_upload_file` binding. Checkpoint
checks Hold-B between script statements, so the upload and `unlink()` are kept
in one conditional expression: the upload result is evaluated first and both
branches remove the payload before the interpreter can reach its next abort
check. The duplicated branches are intentional because picoc does not implement
the comma operator.

## Downloads

Choose **Download**, select a save or extdata mapping, and enter a new
Checkpoint backup name. The script first confirms that the mapped Game ID is
present on the server.

Downloads use a temporary folder named:

```text
<backup-name>.citrahold-partial
```

The server's file list is validated before any file is fetched. Each returned
path must be a non-empty relative path without `\`, an absolute prefix, or
`..`. The `files` list must be a non-empty JSON array of strings. The special
`citraholdDirectoryDummy` marker recreates an empty directory.

Each ordinary file is downloaded one file at a time in HTTP range chunks. A
valid ranged response must be HTTP 206 with a `Content-Range` header whose
start matches the requested offset, whose end matches the response body size,
and whose total remains stable across chunks. A single HTTP 200 full-response
is accepted only for the first request for a file. Misordered, duplicated,
short, oversized, or otherwise inconsistent responses are rejected.

The incomplete folder is renamed to its final Checkpoint backup name only
after every file succeeds. On an ordinary failure, the incomplete folder is
removed. If Hold-B aborts the interpreter before that cleanup runs, retrying the
same backup name identifies the stale partial folder and asks for confirmation
before removing it.

The backup name is validated as one FAT-safe path component before any path is
built. It cannot be empty, `.` or `..`; contain `/`, `\`, `..`, control
characters, or FAT-invalid punctuation; start or end with a space; or end with
a dot. Overlong final and temporary paths are rejected before filesystem work.

## Server API contract

Requests use HTTP `POST`. JSON requests use:

```text
Content-Type: application/json
```

Authenticated requests include the active full token in a `token` property.

| Purpose | Route | Request/response details |
| --- | --- | --- |
| Health check | `/areyouawake` | Used by **Test active server**. |
| Version and messages | `/softwareVersion` | Reads the `3ds` version list and `motd3ds` message list. |
| Exchange shorthand token | `/getToken` | Sends `{"shorthandToken":"..."}`; expects a string `token`. |
| Verify token | `/getUserID` | Sends `{"token":"..."}`; expects a string `userID`. |
| List save Game IDs | `/getSaves` | Sends `{"token":"..."}`; expects a string array in `games`. |
| List extdata Game IDs | `/getExtdata` | Sends `{"token":"..."}`; expects a string array in `games`. |
| Save update time | `/getSavesLastUpdated` | Sends `token` and `game`; reads string `lastModified` when present. |
| Extdata update time | `/getExtdataLastUpdated` | Sends `token` and `game`; reads string `lastModified` when present. |
| Upload saves | `/uploadMultiSaves` | Receives the multi-file upload object described above. |
| Upload extdata | `/uploadMultiExtdata` | Receives the multi-file upload object described above. |
| List save files | `/downloadSaves` | Sends `token` and `game`; expects a string array in `files`. |
| Download save file | `/downloadSaves` | Sends `token`, `game`, and `file`, plus a `Range` header. |
| List extdata files | `/downloadExtdata` | Sends `token` and `game`; expects a string array in `files`. |
| Download extdata file | `/downloadExtdata` | Sends `token`, `game`, and `file`, plus a `Range` header. |

For file downloads, the server should return HTTP 206 and a complete
`Content-Range: bytes start-end/total` header for each requested range. The
script also accepts a complete HTTP 200 response for the first request for a
file, as described above.

## Verification status

The preceding script revision was exercised on a 3DS with the active server for
server activation, Game ID refresh, save and extdata upload/download, invalid
reauthentication, retained-token relaunch, first-run cancellation, invalid
token handling, successful shorthand-token setup, and relaunch using the saved
vault. Those tests completed without a script crash. Because this revision
changes upload cleanup sequencing and adds local backup-name validation, repeat
the hardware upload, Hold-B upload-abort, and download-name tests before merging.

The source checks used for the script are:

```text
./tools/scriptlint.sh scripts/3ds/universal/citrahold.c
git diff --check
```

## Notes and limitations

- Removing `citrahold.vault` removes the saved Citrahold configuration,
  credentials, passphrase state, mappings, and cached remote IDs from the
  script's point of view. Back up the vault first if it may be needed.
