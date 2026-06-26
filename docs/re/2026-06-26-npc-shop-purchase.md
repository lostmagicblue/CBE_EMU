# NPC Shop Purchase Phase

Date: 2026-06-26

Status: implemented for shop open, DSH-backed catalog paging, shop-friendly catalog ordering, host DSH lookup compatibility, and buy-result packets; needs end-to-end NPC click validation

## 1. Current Block

- Visible symptom: after NPC dialog purchase flow, clicking the dialog `购买` button opens the shop, but the list either stayed on one `传送石` row or became selectable blank rows with no item names.
- Minimum target for this pass:
  - answer the shop-open `1/1/14(actorId)` request with the response objects consumed by `mmShopMstarWqvga.cbm`;
  - expose the local `item.dsh` and `equip.dsh` rows through the mmShop item-page format instead of one hard-coded row;
  - support the confirmed shop purchase result packet so the shop module does not receive an echo/empty response for a state-changing buy request.

## 2. IDA Evidence

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmShopMstarWqvga.cbm` | `sub_11F0` / `0x11F0` | shop-open request builder | Builds `WT 1/1/14(actorId)` when the NPC dialog opens the buy branch. |
| `mmShopMstarWqvga.cbm` | `sub_1038` / `0x1038` | shop init | Registers `sub_9DE` as the network parser, zeros the two page-state structs at `r9+10792`, then immediately sends `sub_618(0,5)` and `sub_618(1,6)`. |
| `mmShopMstarWqvga.cbm` | `sub_9DE` / `0x9DE` | shop-open/network parser | Iterates response objects. For kind `14`: subtype `14` reads `result` and `shopinfo`; subtype `4` reads `coolmoney` and `ticket`; subtype `5..13` calls the item-page parser; subtype `3` reads `seq` and `result` for buy completion. For kind `1` subtype `14`, it reads revive/ruffian/type fields, so echoing the request is the wrong contract for opening a shop. |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | shop item page parser | Reads `totalnum`, then raw `iteminfo`: row count, item id, item name, small flags, price, stock/count, another flag, and the shared item extra block. |
| `mmShopMstarWqvga.cbm` | `sub_618` / `0x618` | shop page request builder | Builds `WT 1/14/<subtype>` with an `index` byte. This is the paging path for subtypes `5..13`; subtype `5` is the buy catalog used by this mock. |
| `mmGameMstarWqvga.cbm` | `sub_418C` / `0x418C` | shop/backpack item-list parser | The `17/1` branch opens `item.dsh` and `equip.dsh`, reads raw `iteminfo` as `row_count` followed by `itemId + common item-extra`, allocates `324 * row_count`, sorts rows by item id, and fills display data from local DSH rows. If the DSH open/lookup fails, the UI still has selectable rows but names/prices remain blank. |
| `江湖OL.CBE` | `ParseEquipAttributes` / `0x010185C2` | shared item-extra parser | The vtable `+2452` helper first reads two i16-style fields, then one u8 `attr_count`; it only reads attr slots while `slot_index < attr_count`. The six arrays passed by callers are destination capacity, not fields that must always be sent. |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | shop page cursor and row parser | `sub_7BC` starts writing rows at the page-state start cursor (`[page+6]`) and advances it by `row_count`. The `14/5` row tail calls `ParseEquipAttributes`, so zero-attribute rows must stop after `i16, i16, u8 attr_count=0`. |
| host file/DF I/O | `src/vmFunc.c` | local DSH lookup compatibility | `mmGame:0x418C` asks for bare `item.dsh/equip.dsh`. The files are present under `JHOnlineData/`, so read-only host file open and DF filename lookup now resolve bare `.dsh` names to `JHOnlineData/<name>` / matching basename. |
| runtime dispatch order | `src/mock-server.c` | NPC dialog buy combo | The NPC dialog buy path can combine the shop object family with scene/dialog follow-up objects such as `2/10` or `27/4`. A broad `27/4` handler must not run before the shop-family handler, or the shop page remains stale. |
| runtime packet trace | `WT 17/2` | shop buy request | Buy confirmation emits a single `1/17/2` object with `shopId`; the response consumed by the active shop parser is not an echo of this object. |
| `mmShopMstarWqvga.cbm` | `0xC2C` string | buy-result field name | The string referenced by the subtype-3 branch is `seq`. |
| `江湖OL.CBE` | `sub_1033544` / `0x01033544` | main item business parser comparison | For acquire subtype `7/8 type=4`, the parser reads field `seq` and writes it into item `+276`; this confirms the field name and typed value used by the item insertion contract. |
| `mmGameMstarWqvga.cbm` | `sub_418C` / `0x418C` | backpack/item sync parser comparison | The already implemented backpack-open response pairs `17/1 iteminfo` with `7/42 booksinfo`. Runtime showed the NPC purchase flow can emit a combined `17/1 + 7/42` request before final buy confirmation, so answer it with the same parser-safe pair. |

## 3. Request / Response Contract

### Shop Open Request

- WT object: `1/1/14`
- Required detector fields:
  - `actorId` as u32

### Shop Open Response

- WT objects:
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=999999, ticket=0 }`
- Catalog pages are not bundled into the open response. `mmShop:0x1038` clears page state and immediately calls `sub_618(0,5)` / `sub_618(1,6)`, so page rows are answered only through the request-driven `1/14/<subtype>(index)` path.
- Catalog page size: `10` rows per page response.

