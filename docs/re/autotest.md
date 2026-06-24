# Autotest Helper

The emulator supports a small SDL-side automation helper for repeatable smoke tests.

Run from `bin` so game assets resolve:

```powershell
.\main.exe --autotest --shot-ms=1000 --actions=5000:key:f,17000:key:f,19000:key:q
```

## Options

- `--autotest`: enable screenshots and scripted input.
- `--shot-ms=N`: save one BMP every `N` milliseconds. Minimum is `100`.
- `--actions=...`: comma or semicolon separated action list.

## Action Format

Tap:

```text
time_ms:tap:x:y
```

Key:

```text
time_ms:key:f
time_ms:key:q
time_ms:key:enter
time_ms:key:esc
```

Useful key mapping:

- `f`: OK
- `q`: left soft key
- `e`: right soft key
- `w/s/a/d`: d-pad

## Output

- Screenshots: `bin/autotest/screens/*.bmp`
- Test state summary: `bin/autotest/state.txt`

The state file is only produced when `--autotest` or `CBE_AUTOTEST` is enabled. It is a test artifact and must not be treated as protocol evidence; protocol conclusions belong in `docs/re/`.

