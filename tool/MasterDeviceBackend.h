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
                size_t size, unsigned long arg = 0) = 0;

        /** Factory: create the backend appropriate for this build.
         *
         * \param socketPath  Socket path for the userspace backend.
         *                    Ignored by the kernel backend.
         */
        static MasterDeviceBackend *create(const std::string &socketPath);
};

/****************************************************************************/

#endif
