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
 * Userspace Unix-socket backend for the ethercat command-line tool.
 *
 * Connects to a Unix domain socket exposed by the userspace master library
 * (libethercat.so running in the application process).  Each request/response
 * follows the ec_ipc_request_t / ec_ipc_response_t wire protocol defined in
 * ioctl_types.h.
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sstream>
#include <stdexcept>
using namespace std;

#include "../MasterDeviceBackend.h"
#include "ioctl_types.h"

/****************************************************************************/

/** Send exactly \a len bytes, retrying on EINTR. */
static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = static_cast<const char *>(buf);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = send(fd, p, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                return -errno;
            return -ECONNRESET;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

/** Receive exactly \a len bytes, retrying on EINTR. */
static int recv_all(int fd, void *buf, size_t len)
{
    char *p = static_cast<char *>(buf);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = recv(fd, p, remaining, MSG_WAITALL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        if (n == 0)
            return -ECONNRESET;
        p += n;
        remaining -= n;
    }
    return 0;
}

/****************************************************************************/

class MasterDeviceUspace : public MasterDeviceBackend
{
    public:
        MasterDeviceUspace(const string &socketPath):
            socketPath(socketPath.empty()
                    ? EC_IPC_DEFAULT_SOCKET_PATH : socketPath),
            sockfd(-1),
            masterIndex(0U),
            writable(false)
        {}

        ~MasterDeviceUspace()
        {
            close();
        }

        void open(unsigned int index, bool writable)
        {
            if (sockfd != -1)
                return; // already open
            masterIndex = index;
            this->writable = writable;
            /* TODO: send writable flag in IPC request header for
             * server-side access control enforcement in a future phase. */

            sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sockfd == -1) {
                stringstream err;
                err << "Failed to create socket: " << strerror(errno);
                throw runtime_error(err.str());
            }

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socketPath.c_str(),
                    sizeof(addr.sun_path) - 1);

            if (connect(sockfd,
                        reinterpret_cast<struct sockaddr *>(&addr),
                        sizeof(addr)) == -1) {
                int savedErrno = errno;
                ::close(sockfd);
                sockfd = -1;
                stringstream err;
                err << "Failed to connect to master socket "
                    << socketPath << ": " << strerror(savedErrno);
                throw runtime_error(err.str());
            }
        }

        void close()
        {
            if (sockfd != -1) {
                ::close(sockfd);
                sockfd = -1;
            }
        }

        int request(unsigned int cmd, void *data,
                size_t size, uint32_t arg = 0)
        {
            ec_ipc_request_t req;
            req.version_magic = EC_IOCTL_VERSION_MAGIC;
            req.cmd = cmd;
            req.master_index = masterIndex;

            /* Determine payload before sending the header. */
            uint32_t argVal = 0;
            if (data && size > 0) {
                req.data_size = static_cast<uint32_t>(size);
            } else if (!data) {
                argVal = arg;
                req.data_size = static_cast<uint32_t>(sizeof(argVal));
            } else {
                req.data_size = 0;
            }

            int ret = send_all(sockfd, &req, sizeof(req));
            if (ret < 0)
                return ret;

            if (data && size > 0) {
                ret = send_all(sockfd, data, size);
                if (ret < 0)
                    return ret;
            } else if (!data) {
                ret = send_all(sockfd, &argVal, sizeof(argVal));
                if (ret < 0)
                    return ret;
            }

            ec_ipc_response_t resp;
            ret = recv_all(sockfd, &resp, sizeof(resp));
            if (ret < 0)
                return ret;

            if (resp.data_size > 0 && data && size > 0) {
                size_t recvSize = resp.data_size < size ? resp.data_size : size;
                ret = recv_all(sockfd, data, recvSize);
                if (ret < 0)
                    return ret;
                /* Discard any extra bytes. */
                if (resp.data_size > size) {
                    char discard[256];
                    size_t remaining = resp.data_size - size;
                    while (remaining > 0) {
                        size_t chunk = remaining < sizeof(discard)
                                ? remaining : sizeof(discard);
                        ret = recv_all(sockfd, discard, chunk);
                        if (ret < 0)
                            return ret;
                        remaining -= chunk;
                    }
                }
            } else if (resp.data_size > 0) {
                /* Drain response payload we cannot store. */
                char discard[256];
                size_t remaining = resp.data_size;
                while (remaining > 0) {
                    size_t chunk = remaining < sizeof(discard)
                            ? remaining : sizeof(discard);
                    ret = recv_all(sockfd, discard, chunk);
                    if (ret < 0)
                        return ret;
                    remaining -= chunk;
                }
            }

            return resp.ret;
        }

    private:
        string socketPath;
        int sockfd;
        unsigned int masterIndex;
        bool writable;
};

/****************************************************************************/

MasterDeviceBackend *MasterDeviceBackend::create(const string &socketPath)
{
    return new MasterDeviceUspace(socketPath);
}

/****************************************************************************/
