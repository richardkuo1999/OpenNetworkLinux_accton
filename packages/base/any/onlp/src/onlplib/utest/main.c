/************************************************************
 * <bsn.cl fy=2014 v=onl>
 * 
 *        Copyright 2014, 2015 Big Switch Networks, Inc.       
 * 
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 * 
 *        http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 * 
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/

#include <onlplib/onlplib_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <AIM/aim.h>

#include <onlplib/onie.h>
#include <onlplib/crc32.h>
#include <arpa/inet.h>

/*
 * Self-contained assertion helpers.
 *
 * These deliberately do NOT use AIM's UT_CHECK so the ONIE regression tests
 * build both under the full ONL tree and under the minimal AIM/IOF stub used
 * by reviews/onie-totallen/run_utest.sh.
 */
static int onie_test_failures = 0;

#define ONIE_CHECK(_cond, _msg)                                         \
    do {                                                                \
        if(!(_cond)) {                                                  \
            onie_test_failures++;                                       \
            printf("  [FAIL] %s (%s:%d)\n", (_msg), __FILE__, __LINE__);\
        }                                                               \
    } while(0)

/*
 * ONIE TlvInfo header/TLV layout, mirrored here only so the tests can build
 * byte-exact eeprom images. Kept in sync with onie.c.
 */
#define ONIE_HDR_LEN            11
#define ONIE_SIG               "TlvInfo"
#define TLV_CODE_PRODUCT_NAME   0x21
#define TLV_CODE_MAC_BASE       0x24
#define TLV_CODE_DEVICE_VERSION 0x26
#define TLV_CODE_MAC_SIZE       0x2A
#define TLV_CODE_CRC_32         0xFE

/* Write the 11-byte TlvInfo header with the given totallen. */
static void onie_hdr(uint8_t* b, uint16_t totallen)
{
    memcpy(b, ONIE_SIG, 8);        /* 7 chars + NUL, fills bytes 0..7 */
    b[8]  = 0x01;                  /* version */
    b[9]  = (totallen >> 8) & 0xFF;
    b[10] = totallen & 0xFF;
}

/*
 * Append a CRC-32 TLV computed over [data .. data+hdr+body-4], exactly as
 * onie.c expects it, so we can build images that PASS the CRC check. Returns
 * the total image length (header + body + CRC TLV).
 *
 * body_len is the number of body bytes already written after the header,
 * NOT counting the CRC TLV itself. totallen in the header must already be set
 * to body_len + 6 (the CRC TLV size).
 */
static int onie_finalize_crc(uint8_t* b, int body_len)
{
    int crc_tlv_off = ONIE_HDR_LEN + body_len;
    uint32_t crc;
    b[crc_tlv_off + 0] = TLV_CODE_CRC_32;
    b[crc_tlv_off + 1] = 4;
    /* CRC covers everything up to but not including the 4 CRC value bytes. */
    crc = onlp_crc32(0, b, crc_tlv_off + 2);
    b[crc_tlv_off + 2] = (crc >> 24) & 0xFF;
    b[crc_tlv_off + 3] = (crc >> 16) & 0xFF;
    b[crc_tlv_off + 4] = (crc >>  8) & 0xFF;
    b[crc_tlv_off + 5] = (crc >>  0) & 0xFF;
    return crc_tlv_off + 6;
}

/*
 * Build a valid image: one PRODUCT_NAME TLV + trailing CRC-32 TLV, with a
 * correct CRC. Returns image length; *totallen_out is the header totallen.
 */
static int onie_build_valid(uint8_t* b, const char* name)
{
    int nlen = (int)strlen(name);
    int body = 2 + nlen;                 /* product-name TLV */
    int totallen = body + 6;             /* + CRC-32 TLV */
    onie_hdr(b, (uint16_t)totallen);
    b[ONIE_HDR_LEN + 0] = TLV_CODE_PRODUCT_NAME;
    b[ONIE_HDR_LEN + 1] = (uint8_t)nlen;
    memcpy(b + ONIE_HDR_LEN + 2, name, nlen);
    return onie_finalize_crc(b, body);
}

