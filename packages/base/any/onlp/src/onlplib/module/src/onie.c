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
 * ONIE System Board Information decoding.
 *
 * See:
 * https://github.com/onie/onie/blob/master/docs/design-spec/hw_requirements.rst
 *
 * This code derived from the feature-sys-eeprom-tlv.patch in the ONIE
 * distribution.
 *
 ***********************************************************/

#include <onlplib/onie.h>
#include <onlplib/crc32.h>

#include <AIM/aim_memory.h>
#include <AIM/aim_string.h>
#include <AIM/aim_printf.h>

#include <arpa/inet.h>

#include "onlplib_log.h"

#include <IOF/iof.h>

/**
 * Header Field Constants
 */
#define TLV_INFO_ID_STRING      "TlvInfo"
#define TLV_INFO_VERSION        0x01
#define TLV_INFO_MAX_LEN        2048
#define TLV_TOTAL_LEN_MAX       (TLV_INFO_MAX_LEN - sizeof(tlvinfo_header_t))

/* A CRC-32 TLV is the minimum any valid TlvInfo body must contain. */
#define TLV_CRC_TLV_LEN         (sizeof(tlvinfo_tlv_t) + 4)
#define TLV_TOTAL_LEN_MIN       TLV_CRC_TLV_LEN

/**
 * Validate checksum
 *
 * avail is the number of bytes that are safe to read from data.
 */
static int checksum_validate__(const uint8_t *data, int avail);


/**
 * ONIE TLV EEPROM Header
 */
typedef struct __attribute__ ((__packed__)) tlvinfo_header_s {
    char        signature[8];       /* 0x00 - 0x07 EEPROM Tag "TlvInfo" */
    uint8_t     version;            /* 0x08        Structure version    */
    uint16_t    totallen;           /* 0x09 - 0x0A Length of all data which follows */
} tlvinfo_header_t;

/**
 * ONIE TLV Entry
 */
typedef struct __attribute__ ((__packed__)) tlvinfo_tlv_s {
    uint8_t  type;
    uint8_t  length;
    uint8_t  value[0];
} tlvinfo_tlv_t;


/**
 *  The TLV Types.
 */
#define TLV_CODE_PRODUCT_NAME   0x21
#define TLV_CODE_PART_NUMBER    0x22
#define TLV_CODE_SERIAL_NUMBER  0x23
#define TLV_CODE_MAC_BASE       0x24
#define TLV_CODE_MANUF_DATE     0x25
#define TLV_CODE_DEVICE_VERSION 0x26
#define TLV_CODE_LABEL_REVISION 0x27
#define TLV_CODE_PLATFORM_NAME  0x28
#define TLV_CODE_ONIE_VERSION   0x29
#define TLV_CODE_MAC_SIZE       0x2A
#define TLV_CODE_MANUF_NAME     0x2B
#define TLV_CODE_MANUF_COUNTRY  0x2C
#define TLV_CODE_VENDOR_NAME    0x2D
#define TLV_CODE_DIAG_VERSION   0x2E
#define TLV_CODE_SERVICE_TAG    0x2F
#define TLV_CODE_VENDOR_EXT     0xFD
#define TLV_CODE_CRC_32         0xFE


