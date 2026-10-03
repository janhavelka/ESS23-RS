# Renaming the repository and local folder

Use **MotorControl-RS** for GitHub and, preferably, the local folder. Public C++
names remain `MotorControlRS`, including includes and the CMake target. The
folder name is not part of the API.

## Preparation status

Prepared on 2026-10-03:

- `library.json` and the E2 board metadata point to
  `https://github.com/janhavelka/MotorControl-RS` (the library URL adds `.git`).
- Source, CMake, PlatformIO configuration and reference generators resolve
  local files relative to the project or script. No source rename is needed.
- Current guidance no longer requires the checkout to be called `ESS23-RS`.
- FieldCore's current tracked build/source/configuration contains no dependency
  on this checkout name. It remains untouched. Local documentation links to
  sibling projects still work when this folder stays under `Projects`.
- Original ESS model names, vendor filenames, hashes and historical evidence
  stay as recorded. They are not repository-name occurrences to replace.

At preparation, GitHub still reported `janhavelka/ESS23-RS`; the new URL did
not exist. The tracked URLs are deliberately prepared for the rename, not a
claim that it already happened. This clone's working `origin` is left at the
existing endpoint so preparation can be committed and pushed.

## GitHub and folder steps

1. On GitHub, open this repository's **Settings > General**, change the
   repository name to **MotorControl-RS**, and select **Rename**.
2. Stop builds/debugging/serial monitors and close the VS Code window and
   terminals using the old folder. In Explorer rename
   `Projects\ESS23-RS` to `Projects\MotorControl-RS`, then reopen that folder.
   Keep its hidden `.git` directory; do not initialize another repository.
3. In a terminal at the renamed project root, update this clone's remote:

```powershell
git remote set-url origin https://github.com/janhavelka/MotorControl-RS.git
git fetch origin
git status -sb
```

GitHub redirects the old repository's web and Git operations after a rename,
but recommends updating existing clones. Do not reuse the old repository name
for a different repository if those redirects are needed.
[GitHub rename documentation](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository).

## Refresh generated paths once

The source is portable; previously generated files are not. The existing
`.pio/libdeps/*/MotorControl-RS.pio-link`, CMake caches and VS Code generated
configuration contain the old absolute path. Refresh these after moving,
before the next build. Renaming caches keeps the old firmware artifacts.

From the **renamed project root**, before starting builds, run these one-time
commands for the existing local caches:

```powershell
if (Test-Path -LiteralPath '.pio') {
    Rename-Item -LiteralPath '.pio' -NewName '.pio-before-rename' -ErrorAction Stop
}
if (Test-Path -LiteralPath 'build/native') {
    Rename-Item -LiteralPath 'build/native' -NewName 'native-before-rename' -ErrorAction Stop
}
```

If a backup with that name already exists, keep it and choose a new backup
name; do not overwrite it. `.pio-before-rename/` is ignored by Git. Do not
delete the whole `build/` directory: `build/bench/` contains retained bench
evidence and original firmware backups. Other old scratch CMake builds under
`build/` also need a fresh build directory if reused.

Rebuild the core and regenerate PlatformIO's VS Code configuration:

```powershell
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
.\scripts\pio.cmd project init --ide vscode
.\scripts\pio.cmd run -e e2_s3_units -e e2_s3_load_timer
```

The last commands rebuild local dependency links and the generated
`.vscode/c_cpp_properties.json` / `.vscode/launch.json` paths. They do not upload
firmware or communicate with the motor. The current generated VS Code files
have no custom edits; if you add custom debug settings before moving, save
those separately before regenerating. Close/reopen the workspace or rebuild
the IntelliSense index if the editor still displays old paths.

The E2 upload environment and COM13 do not change because of a repository
rename. Recheck the actual board/port before any later upload as usual.

## Verification performed

A separate clean source copy named `MotorControl-RS` was built outside the
original checkout on 2026-10-03. All 13 native CTest suites passed, and
`e2_s3_units` plus `e2_s3_load_timer` built successfully. Their local dependency
links resolved to the copied project. The old generated VS Code files were
copied into that fixture, then `pio project init --ide vscode` regenerated both
with the new absolute path. The working checkout and its caches were not moved
or cleared during preparation. No firmware was flashed; no motor behavior
changed. FieldCore remained read-only.
