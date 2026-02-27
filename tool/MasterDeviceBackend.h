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

#ifndef __MASTER_DEVICE_BACKEND_H__
#define __MASTER_DEVICE_BACKEND_H__

#include <stdint.h>
#include <string>

/****************************************************************************/

/** Abstract backend interface for MasterDevice.
 *
 * The kernel ioctl backend and the userspace socket backend both implement
 * this interface.  The concrete backend is selected at link time via the
 * static factory MasterDeviceBackend::create().
 */
class MasterDeviceBackend
{
    public:
        virtual ~MasterDeviceBackend() {}

        /** Open the connection to master \a index.
         *
         * \param index    Master index (0, 1, ...).
         * \param writable True if write access is required.
         * \throws std::runtime_error on failure.
         */
        virtual void open(unsigned int index, bool writable) = 0;

        /** Close the connection. */
        virtual void close() = 0;

        /** Issue a command to the master.
         *
         * \param cmd   ec_tool_cmd value identifying the command.
         * \param data  Pointer to the data struct, or NULL for argonly cmds.
         * \param size  Size of *data (ignored for ioctl backend).
         * \param arg   Integer argument (used when data == NULL).
         * \return 0 on success, -errno on failure.
         */
        virtual int request(unsigned int cmd, void *data,
                size_t size, uint32_t arg = 0) = 0;

        /** Issue a command with separate trailing data buffer.
         *
         * For commands that carry pointer-based data (SDO upload/download,
         * domain data, SII, register, FoE, SoE), the struct and the
         * trailing blob are sent/received separately.
         *
         * \param cmd             ec_tool_cmd value.
         * \param data            Pointer to the ioctl struct.
         * \param size            Size of the struct.
         * \param trailingIn      Data to append after the struct on send (NULL if none).
         * \param trailingInSize  Size of trailingIn.
         * \param trailingOut     Buffer to receive trailing data after the struct (NULL if none).
         * \param trailingOutSize Size of trailingOut buffer.
         * \return 0 on success, -errno on failure.
         */
        virtual int requestTrailingData(unsigned int cmd, void *data,
                size_t size,
                const void *trailingIn, size_t trailingInSize,
                void *trailingOut, size_t trailingOutSize) = 0;

        /** Factory: create the backend appropriate for this build.
         *
         * \param socketPath  Socket path for the userspace backend.
         *                    Ignored by the kernel backend.
         */
        static MasterDeviceBackend *create(const std::string &socketPath);
};

/****************************************************************************/

#endif
