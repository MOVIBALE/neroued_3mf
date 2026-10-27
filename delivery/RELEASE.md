Temporary binary wheels for the generic Production writer changes submitted upstream. These are downstream compatibility builds, not an official Neroued release and not a FatCat SDK version.

- Public source: commit `d4cfaae39fa77a3458d9ad079b5d1bb1eafc5522` in this fork, based on upstream `331ac6fa62a719f0dddacf9f005861b187d54f0c`.
- This revision preserves native Windows paths through directory creation, temporary files, ZIP output and final rename, including Unicode names and long output paths. Existing production.1 assets remain unchanged.
- License: the original AGPL-3.0-or-later license remains unchanged. The source archive is included.
- Platforms: CPython 3.12/3.13/3.14 on Windows x64, macOS 14+ arm64 and Linux x86_64 (glibc 2.28+).
- Each wheel was installed on its matching runner and used to write Core, per-object, merged and grouped Production examples. No automated test suite or native slicer GUI claim is made by this delivery workflow.
- `provenance.json` records the source commit and SHA256 file hashes.

Install without a C++ compiler:

```sh
python -m pip install --only-binary=:all: --find-links https://github.com/MOVIBALE/neroued_3mf/releases/expanded_assets/fatcat-production-2 'neroued-3mf==0.4.0+fatcat.production.2'
```

The FatCat SDK remains independently available as `fatcat-metadata==0.1.0` on PyPI. Replace this temporary writer dependency with an official upstream version after these APIs are accepted and released.
