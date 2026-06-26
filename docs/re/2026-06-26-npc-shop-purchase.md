# NPC Shop Purchase Phase

Date: 2026-06-26

Status: implemented for shop open and buy-result packets; needs end-to-end NPC click validation

## 1. Current Block

- Visible symptom: after NPC dialog purchase flow, clicking buy/purchase can crash the client.
- Minimum target for this pass:
  - answer the shop-open `1/1/14(actorId)` request with the response objects consumed by `mmShopMstarWqvga.cbm`;
  - support the confirmed shop purchase result packet so the shop module does not receive an echo/empty response for a state-changing buy request.

## 2. IDA Evidence

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmShopMstarWqvga.cbm` | `sub_11F0` / `0x11F0` | shop-open request builder | Builds `WT 1/1/14(actorId)` when the NPC dialog opens the buy branch. |
| `mmShopMstarWqvga.cbm` | `sub_9DE` / `0x9DE` | shop-open/network parser | Iterates response objects. For kind `14`: subtype `14` reads `result` and `shopinfo`; subtype `4` reads `coolmoney` and `ticket`; subtype `5..13` calls the item-page parser. For kind `1` subtype `14`, it reads revive/ruffian/type fields, so echoing the request is the wrong contract for opening a shop. |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | shop item page parser | Reads `totalnum`, then raw `iteminfo`: row count, item id, item name, small flags, price, stock/count, another flag, and the shared item extra block. |
| `mmShopMstarWqvga.cbm` | `sub_24E6` / `0x24E6` | shop input / confirmation handler | On buy confirmation, builds a WT request with kind/subtype `17/2`; it writes current item id, requested count, and `shopId`. |
| `mmShopMstarWqvga.cbm` | `sub_418C` / `0x418C` | shop network parser | For `17/1`, reads raw `iteminfo` into the shop item list. For `17/2`, reads `result`; when `result == 1`, it reads an i16 sequence and writes it to the purchased item at offset `+276`, then calls the normal item-manager insert path and shows the success message. |
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
  - `1/14/5 { totalnum=1, iteminfo=<one Teleport Stone row> }`
  - `1/14/6 { totalnum=0 }`
- Default shop row:
  - item id `800`
  - display name `Teleport Stone`
  - price `1`
  - stock `99`

### Purchase Request

- WT object: `1/17/2`
- Required detector fields:
  - `shopId` as u32
- Optional observed / inferred fields:
  - item id
  - count / `num`

### Purchase Response

- WT object: `1/17/2`
- Fields:
  - `result = 1`
  - `seq = 2`

`seq=2` is intentionally distinct from the default backpack item `传送石` at `seq=1`.

### Inventory Sync Combo Request

- Runtime failure:
  - `[error][network] unhandled wt=17/1 len=25 objects=1 first=1/17/1:11,1/7/42:0`
- Narrow detector:
  - request length `25`;
  - first object `1/17/1` with payload length `11`;
  - second object `1/7/42` with empty payload.
- Response:
  - reuse the parser-safe backpack-open response:
    - `1/17/1 { maxnum=40, iteminfo=<default backpack item list> }`
    - `1/7/42 { booknum=0, booksinfo=<empty> }`

## 4. Implementation

- Replaced the old `1/1/14(actorId)` echo with `vm_net_mock_build_shop_actor_query14_response()`:
  - returns the `14/14 + 14/4 + 14/5 + 14/6` shop-open object family;
  - exposes one buyable Teleport Stone row;
  - logs `mock_shop_open14 ... evidence=mmShop:0x11F0/0x9DE/0x7BC`.
- Added `vm_net_mock_build_shop_iteminfo_blob()` for the mmShop item-page row.
- Added `vm_net_mock_is_backpack_items_books_combo_request()` and
  `vm_net_mock_build_backpack_items_books_combo_response()`:
  - matches only the observed `WT len=25` `17/1 + 7/42` combo;
  - dispatch source: `builtin-backpack-items-books-combo`;
  - logs `mock_backpack_items_books_combo ... evidence=mmGame:0x418C runtime=wt17/1-len25`.
- Added `VM_NET_MOCK_SHOP_PURCHASE_ITEM_SEQ`.
- Added `vm_net_mock_is_shop_buy17_request()`:
  - matches only a single `1/17/2` object;
  - requires `shopId`;
  - records `num` when present for trace context.
- Added `vm_net_mock_build_shop_buy17_response()`:
  - returns `1/17/2 { result=1, seq=2 }`;
  - logs `mock_shop_buy17 ... evidence=mmShop:0x24E6/0x418C`.
- Dispatch source: `builtin-shop-buy17`.

## 5. Negative Evidence / Guardrails

- Do not echo the shop-open request back. `1/1/14(actorId)` is request-side; the mmShop open parser expects kind-14 response objects.
- Do not echo the purchase request back. The request carries client-side fields (`shopId`, item id, count); the parser expects a result packet and will read result/seq from the response.
- Do not route `14/*` through the main CBE business dispatcher. The main dispatcher has no kind-14 case; these objects are for `mmShopMstarWqvga.cbm`.
- Do not broaden all `17/1` requests. The crash packet has a distinct two-object shape and a nonempty `17/1` payload; unsupported shapes should still reach unhandled until their parser contract is known.
- Do not use `25/6` as an initial backpack or purchase shortcut. Prior backpack work showed `25/6` is shop/acquire-style and not a capacity setup path.
- Do not write the main item manager directly. `mmShop:0x418C` already calls the item-manager insert callback after parsing `result + seq`.

## 6. Validation

- `make`: passed.
- `git diff --check`: no whitespace errors; only existing LF/CRLF warnings.
- End-to-end NPC click validation is still pending because the current default server path does not force NPC seeding; next probe should start near `00蓬莱仙岛_04.sce` 药师 at `(166,280)` and capture the exact packets from dialog purchase to shop buy.
