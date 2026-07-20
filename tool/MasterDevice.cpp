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

#include <errno.h>
#include <string.h>

#include <sstream>
#include <iomanip>
using namespace std;

#include "MasterDevice.h"
#include "MasterDeviceBackend.h"

/****************************************************************************/

string MasterDevice::globalSocketPath;

/****************************************************************************/

MasterDevice::MasterDevice(unsigned int index):
    index(index),
    masterCount(0U),
    backend(MasterDeviceBackend::create(globalSocketPath))
{
}

/****************************************************************************/

MasterDevice::~MasterDevice()
{
    close();
    delete backend;
}

/****************************************************************************/

void MasterDevice::setIndex(unsigned int i)
{
    index = i;
}

/****************************************************************************/

void MasterDevice::setSocketPath(const string &path)
{
    globalSocketPath = path;
}

/****************************************************************************/

void MasterDevice::open(Permissions perm)
{
    ec_ioctl_module_t module_data;

    try {
        backend->open(index, perm == ReadWrite);
    } catch (const runtime_error &e) {
        throw MasterDeviceException(string(e.what()));
    }

    getModule(&module_data);
    if (module_data.ioctl_version_magic != EC_IOCTL_VERSION_MAGIC) {
        stringstream err;
        err << "ioctl() version magic is differing: "
            << module_data.ioctl_version_magic
            << ", ethercat tool: " << EC_IOCTL_VERSION_MAGIC << endl
            << "A probable reason is that the command-line tool" << endl
            << "you are using is built with a different" << endl
            << "source code version than the running master." << endl
            << "Please rebuild and install both the tool and the master.";
        throw MasterDeviceException(err);
    }
    masterCount = module_data.master_count;
}

/****************************************************************************/

void MasterDevice::close()
{
    backend->close();
}

/****************************************************************************/

