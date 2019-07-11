#include "unpack.h"
//#include "storm/pkware.h"

#define min(a, b)  (((a) < (b)) ? (a) : (b))

typedef struct replay_enc_s {
    byte               *src;               /* point32_ter to start of encoded section */
    int32_t             m04;                /*  */
    byte               *m08;               /* buffer */
    int32_t             m0C;                /* length after decoding */
    int32_t             m10;                /* length before decoding */
    int32_t             m14;                /* buffer length */
} replay_enc_t;

typedef struct esi_s {
    int32_t             m00;
    int32_t             m04;
    int32_t             m08;
    int32_t             m0C;
    int32_t             m10;
    int32_t             m14;
    int32_t             m18;
    int32_t             m1C;
    int32_t             m20;
    replay_enc_t       *m24;
    int32_t             m28;
    int32_t             m2C;
    byte            m30[0x1000];
    byte            m1030[0x1000];
    byte            m2030[0x0204];
    byte            m2234[0x0800];
    byte            m2A34[0x0100];
    byte            m2B34[0x0100];
    byte            m2C34[0x0100];
    byte            m2D34[0x0180];
    byte            m2EB4[0x0100];
    byte            m2FB4[0x0100];
    byte            m30B4[0x0040];
    byte            m30F4[0x0010];
    byte            m3104[0x0010];
    byte            m3114[0x0020];
} esi_t;

/*
 *  needed in unpack_rep_section
 */

byte off_507120[0x40] = {
    0x02, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};

byte off_507160[0x40] = {   /* com1 */
    0x03, 0x0D, 0x05, 0x19, 0x09, 0x11, 0x01, 0x3E, 0x1E, 0x2E, 0x0E, 0x36, 0x16, 0x26, 0x06, 0x3A,
    0x1A, 0x2A, 0x0A, 0x32, 0x12, 0x22, 0x42, 0x02, 0x7C, 0x3C, 0x5C, 0x1C, 0x6C, 0x2C, 0x4C, 0x0C,
    0x74, 0x34, 0x54, 0x14, 0x64, 0x24, 0x44, 0x04, 0x78, 0x38, 0x58, 0x18, 0x68, 0x28, 0x48, 0x08,
    0xF0, 0x70, 0xB0, 0x30, 0xD0, 0x50, 0x90, 0x10, 0xE0, 0x60, 0xA0, 0x20, 0xC0, 0x40, 0x80, 0x00,
};

byte off_5071A0[0x10] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
};

byte off_5071B0[0x20] = {
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00,
    0x08, 0x00, 0x0A, 0x00, 0x0E, 0x00, 0x16, 0x00, 0x26, 0x00, 0x46, 0x00, 0x86, 0x00, 0x06, 0x01,
};

byte off_5071D0[0x10] = {
    0x03, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07,
};

byte off_5071E0[0x10] = {   /* com1 */
    0x05, 0x03, 0x01, 0x06, 0x0A, 0x02, 0x0C, 0x14, 0x04, 0x18, 0x08, 0x30, 0x10, 0x20, 0x40, 0x00,
};

/*
 *  func_esi2C - 0040E560 - vftable function
 */

void func_esi2C(byte *src, int32_t len, replay_enc_t *rep)
{
    if (rep->m0C + len <= rep->m14)
        memcpy(&rep->m08[rep->m0C], src, len);
    rep->m0C += len;
}

/*
 *  func_esi28 - 0040E520 - vftable function
 */

int32_t func_esi28(byte *dst, int len, replay_enc_t *rep)
{
    len = min(rep->m10 - rep->m04, len);
    memcpy(dst, &rep->src[rep->m04], len);
    rep->m04 += len;
    return len;
}

/*
 *  common - 004DAA40 - called from function1 and function2
 */

int32_t common(esi_t *myesi, int count)
{
    int32_t         tmp;

    if ( myesi->m18 < count )
    {
        myesi->m14 >>= (byte)myesi->m18;
        if ( myesi->m1C == myesi->m20 )
        {
            myesi->m20 = func_esi28(myesi->m2234, 0x800, myesi->m24);
            if (myesi->m20 == 0) return 1; else myesi->m1C = 0;
        }
        tmp = myesi->m2234[myesi->m1C];
        tmp <<= 8;
        myesi->m1C++;
        tmp |= myesi->m14;
        myesi->m14 = tmp;
        tmp >>= (count - (byte)myesi->m18);
        myesi->m14 = tmp;
        myesi->m18 += (8 - count);
    } else {
        myesi->m18 -= count;
        myesi->m14 >>= (byte)count;
    }
    return 0;
}