static void
decode_tlv__(onlp_onie_info_t* info, tlvinfo_tlv_t * tlv)
{
    switch (tlv->type)
        {
            /* String TLVs */
#define CASE_TLV_STRING(_info, _member, _code, _tlv)                    \
            case TLV_CODE_##_code :                                     \
                {                                                       \
                    if(_info -> _member) {                              \
                        aim_free((void*) _info -> _member);             \
                    }                                                   \
                    _info -> _member = aim_zmalloc(_tlv->length + 1);   \
                    memcpy((void*) _info -> _member, _tlv->value, _tlv->length); \
                    break; \
                }

            CASE_TLV_STRING(info, product_name, PRODUCT_NAME, tlv);
            CASE_TLV_STRING(info, part_number, PART_NUMBER, tlv);
            CASE_TLV_STRING(info, serial_number, SERIAL_NUMBER, tlv);
            CASE_TLV_STRING(info, manufacture_date, MANUF_DATE, tlv);
            CASE_TLV_STRING(info, label_revision, LABEL_REVISION, tlv);
            CASE_TLV_STRING(info, platform_name, PLATFORM_NAME, tlv);
            CASE_TLV_STRING(info, onie_version, ONIE_VERSION, tlv);
            CASE_TLV_STRING(info, manufacturer, MANUF_NAME, tlv);
            CASE_TLV_STRING(info, country_code, MANUF_COUNTRY, tlv);
            CASE_TLV_STRING(info, vendor, VENDOR_NAME, tlv);
            CASE_TLV_STRING(info, service_tag, SERVICE_TAG, tlv);
            CASE_TLV_STRING(info, diag_version, DIAG_VERSION, tlv);

        case TLV_CODE_MAC_BASE:
            memcpy(info->mac, tlv->value, 6);
            break;

        case TLV_CODE_DEVICE_VERSION:
            info->device_version = tlv->value[0];
            break;

        case TLV_CODE_MAC_SIZE:
            info->mac_range = (tlv->value[0] << 8) | tlv->value[1];
            break;

        case TLV_CODE_VENDOR_EXT:
            {
                onlp_onie_vx_t* vx = aim_zmalloc(sizeof(*vx));
                vx->size = tlv->length;
                memcpy(vx->data, tlv->value, tlv->length);
                list_push(&info->vx_list, &vx->links);
                break;
            }

        case TLV_CODE_CRC_32:
            info->crc =
                (tlv->value[0] << 24) |
                (tlv->value[1] << 16) |
                (tlv->value[2] <<  8) |
                (tlv->value[3]);
            break;

        default:
            AIM_LOG_WARN("ONIE data contains an unrecognized TLV code: 0x%.2x (ignored)", tlv->type);
            break;
        }
}

/**
 *  is_valid_tlvinfo_header
 *
 *  Perform sanity checks on the TlvInfo EEPROM header pointed to by hdr,
 *  bounded by avail (the number of bytes safe to read from hdr):
 *      1. avail is large enough to hold the header.
 *      2. First 8 bytes contain null-terminated ASCII string "TlvInfo".
 *      3. Version byte is 1.
 *      4. Total length is within the spec maximum (2048-11), holds at least
 *         a CRC-32 TLV, and does not run past the readable buffer.
 *
 */
static int is_valid_tlvinfo_header__(const tlvinfo_header_t *hdr, int avail)
{
    int totallen;

    if(hdr == NULL || avail < (int)sizeof(tlvinfo_header_t)) {
        return 0;
    }
    /*
     * Ensure signature is NUL-terminated within its 8-byte field before
     * calling strcmp(), otherwise strcmp() can read past the field.
     */
    if(memchr(hdr->signature, 0, sizeof(hdr->signature)) == NULL) {
        return 0;
    }
    if(strcmp(hdr->signature, TLV_INFO_ID_STRING) != 0) {
        return 0;
    }
    if(hdr->version != TLV_INFO_VERSION) {
        return 0;
    }

    totallen = ntohs(hdr->totallen);

    if(totallen > (int)TLV_TOTAL_LEN_MAX) {
        return 0;
    }
    if(totallen < (int)TLV_TOTAL_LEN_MIN) {
        return 0;
    }
    /* Core bound: totallen must fit inside the readable buffer. */
    if(totallen > avail - (int)sizeof(tlvinfo_header_t)) {
        return 0;
    }
    return 1;
}


/**
 *  is_valid_tlv
 *
 *  Validate the TLV that starts at 'curr_tlv' within 'data', bounded by
 *  'tlv_end' (the end of the declared TLV region):
 *      1. The fixed TLV header (type + length) fits before tlv_end.
 *      2. The type code is not reserved (0x00 or 0xFF).
 *      3. The value does not run past tlv_end.
 *      4. Fixed-width TLVs carry exactly the length the ONIE spec mandates.
 *  Returns 1 if the TLV is valid, 0 otherwise (logging the reason).
 */
