# Releasing `remember`

End-to-end steps for cutting a new version and publishing it to Homebrew.

`remember` is distributed through a **Homebrew tap**:
[`itayavtalyon/homebrew-remember`](https://github.com/itayavtalyon/homebrew-remember),
installed as `brew install itayavtalyon/remember/remember`. The formula builds
the release binary **from source** (needs only `cmake`; SQLite is vendored, so
there are no other dependencies). Homebrew cannot write into `~/.claude`; the
keg ships `share/remember/SKILL.md` and `remember-install-skill` (printed as a
caveat). Users run that once per agent product.

A release is two things:

1. A **git tag + GitHub release** on this repo — GitHub serves a source tarball
   at a stable URL for that tag.
2. A **formula bump** in the tap — point its `url` at the new tag and update the
   `sha256` to that tarball's checksum.

You only release when you decide to; ordinary commits to `main` publish nothing.
Users who want bleeding-edge can `brew install --head remember` to build from
`main` with no release at all.

---

## 0. Prerequisites (once per machine)

- `gh` authenticated (`gh auth status`) with `repo` scope.
- Homebrew installed (`brew --version`).
- Push access to both `itayavtalyon/remember` and `itayavtalyon/homebrew-remember`.

## 1. Pick the version and make it authoritative

Versioning is [SemVer](https://semver.org/): `MAJOR.MINOR.PATCH`. Pre-1.0, breaking
changes bump MINOR, everything else bumps PATCH.

> **Gotcha — the version lives in the code, and the formula's test checks it.**
> `remember --version` prints the CMake project version, and the Homebrew
> `test` block asserts that output equals the release tag (`vX.Y.Z` → `X.Y.Z`).
> If the tag and the compiled version disagree, `brew test` fails. So the version
> must be bumped **in the source** and match the tag exactly.

Update **both** places to the new version (they must agree):

- [`CMakeLists.txt`](../CMakeLists.txt) line 2 — `project(remember VERSION X.Y.Z ...)`
  (authoritative; compiled in as `REMEMBER_VERSION`).
- [`src/remember_app.c`](../src/remember_app.c) — the `#define REMEMBER_VERSION "X.Y.Z"`
  fallback (used only when the build doesn't define it; keep it in sync anyway).

Optionally add a section to the README's **What's New** describing the release.

Land these on `main` through a normal PR (`just`-equivalent gate: the CI on the
PR must be green). Do **not** tag until the version bump is merged to `main`.

## 2. Cut the GitHub release

From an up-to-date `main` that contains the version bump:

```bash
git checkout main && git pull --ff-only

# Tag and push (annotated tag).
git tag -a vX.Y.Z -m "remember vX.Y.Z"
git push origin vX.Y.Z

# Create the GitHub release from that tag, with auto-generated notes.
gh release create vX.Y.Z --title "remember vX.Y.Z" --generate-notes
```

GitHub now serves the source tarball at:

```
https://github.com/itayavtalyon/remember/archive/refs/tags/vX.Y.Z.tar.gz
```

Sanity-check the tag builds and reports the right version:

```bash
curl -sL https://github.com/itayavtalyon/remember/archive/refs/tags/vX.Y.Z.tar.gz -o /tmp/remember-X.Y.Z.tar.gz
shasum -a 256 /tmp/remember-X.Y.Z.tar.gz     # note this checksum for step 3
```

## 3. Bump the formula in the tap

### The easy way (recommended)

Homebrew computes the new URL + checksum and opens a PR against the tap for you:

```bash
brew bump-formula-pr --version X.Y.Z itayavtalyon/remember/remember
```

Review and merge that PR. Done.

### The manual way (fallback)

Edit `Formula/remember.rb` in the tap repo:

```ruby
url "https://github.com/itayavtalyon/remember/archive/refs/tags/vX.Y.Z.tar.gz"
sha256 "<the shasum -a 256 value from step 2>"
```

Commit and push (or PR) to the tap's default branch.

## 4. Validate

Against the updated formula (from a clone of the tap, or the installed tap):

```bash
brew update
brew install --build-from-source remember     # or the local ./Formula/remember.rb
brew test remember                             # runs the formula's test block
brew audit --strict remember                   # style + correctness checks
remember --version                             # should print: remember X.Y.Z
```

If `brew` already has an older `remember` installed, `brew upgrade remember`
exercises the upgrade path.

## 5. Announce / wrap up

- Confirm the GitHub release notes read well (edit if needed).
- If the README's **What's New** wasn't updated in step 1, do it now.

---

## First-time tap setup (already done — reference only)

The tap repo `itayavtalyon/homebrew-remember` was created once with:

- `Formula/remember.rb` — the source-build formula (`depends_on "cmake" => :build`,
  a `cmake` configure/build/install, caveats for `remember-install-skill`, and a
  `test` block that runs `--version`, an add/list round-trip, and the skill
  installer against a fake `HOME`).
- `README.md` and an MIT `LICENSE`.

`brew install itayavtalyon/remember/remember` taps the repo automatically; there
is nothing to register with Homebrew centrally.

## Deferred: bottles (precompiled binaries)

Today the formula builds from source, so a release is just *tag + formula bump*.
**Bottles** would ship prebuilt binaries per macOS version/arch so users don't
compile — faster installs, but each release then also needs CI to build and
upload the bottles and a `bottle do` block in the formula. Worth adding once the
source-build flow is proven; until then, keep releases to the steps above.

## Deferred: auto-bump on release

A GitHub Action in this repo could run `brew bump-formula-pr` against the tap
automatically whenever a release tag is pushed, removing step 3's manual command.
Nice convenience, one more moving part — add it after a couple of manual releases.

## Quick reference

```bash
# after the version bump is merged to main:
git checkout main && git pull --ff-only
git tag -a vX.Y.Z -m "remember vX.Y.Z" && git push origin vX.Y.Z
gh release create vX.Y.Z --title "remember vX.Y.Z" --generate-notes
brew bump-formula-pr --version X.Y.Z itayavtalyon/remember/remember
# review+merge the tap PR, then:
brew update && brew upgrade remember && remember --version
remember-install-skill   # once per machine / new agent product
```
