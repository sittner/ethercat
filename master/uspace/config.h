/*****************************************************************************
 *
 *  Standalone userspace EtherCAT master build configuration.
 *  This file replaces the autoconf-generated config.h for the standalone
 *  master/uspace/ build.
 *
 ****************************************************************************/

#ifndef __EC_USPACE_CONFIG_H__
#define __EC_USPACE_CONFIG_H__

/** Package version string. */
#define VERSION "1.6.8"

/** Source revision (0 for standalone builds). */
#define REV 0

/** Maximum number of EtherCAT devices per master (1 for userspace). */
#define EC_MAX_NUM_DEVICES 1

/** EoE support disabled for standalone userspace build. */
#undef EC_EOE

#endif /* __EC_USPACE_CONFIG_H__ */