static int is_valid_tlv__(const uint8_t *data, int curr_tlv, int tlv_end)
{
    const tlvinfo_tlv_t *tlv;

    /* The fixed header (type + length) must fit before tlv_end. */
    if(curr_tlv + (int)sizeof(tlvinfo_tlv_t) > tlv_end) {
        AIM_LOG_ERROR("ONIE data has a truncated TLV header at offset %d.", curr_tlv);
        return 0;
    }

    tlv = (const tlvinfo_tlv_t *) &data[curr_tlv];

    if((tlv->type == 0x00) || (tlv->type == 0xFF)) {
        AIM_LOG_ERROR("ONIE data invalid TLV field starting at offset %d", curr_tlv);
        return 0;
    }

    /* The value must not run past the declared data length. */
    if(curr_tlv + (int)sizeof(tlvinfo_tlv_t) + tlv->length > tlv_end) {
        AIM_LOG_ERROR("ONIE TLV at offset %d (type 0x%.2x, length %d) overruns "
                      "the declared data length.",
                      curr_tlv, tlv->type, tlv->length);
        return 0;
    }

    /* Fixed-width TLVs are decoded by reading a fixed number of value bytes */
    switch(tlv->type) {
    case TLV_CODE_MAC_BASE:       if(tlv->length != 6) goto bad_len; break;
    case TLV_CODE_MAC_SIZE:       if(tlv->length != 2) goto bad_len; break;
    case TLV_CODE_DEVICE_VERSION: if(tlv->length != 1) goto bad_len; break;
    case TLV_CODE_CRC_32:         if(tlv->length != 4) goto bad_len; break;
    default: break;
    }

    return 1;

 bad_len:
    AIM_LOG_ERROR("ONIE TLV at offset %d (type 0x%.2x) has bad length %d.",
                  curr_tlv, tlv->type, tlv->length);
    return 0;
}


/*
 * Resolve how many bytes may be read from the ONIE buffer, bounding an
 * out-of-range 'totallen' from walking past it:
 *   size > 0 : clamp to the caller-supplied size (preferred).
 *   size == 0: empty buffer, nothing to read.
 *   size < 0 : size unknown (legacy callers); fall back to the ONIE spec max.
 *              Residual risk: a real buffer < 2048 bytes can still be over-read
 *              up to 2048 -- callers should pass the real size.
 * Returns the readable byte count, or -1 if the buffer is unusable.
 */
static int
readable_avail__(int size)
{
    if(size == 0) {
        return -1;
    }
    if(size > 0) {
        return (size > TLV_INFO_MAX_LEN) ? TLV_INFO_MAX_LEN : size;
    }
    return TLV_INFO_MAX_LEN;
}

int
onlp_onie_decode(onlp_onie_info_t* rv, const uint8_t* data, int size)
{
    int tlv_end;
    int curr_tlv;
    int totallen;
    int avail;
    tlvinfo_header_t* data_hdr = (tlvinfo_header_t *) data;
    tlvinfo_tlv_t* data_tlv;

    if(rv == NULL || data == NULL) {
        return -1;
    }

    memset(rv, 0, sizeof(*rv));
    list_init(&rv->vx_list);

    avail = readable_avail__(size);
    if(avail < 0) {
        AIM_LOG_ERROR("ONIE data buffer is empty or has an invalid size.");
        return -1;
    }

    if ( !is_valid_tlvinfo_header__(data_hdr, avail) ) {
        AIM_LOG_ERROR("ONIE data is not in TlvInfo format.");
        return -1;
    }

    totallen = ntohs(data_hdr->totallen);

    /* Validate CRC checksum before attempting to parse */
    if(checksum_validate__(data, avail) != 0) {
        /* Error already logged */
        return -1;
    }

    rv->_hdr_id_string = aim_strdup(data_hdr->signature);
    rv->_hdr_version = data_hdr->version;
    rv->_hdr_length = totallen;

    curr_tlv = sizeof(tlvinfo_header_t);
    tlv_end  = sizeof(tlvinfo_header_t) + totallen;
    while (curr_tlv < tlv_end) {
        if (!is_valid_tlv__(data, curr_tlv, tlv_end)) {
            /* Error already logged */
            onlp_onie_info_free(rv);
            memset(rv, 0, sizeof(*rv));
            list_init(&rv->vx_list);
            return -1;
        }
        data_tlv = (tlvinfo_tlv_t *) &data[curr_tlv];
        decode_tlv__(rv, data_tlv);
        curr_tlv += sizeof(tlvinfo_tlv_t) + data_tlv->length;
    }

    /* Mark the CRC valid only after a clean walk, never on a bounds failure. */
    rv->_hdr_valid_crc = 1;
    return 0;
}

