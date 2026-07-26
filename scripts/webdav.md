# WebDAV save sync

The `webdav` universal script uploads your Checkpoint save backups to any WebDAV
server — Nextcloud, ownCloud, Synology, Apache `mod_dav`, `rclone serve webdav`,
most NAS "personal cloud" apps — and downloads them back onto the console. It
runs on both 3DS and Switch (one script, no per-console edits) and is bundled
with Checkpoint, so it appears in the **Scripts** list out of the box. You can
also drop an updated copy on the SD card to override the bundled one
(`/3ds/Checkpoint/scripts/universal/webdav.c`, or
`/switch/Checkpoint/scripts/universal/webdav.c`).

Compared with [`googledrive`](googledrive.md), the trade is simple: no account to
create, no Google Cloud project, no consent screen, no token that expires — just
a URL, a username and a password. And no cloud at all unless you already have
one.

---

## What you need

- A WebDAV URL you can write to.
- A username and a password for it. **Use an app password if your server can mint
  one** (Nextcloud: *Settings → Security → Devices & sessions*; ownCloud and
  Synology have the same feature). Then losing the console costs you one
  revocation, not your account.
- Wi-Fi on the console.

### The URL for common servers

| Server | Base URL |
| --- | --- |
| Nextcloud / ownCloud | `https://cloud.example.com/remote.php/dav/files/<username>` |
| Synology WebDAV Server | `https://nas.example.com:5006/<shared folder>` |
| Apache `mod_dav` | whatever you gave `<Location>`, e.g. `https://example.com/dav` |
| `rclone serve webdav` | `http://192.168.1.10:8080` |
| Box / 4shared / pCloud | their published WebDAV endpoint |

The script appends `/Checkpoint/<3ds|switch>/...` to whatever you give it, so
point it at a folder you are happy for it to fill. A trailing `/` is trimmed for
you; the scheme (`http://` or `https://`) is required.

> **Redirects don't work.** Uploads deliberately do not follow them (a redirect
> would re-send the whole body). If your server answers `http://` with a redirect
> to `https://`, enter the `https://` address.

---

## Setting it up

Two ways in. Both end with the details sealed into `config/webdav.vault`.

### The file (recommended, and the only sane route for a long URL)

Create a small JSON file on the SD card at

- 3DS: `/3ds/Checkpoint/config/webdav.json`
- Switch: `/switch/Checkpoint/config/webdav.json`

```json
{
  "url": "https://cloud.example.com/remote.php/dav/files/alice",
  "user": "alice",
  "password": "abcde-fghij-klmno-pqrst-uvwxy"
}
```

Run the script once. It reads the file, asks whether you want a passphrase,
seals everything, and then **deletes** the JSON — it held your password in the
clear. Keep your own copy off-console if you want one.

### The keyboard

If there is no `webdav.json`, the script asks for the three fields with the
console keyboard. On 3DS the keyboard caps input at **63 characters**, which a
Nextcloud dav URL usually exceeds — that is exactly what the file route is for.

Either way, finish with **Settings → Test connection**: it does one `PROPFIND`
against your base URL and tells you in words whether the URL, the username and
the password all work.

---

## Using it

1. Launch Checkpoint. (Optionally highlight a title first if you want the
   **Upload this title** option.)
2. Open the **Scripts** action and run **webdav**.
3. Choose what to do:
   - **Upload all titles** — every title with at least one backup on the SD card.
     Backups already on the server are **skipped**, so a second run only sends
     what is new. That matters on a 3DS over Wi-Fi.
   - **Upload this title** — the title you had highlighted (only offered when one
     was), same skipping.
   - **Upload one backup…** — pick a title, then one backup folder. This one
     **always** uploads, replacing the copy on the server. It is how you force a
     re-upload of something the bulk runs skip.
   - **Restore from server…** — lists the titles on the server that are also
     installed on this console, then that title's backups. The one you pick is
     downloaded and unpacked into Checkpoint's backup folder for that title.
   - **Settings…** — connection test, server details, passphrase, forget server.
4. **Hold B** to cancel at any point. A summary reports how many were uploaded,
   skipped and failed.

Files land on the server under:

```
<base URL>/Checkpoint/<3ds|switch>/<title name>/<backup name>.zip
```

Each console keeps its backups in its own subfolder, so a 3DS and a Switch
syncing to the same server never mix titles up. Names get the characters Windows
shares and several WebDAV front ends refuse (`/ \ : * ? " < > |`, trailing dots)
replaced with `_`.

### Restore is a download, not a restore

**Restore from server** only puts the backup back in Checkpoint's own backup
list. Nothing is written to the game's save until you select it in Checkpoint and
press **Restore** — the normal path, with the normal confirmation. If a folder of
that name already exists locally, the download lands next to it as
`<name> (2)`, never merged into it.