### Shop Catalog Source

- `item.dsh`: 230 rows, parsed from `ID`, `名称`, `形象`, `价值`, and `堆叠数`.
- `equip.dsh`: 1485 rows, parsed from `ID`, `名称`, and `价值`.
- Current max catalog capacity: `2048` rows; the local resources load the full `item.dsh` plus the first bounded slice of `equip.dsh`.
- Runtime DSH visibility matters twice:
  - mock-server loads `JHOnlineData/item.dsh` and `JHOnlineData/equip.dsh` to build `14/5` rows with server-provided names;
  - `mmGame:0x418C` independently opens bare `item.dsh/equip.dsh` while rendering the `17/1` list, so host I/O must resolve those bare names to the same local resources.
- The in-memory shop order is intentionally not raw DSH order:
  - equipment ids `>=1000` first, because the current NPC purchase path is the equipment shop and `mmGame:0x418C` has a distinct `equip.dsh` display-fill branch;
  - `800..999` item ids next, for shop-facing consumables such as `传送石`, `复活石`, experience cards, packs, and crystals;
  - lower material/task ids last.
- Shop row names are capped at 12 bytes to match `sub_7BC`'s 13-byte destination including the NUL terminator.
- Fallback, only if both DSH files are unavailable: one `Teleport Stone` row.

### Shop Page Request

- WT object: `1/14/<subtype>`
- Supported fields:
  - `index` as u8 or u32
- Response:
  - subtype `5`: `1/14/5 { totalnum=<catalog count>, iteminfo=<catalog page index rows> }`
  - subtypes `6..13`: empty page with `totalnum=0` and one raw row-count byte `0`

### Shop Info Refresh Request

- WT objects:
  - `1/14/14`
  - `1/14/4`
- IDA evidence:
  - `mmShop:0x1038` calls `sub_6D6()` and `sub_6BC()` before page requests;
  - those build the same kind-14 status/money requests that `sub_9DE` parses.
- Response:
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=999999, ticket=0 }`

### NPC Dialog Buy + Shop Combo Request

- Core WT shape: contains the shop-open family and may include `14/14 + 14/4 + 14/5 + 14/6` markers from the dialog path.
- Optional companion objects seen or supported on the NPC dialog path:
  - `2/10` actor-other follow-up;
  - `27/11` / `27/4` fb-target dialog follow-up;
  - `7/42` book info.
- Previous handling was too narrow (`requestLen == 61`) and was placed after the broad `27/4` handler. If the NPC dialog buy packet carried `27/4` or had a different length, it could be answered without the shop item page, leaving the UI at the previous single `800` row.
- Response now returns:
  - requested safe scene/dialog follow-up objects (`2/10`, `27/11`, `27/4`, `7/42`) when present;
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=999999, ticket=0 }`
  - `1/14/5 { totalnum=<catalog count>, iteminfo=<catalog page index rows> }`
  - `1/14/6 { totalnum=0, iteminfo=<row_count 0> }`
- If `14/5/14/6` are batched in the same request, do not drop them. `mmShop:0x9DE` uses one local response-object counter and clears loading after the full four-object family is parsed.

### Purchase Request

- WT object: `1/17/2`
- Required detector fields:
  - `shopId` as u32
- Optional observed / inferred fields:
  - item id
  - count / `num`

### Purchase Response