int
onlp_onie_decode_file(onlp_onie_info_t* onie, const char* file)
{
    char* data;
    long  fsize;
    int   size;
    size_t nread;
    FILE* fp;
    int rv;

    if(onie == NULL || file == NULL) {
        return -1;
    }

    /* Initialise so a caller testing 'rv >= 0' never sees garbage on error. */
    memset(onie, 0, sizeof(*onie));
    list_init(&onie->vx_list);

    if((fp = fopen(file, "rb")) == NULL) {
        return -1;
    }

    if(fseek(fp, 0L, SEEK_END) != 0 || (fsize = ftell(fp)) < 0) {
        fclose(fp);
        return -1;
    }
    if(fsize < (long)sizeof(tlvinfo_header_t)) {
        AIM_LOG_ERROR("ONIE data file '%s' is too small (%ld bytes).", file, fsize);
        fclose(fp);
        return -1;
    }

    /* Never read (or allocate) more than the ONIE spec maximum. */
    size = (fsize > TLV_INFO_MAX_LEN) ? TLV_INFO_MAX_LEN : (int)fsize;
    rewind(fp);

    data  = aim_zmalloc(size);
    nread = fread(data, 1, size, fp);
    fclose(fp);

    if(nread != (size_t)size) {
        AIM_LOG_ERROR("ONIE data file '%s' short read (%zu of %d bytes).",
                      file, nread, size);
        aim_free(data);
        return -1;
    }

    /* Pass the real, bounded size so the strict (size > 0) path is used. */
    rv = onlp_onie_decode(onie, (uint8_t*)data, size);
    aim_free(data);
    return rv;
}

/**
 *  Validate the checksum in the provided TlvInfo EEPROM data. First,
 *  verify that the TlvInfo header is valid, then make sure the last
 *  TLV is a CRC-32 TLV. Then calculate the CRC over the EEPROM data
 *  and compare it to the value stored in the EEPROM CRC-32 TLV.
 */
