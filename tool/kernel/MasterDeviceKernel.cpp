/*****************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 ****************************************************************************/

/** \file
 * Kernel ioctl backend for the ethercat command-line tool.
 *
 * Opens /dev/EtherCATN and issues ioctl() calls.  The ec_tool_cmd values are
 * mapped to the corresponding EC_IOCTL_* macros via a compile-time table.
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include <stdexcept>
using namespace std;

#include "../MasterDeviceBackend.h"
#include "../../master/kernel/ioctl.h"

/****************************************************************************/

/** Lookup table: ec_tool_cmd → EC_IOCTL_* macro value. */
static const unsigned long ioctl_cmd_table[] = {
    EC_IOCTL_MODULE,                // EC_CMD_MODULE = 0x00
    EC_IOCTL_MASTER,                // EC_CMD_MASTER = 0x01
    EC_IOCTL_SLAVE,                 // EC_CMD_SLAVE = 0x02
    EC_IOCTL_SLAVE_SYNC,            // EC_CMD_SLAVE_SYNC = 0x03
    EC_IOCTL_SLAVE_SYNC_PDO,        // EC_CMD_SLAVE_SYNC_PDO = 0x04
    EC_IOCTL_SLAVE_SYNC_PDO_ENTRY,  // EC_CMD_SLAVE_SYNC_PDO_ENTRY = 0x05
    EC_IOCTL_DOMAIN,                // EC_CMD_DOMAIN = 0x06
    EC_IOCTL_DOMAIN_FMMU,           // EC_CMD_DOMAIN_FMMU = 0x07
    EC_IOCTL_DOMAIN_DATA,           // EC_CMD_DOMAIN_DATA = 0x08
    EC_IOCTL_MASTER_DEBUG,          // EC_CMD_MASTER_DEBUG = 0x09
    EC_IOCTL_MASTER_RESCAN,         // EC_CMD_MASTER_RESCAN = 0x0a
    EC_IOCTL_SLAVE_STATE,           // EC_CMD_SLAVE_STATE = 0x0b
    EC_IOCTL_SLAVE_SDO,             // EC_CMD_SLAVE_SDO = 0x0c
    EC_IOCTL_SLAVE_SDO_ENTRY,       // EC_CMD_SLAVE_SDO_ENTRY = 0x0d
    EC_IOCTL_SLAVE_SDO_UPLOAD,      // EC_CMD_SLAVE_SDO_UPLOAD = 0x0e
    EC_IOCTL_SLAVE_SDO_DOWNLOAD,    // EC_CMD_SLAVE_SDO_DOWNLOAD = 0x0f
    EC_IOCTL_SLAVE_SII_READ,        // EC_CMD_SLAVE_SII_READ = 0x10
    EC_IOCTL_SLAVE_SII_WRITE,       // EC_CMD_SLAVE_SII_WRITE = 0x11
    EC_IOCTL_SLAVE_REG_READ,        // EC_CMD_SLAVE_REG_READ = 0x12
    EC_IOCTL_SLAVE_REG_WRITE,       // EC_CMD_SLAVE_REG_WRITE = 0x13
    EC_IOCTL_SLAVE_FOE_READ,        // EC_CMD_SLAVE_FOE_READ = 0x14
    EC_IOCTL_SLAVE_FOE_WRITE,       // EC_CMD_SLAVE_FOE_WRITE = 0x15
    EC_IOCTL_SLAVE_SOE_READ,        // EC_CMD_SLAVE_SOE_READ = 0x16
    EC_IOCTL_SLAVE_SOE_WRITE,       // EC_CMD_SLAVE_SOE_WRITE = 0x17
#ifdef EC_EOE
    EC_IOCTL_SLAVE_EOE_IP_PARAM,    // EC_CMD_SLAVE_EOE_IP_PARAM = 0x18
#else
    0,                              // EC_CMD_SLAVE_EOE_IP_PARAM = 0x18 (unused)
#endif
    EC_IOCTL_CONFIG,                // EC_CMD_CONFIG = 0x19
    EC_IOCTL_CONFIG_PDO,            // EC_CMD_CONFIG_PDO = 0x1a
    EC_IOCTL_CONFIG_PDO_ENTRY,      // EC_CMD_CONFIG_PDO_ENTRY = 0x1b
    EC_IOCTL_CONFIG_SDO,            // EC_CMD_CONFIG_SDO = 0x1c
    EC_IOCTL_CONFIG_IDN,            // EC_CMD_CONFIG_IDN = 0x1d
    EC_IOCTL_CONFIG_FLAG,           // EC_CMD_CONFIG_FLAG = 0x1e
#ifdef EC_EOE
    EC_IOCTL_CONFIG_EOE_IP_PARAM,   // EC_CMD_CONFIG_EOE_IP_PARAM = 0x1f
    EC_IOCTL_EOE_HANDLER,           // EC_CMD_EOE_HANDLER = 0x20
#else
    0,                              // EC_CMD_CONFIG_EOE_IP_PARAM = 0x1f (unused)
    0,                              // EC_CMD_EOE_HANDLER = 0x20 (unused)
#endif
};

/****************************************************************************/

class MasterDeviceKernel : public MasterDeviceBackend
{
    public:
        MasterDeviceKernel():
            fd(-1)
        {}

        ~MasterDeviceKernel()
        {
            close();
        }

        void open(unsigned int index, bool writable)
        {
            if (fd != -1)
                return; // already open

            stringstream deviceName;
            deviceName << "/dev/EtherCAT" << index;

            fd = ::open(deviceName.str().c_str(),
                    writable ? O_RDWR : O_RDONLY);
            if (fd == -1) {
                stringstream err;
                err << "Failed to open master device "
                    << deviceName.str() << ": " << strerror(errno);
                throw runtime_error(err.str());
            }
        }

        void close()
        {
            if (fd != -1) {
                ::close(fd);
                fd = -1;
            }
        }

        int request(unsigned int cmd, void *data,
                size_t /*size*/, unsigned long arg = 0)
        {
            if (cmd >= sizeof(ioctl_cmd_table) / sizeof(ioctl_cmd_table[0]))
                return -EINVAL;

            unsigned long ioctlCmd = ioctl_cmd_table[cmd];
            int ret;

            if (data == NULL) {
                ret = ioctl(fd, ioctlCmd, arg);
            } else {
                ret = ioctl(fd, ioctlCmd, data);
            }

            return ret < 0 ? -errno : ret;
        }

    private:
        int fd;
};


/****************************************************************************/

MasterDeviceBackend *MasterDeviceBackend::create(const string & /*socketPath*/)
{
    return new MasterDeviceKernel();
}

/****************************************************************************/