- WT object: `1/14/3`
- Fields:
  - `seq = 2`
  - `result = 1`

`seq=2` is intentionally distinct from the default backpack item `传送石` at `seq=1`.

### NPC Shop 17/1 List Combo Request

- Runtime failure:
  - `[error][network] unhandled wt=17/1 len=25 objects=1 first=1/17/1:11,1/7/42:0`
- Narrow detector:
  - request length `25`;
  - first object `1/17/1` with payload length `11`;
  - second object `1/7/42` with empty payload.
- Response:
  - answer the `mmGame:0x418C` visible list parser, not the backpack default list:
    - `1/17/1 { iteminfo=<shop item id list> }`
    - `1/7/42 { booknum=0, booksinfo=<empty> }`
- `iteminfo` rows:
  - one `u8 row_count`, currently capped at `10` for parser safety;
  - each row is `u32 itemId` plus the shared common item-extra block:
    `i16 stack/runtime`, `i16 reserved`, `u8 attr_count`;
  - with `attr_count=0`, no attr slots follow;
  - low material/task-drop ids are omitted;
  - equipment ids are emitted before `800..999` consumables because this NPC path is the equipment shop UI and `mmGame:0x418C` has a distinct `equip.dsh` branch for ids `>=1000`.
- Shop context fallback:
  - after a shop open/page/interaction response, set a one-shot shop-list pending flag;
  - if the next request is empty `1/17/1`, return the same shop `17/1` item list instead of the backpack default;
  - if the next request is empty `1/7/42`, return shop `17/1` item list plus empty `7/42` books.

## 4. Implementation

- Replaced the old `1/1/14(actorId)` echo with `vm_net_mock_build_shop_actor_query14_response()`:
  - returns only the `14/14 + 14/4` shop-open object family;
  - leaves catalog rows to the client-driven `1/14/<subtype>(index)` requests emitted by `mmShop:0x1038/sub_618`;
  - logs `mock_shop_open14 ... pages=request-driven evidence=mmShop:0x11F0/0x1038/0x618/0x9DE`.
- Added a lazy DSH catalog loader for `item.dsh` and `equip.dsh`.
- Added host-side `.dsh` lookup compatibility in `src/vmFunc.c`:
  - ordinary read-only file open for bare `item.dsh/equip.dsh` tries `JHOnlineData/<name>`;
  - `vm_file_exists` uses the same fallback;
  - DF resource-name lookup performs a `.dsh`-only basename fallback after exact matching fails.
- Added catalog ordering so the first buy page starts with equipment rows instead
  of raw material rows like `木材` or shop-special consumables that do not match
  this NPC's equipment-shop path.
- Capped row names to the mmShop display buffer size to avoid later long names
  overwriting the fixed 64-byte row slots parsed by `sub_7BC`.
- DSH file lookup now also tries paths relative to the running `main.exe`
  directory and its parent so launching from a different working directory does
  not silently fall back to one Teleport Stone.
- Runtime console evidence:
  - success: `[info][network] mock_shop_catalog total=... items=... equips=... first=... source=item.dsh/equip.dsh`
  - fallback: `[warn][network] mock_shop_catalog fallback=item.dsh/equip.dsh-not-found item=800`
- Added `vm_net_mock_build_shop_iteminfo_page_blob()` and
  `vm_net_mock_append_shop_catalog_page_object()` for the mmShop item-page rows.
- Confirmed the shared item-extra encoding for both visible item-list paths:
  - `17/1` and `14/5` both call `JianghuOL:0x010185C2`;
  - the wire layout is `i16, i16, u8 attr_count`, followed by exactly
    `attr_count` slots of `u8, u8, u8, i16`;
  - for the current zero-attribute shop rows, the correct tail is just the
    two i16 fields plus `attr_count=0`.
- Added `vm_net_mock_is_shop_page14_request()` and
  `vm_net_mock_build_shop_page14_response()`:
  - responds to `1/14/5(index)` with the requested catalog page;
  - returns empty pages for other shop list subtypes until their business meaning is needed;
  - dispatch source: `builtin-shop-page14`;
  - logs `mock_shop_page14 ... evidence=mmShop:0x618/0x7BC`.
- Added `vm_net_mock_is_shop_info14_request()` and
  `vm_net_mock_build_shop_info14_response()`:
  - answers standalone `1/14/14` and `1/14/4` init refresh requests;
  - dispatch source: `builtin-shop-info14`;
  - logs `mock_shop_info14 ... evidence=mmShop:0x6D6/0x6BC/0x9DE`.
