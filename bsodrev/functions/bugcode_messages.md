# Stop-code friendly names — the `KiBugCodeMessages` message table

## Where the name string comes from

`BcpDisplayErrorInformation` (`0x140349380`) only draws the fixed labels
(`Stop Code:`, `What failed:`, the support text). The stop-code *string* is an
incoming argument (`a4 + 16`, a `UNICODE_STRING`) — the display routine never
looks anything up itself.

The chain that produces it:

```
KiDisplayBlueScreen            0x1402A8D68
  └─ KeGetBugMessageText(KiBugCheckData, &string, ...)   0x1402A8814
        └─ walk KiBugCodeMessages global                  0x140572638
  └─ miss: RtlStringCbPrintfExA(..., "0x%08x", KiBugCheckData)
```

`BgpFwDisplayBugCheckScreen` (`0x140349DF0`) follows the same branch: take the
returned message text when present, otherwise format the raw code as
`0x%08x`. That fallback format was verified in both callers — an unknown code
is shown lowercase, 8 hex digits, e.g. `0xdeadbeef`.

## `KeGetBugMessageText` — `0x1402A8814`

```c
/* pseudo-C, simplified */
KiMsg = KiBugCodeMessages;                 /* 0x140572638, zero-filled on disk */
count = *(u32 *)KiMsg;
p = (u8 *)KiMsg + 4;
for (k = 0; k < count; k++) {
    lo  = *(u32 *)(p + 0);
    hi  = *(u32 *)(p + 4);
    off = *(u32 *)(p + 8);
    p += 12;
    if (lo <= code && code <= hi) {
        entry = (u8 *)KiMsg + off;
        entry += (code - lo) * entry_stride;   /* packed block, see below */
        len   = *(u16 *)(entry + 0);
        flags = *(u16 *)(entry + 2);
        text  = (char *)(entry + 4);
        return text;                            /* NULL-terminated ASCII */
    }
}
return NULL;
```

## How the global is populated — `InitBootProcessor` `0x140A08698`

The static `.data` image of `KiBugCodeMessages` is zero-filled. At boot the
kernel pulls the table out of its own PE resources:

```c
ResourceInfo.Type = 11;        /* RT_MESSAGETABLE */
ResourceInfo.Name = 1;         /* resource name id 1 */
LdrFindResource_U(...);
LdrAccessResource(...);
KiBugCodeMessages = Resource;
ExAllocatePoolWithTag(...);    /* then a private copy is made */
```

So the authoritative source is the **RT_MESSAGETABLE (Type 11, ID 1)** data
item embedded in `ntoskrnl.exe`. On 10.0.18362.2274 it sits at RVA
`0xaa51e0`, size `17024` bytes (file raw `0x965be0`).

## On-disk message table layout

```
header:
  DWORD block_count
  block_count x { DWORD LoId, DWORD HighId, DWORD Offset }

string block at base + Offset:
  WORD Length        ; whole entry length, header + text
  WORD Flags
  BYTE text[Length - 4]   ; ASCII, NUL-terminated
```

The parser in [`../scripts/parse_messagetable.py`](../scripts/parse_messagetable.py)
reproduces this: PE section table → `.rsrc` directory walk (type 11 / name 1 /
first language) → range blocks → packed entries.

## What shipped

The dump contains **483** pairs. Besides real bugcheck codes it also carries
shell-saver diagnostics (`SAVER_*`), Xbox codes (`XBOX_*`), `BC_*` verifier
faults, livedump codes, and `0x40000xxx` console/version templates with
`%hs`/`%u` placeholders that never appear on a bugcheck screen.

`bsodrev/scripts/gen_bugcodes.py` filters the JSON down to the codes that can
actually be displayed on the bugcheck screen (`SAVER_*`, `XBOX_*`, placeholders
and version templates dropped) and regenerates `src/bugcodes.c`. On this build
**429** entries are kept, including `0x20001 HYPERVISOR_ERROR`.

## Verified samples

| code | name |
|---|---|
| `0x0000000A` | `IRQL_NOT_LESS_OR_EQUAL` |
| `0x00000050` | `PAGE_FAULT_IN_NONPAGED_AREA` |
| `0x0000007B` | `INACCESSIBLE_BOOT_DEVICE` |
| `0x000000EF` | `CRITICAL_PROCESS_DIED` |
| `0x00000133` | `DPC_WATCHDOG_VIOLATION` |
| `0x00000139` | `KERNEL_SECURITY_CHECK_FAILURE` |
| `0x00020001` | `HYPERVISOR_ERROR` |
| `0xDEADBEEF` | *(miss)* → rendered `0xdeadbeef` |