static int
checksum_validate__(const uint8_t *data, int avail)
{
    const tlvinfo_header_t* data_hdr = (const tlvinfo_header_t *) data;
    const tlvinfo_tlv_t* data_crc;
    unsigned int calc_crc;
    unsigned int stored_crc;
    int totallen;
    int crc_offset;

    /* Is the eeprom header valid? */
    if (!is_valid_tlvinfo_header__(data_hdr, avail)) {
        AIM_LOG_ERROR("ONIE header is invalid; refusing to validate the CRC.");
        return -1;
    }

    totallen   = ntohs(data_hdr->totallen);
    crc_offset = (int)sizeof(tlvinfo_header_t) + totallen - (int)TLV_CRC_TLV_LEN;

    /* The CRC TLV must sit within the readable buffer. */
    if(crc_offset < (int)sizeof(tlvinfo_header_t) ||
       crc_offset + (int)TLV_CRC_TLV_LEN > avail) {
        AIM_LOG_ERROR("ONIE CRC TLV offset %d is out of range (%d bytes available).",
                      crc_offset, avail);
        return -1;
    }

    /* Is the last TLV a CRC? */
    data_crc = (const tlvinfo_tlv_t *) &data[crc_offset];
    if ((data_crc->type != TLV_CODE_CRC_32) || (data_crc->length != 4)) {
        AIM_LOG_ERROR("ONIE CRC TLV is invalid.");
        return -1;
    }

    /* Calculate the checksum */
    calc_crc = onlp_crc32(0, (void *)data,
                          (int)sizeof(tlvinfo_header_t) + totallen - 4);
    stored_crc = (data_crc->value[0] << 24) |
        (data_crc->value[1] << 16) |
        (data_crc->value[2] <<  8) |
        data_crc->value[3];

    if(calc_crc != stored_crc)  {
        AIM_LOG_ERROR("ONIE data crc error: expected 0x%.8x calculated 0x%.8x",
                      stored_crc, calc_crc);
        return -1;
    }
    return 0;
}

void
onlp_onie_info_free(onlp_onie_info_t* info)
{
    if(info) {
        aim_free(info->product_name);
        aim_free(info->part_number);
        aim_free(info->serial_number);
        aim_free(info->manufacture_date);
        aim_free(info->label_revision);
        aim_free(info->platform_name);
        aim_free(info->onie_version);
        aim_free(info->manufacturer);
        aim_free(info->country_code);
        aim_free(info->vendor);
        aim_free(info->diag_version);
        aim_free(info->service_tag);
        aim_free(info->_hdr_id_string);

        list_links_t *cur, *next;
        LIST_FOREACH_SAFE(&info->vx_list, cur, next) {
            onlp_onie_vx_t* vx = container_of(cur, links, onlp_onie_vx_t);
            aim_free(vx);
        }
    }
}

void
onlp_onie_show(onlp_onie_info_t* info, aim_pvs_t* pvs)
{
    iof_t iof;
    iof_init(&iof, pvs);
    if(info->product_name) {
        iof_iprintf(&iof, "Product Name: %s", info->product_name);
    }
    if(info->part_number) {
        iof_iprintf(&iof, "Part Number: %s", info->part_number);
    }
    if(info->serial_number) {
        iof_iprintf(&iof, "Serial Number: %s", info->serial_number);
    }
    if(info->mac) {
        iof_iprintf(&iof, "MAC: %{mac}", info->mac);
    }
    if(info->mac_range) {
        iof_iprintf(&iof, "MAC Range: %d", info->mac_range);
    }
    if(info->manufacturer) {
        iof_iprintf(&iof, "Manufacturer: %s", info->manufacturer);
    }
    if(info->manufacture_date) {
        iof_iprintf(&iof, "Manufacture Date: %s", info->manufacture_date);
    }
    if(info->vendor) {
        iof_iprintf(&iof, "Vendor: %s", info->vendor);
    }
    if(info->platform_name) {
        iof_iprintf(&iof, "Platform Name: %s", info->platform_name);
    }
    if(info->device_version) {
        iof_iprintf(&iof, "Device Version: %u", info->device_version);
    }
    if(info->label_revision) {
        iof_iprintf(&iof, "Label Revision: %s", info->label_revision);
    }
    if(info->country_code) {
        iof_iprintf(&iof, "Country Code: %s", info->country_code);
    }
    if(info->diag_version) {
        iof_iprintf(&iof, "Diag Version: %s", info->diag_version);
    }
    if(info->service_tag) {
        iof_iprintf(&iof, "Service Tag: %s", info->service_tag);
    }
    if(info->onie_version) {
        iof_iprintf(&iof, "ONIE Version: %s", info->onie_version);
    }
}

#include <cjson/cJSON.h>
#include <cjson_util/cjson_util.h>

