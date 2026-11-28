/* Bugcheck code -> friendly name table, reverse engineered from the
 * ntoskrnl.exe RT_MESSAGETABLE (Type 11, ID 1).
 *
 * KeGetBugMessageText walks KiBugCodeMessages range blocks; a miss means the
 * screen falls back to the raw code formatted as "0x%08x" (verified in
 * KiDisplayBlueScreen / BgpFwDisplayBugCheckScreen).
 */
#ifndef BSOD_BUGCODES_H
#define BSOD_BUGCODES_H

#include <stdint.h>

typedef struct {
    uint32_t code;
    const char *name;
} BugCode;

extern const BugCode g_bugcodes[];
extern const int g_bugcode_count;

/* Returns the friendly name, or NULL when the code is not in the table. */
const char *bugcode_lookup(uint32_t code);

#endif /* BSOD_BUGCODES_H */
