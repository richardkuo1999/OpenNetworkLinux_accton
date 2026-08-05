/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2013 Accton Technology Corporation.
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
#include <onlp/platformi/sfpi.h>
#include <onlplib/i2c.h>
#include <onlplib/file.h>
#include "x86_64_accton_as9716_32d_int.h"
#include "x86_64_accton_as9716_32d_log.h"
#include <syslog.h>

#define SFP_PORT_MIN 32
#define SFP_PORT_MAX 33
#define QSFP_PORT_MIN 0
#define QSFP_PORT_MAX 31
#define MIN_PORT QSFP_PORT_MIN
#define MAX_PORT SFP_PORT_MAX

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < SFP_PORT_MIN || _port > SFP_PORT_MAX) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < QSFP_PORT_MIN || _port > QSFP_PORT_MAX ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_PORT(_port) \
    do { \
        if (_port < MIN_PORT || _port > MAX_PORT ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define PORT_EEPROM_FORMAT              "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT		    "/sys/bus/i2c/devices/%d-00%d/module_present_%d"
#define MODULE_RXLOS_FORMAT             "/sys/bus/i2c/devices/%d-00%d/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT           "/sys/bus/i2c/devices/%d-00%d/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT         "/sys/bus/i2c/devices/%d-00%d/module_tx_disable_%d"
#define MODULE_RESET_FORMAT             "/sys/bus/i2c/devices/%d-00%d/module_reset_%d"

/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR 0x50

/*QSFP identify offsets*/
#define QSFP_EEPROM_OFFSET_IDENTIFIER 0x0

/* QSFP eeprom offsets*/
#define QSFP_EEPROM_OFFSET_TXDIS 0x56
#define QSFP_EEPROM_OFFSET_LPMODE 0x5D

/* QSFP DD eeprom offsets*/
#define QSFP_DD_EEPROM_OFFSET_BANK_SELECT 0x7E
#define QSFP_DD_EEPROM_OFFSET_PAGE_SELECT 0x7F
#define QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1 0x9B
#define QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX 0x82
#define QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW 0x1A

/*QSFP Specific*/
#define QSFP_LPMODE 0x3

/* QSFP DD Specific*/
#define QSFP_DD_PAGE_ADMIN_INFO 0x0
#define QSFP_DD_PAGE_ADVERTISING 0x1
#define QSFP_DD_PAGE_LANE_CTRL 0x10
#define QSFP_DD_LOWER_OFFSET_STATUS 0x02
#define QSFP_DD_FLAT_MEM 0x80                          /* byte 0x02 bit 7 */
#define QSFP_DD_P01H_TX_DISABLE_SUPPORT 0x2
#define QSFP_DD_LPMODE 0x10

/* OSFP IDENTIFIER Specific*/
#define QSFP_DD_IDENTIFIER 0x18

int sfp_map_bus[] ={25, 26, 27, 28, 29, 30, 31, 32,
                    33, 34, 35, 36, 37, 38, 39, 40,
                    41, 42, 43, 44, 45, 46, 47, 48,
                    49, 50, 51, 52, 53, 54, 55, 56,
                    57, 58};

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/

int
onlp_sfpi_init(void)
{
    /* Called at initialization time */
    return ONLP_STATUS_OK;
}
int
onlp_sfpi_map_bus_index(int port)
{
    if(port < 0 || port >=34)
        return ONLP_STATUS_E_INTERNAL;
    return sfp_map_bus[port];
}

int
onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /*
     * Ports {0, 34}
     */
    int p;

    for(p = 0; p < 34; p++) {
        AIM_BITMAP_SET(bmap, p);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_is_present(int port)
{
    /*
     * Return 1 if present.
     * Return 0 if not present.
     * Return < 0 if error.
     */
    int present;
    int bus, addr;

    VALIDATE_PORT(port);
        
    if(port >=0 && port < 16)
    {
       addr = 61;
       bus  = 20;
    }
    else
    {
       addr = 62;
       bus  = 21;
    }

	if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, bus, addr, (port+1)) < 0) {
        syslog(LOG_ERR, "Unable to read present status from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return present;
}

int
onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i, val;

    AIM_BITMAP_CLR_ALL(dst);

    /* Only SFP+ ports (32-33) support rx_los; QSFPs have no such signal. */
    for (i = SFP_PORT_MIN; i <= SFP_PORT_MAX; i++) {
        if (onlp_file_read_int(&val, MODULE_RXLOS_FORMAT, 21, 62, i+1) < 0) {
            AIM_LOG_ERROR("Unable to read rx_los from port(%d)\r\n", i);
            return ONLP_STATUS_E_INTERNAL;
        }
        AIM_BITMAP_MOD(dst, i, val);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */
    int size = 0;
    VALIDATE_PORT(port);

    memset(data, 0, 256);

	if(onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, onlp_sfpi_map_bus_index(port)) != ONLP_STATUS_OK) {
        syslog(LOG_ERR, "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        syslog(LOG_ERR, "Unable to read eeprom from port(%d), size is different!", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    FILE* fp;
    char file[64] = {0};

    sprintf(file, PORT_EEPROM_FORMAT, onlp_sfpi_map_bus_index(port));
    fp = fopen(file, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog(LOG_ERR, "Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog(LOG_ERR, "Unable to read the module_eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    int bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_readb(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
    int bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_writeb(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    int bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_readw(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    int bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_writew(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int rv = ONLP_STATUS_E_INTERNAL;
    int addr=0;
    int bus=0;
    int present = 0;
    int lpmode_value = 0;
    int identifier = 0;
    int status_byte = 0;
    int eeprom_control;

    VALIDATE_PORT(port);

    if(port < 16){
        addr=61;
        bus=20;
    }
    else{
        addr=62;
        bus=21;        
    }
    switch(control)
        {
        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
            {
                present = onlp_sfpi_is_present(port);
                if (present == 1) {
                    if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) { //SFP
                        if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, bus, addr, (port+1)) < 0) {
                            syslog(LOG_ERR, "Unable to write tx_disable status to port(%d)", port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else {
                            rv = ONLP_STATUS_OK;
                        }
                    }
                    else { //QSFP

                        identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
                        if(identifier < 0){
                            syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): read identifier from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else if (identifier == QSFP_DD_IDENTIFIER) { /*QSFP DD*/
                            /* Flat-memory CMIS modules do not implement page 01h/10h */
                            if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                                syslog(LOG_ERR, "Failed to read Status byte, unable to write tx_disable status to port(%d)", port);
                                rv = ONLP_STATUS_E_INTERNAL;
                                break;
                            }
                            if (status_byte & QSFP_DD_FLAT_MEM) {
                                rv = ONLP_STATUS_E_UNSUPPORTED;
                                break;
                            }
                            if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                                syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): write page to eeprom fail",
                                        port);
                                goto restore;
                            }
                            if ((eeprom_control = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1)) < 0) {
                                syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): read control from eeprom fail",
                                        port);
                                rv = eeprom_control;
                                goto restore;
                            }
                            if (eeprom_control & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                                if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                                    syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): write bank to eeprom fail",
                                            port);
                                    goto restore;
                                }
                                if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                                    syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): write page to eeprom fail",
                                            port);
                                    goto restore;
                                }
                                if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX, (value & 0xff))) < 0) {
                                    syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): write TX disable to eeprom fail",
                                            port);
                                    goto restore;
                                }
                            } else {
                                rv = ONLP_STATUS_E_UNSUPPORTED;
                                goto restore;
                            }

                        restore:
                            if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
                                syslog(LOG_ERR, "Failed to restore Page Select to Admin Info on port(%d)!", port);
                            }

                            if (rv < 0) {
                                rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                            } else {
                                rv = ONLP_STATUS_OK;
                            }
                        } else { /* QSFP 28 or QSFP+ */
                            /* txdis valid bit(bit0-bit3), xxxx 1111 */
                            value = value&0xf;

                            if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, value) < 0 ){
                                syslog(LOG_ERR, "Unable to write tx_disable status to port(%d): write TX disable to eeprom fail", 
                                        port);
                                rv = ONLP_STATUS_E_INTERNAL;
                            }
                            else {
                                rv = ONLP_STATUS_OK;
                            }
                        }
                    }
                } 
                else {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }
        case ONLP_SFP_CONTROL_RESET:
            {
                VALIDATE_QSFP(port);
                if (onlp_file_write_int(value, MODULE_RESET_FORMAT, bus, addr, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to write reset status to port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }
        case ONLP_SFP_CONTROL_LP_MODE:
            {
                VALIDATE_QSFP(port);
                present = onlp_sfpi_is_present(port);
                if (present == 1) {
                    identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
                    if(identifier < 0){
                        syslog(LOG_ERR, "Unable to write LP mode status to port(%d): read identifier from eeprom fail", 
                                port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else if (identifier == QSFP_DD_IDENTIFIER) { /*QSFP DD*/
                        
                        lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW);
                        if(lpmode_value < 0){
                            syslog(LOG_ERR, "Unable to write LP mode status to port(%d): read LP mode value from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else{
                            if(value)
                                lpmode_value |= QSFP_DD_LPMODE;
                            else
                                lpmode_value &= ~QSFP_DD_LPMODE;
                            if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW, lpmode_value) < 0){
                                syslog(LOG_ERR, "Unable to write LP mode status to port(%d): write LP mode value to eeprom fail", 
                                        port);
                                rv = ONLP_STATUS_E_INTERNAL;
                            }
                            else {
                                rv = ONLP_STATUS_OK;
                            }
                        }


                    } else { /* QSFP 28 or QSFP+*/
                        /* lpmode valid bit(bit0):set LP/txdis mode bit(bit1):set low/high power mode */
                        lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
                        if(lpmode_value < 0){
                            syslog(LOG_ERR, "Unable to write LP mode status to port(%d): read LP mode value from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else{
                            if(value){
                                lpmode_value |= QSFP_LPMODE;
                            } else{
                                lpmode_value &= ~QSFP_LPMODE;
                            }

                            if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE, lpmode_value)< 0){
                                syslog(LOG_ERR, "Unable to write LP mode status to port(%d): write LP mode value to eeprom fail", 
                                        port);
                                rv = ONLP_STATUS_E_INTERNAL;
                            }
                            else {
                                rv = ONLP_STATUS_OK;
                            }
                        }

                    }
                }
                else {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }

        default:
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }

    return rv;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    int rv = ONLP_STATUS_E_INTERNAL;
    int addr = 0;
    int bus  = 0;
    int present = 0;
    int lpmode_value = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;
    int tx_dis = 0;

    VALIDATE_PORT(port);

    if(port < 16){
        addr=61;
        bus=20;
    }
    else{
        addr=62;
        bus=21;        
    }

    switch(control)
        {
        case ONLP_SFP_CONTROL_RX_LOS:
            {
                VALIDATE_SFP(port);
                if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, bus, addr, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to read rx_loss status from port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }

        case ONLP_SFP_CONTROL_TX_FAULT:
            {
                VALIDATE_SFP(port);
                if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, bus, addr, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to read tx_fault status from port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }

        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
            {
                present = onlp_sfpi_is_present(port);
                /* read qsfp eeprom offset of tx disable if qsfp on the port */
                if(present == 1) {
                    if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) {
                        if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, bus, addr, (port+1)) < 0) {
                            syslog(LOG_ERR, "Unable to read tx_disabled status from port(%d)", port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else {
                            rv = ONLP_STATUS_OK;
                        }
                    }
                    else {
                        identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
                        if(identifier < 0){
                            syslog(LOG_ERR, "Unable to read tx_disabled status from port(%d): read identifier from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else if (identifier == QSFP_DD_IDENTIFIER) {/* QSFP DD */
                            /* Flat-memory CMIS modules do not implement page 01h/10h */
                            if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                                syslog(LOG_ERR, "Failed to read Status byte, unable to read tx_disable status from port(%d)", port);
                                rv = ONLP_STATUS_E_INTERNAL;
                                break;
                            }
                            if (status_byte & QSFP_DD_FLAT_MEM) {
                                rv = ONLP_STATUS_E_UNSUPPORTED;
                                break;
                            }
                            if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                                syslog(LOG_ERR, "Failed to switch to Advertising Page on port(%d)", port);
                                goto restore;
                            }
                            if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1)) < 0) {
                                syslog(LOG_ERR, "Failed to read Support Control on port(%d)", port);
                                rv = support_ctrls;
                                goto restore;
                            }
                            if (!(support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT)) {
                                rv = ONLP_STATUS_E_UNSUPPORTED;
                                goto restore;
                            }
                            if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                                syslog(LOG_ERR, "Unable to read tx_disable status from port(%d): write bank to eeprom fail",
                                        port);
                                goto restore;
                            }
                            if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                                syslog(LOG_ERR, "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                                goto restore;
                            }
                            if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX)) < 0) {
                                syslog(LOG_ERR, "Unable to read tx_disable status from port(%d): read TX disable from eeprom fail",
                                        port);
                                rv = tx_dis;
                                goto restore;
                            }

                        restore:
                            if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
                                syslog(LOG_ERR, "Failed to restore Page Select to Admin Info on port(%d)!", port);
                            }

                            if (rv < 0) {
                                rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                            } else {
                                *value = (tx_dis & 0xff);
                                rv = ONLP_STATUS_OK;
                            }
                        }
                        else { /* QSFP 28 or QSFP+ */
                            tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS);
                            if(tx_dis < 0){
                                syslog(LOG_ERR, "Unable to read tx_disable status from port(%d): read TX disable from eeprom fail", 
                                        port);
                                rv = ONLP_STATUS_E_INTERNAL;
                            }
                            else {
                                *value = (tx_dis & 0xf);
                                rv = ONLP_STATUS_OK;
                            }
                        }
                    }
                }
                else {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }

        case ONLP_SFP_CONTROL_RESET:
            {
                VALIDATE_QSFP(port);
                if (onlp_file_read_int(value, MODULE_RESET_FORMAT, bus, addr, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to get reset status to port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }

        case ONLP_SFP_CONTROL_LP_MODE:
            {
                VALIDATE_QSFP(port);
                present = onlp_sfpi_is_present(port);
                if (present == 1) {
                    identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
                    if(identifier < 0){
                        syslog(LOG_ERR, "Unable to read LP mode status from port(%d): read identifier from eeprom fail", 
                                port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
                        /* lpmode valid bit(bit4):Low power requset sw */
                        lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW);
                        if(lpmode_value < 0){
                            syslog(LOG_ERR, "Unable to read LP mode status from port(%d): read LP mode value from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else {
                            *value = !!(lpmode_value & QSFP_DD_LPMODE);
                            rv = ONLP_STATUS_OK;
                        }
                    } else { /* QSFP 28 or QSFP+ */
                        /* lpmode valid bit(bit0):set LP/txdis mode bit(bit1):set low/high power mode */
                        lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
                        if(lpmode_value < 0){
                            syslog(LOG_ERR, "Unable to read LP mode status from port(%d): read LP mode value from eeprom fail", 
                                    port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else {
                            *value = ((lpmode_value & QSFP_LPMODE) == QSFP_LPMODE);
                            rv = ONLP_STATUS_OK;
                        }
                    }
                }
                else {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }
        default:
            rv = ONLP_STATUS_E_UNSUPPORTED;
        }

    return rv;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}
