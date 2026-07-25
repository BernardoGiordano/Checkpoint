# Google Drive save sync

The `googledrive` universal script uploads your Checkpoint save backups to your
own Google Drive. It runs on both 3DS and Switch (one script, no per-console
edits) and is bundled with Checkpoint — it appears in the **Scripts** list out of
the box. You can also drop an updated copy on the SD card to override the bundled
one (`/3ds/Checkpoint/scripts/universal/googledrive.c`, or
`/switch/Checkpoint/scripts/universal/googledrive.c`).

Because the console can't open a browser, sign-in uses Google's **OAuth 2.0
Device flow**. You do a one-time setup in the
Google Cloud Console to get your own credentials, then the console signs in by
showing you a short code to type on your phone or PC.

Everything the script touches is scoped to `drive.file`: it can only ever see the
files **it** creates, never the rest of your Drive.

---

## One-time Google setup

You only do this once, on a computer.

1. **Project** — go to <https://console.cloud.google.com>, open the project
   picker in the top bar → **New Project**, name it `Checkpoint`, **Create**, then
   select it.
2. **Enable the API** — *APIs & Services → Enabled APIs & services →
   \+ Enable APIs and Services* → search **"Google Drive API"** → **Enable**.
3. **OAuth consent screen** (*APIs & Services → OAuth consent screen*):
   - **Audience / User type:** External → **Create**.
   - **Branding:** App name (`Checkpoint`), your support email, developer contact
     email → **Save**.
   - **Audience → Test users → + Add users:** add your own Google account email.
     Leave **Publishing status = Testing**.
4. **Create the client** — *Credentials → + Create Credentials → OAuth client ID*
   → **Application type: "TVs and Limited Input devices"** → name it → **Create**.
   > This application type is what unlocks the device flow. A *Desktop* or *Web*
   > client will **not** work.
5. **Download JSON** — in the dialog (or the ⬇ next to the client), **Download
   JSON**, and rename the file to `client_secret.json`.
6. **Copy it to the SD card** at:
   - 3DS: `/3ds/Checkpoint/config/client_secret.json`
   - Switch: `/switch/Checkpoint/config/client_secret.json`

   (Create the `config` folder if it isn't there yet.)

   The script reads this file **once**, folds it into the encrypted vault, and
   then **deletes it**. That's deliberate — see [How the credentials are
   stored](#how-the-credentials-are-stored). Keep your own copy off-console if
   you want one.

### Notes on the "testing" app

An unverified app left in **Testing** is fine for personal use (up to 100 test
users), but Google may expire its refresh tokens after ~7 days for some
configurations — if that happens the script just runs the sign-in flow again.
Publishing the app removes that expiry but requires Google's review, which is out
of scope for a personal setup.

---

## How the credentials are stored

The thing worth protecting is the **refresh token**: it mints new access tokens
until it is revoked. (The client secret matters much less — for a
device/"installed" client Google does not treat it as confidential, and on its
own it grants access to nothing.)

After the first run, all of it lives in one encrypted file,
`config/gdrive.vault`, written by Checkpoint's `device_seal` API: AES-256-GCM
under a key derived from material only your console's own services can answer for
(the NAND CID on 3DS, an SPL device-unique key on Switch). That material is never
written to the SD card.

**What that buys you:**

- Pulling the SD card and reading it on a PC gets you nothing usable.
- Zipping up your `config/` folder and posting it leaks nothing.
- Something scanning the card for `*token*.json` / `client_secret.json` finds
  nothing — the plaintext files are gone.
- Moving the card to another console does not carry your sign-in with it.

**What it does not buy you:** protection from other homebrew *on the same
console*. Neither the 3DS nor the Switch isolates homebrew apps from each other,
and Checkpoint is open source, so any other app on your console can reproduce the
console-bound half of the key by reading Checkpoint's source. Encryption alone
raises the bar here; it is not a wall.

### The passphrase (Security → Set or change passphrase)

The passphrase is the half that *is* a wall. It is stretched with PBKDF2 and
mixed into the key, and it exists nowhere but in your head — not on the card, not
in the console. An attacker holding `gdrive.vault` has to guess it.

The cost is one keyboard prompt per run. It's off by default, and the script
offers it on first sign-in.

Minimum 8 characters, and this is enforced rather than warned about: a
four-character passphrase is guessed offline in seconds, and typing one would
leave you believing the vault was protected when it wasn't. **There is no
recovery** — forget it and your only option is *Sign out* followed by a fresh
sign-in.

### If a token does leak

*Security → Sign out (revoke access)* revokes the refresh token at Google and
deletes the vault. That's the step that actually helps, and it works even if the
file already got out. You can also revoke by hand at
<https://myaccount.google.com/permissions>.

Because the scope is `drive.file`, a leaked token reaches only the files this
script uploaded — never the rest of your Drive.

---

## Using it

1. Launch Checkpoint. (Optionally highlight a title first if you want the
   **This title** option.)
2. Open the **Scripts** action and run **googledrive**.
3. **First run:** the script asks whether to protect the saved sign-in with a
   passphrase, then shows a short code and `google.com/device`. On your phone/PC
   open that page, sign in, and enter the code. Leave the message on screen while
   it waits — it finishes automatically. The refresh token goes into the encrypted
   `config/gdrive.vault`, so later runs sign in silently.
4. Choose what to do:
   - **All titles** — every title that has at least one backup on the SD card.
   - **This title** — the title you had highlighted (only offered when one was).
   - **One backup…** — pick a single title, then a single backup folder.
   - **Security…** — set/change/remove the passphrase, or sign out and revoke.
5. Each backup folder is zipped and uploaded. Progress shows on the status line;
   **hold B** to cancel. A summary reports how many uploaded / failed.

Uploaded files land in Drive under:

```
My Drive / Checkpoint / <3ds|switch> / <title name> / <backup name>.zip
```

Each console keeps its backups in its own subfolder, so a 3DS and a Switch syncing
to the same account never mix titles up.

Re-running a sync of a backup whose name already exists **updates** that Drive
file in place rather than making a duplicate.

---

## Troubleshooting

- **"Missing config/client_secret.json"** — the file isn't at the path in step 6,
  or is named differently.
- **"Could not start Google sign-in"** — check Wi-Fi, and that the **Google Drive
  API** is enabled for your project (setup step 2).
- **Sign-in fails right after entering the code** — make sure the OAuth client is
  the **"TVs and Limited Input devices"** type and that your Google account is
  listed under **Test users**.
- **It asks you to sign in again after about a week** — expected for an app left
  in *Testing* (see the note above); just complete the device flow again.

---

The script itself is [`common/universal/googledrive.c`](common/universal/googledrive.c).
For the API it is written against — and for writing your own scripts — see
[`README.md`](README.md), the scripting manual in this folder.
