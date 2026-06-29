# 血剑 Online IMG DataPage

## Current target

Run `CBE/血剑Online.CBE` without hitting the early platform API asserts.

## Runtime evidence

- Initial run stopped at `src/main.c:6842`, LCD manager `idx=45`, `IMG_InitDataPage`.
- After adding `IMG_InitDataPage`, the next run stopped in CBE code at `pc=0x0101200e` while reading address `0x38`.
- Local Thumb disassembly at `ROM+0x1200e` showed the client calling a manager getter, then reading `[r0+0x38]`; `r0=0` meant the data package pointer was not visible through `DF_GetDataPackage`.

## IDA evidence

Firmware instance: `8533n_7835.axf`.

- `IMG_getappDataPackage` `0x5C97B4`: allocates and clears a 108-byte app data package.
- `IMG_InitDataPageEx` `0x5C9814`: if `*(u16 *)(dp+8)==0`, calls `initDFDataPackage(dp, 5)`, then `dp->DoLoading(dp, a1, a2)` at `dp+48`; otherwise returns the count.
- `IMG_InitDataPage` `0x5C983E`: calls `IMG_getappDataPackage()` then `IMG_InitDataPageEx(a1, a2, dp)`.
- `IMG_CreateImageFormIdEx` `0x5C999C`: checks `id < *(u16 *)(dp+8)`, resolves resource data through `dp+84/+16/+24`, then calls `IMG_CreateImageFormStream(data, out)`.
- `IMG_InitDataPageTxt` `0x5C9854`: chooses app/inner data package and calls `DF_DataPackage_InitTxt`.
- `vMGetImageWidth` `0x531478` and `vMGetImageHeight` `0x53147C`: return image header offsets `+4` and `+6`.
- `vMDestoryImage` `0x531480`: calls `IMG_Destory` only when the image pointer and `*image` are nonzero.
- `DF_DataPackage_DoLoading` `0x5CA4F0`: always falls back to `DF_DataPackage_LoadFromTResource(dp, a2)` unless `a3` selects the TCard path.

## Implementation notes

- Added host-side app/inner/current IMG data package slots in `src/vmFunc.c`.
- `IMG_InitDataPageEx` now initializes a 5-slot DataPackage and syncs it to `VM_DreamFactory_DataPackage_ADDRESS`, because this CBE immediately uses `DF_GetDataPackage()->GetFile`.
- `IMG_CreateImageFormIdEx` and `IMG_CreateImageFormResForVm` reuse the existing `IMG_CreateImageFormStream` decoder path.
- Fixed the local `VM_DF_DataPackage_DoLoading` fallthrough so it matches the firmware function.

## Validation

- `make` passed.
- `bin/main.exe --autotest --shot-ms=1000 --max-ms=8000` exited normally.
- `bin/main.exe --autotest --shot-ms=5000 --max-ms=20000` exited normally.
- `bin/main.exe --autotest --shot-ms=10000 --max-ms=60000` exited normally.

The current visible output is still black, so the next investigation should focus on resource lookup progress and the draw/image creation paths, not this early DataPackage assert.
