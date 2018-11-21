#ifndef _unpack_h
#define _unpack_h

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>

#define SEC_HDR 0x00000001
#define SEC_MAP 0x00000002
#define SEC_CMD 0x00000004
#define SEC_ALL 0x00000007

typedef unsigned char byte;
typedef uint16_t word;
typedef uint32_t dword;

typedef byte hdr_t;				/* nearly complete header information available */
typedef byte cmd_t;				/* no data definition yet */
typedef byte map_t;				/* uncomplete data definition yet */

typedef struct replay_dec_s {
    int32_t             hdr_size;
    hdr_t           *hdr;
    int32_t             cmd_size;
    cmd_t           *cmd;
    int32_t             map_size;
    map_t           *map;
} replay_dec_t;

/* function prototypes */
/* int32_t replay_pack(replay_dec_t *replay, const char *path); */
void replay_unpack(replay_dec_t *replay, const char *path, int32_t sections);
int32_t unpack_section(FILE *file, byte *result, int size);

#endif /* _unpack_h */