/*
 *  function1 - 004DA810 - called from uncompress_replay
 *
 *  returns a character OR 0x306 as error
 *  OR 0x100 + 2 + the length of a region already present
 *
 */

int32_t function1(esi_t *myesi)
{
    int32_t         x, result;

    /* myesi->m14 is odd */
    if (1 & myesi->m14)
    {
        if (common(myesi, 1)) return 0x306;
        result = myesi->m2B34[(byte)myesi->m14];
        if (common(myesi, myesi->m30F4[result])) return 0x306;
        if (myesi->m3104[result])
        {
            x = ((1 << myesi->m3104[result]) - 1) & myesi->m14;
            if (common(myesi, myesi->m3104[result]) && (result + x) != 0x10E) return 0x306;
            memcpy(&result, &myesi->m3114[2*result], 2);
            result += x;
        }
        return result + 0x100;
    }
    /* myesi->m14 is even */
    if (common(myesi, 1)) return 0x306;
    if (myesi->m04 == 0)
    {
        result = (byte)myesi->m14;
        if (common(myesi, 8)) return 0x306;
        return result;
    }
    if ((byte)myesi->m14 == 0)
    {
        if (common(myesi, 8)) return 0x306;
        result = myesi->m2EB4[(byte)myesi->m14];
    }
    else
    {
        result = myesi->m2C34[(byte)myesi->m14];
        if (result == 0xFF )
        {
            if ((myesi->m14 & 0x3F) == 0)
            {
                if (common(myesi, 6)) return 0x306;
                result = myesi->m2C34[myesi->m14 & 0x7F];
            }
            else
            {
                if (common(myesi, 4)) return 0x306;
                result = myesi->m2D34[myesi->m14 & 0xFF];
            }
        }
    }
    if (common(myesi, myesi->m2FB4[result])) return 0x306;
    return result;
}

/*
 *  function2 - 004DA9C0 - called from uncompress_replay
 *
 *  returns the difference between the current location and
 *  location of the region already present
 *
 */

int32_t function2(esi_t *myesi, int len)
{
    int32_t         tmp;

    tmp = myesi->m2A34[(byte)myesi->m14];
    if (common(myesi, myesi->m30B4[tmp])) return 0;
    if (len != 2)
    {
        tmp <<= (byte)myesi->m0C;
        tmp |= myesi->m14 & myesi->m10;
        if (common(myesi, myesi->m0C)) return 0;
    }
    else
    {
        tmp <<= 2;
        tmp |= myesi->m14 & 3;
        if (common(myesi, 2)) return 0;
    }   /* A38 */
    return tmp + 1;
}

/*
 *  unpack_rep_chunk - 004DA710 - unpacks up to ??? bytes
 */

int32_t unpack_rep_chunk(esi_t *myesi)
{
    int32_t         tmp, len;

    myesi->m08 = 0x1000;
    do {
        len = function1(myesi);
        if (len >= 0x305) break;
        if (len >= 0x100)
        {   /* decode region of length len -0xFE */
            len -= 0xFE;
            tmp = function2(myesi, len);
            if (tmp == 0) { len = 0x306; break; }
            for (; len>0; myesi->m08++,len--)
                myesi->m30[myesi->m08] = myesi->m30[myesi->m08 - tmp];
        }
        else
        {   /* just copy the character */
            myesi->m30[myesi->m08] = len;
            myesi->m08 ++;
        }
        if (myesi->m08 < 0x2000) continue;
        func_esi2C(myesi->m1030, 0x1000, myesi->m24);           /* src, len, rep (dst) */
        memmove(myesi->m30, myesi->m1030, myesi->m08-0x1000);    /* dst, src, count */
        myesi->m08 -= 0x1000;
    } while (1);
    func_esi2C(myesi->m1030, myesi->m08 - 0x1000, myesi->m24);
    return len;
}

/*
 *  com1 - 004DAAD0 - called from unpack_rep_section
 */

void com1(int32_t strlen, byte *src, byte *str, byte *dst)
{
    int32_t         n,x,y;

    for (n=strlen-1; n>=0; n--)
        for (x=str[n], y=1<<src[n]; x<0x100; x+=y)
            dst[x] = n;
}

/*
 *  unpack_rep_section - 004DA590 - called from unpack_section
 */

