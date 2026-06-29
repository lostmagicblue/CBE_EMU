# Angry Birds CBE startup and Storage_Date

## Symptom

`CBE/愤怒的小鸟.CBE` started without an assert, but the screen stayed black. Runtime PC sampling showed the client looping in `0x010110a8..0x010110c6`, copying a record entry whose length was `0x834c`. The loop counter is sign-extended to 16 bits, so any length above `0x7fff` can become an infinite loop.

## Runtime evidence

The stuck function was the game's record loader around `0x01011018`. It calls GameUtil manager slot 37 through `VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS + 37 * 4`, passing:

- `r0`: record/storage name object
- `r1`: destination buffer
- `r2`: buffer length
- `r3`: read flag

Before the fix our slot returned `currentTime` and left the buffer unchanged. The record parser then consumed stale heap bytes that happened to begin with the package directory bytes `0B 46 0C 00 ...`, producing bogus record counts and lengths.

After implementing storage semantics, a missing save returns `0` and zero-fills the buffer. The game skips record parsing on first launch. Later writes create `nvram/..._storage_AngryBirdData.bin`, and subsequent reads return valid record data such as `ANGRYBIRD02`.

## IDA evidence

Firmware `8533n_7835.axf`:

- `vMInitGameUtilManager` at `0x5d5e3a` assigns manager slot 37 to `Storage_Date`.
- `Storage_Date` at `0x684d90` has signature equivalent to `Storage_Date(name, buffer, len, isRead)`.
- On read, the firmware clears the destination buffer, opens persistent storage, reads `len` bytes if present, and returns failure when no file/storage record exists.

## Implemented behavior

`src/main.c` now implements GameUtil slot 38 (1-based hook index for zero-based firmware slot 37) as persistent Storage_Date support:

- `isRead != 0`: zero-fill VM buffer, read from `bin/nvram`, return `1` only when storage exists.
- `isRead == 0`: write `len` bytes from VM buffer to `bin/nvram`, return `1` on full write.
- The storage key is scoped by current `LOAD_CBE_PATH` and sanitized Storage_Date name.

`src/vmFunc.c` also fixes `DF_DataPackage_LoadFromTResource` to match firmware pointer semantics:

- `DF_ReadInt(v27, &v21)` is represented by a VM temp pointer, not the integer offset value.
- `namePtr` is passed as the string pointer value to `LocateDataPackage` and `GetPackageID`, not the address of the local/VM slot that stores it.

## Validation

From `bin`:

- `main.exe --autotest --shot-ms=1000 --max-ms=12000` reaches the Angry Birds menu.
- `main.exe --autotest --shot-ms=1000 --max-ms=16000 --actions=2500:tap:125:205` reaches the tutorial screen.

Last observed tutorial screenshot: `bin/autotest/screens/000015_00015036.bmp`.