Downloads pass through memory, unlike uploads (which stream from the SD card). On
a 3DS a very large backup can therefore fail with "too big to download on this
console" — the upload direction has no such limit.

---

## How the credentials are stored

The password is the asset. After setup it lives in one encrypted file,
`config/webdav.vault`, written by Checkpoint's `device_seal` API: AES-256-GCM
under a key derived from material only your console's own services can answer for
(the NAND CID on 3DS, an SPL device-unique key on Switch). That material is never
written to the SD card.

**What that buys you:**

- Pulling the SD card and reading it on a PC gets you nothing usable.
- Zipping up your `config/` folder and posting it leaks nothing.
- Something scanning the card for a `*.json` full of passwords finds nothing —
  the plaintext file is deleted at setup.
- Moving the card to another console does not carry your server details with it.

**What it does not buy you:** protection from other homebrew *on the same
console*. Neither the 3DS nor the Switch isolates homebrew apps from each other,
and Checkpoint is open source, so any other app on your console can reproduce the
console-bound half of the key by reading Checkpoint's source. Encryption alone
raises the bar here; it is not a wall.

### The passphrase (Settings → Set or change passphrase)

The passphrase is the half that *is* a wall. It is stretched with PBKDF2 and
mixed into the key, and it exists nowhere but in your head — not on the card, not
in the console. An attacker holding `webdav.vault` has to guess it.

The cost is one keyboard prompt per run. It's off by default, and the script
offers it during setup.

Minimum 8 characters, and this is enforced rather than warned about: a
four-character passphrase is guessed offline in seconds, and typing one would
leave you believing the vault was protected when it wasn't. **There is no
recovery** — forget it and your only option is *Forget this server* followed by
entering the details again.

### Transport

Checkpoint's HTTP layer does **not** verify TLS certificates. `https://` still
encrypts the traffic, but a determined attacker on the path between console and
server could impersonate it. Use a server you reach over your own LAN or a link
you trust, and give the script an app password scoped to a share that holds
nothing else.

### If the console is lost

*Settings → Forget this server* deletes the vault from the console — but if the
console is out of your hands, that is not the step that helps. **Revoke the app
password on the server.** Backups already uploaded stay where they are.

---

## Troubleshooting

The script turns the server's answer into a sentence; here is what each one
means.

- **"The server refused the username or password (401)"** — wrong credentials, or
  the server wants an app password rather than the account one (Nextcloud does
  when two-factor auth is on).
- **"The server has nothing at that URL (404)"** — usually a base URL missing its
  tail. For Nextcloud it must end with `/remote.php/dav/files/<username>`.
- **"that URL does not speak WebDAV (405)"** — the address answers HTTP but is
  not a WebDAV collection; check the path.
- **"The server redirected the request"** — enter the address it redirects to.
  `http://` → `https://` is the usual case.
- **"The server is out of space (507)"** — the account's quota is full.
- **"The server could not be reached (transfer error …)"** — Wi-Fi, DNS, port, or
  a self-signed certificate the console cannot reach at all; the number is a
  negated libcurl code.
- **"There are no backups on the server for this console yet"** — restore looks
  under `Checkpoint/<3ds|switch>/`, so a Switch cannot see a 3DS's uploads (and
  vice versa). That is intentional.
- **"The server has backups, but none of those titles are installed"** — restore
  matches server folders to installed titles by name. Install the title (or, on
  Switch, switch to the profile that owns it) and try again.

Every run also writes its steps to the script transcript and to Checkpoint's log
file — that is what to attach to a bug report.

## Trying it without a server

If you have no WebDAV server — or you are changing the script and want to see
what it actually sends — the repository ships a throwaway one:

```bash
tools/webdav-testserver.py --root /tmp/dav
```

It serves a plain directory over the part of WebDAV this script uses (OPTIONS,
PROPFIND, MKCOL, PUT, GET, DELETE, HTTP Basic), needs nothing installed beyond
Python 3, and prints the base URL to type into Settings along with the default
credentials (`ckpt` / `ckpt`). Put the console on the same Wi-Fi as the PC and
every request shows up as a log line — method, path, status, bytes — so an
upload, a listing and a restore each verify themselves. Uploads land in
`/tmp/dav/Checkpoint/<3ds|switch>/<title>/`, where they can be unzipped and
diffed against the SD card.

It speaks plain HTTP with one shared account and no TLS: fine on a home network,
never on one you do not control.

---

The script itself is [`common/universal/webdav.c`](common/universal/webdav.c).
For the API it is written against — and for writing your own scripts — see
[`README.md`](README.md), the scripting manual in this folder.