/*
 * ONIE regression tests.
 *
 * Every "malicious" vector must be cleanly REJECTED (rv < 0) with no memory
 * error (run under ASAN by run_utest.sh); the valid control must SUCCEED and
 * decode its fields, so a blanket "reject everything" cannot pass.
 */
static void onie_test(void)
{
    printf("-- onie regression --\n");

    /* Control: a well-formed image with a correct CRC must decode. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int len = onie_build_valid(buf, "SWITCH-1");
        int rv = onlp_onie_decode(&info, buf, len);
        ONIE_CHECK(rv == 0, "valid image should decode");
        ONIE_CHECK(info.product_name != NULL &&
                   strcmp(info.product_name, "SWITCH-1") == 0,
                   "valid image should yield product_name");
        ONIE_CHECK(info._hdr_valid_crc == 1,
                   "valid image should set _hdr_valid_crc");
        onlp_onie_info_free(&info);
    }

    /* Axis 2 / size fallback contract: when size<=0 the parser assumes the
     * ONIE spec maximum (2048) is readable. With a buffer that honours that
     * contract, an inflated totallen with no valid CRC must be rejected by
     * the CRC/bounds checks -- not walk past 2048. (A buffer smaller than
     * 2048 combined with size<=0 is the documented residual risk in
     * FIX-PLAN.md and is intentionally out of scope here.) */
    {
        onlp_onie_info_t info;
        uint8_t* buf = calloc(1, 2048);
        onie_hdr(buf, 2037);
        buf[11] = TLV_CODE_PRODUCT_NAME; buf[12] = 4; memcpy(buf + 13, "OK!!", 4);
        int rv = onlp_onie_decode(&info, buf, -1);
        ONIE_CHECK(rv < 0, "size=-1 inflated totallen w/o valid CRC must be rejected");
        onlp_onie_info_free(&info);
        free(buf);
    }

    /* Axis 2 / size=0 (empty-file path) must be rejected, not parsed. */
    {
        onlp_onie_info_t info;
        uint8_t* buf = calloc(1, 1);
        buf[0] = 'T';
        int rv = onlp_onie_decode(&info, buf, 0);
        ONIE_CHECK(rv < 0, "size=0 must be rejected");
        onlp_onie_info_free(&info);
        free(buf);
    }

    /* Axis 2 / totallen bound: correct size but totallen runs past buffer. */
    {
        onlp_onie_info_t info;
        uint8_t* buf = calloc(1, 256);
        onie_hdr(buf, 2037);                 /* 2037 > 256 - 11 */
        int rv = onlp_onie_decode(&info, buf, 256);
        ONIE_CHECK(rv < 0, "totallen past buffer must be rejected");
        onlp_onie_info_free(&info);
        free(buf);
    }

    /* Axis 2 / per-TLV length: valid CRC but a TLV whose length overruns the
     * declared body. This is the "recomputed CRC" vector: axis 1 lets it in,
     * axis 2 (per-TLV bound) must stop it. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int body = 2 + 4;                    /* product-name TLV, len 4 */
        int totallen = body + 6;
        onie_hdr(buf, (uint16_t)totallen);
        buf[11] = TLV_CODE_PRODUCT_NAME;
        buf[12] = 200;                       /* lies: 200 > remaining body */
        memcpy(buf + 13, "OK!!", 4);
        onie_finalize_crc(buf, body);        /* CRC is correct for the bytes */
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "TLV length overrun must be rejected even with valid CRC");
        onlp_onie_info_free(&info);
    }

    /* Axis 2 / fixed-width TLV too short: MAC_BASE with length 3. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int body = 2 + 3;                    /* MAC_BASE TLV, bogus len 3 */
        int totallen = body + 6;
        onie_hdr(buf, (uint16_t)totallen);
        buf[11] = TLV_CODE_MAC_BASE;
        buf[12] = 3;
        memset(buf + 13, 0x11, 3);
        onie_finalize_crc(buf, body);
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "short MAC_BASE TLV must be rejected");
        onlp_onie_info_free(&info);
    }

    /* Axis 1 / CRC fail-closed: no CRC TLV present -> must NOT be accepted. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        onie_hdr(buf, 6);                    /* claims a 6-byte body ... */
        buf[11] = TLV_CODE_PRODUCT_NAME;     /* ... but it is not a CRC TLV */
        buf[12] = 4;
        memcpy(buf + 13, "OK!!", 4);
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "missing CRC TLV must fail closed");
        onlp_onie_info_free(&info);
    }

    /* Axis 1 / CRC mismatch: correct structure, wrong CRC value. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int len = onie_build_valid(buf, "SWITCH-1");
        buf[len - 1] ^= 0xFF;                /* corrupt stored CRC */
        int rv = onlp_onie_decode(&info, buf, len);
        ONIE_CHECK(rv < 0, "CRC mismatch must be rejected");
        onlp_onie_info_free(&info);
    }

    /* decode_file: non-existent file must fail and leave a zeroed struct. */
    {
        onlp_onie_info_t info;
        memset(&info, 0xAA, sizeof(info));
        int rv = onlp_onie_decode_file(&info, "/nonexistent/onie/file");
        ONIE_CHECK(rv < 0, "decode_file on missing file must fail");
        ONIE_CHECK(info.product_name == NULL,
                   "decode_file must zero the struct on failure");
    }

    /* decode_file: a directory yields a short read; must fail, not succeed. */
    {
        onlp_onie_info_t info;
        memset(&info, 0xAA, sizeof(info));
        int rv = onlp_onie_decode_file(&info, "/tmp");
        ONIE_CHECK(rv < 0, "decode_file short-read must fail");
    }

    /* Axis 2 / fixed-width TLV: MAC_SIZE with length 1 (needs 2). */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int body = 2 + 1;
        onie_hdr(buf, (uint16_t)(body + 6));
        buf[11] = TLV_CODE_MAC_SIZE; buf[12] = 1; buf[13] = 0x01;
        onie_finalize_crc(buf, body);
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "short MAC_SIZE TLV must be rejected");
        onlp_onie_info_free(&info);
    }

    /* Axis 2 / fixed-width TLV: DEVICE_VERSION with length 0 (needs 1). */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        int body = 2 + 0;
        onie_hdr(buf, (uint16_t)(body + 6));
        buf[11] = TLV_CODE_DEVICE_VERSION; buf[12] = 0;
        onie_finalize_crc(buf, body);
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "zero-length DEVICE_VERSION TLV must be rejected");
        onlp_onie_info_free(&info);
    }

    /* Axis 2 / truncated TLV header: totallen leaves only 1 byte for a TLV. */
    {
        onlp_onie_info_t info;
        uint8_t buf[64] = {0};
        /* One stray byte before the CRC TLV: body = 1 (truncated TLV header). */
        int body = 1;
        onie_hdr(buf, (uint16_t)(body + 6));
        buf[11] = TLV_CODE_PRODUCT_NAME;   /* only the type byte fits */
        onie_finalize_crc(buf, body);
        int rv = onlp_onie_decode(&info, buf, sizeof(buf));
        ONIE_CHECK(rv < 0, "truncated TLV header must be rejected");
        onlp_onie_info_free(&info);
    }

    /* decode_file: an empty file must be rejected (too small), struct zeroed. */
    {
        onlp_onie_info_t info;
        FILE* fp = fopen("/tmp/onie_empty_test.bin", "wb");
        if(fp) { fclose(fp); }
        memset(&info, 0xAA, sizeof(info));
        int rv = onlp_onie_decode_file(&info, "/tmp/onie_empty_test.bin");
        ONIE_CHECK(rv < 0, "decode_file on empty file must fail");
        ONIE_CHECK(info.product_name == NULL,
                   "decode_file must zero the struct on empty-file failure");
        remove("/tmp/onie_empty_test.bin");
    }

    if(onie_test_failures == 0) {
        printf("-- onie regression PASSED --\n");
    }
    else {
        printf("-- onie regression FAILED (%d checks) --\n", onie_test_failures);
    }
}

int aim_main(int argc, char* argv[])
{
    onie_test();
    onlplib_config_show(&aim_pvs_stdout);
    if(onie_test_failures) {
        printf("onlplib Utest FAILED\n");
        return 1;
    }
    printf("onlplib Utest PASSED\n");
    return 0;
}

