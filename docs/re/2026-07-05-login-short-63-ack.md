# Login Short 0x63/1 Ack

## Runtime Symptom

After the screen-manager implementation was improved, entering the login screen
showed a brief unpack-error popup while the server logged:

```text
net_send connect=2 wt=99/1 len=9 source=builtin-short-63-1-echo resp=9
```

## Request Signature

- WT kind/subtype: `99/1` (`0x63/1`)
- Length: `9`
- Bytes shape: one empty request-side object.

## IDA Evidence

- `JianghuOL.CBE:0x01012E4D` (`net_business_dispatch_by_subcmd`) calls
  `event_packet_init(...)` before dispatching business objects. If parsing
  fails, it calls `ui_show_message_box(&gbk_msg_unpack_error, ...)`.
- `JianghuOL.CBE:0x0103467A` (`event_packet_parse_WT`) accepts:
  - `WT`
  - big-endian total length
  - object count
  - zero or more normal response objects.
- The main business switch has no `0x63` response case.

## Negative Evidence

The previous handler echoed the request bytes as the response. That echoed
packet is not a valid response-object layout for `event_packet_parse_WT`, so the
parser fails before business dispatch and the client displays the unpack-error
popup.

## Server Contract

Respond to `0x63/1` with a legal empty WT ack:

```text
57 54 00 05 00
```

This keeps the network event lifecycle intact without feeding the client an
unsupported or malformed business object.

Implementation source:

- `src/mock-server.c`
- handled as `builtin-short-63-1-empty-ack`