- Updated `vm_net_mock_build_scene_interaction_followup_response()`:
  - detector now matches the shop object family instead of a fixed packet length;
  - it preserves requested scene/dialog follow-up objects before appending the full mmShop `14/14 + 14/4 + 14/5 + 14/6` family;
  - dispatch is before the broad `27/4` follow-up handler;
  - logs `mock_shop_scene_interaction_combo ... page5/page6 evidence=runtime:npc-buy-shop-family mmShop:0x1038/0x618/0x9DE`.
- Added `vm_net_mock_is_backpack_items_books_combo_request()` and
  `vm_net_mock_build_shop_items_books_combo_response()`:
  - matches the two-object `17/1 + 7/42` combo by object structure instead of
    hard-coding total packet length;
  - dispatch source: `builtin-shop-items-books-combo`;
  - returns the `mmGame:0x418C` `17/1` visible list instead of the normal backpack's single `传送石`;
  - logs `mock_shop_items_books_combo ... ids=... evidence=mmGame:0x418C runtime=npc-buy-wt17/1-len25`.
- Updated the `17/1` visible list order after the blank-row negative:
  - previous first rows were `800..809` from `item.dsh`;
  - current first rows are equipment ids such as `1001`, so the parser takes the `equip.dsh` display-fill branch used by this equipment NPC.
- Updated the `14/5` buy-page catalog order to match the same equipment-first
  sequence after runtime showed only one later `木制宽剑` row among otherwise
  blank rows. This keeps the mmShop page parser and mmGame item sync parser on
  the same first-page item family.
- Negative runtime after the previous fixed-slot row-extra build produced
  selectable blank rows with `木制宽剑` only in the fifth visible slot. The
  root cause is row-tail misalignment: the client sorted zero/garbage ids ahead
  of the first valid equipment id. The current implementation sends
  `attr_count=0` and no empty slots, so each following itemId starts at the
  parser's expected cursor.
- Added read-only autotest probes for the blank-row symptom:
  - `shop_parser rows/item/name/done` follows `mmShop:0x7BC` by current module base and prints `row_count`, `item_id`, copied `name_hex`, and final buy/sell first rows;
  - `backpack_parser dsh_lookup` follows `mmGame:0x418C` at runtime `0x05184498/0x051844DA` and prints `item/equip` lookup result per row;
  - `backpack_parser commit` now includes first-row `name_hex`, `price`, and `flag278`.
- Added one-shot shop-context handlers:
  - `builtin-shop-items17` for empty `17/1`;
  - `builtin-shop-items-books` for empty `7/42`;
  - both run before the normal backpack handlers.
- Moved all shop-specific dispatch entries to the front of
  `vm_net_mock_build_response()` after update chunks and before rule/scene/battle
  fallbacks:
  - `builtin-scene-interaction-followup`;
  - `builtin-shop-actor-query14`;
  - `builtin-shop-info14`;
  - `builtin-shop-page14`;
  - `builtin-shop-buy17`;
  - `builtin-shop-items-books-combo`;
  - one-shot `builtin-shop-items-books` / `builtin-shop-items17`.
- Added normal `[info][network]` preview logs for the current shop page and
  `17/1` list. The first page should report ids beginning with
  `1001,1002,1003,1004,...` for this equipment NPC.
- Added `VM_NET_MOCK_SHOP_PURCHASE_ITEM_SEQ`.
- Added `vm_net_mock_is_shop_buy17_request()`:
  - matches only a single `1/17/2` object;
  - requires `shopId`;
  - records `num` when present for trace context.
- Added `vm_net_mock_build_shop_buy17_response()`:
  - answers runtime request `1/17/2(shopId)` with parser-side result `1/14/3 { seq=2, result=1 }`;
  - logs `mock_shop_buy17 ... resp=14/3 evidence=mmShop:0x9DE(seq/result)`.
- Dispatch source: `builtin-shop-buy17`.

## 5. Negative Evidence / Guardrails

