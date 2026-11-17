## What changed

<!-- What this pull request does and why. Link the issue it fixes, if any. -->

## Checks

<!-- Paste the last lines of each check you ran. CONTRIBUTING.md says which a change needs. -->

- [ ] `make test` (required)
  ```
  ```
- [ ] In-game scenarios, `STRICT=1 tools/native/race_check.sh` (anything that runs in the game; needs your disc)
  ```
  ```
- [ ] Golden compare, `tools/golden/baseline.sh` + `compare.sh` (anything that runs in the game; needs your disc)
  ```
  ```
- [ ] I don't have the disc / the time for the in-game checks: please run them

## Rules

- [ ] No game data, assets, disc files or captures in this change
- [ ] With every setting at its default, gameplay is unchanged (a gameplay-changing feature is opt-in)
- [ ] Docs updated where behaviour changed
