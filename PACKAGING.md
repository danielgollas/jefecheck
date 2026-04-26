# Packaging

JefeCheck ships through three channels:

| Platform | Channel | Install command |
|---|---|---|
| macOS | Homebrew Cask (direct URL) | `brew install --cask https://raw.githubusercontent.com/danielgollas/jefecheck/main/Casks/jefecheck.rb` |
| Windows | Winget | `winget install danielgollas.JefeCheck` |
| Linux | Tarball + `install.sh` | Download from [Releases](https://github.com/danielgollas/jefecheck/releases) |

The Cask and Winget manifests are version-controlled in this repo (`Casks/jefecheck.rb`, `packaging/winget/manifests/`). The `update-manifests.yml` workflow runs on every published release, downloads the new artifacts, recomputes SHA256s, and opens a PR bumping both manifests.

## One-time setup

### Homebrew Cask

Three escalating options:

**1. Direct URL install (current — no tap repo needed):**
```bash
brew install --cask https://raw.githubusercontent.com/danielgollas/jefecheck/main/Casks/jefecheck.rb
```
The Cask file in this repo is the source of truth. Homebrew downloads the .dmg, copies `JefeCheck.app` to `/Applications`, and strips quarantine — launches cleanly with no Gatekeeper warnings. The `update-manifests.yml` workflow keeps the SHA up to date on every release. Slight downside: long URL.

**2. Personal tap (cleaner command, requires new repo):**
1. Create a new GitHub repo named `homebrew-jefecheck` (Homebrew requires the `homebrew-` prefix).
2. Copy `Casks/jefecheck.rb` from this repo into the tap repo's root (or `Casks/` subdir).
3. Users install with:
   ```bash
   brew install --cask danielgollas/jefecheck/jefecheck
   ```
4. Sync the file from this repo to the tap on each release (manually or via PAT-secured workflow).

**3. Submission to homebrew-cask (cleanest UX, hardest to get accepted):**
Cleanest command: `brew install --cask jefecheck` (no tap, no URL). Open a PR against [homebrew/homebrew-cask](https://github.com/Homebrew/homebrew-cask) using `Casks/jefecheck.rb`. Reviewers generally want notarized apps for unsigned-binary submissions, so this likely needs an Apple Developer ID first.

### Winget

Winget manifests in `packaging/winget/manifests/` need to be submitted to [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) — Microsoft's central package repository. After acceptance, users install with `winget install danielgollas.JefeCheck`.

**Manual submission (per release):**
1. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
2. Copy `packaging/winget/manifests/d/danielgollas/JefeCheck/<version>/` into the fork's `manifests/d/danielgollas/JefeCheck/<version>/`.
3. Open a PR against `microsoft/winget-pkgs:master`.

**Automated submission (recommended once first version is accepted):**
Add the [`vedantmgoyal2009/winget-releaser`](https://github.com/vedantmgoyal2009/winget-releaser) action to `release.yml`. It needs:
- A fork of `microsoft/winget-pkgs` on your account.
- A classic PAT (`WINGET_TOKEN` secret) with `repo`, `workflow` scopes.

Then on every release tag, it auto-PRs the new manifest to upstream.

## How the auto-update workflow works

`.github/workflows/update-manifests.yml` triggers on `release.published`. It:

1. Downloads `jefecheck-macos-arm64.dmg` and `jefecheck-windows-x64.zip` from the release.
2. Computes SHA256 of each.
3. Rewrites `Casks/jefecheck.rb` with new version + DMG SHA.
4. Creates a new `packaging/winget/manifests/d/danielgollas/JefeCheck/<version>/` directory with the 3 winget YAML files.
5. Opens a PR titled `chore: bump packaging manifests to v<version>`.

You merge the PR, then sync to the homebrew tap repo and submit the winget PR (manually or via automation).

## Versioning

When releasing v1.7.3 (say):
1. `git tag v1.7.3 && git push origin v1.7.3` — kicks off the build/release workflow.
2. Release workflow uploads artifacts to GitHub Releases.
3. `update-manifests.yml` fires, opens a manifest-bump PR.
4. You merge the manifest-bump PR.
5. (Manual or automated) push to homebrew tap + submit winget PR.

## Notes on macOS signing

The macOS `.app` is **ad-hoc signed** (`codesign -s -`) but **not notarized** (no Apple Developer ID). Consequences:

- Direct .dmg downloads: Gatekeeper shows "unidentified developer" warning. User bypasses via right-click → Open.
- Homebrew Cask install: quarantine xattr is stripped during install, so the app launches cleanly.
- For the cleanest UX (no warnings ever), purchase an Apple Developer ID ($99/yr) and add notarization steps to `release.yml` using `xcrun notarytool`.