void
onlp_onie_show_json(onlp_onie_info_t* info, aim_pvs_t* pvs)
{
    cJSON* cj = cJSON_CreateObject();

#define _S(_name, _member)                                              \
    do {                                                                \
        if(info-> _member) {                                            \
            cJSON_AddStringToObject(cj, #_name, info-> _member);        \
        } else {                                                        \
            cJSON_AddNullToObject(cj, #_name);                          \
        }                                                               \
    } while(0)

#define _N(_name, _member)                                      \
    do {                                                        \
        cJSON_AddNumberToObject(cj, #_name, info-> _member);    \
    } while(0)

    _S(Product Name, product_name);
    _S(Part Number, part_number);
    _S(Serial Number, serial_number);
    {
        char* mac = aim_dfstrdup("%{mac}", info->mac);
        cJSON_AddStringToObject(cj, "MAC", mac);
        aim_free(mac);
    }
    _S(Manufacturer, manufacturer);
    _S(Manufacture Date,manufacture_date);
    _S(Vendor,vendor);
    _S(Platform Name,platform_name);
    _S(Label Revision,label_revision);
    _S(Country Code,country_code);
    _S(Diag Version,diag_version);
    _S(Service Tag,service_tag);
    _S(ONIE Version,onie_version);
    _N(Device Version, device_version);
    {
        char* crc = aim_fstrdup("0x%x", info->crc);
        cJSON_AddStringToObject(cj, "CRC", crc);
        aim_free(crc);
    }
    char* out = cJSON_Print(cj);
    aim_printf(pvs, "%s\n", out);
    free(out);
    cJSON_Delete(cj);
}

static char*
lookup_entry__(cJSON* cj, const char* name, int code)
{
    char* str = NULL;
    int rv = cjson_util_lookup_string(cj, &str, "0x%x", code);
    if(rv < 0) {
        rv = cjson_util_lookup_string(cj, &str, name);
    }
    if(rv < 0) {
        return NULL;
    }
    else {
        return aim_strdup(str);
    }
}

int
onlp_onie_read_json(onlp_onie_info_t* info, const char* fname)
{
    cJSON* cj;

    memset(info, 0, sizeof(*info));

    list_init(&info->vx_list);

    int rv = cjson_util_parse_file(fname, &cj);
    if(rv < 0) {
        AIM_LOG_ERROR("Could not parse ONIE JSON file '%s' rv=%{aim_error}",
                      fname, rv);
        return rv;
    }

#define ONIE_TLV_ENTRY_str(_member, _name, _code)                   \
    do {                                                            \
        info->_member = lookup_entry__(cj, #_name, _code);          \
    } while(0)

#define ONIE_TLV_ENTRY_mac(_member, _name, _code)             \
    do {                                                      \
        char* str = lookup_entry__(cj, #_name, _code);        \
        int mac[6] = {0};                                     \
        if(str) {                                             \
            int i;                                            \
            sscanf(str, "%x:%x:%x:%x:%x:%x",                  \
                   mac+0, mac+1, mac+2, mac+3, mac+4, mac+5); \
            for(i = 0; i < 6; i++) info->mac[i] = mac[i];     \
            aim_free(str);                                    \
        }                                                     \
    } while(0)

#define ONIE_TLV_ENTRY_byte(_member, _name, _code)      \
    do {                                                \
        char* v = lookup_entry__(cj, #_name, _code);    \
        if(v) {                                         \
            info->_member = atoi(v);                    \
            aim_free(v);                                \
        }                                               \
    } while(0)

#define ONIE_TLV_ENTRY_int16(_member, _name, _code)     \
    do {                                                \
        char* v = lookup_entry__(cj, #_name, _code);    \
        if(v) {                                         \
            info->_member = atoi(v);                    \
            aim_free(v);                                \
        }                                               \
    } while(0)

#define ONIE_TLV_ENTRY(_member, _name, _code, _type)    \
    ONIE_TLV_ENTRY_##_type(_member, _name, _code);

    #include <onlplib/onlplib.x>


    cJSON_Delete(cj);
    return 0;
}