- Do not echo the shop-open request back. `1/1/14(actorId)` is request-side; the mmShop open parser expects kind-14 response objects.
- Do not echo the purchase request back. The request carries client-side fields (`shopId`, item id, count); the parser expects kind `14` subtype `3` and will read `seq/result` from that response.
- Do not route `14/*` through the main CBE business dispatcher. The main dispatcher has no kind-14 case; these objects are for `mmShopMstarWqvga.cbm`.
- Do not restore raw DSH ordering or consumable-first ordering for this equipment
  NPC buy page. The raw first rows are material/task items, and the
  consumable-first page produced mostly blank rows while a later `1001`
  `木制宽剑` row rendered.
- Do not pack the whole catalog into one `iteminfo` object. WT packet and object lengths are 16-bit, and `mmShop` already has a page request path; keep the catalog paged.
- Do not send fixed empty attr slots when `attr_count=0`. IDA shows
  `ParseEquipAttributes(0x010185C2)` reads only as many slots as the count byte
  declares; unconditional empty slots shift the next row's itemId and create
  blank shop rows.
- Do not drop `14/5` or `14/6` when they are already present in the same batched
  shop-family request. The no-bundling rule applies only to the initial
  `1/1/14(actorId)` open response; batched `14/14+14/4+14/5+14/6` must be
  answered as a complete family for `mmShop:0x9DE`.
- If the UI still shows only `传送石`, first check whether the process was
  restarted and whether the success/fallback catalog log above was printed.
  A fallback log means the issue is host resource lookup, not mmShop parsing.
- If the UI shows selectable blank rows, first check `mmGame:0x418C` DSH lookup:
  row count and IDs parsed, but local DSH display fill did not run or did not find
  the requested item/equip row.
- Also check `mmShop:0x7BC` first. If `shop_parser name_hex` is nonzero but the
  UI is blank, the problem is in shop rendering/active page selection; if
  `name_hex` is all zero while `item_id` is valid, the server-side `14/5`
  string stream format is wrong for this parser.
- Do not answer the NPC dialog buy combo through the broad `27/4` handler before
  checking for the shop object family. That leaves the `14/5` shop item page
  stale and can keep the visible list at the previous single `800` row.
- Do not move shop handlers behind rule/scene/battle/default backpack fallbacks.
  The NPC buy path overlaps with generic scene follow-up and backpack request
  shapes, so the specific shop contracts must stay first.
- Do not broaden all `17/1` requests. The shop-list packet has a distinct two-object shape and a nonempty `17/1` payload; plain empty `17/1` remains the backpack default list except for the one-shot shop-context flag.
- Do not answer the NPC shop `17/1 + 7/42` combo with the normal backpack-open response. That response intentionally contains only the default `传送石` stack, which exactly reproduces the one-row shop symptom.
- Do not send the full item+equip catalog through one `17/1` raw field. Runtime after the first `17/1` implementation showed a packet-parse error on shop entry; keep this path page-sized because `mmGame:0x418C` copies `iteminfo` into a 1024-byte stream buffer.
- Do not use `25/6` as an initial backpack or purchase shortcut. Prior backpack work showed `25/6` is shop/acquire-style and not a capacity setup path.
- Do not write the main item manager directly. `mmShop:0x9DE` already reaches the normal item insert flow after parsing subtype `14/3` `seq + result`.

## 6. Validation

- `make`: passed after the `ParseEquipAttributes` attr-count fix.
- `git diff --check`: no whitespace errors; only existing LF/CRLF warnings.
- Smoke run from `bin`: `.\main.exe --autotest --shot-ms=5000 --max-ms=12000` exited through `autotest_exit` with no assert. This path does not reach NPC shop.
- Latest dispatch pass: shop handlers now run before rule/scene/battle/default
  fallbacks, and `17/1+7/42` detection is structure-based. Server logs now print
  the emitted id preview for both `14/5` and `17/1` shop lists.
- Latest blank-row pass: `ParseEquipAttributes(0x010185C2)` shows the row tail is count-based, not fixed-slot. The server now writes `i16, i16, u8 attr_count=0` for both `17/1` and `14/5`, which prevents stale empty attr bytes from being parsed as later item ids.
- Added parser probes after the latest blank-row report; the next useful
  validation artifact is a manual or scripted NPC-shop entry with
  `mock_shop_items_books_combo ... iteminfo_len=173` and
  `backpack_parser ... item0=1001 name_hex=...` in `bin/autotest/state.txt`.
- End-to-end NPC click validation is still pending because the current default server path does not force NPC seeding; next probe should start near `00蓬莱仙岛_04.sce` 药师 at `(166,280)` and capture the exact packets from dialog purchase to shop buy.
