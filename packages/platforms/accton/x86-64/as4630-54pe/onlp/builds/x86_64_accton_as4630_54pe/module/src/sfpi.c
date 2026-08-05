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
#include "x86_64_accton_as4630_54pe_int.h"
#include "x86_64_accton_as4630_54pe_log.h"

#define PORT_EEPROM_FORMAT              "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT		    "/sys/bus/i2c/devices/%d-00%d/module_present_%d"
#define MODULE_RXLOS_FORMAT             "/sys/bus/i2c/devices/%d-00%d/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT           "/sys/bus/i2c/devices/%d-00%d/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT         "/sys/bus/i2c/devices/%d-00%d/module_tx_disable_%d"
#define MODULE_RESET_FORMAT             "/sys/bus/i2c/devices/%d-00%d/module_reset_%d"
#define MODULE_LPMODE_FORMAT            "/sys/bus/i2c/devices/%d-00%d/module_lpmode_%d"
/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR             0x50
/* QSFP tx disable offset */
#define QSFP_EEPROM_OFFSET_TXDIS        0x56

int port_bus_index[] ={18, 19, 20, 21, 22, 23};
#define PORT_BUS_INDEX(port) (port_bus_index[port-48])

#define VALIDATE(_port) \
    do { \
        if (_port < 48 || _port > 53) { \
            return ONLP_STATUS_E_INVALID; \
        } \
    } while(0)

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < 48 || _port > 51) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < 52 || _port > 53) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

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
onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /*
     * Ports {48, 54}
     */
    int p;

    for(p = 48; p < 54; p++) {
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

    VALIDATE(port);

    addr = 60;
    bus = 3;

    if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, bus, addr, (port+1)) < 0) {
        AIM_LOG_ERROR("Unable to read present status from port(%d)\r\n", port);
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
    int port;
    int value;

    AIM_BITMAP_CLR_ALL(dst);

    /* Only SFP+ ports 48..51 (platform port 49..52) expose rx_los;
     * QSFP ports 52..53 do not carry this signal. Delegate to
     * onlp_sfpi_control_get() so this API shares one sysfs source of
     * truth with the per-port path (module_rx_los_<n> under the CPLD).
     */
    for (port = 48; port <= 51; port++) {
        if (onlp_sfpi_control_get(port, ONLP_SFP_CONTROL_RX_LOS, &value) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to read rx_los status from port(%d)\r\n", port);
            continue;
        }
        AIM_BITMAP_MOD(dst, port, value ? 1 : 0);
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
    VALIDATE(port);

    memset(data, 0, 256);

	if(onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, PORT_BUS_INDEX(port)) != ONLP_STATUS_OK) {
        AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        AIM_LOG_ERROR("Unable to read eeprom from port(%d), size is different!\r\n", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    FILE* fp;
    char file[64] = {0};

    VALIDATE(port);
    sprintf(file, PORT_EEPROM_FORMAT, PORT_BUS_INDEX(port));
    fp = fopen(file, "r");
    if(fp == NULL) {
        AIM_LOG_ERROR("Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        AIM_LOG_ERROR("Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        AIM_LOG_ERROR("Unable to read the module_eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE(port);
    return onlp_i2c_readb(PORT_BUS_INDEX(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
    VALIDATE(port);
    return onlp_i2c_writeb(PORT_BUS_INDEX(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE(port);
    return onlp_i2c_readw(PORT_BUS_INDEX(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    VALIDATE(port);
    return onlp_i2c_writew(PORT_BUS_INDEX(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int addr = 60;
    int bus  = 3;
    int present = 0;

    switch(control)
    {
        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
        {
            VALIDATE(port);
            present = onlp_sfpi_is_present(port);

            if (present == 1) {
                if (port>=48 && port<=51) {
                    if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, bus, addr, (port+1)) < 0) {
                        AIM_LOG_ERROR("Unable to write tx_disable status to port(%d)\r\n", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }

                    return  ONLP_STATUS_OK;
                } else {
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, (value & 0xf)) < 0) {
                        AIM_LOG_ERROR("Unable to write tx_disable status to port(%d)\r\n", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    return ONLP_STATUS_OK;
                }
            } else {
                return ONLP_STATUS_E_INTERNAL;
            }
        }
        case ONLP_SFP_CONTROL_RESET:
        {
            if(port>=52 && port<=53) {
                if (onlp_file_write_int(value, MODULE_RESET_FORMAT, bus, addr, (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to write reset status to port(%d)\r\n", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
            return ONLP_STATUS_E_UNSUPPORTED;
        }
        case ONLP_SFP_CONTROL_LP_MODE:
        {
            if(port>=52 && port<=53) {
                if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT, bus, addr, (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to write lpmode status to port(%d)\r\n", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
            return ONLP_STATUS_E_UNSUPPORTED;
        }
        default:
            break;
    }

    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    int addr = 60;
    int bus  = 3;
    int present = 0;
    int tx_dis = 0;

    switch(control)
    {
        case ONLP_SFP_CONTROL_RX_LOS:
        {
            if(port>=48 && port<=51) {
                if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, bus, addr, (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to read rx_loss status from port(%d)\r\n", port);
                    return  ONLP_STATUS_E_INTERNAL;
                }
                return  ONLP_STATUS_OK;
            }
            return ONLP_STATUS_E_UNSUPPORTED;
        }

        case ONLP_SFP_CONTROL_TX_FAULT:
        {
            if(port>=48 && port<=51) {
                if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, bus, addr, (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to read tx_fault status from port(%d)\r\n", port);
                    return  ONLP_STATUS_E_INTERNAL;
                }
                return  ONLP_STATUS_OK;
            }
            return ONLP_STATUS_E_UNSUPPORTED;
        }

        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
        {
            VALIDATE(port);
            present = onlp_sfpi_is_present(port);

            if (present == 1) {
                if (port>=48 && port<=51) {
                    if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, bus, addr, (port+1)) < 0) {
                        AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d)\r\n", port);
                        return  ONLP_STATUS_E_INTERNAL;
                    }

                    return  ONLP_STATUS_OK;
                } else {
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS)) < 0) {
                        AIM_LOG_ERROR("Unable to read tx_disable status from port(%d)\r\n", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    *value = (tx_dis & 0xf);
                    return ONLP_STATUS_OK;
                }
            } else {
                return  ONLP_STATUS_E_INTERNAL;
            }
        }
        case ONLP_SFP_CONTROL_RESET: 
        {
            if(port>=52 && port<=53) {
                if (onlp_file_read_int(value, MODULE_RESET_FORMAT, bus, addr, (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to read reset status from port(%d)\r\n", port);
                    return ONLP_STATUS_E_INTERNAL;
                 }
                 return  ONLP_STATUS_OK;
           }
           return  ONLP_STATUS_E_UNSUPPORTED;
           
        }

        case ONLP_SFP_CONTROL_LP_MODE:
        {
            if(port>=52 && port<=53) {
                if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT, bus, addr,  (port+1)) < 0) {
                    AIM_LOG_ERROR("Unable to read lpmode status from port(%d)\r\n", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
            return  ONLP_STATUS_E_UNSUPPORTED;
        }
        default:
            break;
    }

    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}