int32_t unpack_rep_section(esi_t *myesi, replay_enc_t *rep)
{
    myesi->m24 = rep;
    myesi->m1C = 0x800;
    myesi->m20 = func_esi28(myesi->m2234, myesi->m1C, myesi->m24);
    if (myesi->m20 <= 4) return 3;
    myesi->m04 = (int32_t)rep->src[0];
    myesi->m0C = (int32_t)rep->src[1];
    myesi->m14 = (int32_t)rep->src[2];
    myesi->m18 = 0;
    myesi->m1C = 3;
    if (myesi->m0C < 4 || myesi->m0C > 6) return 1;
    myesi->m10 = (1<<myesi->m0C) - 1;                                   /* 2^n - 1 */

    /* if (myesi->m04 == 1) print32_tf("Oops\n"); */                        /* Should never be true */
    if (myesi->m04 != 0) return 2;

    memcpy(myesi->m30F4, off_5071D0, sizeof(off_5071D0));               /* dst, src, len */
    com1(sizeof(off_5071E0), myesi->m30F4, off_5071E0, myesi->m2B34);   /* len, src, str, dst */
    memcpy(myesi->m3104, off_5071A0, sizeof(off_5071A0));               /* dst, src, len */
    memcpy(myesi->m3114, off_5071B0, sizeof(off_5071B0));               /* dst, src, len */
    memcpy(myesi->m30B4, off_507120, sizeof(off_507120));               /* dst, src, len */
    com1(sizeof(off_507160), myesi->m30B4, off_507160, myesi->m2A34);   /* len, src, str, dst */
    unpack_rep_chunk(myesi);
    return 0;                                                           /* FIXME */
    // sub     eax, 306h
    // cmp     eax, 1
    // sbb     eax, eax
    // and     eax, 4
    // retn
}

/*
 *  unpack_section - 40E5B0 - unpacks a replay section
 */

int32_t unpack_section(FILE *file, byte *result, int size)
{
    replay_enc_t    rep;
    esi_t           myesi;
    byte            buffer[0x2000];
    int32_t             check, count, length, n, len=0, m1C, m20=0;

    if (size == 0) return 4;
    if (fread(&check, 1, 4, file) == 0) return 4;
    if (fread(&count, 1, 4, file) == 0) return 4;
    /* clear myesi struct */
    memset(&myesi, 0, sizeof(myesi));

    for (n=0, m1C=0; n < count; n++, m1C+=sizeof(buffer), m20+=len)
    {
        if (fread(&length, 1, 4, file) == 0) return 4;
        if (length > size-m20) return 4;
        if (fread(result, 1, length, file) == 0) return 4;
        if (length == (int32_t)(min(size-m1C, sizeof(buffer)))) continue;


        // init rep struct
        rep.src = (byte*)result;
        rep.m04 = 0;
        rep.m08 = buffer;
        rep.m0C = 0;
        rep.m10 = length;
        rep.m14 = sizeof(buffer);
        // unpack replay section
        if (unpack_rep_section(&myesi, &rep) == 0 && rep.m0C <= sizeof(buffer)) len = rep.m0C; else len = 0;
        if (len == 0 || len > size) return 4;

		// Main decompression functions
		/*
		uint32_t outlength = sizeof(buffer);
		Explode4(buffer, &outlength, result, length);
		len = (int32_t)outlength;
		*/


        memcpy(result, buffer, len);
        result += len;
    }
    return 0;
}

void replay_unpack(replay_dec_t *rep, const char *path, int32_t sections)
{
    int32_t             repID;
    FILE            *file;

    file = fopen(path, "rb");
    if (file == NULL) return;

    unpack_section(file, (byte*)&repID, sizeof(repID));
    if (repID != 0x53526572) goto unpack_end;

    rep->hdr_size = 0x279;
    /* if (sections & SEC_HDR) */
        rep->hdr = (hdr_t *)malloc(rep->hdr_size * sizeof(byte));
        if (rep->hdr == NULL) goto unpack_end;
        unpack_section(file, rep->hdr, rep->hdr_size);

    unpack_section(file, (byte*)&rep->cmd_size, sizeof(rep->cmd_size));
    /* if (sections & SEC_CMD) */
        rep->cmd = (byte *)malloc(rep->cmd_size * sizeof(byte));
        if (rep->cmd == NULL) goto unpack_end;
        unpack_section(file, rep->cmd, rep->cmd_size);

    unpack_section(file, (byte*)&rep->map_size, sizeof(rep->map_size));
    /* if (sections & SEC_MAP) */
        rep->map = (byte *)malloc(rep->map_size * sizeof(byte));
        if (rep->map == NULL) goto unpack_end;
        unpack_section(file, rep->map, rep->map_size);

unpack_end:
    fclose(file);
    return;
}