void MasterDevice::getModule(ec_ioctl_module_t *data)
{
    int ret = backend->request(EC_CMD_MODULE, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get module information: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getMaster(ec_ioctl_master_t *data)
{
    int ret = backend->request(EC_CMD_MASTER, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get master information: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfig(ec_ioctl_config_t *data, unsigned int index)
{
    data->config_index = index;

    int ret = backend->request(EC_CMD_CONFIG, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave configuration: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfigPdo(
        ec_ioctl_config_pdo_t *data,
        unsigned int index,
        uint8_t sync_index,
        uint16_t pdo_pos
        )
{
    data->config_index = index;
    data->sync_index = sync_index;
    data->pdo_pos = pdo_pos;

    int ret = backend->request(EC_CMD_CONFIG_PDO, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave config PDO: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfigPdoEntry(
        ec_ioctl_config_pdo_entry_t *data,
        unsigned int index,
        uint8_t sync_index,
        uint16_t pdo_pos,
        uint8_t entry_pos
        )
{
    data->config_index = index;
    data->sync_index = sync_index;
    data->pdo_pos = pdo_pos;
    data->entry_pos = entry_pos;

    int ret = backend->request(EC_CMD_CONFIG_PDO_ENTRY, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave config PDO entry: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfigSdo(
        ec_ioctl_config_sdo_t *data,
        unsigned int index,
        unsigned int sdo_pos
        )
{
    data->config_index = index;
    data->sdo_pos = sdo_pos;

    int ret = backend->request(EC_CMD_CONFIG_SDO, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave config SDO: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfigIdn(
        ec_ioctl_config_idn_t *data,
        unsigned int index,
        unsigned int pos
        )
{
    data->config_index = index;
    data->idn_pos = pos;

    int ret = backend->request(EC_CMD_CONFIG_IDN, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave config IDN: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getConfigFlag(
        ec_ioctl_config_flag_t *data,
        unsigned int index,
        unsigned int pos
        )
{
    data->config_index = index;
    data->flag_pos = pos;

    int ret = backend->request(EC_CMD_CONFIG_FLAG, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave config flag: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getDomain(ec_ioctl_domain_t *data, unsigned int index)
{
    data->index = index;

    int ret = backend->request(EC_CMD_DOMAIN, data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get domain: ";
        if (errno == EINVAL)
            err << "Domain " << index << " does not exist!";
        else
            err << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getData(ec_ioctl_domain_data_t *data,
        unsigned int domainIndex, unsigned int dataSize, unsigned char *mem)
{
    data->domain_index = domainIndex;
    data->data_size = dataSize;
    data->target = mem;

    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->target;
    data->target = NULL;
    int ret = backend->requestTrailingData(EC_CMD_DOMAIN_DATA,
            data, sizeof(*data),
            NULL, 0,
            mem, dataSize);
    data->target = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get domain data: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getSlave(ec_ioctl_slave_t *slave, uint16_t slaveIndex)
{
    slave->position = slaveIndex;

    int ret = backend->request(EC_CMD_SLAVE, slave, sizeof(*slave));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get slave: ";
        if (errno == EINVAL)
            err << "Slave " << slaveIndex << " does not exist!";
        else
            err << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getFmmu(
        ec_ioctl_domain_fmmu_t *fmmu,
        unsigned int domainIndex,
        unsigned int fmmuIndex
        )
{
    fmmu->domain_index = domainIndex;
    fmmu->fmmu_index = fmmuIndex;

    int ret = backend->request(EC_CMD_DOMAIN_FMMU, fmmu, sizeof(*fmmu));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get domain FMMU: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getSync(
        ec_ioctl_slave_sync_t *sync,
        uint16_t slaveIndex,
        uint8_t syncIndex
        )
{
    sync->slave_position = slaveIndex;
    sync->sync_index = syncIndex;

    int ret = backend->request(EC_CMD_SLAVE_SYNC, sync, sizeof(*sync));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get sync manager: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getPdo(
        ec_ioctl_slave_sync_pdo_t *pdo,
        uint16_t slaveIndex,
        uint8_t syncIndex,
        uint8_t pdoPos
        )
{
    pdo->slave_position = slaveIndex;
    pdo->sync_index = syncIndex;
    pdo->pdo_pos = pdoPos;

    int ret = backend->request(EC_CMD_SLAVE_SYNC_PDO, pdo, sizeof(*pdo));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get PDO: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getPdoEntry(
        ec_ioctl_slave_sync_pdo_entry_t *entry,
        uint16_t slaveIndex,
        uint8_t syncIndex,
        uint8_t pdoPos,
        uint8_t entryPos
        )
{
    entry->slave_position = slaveIndex;
    entry->sync_index = syncIndex;
    entry->pdo_pos = pdoPos;
    entry->entry_pos = entryPos;

    int ret = backend->request(EC_CMD_SLAVE_SYNC_PDO_ENTRY,
            entry, sizeof(*entry));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get PDO entry: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getSdo(
        ec_ioctl_slave_sdo_t *sdo,
        uint16_t slaveIndex,
        uint16_t sdoPosition
        )
{
    sdo->slave_position = slaveIndex;
    sdo->sdo_position = sdoPosition;

    int ret = backend->request(EC_CMD_SLAVE_SDO, sdo, sizeof(*sdo));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get SDO: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::getSdoEntry(
        ec_ioctl_slave_sdo_entry_t *entry,
        uint16_t slaveIndex,
        int sdoSpec,
        uint8_t entrySubindex
        )
{
    entry->slave_position = slaveIndex;
    entry->sdo_spec = sdoSpec;
    entry->sdo_entry_subindex = entrySubindex;

    int ret = backend->request(EC_CMD_SLAVE_SDO_ENTRY, entry, sizeof(*entry));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get SDO entry: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::readSii(
        ec_ioctl_slave_sii_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint16_t *ptr_save = data->words;
    data->words = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SII_READ,
            data, sizeof(*data),
            NULL, 0,
            ptr_save, data->nwords * 2);
    data->words = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to read SII: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::writeSii(
        ec_ioctl_slave_sii_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint16_t *ptr_save = data->words;
    data->words = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SII_WRITE,
            data, sizeof(*data),
            ptr_save, data->nwords * 2,
            NULL, 0);
    data->words = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to write SII: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::readReg(
        ec_ioctl_slave_reg_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->data;
    data->data = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_REG_READ,
            data, sizeof(*data),
            NULL, 0,
            ptr_save, data->size);
    data->data = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to read register: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::writeReg(
        ec_ioctl_slave_reg_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->data;
    data->data = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_REG_WRITE,
            data, sizeof(*data),
            ptr_save, data->size,
            NULL, 0);
    data->data = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to write register: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::readFoe(
        ec_ioctl_slave_foe_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->buffer;
    data->buffer = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_FOE_READ,
            data, sizeof(*data),
            NULL, 0,
            ptr_save, data->buffer_size);
    data->buffer = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to read via FoE: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::writeFoe(
        ec_ioctl_slave_foe_t *data
        )
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->buffer;
    data->buffer = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_FOE_WRITE,
            data, sizeof(*data),
            ptr_save, data->buffer_size,
            NULL, 0);
    data->buffer = ptr_save;
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to write via FoE: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::setDebug(unsigned int debugLevel)
{
    int ret = backend->request(EC_CMD_MASTER_DEBUG, NULL, 0, debugLevel);
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to set debug level: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::rescan()
{
    int ret = backend->request(EC_CMD_MASTER_RESCAN, NULL, 0, 0);
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to command rescan: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::sdoDownload(ec_ioctl_slave_sdo_download_t *data)
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->data;
    data->data = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SDO_DOWNLOAD,
            data, sizeof(*data),
            ptr_save, data->data_size,
            NULL, 0);
    data->data = ptr_save;
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->abort_code) {
            throw MasterDeviceSdoAbortException(data->abort_code);
        } else {
            stringstream err;
            err << "Failed to download SDO: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

/****************************************************************************/

void MasterDevice::sdoUpload(ec_ioctl_slave_sdo_upload_t *data)
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->target;
    data->target = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SDO_UPLOAD,
            data, sizeof(*data),
            NULL, 0,
            ptr_save, data->target_size);
    data->target = ptr_save;
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->abort_code) {
            throw MasterDeviceSdoAbortException(data->abort_code);
        } else {
            stringstream err;
            err << "Failed to upload SDO: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

/****************************************************************************/

void MasterDevice::requestState(
        uint16_t slavePosition,
        uint8_t state
        )
{
    ec_ioctl_slave_state_t data;

    data.slave_position = slavePosition;
    data.al_state = state;

    int ret = backend->request(EC_CMD_SLAVE_STATE, &data, sizeof(data));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to request slave state: ";
        if (errno == EINVAL)
            err << "Slave " << slavePosition << " does not exist!";
        else
            err << strerror(errno);
        throw MasterDeviceException(err);
    }
}

/****************************************************************************/

void MasterDevice::readSoe(ec_ioctl_slave_soe_read_t *data)
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->data;
    data->data = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SOE_READ,
            data, sizeof(*data),
            NULL, 0,
            ptr_save, data->mem_size);
    data->data = ptr_save;
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->error_code) {
            throw MasterDeviceSoeException(data->error_code);
        } else {
            stringstream err;
            err << "Failed to read IDN: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

/****************************************************************************/

void MasterDevice::writeSoe(ec_ioctl_slave_soe_write_t *data)
{
    /* The struct is echoed back over IPC with the server's
     * pointer value; preserve the local pointer and null it in
     * the sent copy so no tool heap address goes on the wire. */
    uint8_t *ptr_save = data->data;
    data->data = NULL;
    int ret = backend->requestTrailingData(EC_CMD_SLAVE_SOE_WRITE,
            data, sizeof(*data),
            ptr_save, data->data_size,
            NULL, 0);
    data->data = ptr_save;
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->error_code) {
            throw MasterDeviceSoeException(data->error_code);
        } else {
            stringstream err;
            err << "Failed to write IDN: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

/****************************************************************************/

#ifdef EC_EOE

void MasterDevice::getEoeHandler(
        ec_ioctl_eoe_handler_t *eoe,
        uint16_t eoeHandlerIndex
        )
{
    eoe->eoe_index = eoeHandlerIndex;

    int ret = backend->request(EC_CMD_EOE_HANDLER, eoe, sizeof(*eoe));
    if (ret < 0) {
        errno = -ret;
        stringstream err;
        err << "Failed to get EoE handler: " << strerror(errno);
        throw MasterDeviceException(err);
    }
}

#endif

/****************************************************************************/

#ifdef EC_EOE

void MasterDevice::getIpParam(ec_ioctl_eoe_ip_t *data, uint16_t config_index)
{
    data->config_index = config_index;

    int ret = backend->request(EC_CMD_CONFIG_EOE_IP_PARAM,
            data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->result) {
            throw MasterDeviceEoeException(data->result);
        } else {
            stringstream err;
            err << "Failed to set IP parameters: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

#endif

/****************************************************************************/

#ifdef EC_EOE

void MasterDevice::setIpParam(ec_ioctl_eoe_ip_t *data)
{
    int ret = backend->request(EC_CMD_SLAVE_EOE_IP_PARAM,
            data, sizeof(*data));
    if (ret < 0) {
        errno = -ret;
        if (errno == EIO && data->result) {
            throw MasterDeviceEoeException(data->result);
        } else {
            stringstream err;
            err << "Failed to set IP parameters: " << strerror(errno);
            throw MasterDeviceException(err);
        }
    }
}

#endif

/****************************************************************************/
