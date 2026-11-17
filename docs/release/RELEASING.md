# Making a release

How to publish a release so that players get it, and so that the launcher's update check sees it.

## How the launcher finds releases

Once a day (and when the player presses **Check now** on the launcher's Settings page), the launcher asks GitHub for
the repository's latest release:

```
GET https://api.github.com/repos/u0rhs/burnout3-native/releases/latest
User-Agent: b3port-launcher/<version>
Accept: application/vnd.github+json
X-GitHub-Api-Version: 2022-11-28
```

Nothing else is sent. It reads the release's `tag_name`, `name`, `html_url`, `published_at`, `prerelease` and the
first lines of its notes, and compares the tag with the version built into the launcher (`VERSION` plus the commit,
`include/b3report/version.h`). When the tag is newer, the Play page shows **Update available: v0.2.0 — <name>** with
a **Download** button that opens the release's page in the browser. The launcher never downloads or installs
anything itself. The code is `src/b3launcher/update.h` (the comparison, the JSON, the remembered answer in
`launcher.ini`) and `update_net.h` (the request: WinHTTP on Windows, the system's libcurl on Linux and macOS).

What that means for a release:

- **`releases/latest` is the newest published release that is not a draft and not a pre-release.** Drafts and
  pre-releases are never offered to players. Mark every test or release-candidate build as a **pre-release**.
- **The tag must be a version:** `vMAJOR.MINOR.PATCH`, for example `v0.2.0` (Semantic Versioning; a pre-release tag
  can be `v0.3.0-rc.1`). A tag that is not a version is ignored: the launcher then says nothing.
- **The build must carry the same version as the tag.** The launcher compares its own version with the tag, so a
  `v0.2.0` release must be built with `VERSION` set to `0.2.0` (or `0.2.0-test`: a `-test` build counts as its base
  version). If the build in the release says `0.1.0`, players who install it will be told that `v0.2.0` is out.
- A build that is newer than the latest release (a developer's build) is never told to update.

## Steps

1. **Pick the version.** Raise MAJOR, MINOR or PATCH as usual. Set the `VERSION` file to it (for example `0.2.0`)
   on `main`, and commit.
2. **Check the tree.** `make -j8 NPROC=8 test`, the race checks, and the launcher's screenshot checks
   (`build/b3launcher --screenshot <dir> --pref <dir>`, which must exit 0 with every check PASS). The tree must
   be clean: a build from uncommitted changes is stamped `-dirty`, and the packaging scripts refuse or warn about
   it.
3. **Tag the commit on `main`:**

   ```
   git tag -a v0.2.0 -m "Burnout 3 native port 0.2.0"
   git push origin v0.2.0
   ```

4. **Build the packages from that commit:**
   - Windows: `tools/release/package_win.sh` gives `burnout3-native-0.2.0-<commit>-win64.zip`.
   - Linux: `tools/release/package_linux.sh` gives `burnout3-native-0.2.0-<commit>-linux-x86_64.tar.gz` (with
     `SMOKE=1` it also runs the packaged build's smoke test).
   - Mac: the Mac disk image (`.dmg`) for Apple Silicon.

   (The packages' names are to become `burnout3-native-...`. The update check does not look at the attached
   files' names, so either works.)

   Check that each package's `VERSION.txt` (and the launcher's Credits page) shows `0.2.0+<the tagged commit>`.
5. **Create the GitHub Release** for the tag (the repository's **Releases > Draft a new release**, or
   `gh release create v0.2.0 --title "..." --notes-file notes.md <files>`):
   - Title: a short name. The launcher shows it after the tag ("Update available: v0.2.0 — <title>").
   - Notes: start with the most important changes. The launcher shows only the first lines (headings and list marks
     taken off), so put a one-line summary first.
   - Attach the Windows zip, the Linux tarball and the Mac dmg.
   - Tick **Set as a pre-release** for anything that is not meant for every player. Leave it unticked for a normal
     release, and make sure it is marked as the latest release.
   - Publish. A draft is invisible to the launcher.
6. **Check it.** Open `https://api.github.com/repos/u0rhs/burnout3-native/releases/latest` in a browser: its
   `tag_name` must be the new tag. Launchers of older versions show the banner at their next daily check (or at once
   with Settings > Launcher > **Check now**).

## Trying the banner without a release

The launcher has a test hook: with `B3LAUNCHER_UPDATE_FILE=<file>` it reads the answer from that file instead of
asking GitHub, at the launcher's next check (the daily one, or Settings > Launcher > **Check now** at once). The file
holds a `releases/latest` JSON answer (a 200), or a status line, an empty line and a body for another status:

```
HTTP/1.1 404 Not Found

{"message": "Not Found"}
```

The `--screenshot` run uses the same hook with canned answers (a newer release, the same release, no release, no
answer, the check turned off) and never makes a request; its shots are `update_available.png`,
`update_available_credits.png`, `update_current_credits.png`, `update_error_credits.png` and `update_settings.png`.
