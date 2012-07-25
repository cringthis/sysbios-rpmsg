/*
 * Copyright (c) 2011-2012, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  ======== rsc_table_ipu.h ========
 *
 *  Define the resource table entries for all IPU cores. This will be
 *  incorporated into corresponding base images, and used by the remoteproc
 *  on the host-side to allocated/reserve resources.
 *
 */

#ifndef _RSC_TABLE_IPU_H_
#define _RSC_TABLE_IPU_H_

#include <ti/resources/rsc_types.h>

/* Ducati Memory Map: */
#define L4_44XX_BASE            0x4a000000

#define L4_PERIPHERAL_L4CFG     (L4_44XX_BASE)
#define IPU_PERIPHERAL_L4CFG    0xAA000000

#define L4_PERIPHERAL_L4PER     0x48000000
#define IPU_PERIPHERAL_L4PER    0xA8000000

#define L4_PERIPHERAL_L4EMU     0x54000000
#define IPU_PERIPHERAL_L4EMU    0xB4000000

#define L3_IVAHD_CONFIG         0x5A000000
#define IPU_IVAHD_CONFIG        0xBA000000

#define L3_IVAHD_SL2            0x5B000000
#define IPU_IVAHD_SL2           0xBB000000

#define L3_TILER_MODE_0_1       0x60000000
#define IPU_TILER_MODE_0_1      0x60000000

#define L3_TILER_MODE_2         0x70000000
#define IPU_TILER_MODE_2        0x70000000

#define L3_TILER_MODE_3         0x78000000
#define IPU_TILER_MODE_3        0x78000000

#define L3_IVAHD_CONFIG         0x5A000000
#define IPU_IVAHD_CONFIG        0xBA000000

#define L3_IVAHD_SL2            0x5B000000
#define IPU_IVAHD_SL2           0xBB000000

#define TEXT_DA                 0x00000000
#define DATA_DA                 0x80000000

#define IPC_DA                  0xA0000000
#define IPC_PA                  0xA9000000

#define RPMSG_VRING0_DA         0xA0000000
#define RPMSG_VRING1_DA         0xA0004000

#define CONSOLE_VRING0_DA       0xA0008000
#define CONSOLE_VRING1_DA       0xA000C000

#define BUFS0_DA                0xA0040000
#define BUFS1_DA                0xA0080000

/*
 * sizes of the virtqueues (expressed in number of buffers supported,
 * and must be power of 2)
 */
#define RPMSG_VQ0_SIZE          256
#define RPMSG_VQ1_SIZE          256

#define CONSOLE_VQ0_SIZE        256
#define CONSOLE_VQ1_SIZE        256

#ifndef DATA_SIZE
#  define DATA_SIZE  (SZ_1M * 96)  /* OMX is a little piggy */
#endif

#  define TEXT_SIZE  (SZ_4M)

/* flip up bits whose indices represent features we support */
#define RPMSG_IPU_C0_FEATURES         1

struct resource_table {
	UInt32 version;
	UInt32 num;
	UInt32 reserved[2];
	UInt32 offset[12];

	/* rpmsg vdev entry */
	struct fw_rsc_vdev rpmsg_vdev;
	struct fw_rsc_vdev_vring rpmsg_vring0;
	struct fw_rsc_vdev_vring rpmsg_vring1;

	/* data carveout entry */
	struct fw_rsc_carveout data_cout;

	/* text carveout entry */
	struct fw_rsc_carveout text_cout;

	/* trace entry */
	struct fw_rsc_trace trace;

	/* devmem entry */
	struct fw_rsc_devmem devmem0;

	/* devmem entry */
	struct fw_rsc_devmem devmem1;

	/* devmem entry */
	struct fw_rsc_devmem devmem2;

	/* devmem entry */
	struct fw_rsc_devmem devmem3;

	/* devmem entry */
	struct fw_rsc_devmem devmem4;

	/* devmem entry */
	struct fw_rsc_devmem devmem5;

	/* devmem entry */
	struct fw_rsc_devmem devmem6;

	/* devmem entry */
	struct fw_rsc_devmem devmem7;
};

#define TRACEBUFADDR (UInt32)&xdc_runtime_SysMin_Module_State_0_outbuf__A

#pragma DATA_SECTION(ti_resources_ResourceTable, ".resource_table")
#pragma DATA_ALIGN(ti_resources_ResourceTable, 4096)

struct resource_table ti_resources_ResourceTable = {
	1, /* we're the first version that implements this */
	12, /* number of entries in the table */
	0, 0, /* reserved, must be zero */
	/* offsets to entries */
	{
		offsetof(struct resource_table, rpmsg_vdev),
		offsetof(struct resource_table, data_cout),
		offsetof(struct resource_table, text_cout),
		offsetof(struct resource_table, trace),
		offsetof(struct resource_table, devmem0),
		offsetof(struct resource_table, devmem1),
		offsetof(struct resource_table, devmem2),
		offsetof(struct resource_table, devmem3),
		offsetof(struct resource_table, devmem4),
		offsetof(struct resource_table, devmem5),
		offsetof(struct resource_table, devmem6),
		offsetof(struct resource_table, devmem7),
	},

	/* rpmsg vdev entry */
	{
		TYPE_VDEV, VIRTIO_ID_RPMSG, 0,
		RPMSG_IPU_C0_FEATURES, 0, 0, 0, 2, { 0, 0 },
		/* no config data */
	},
	/* the two vrings */
	{ RPMSG_VRING0_DA, 4096, RPMSG_VQ0_SIZE, 1, 0 },
	{ RPMSG_VRING1_DA, 4096, RPMSG_VQ1_SIZE, 2, 0 },

	{
		TYPE_CARVEOUT, DATA_DA, 0, DATA_SIZE, 0, 0, "IPU_MEM_DATA",
	},

	{
		TYPE_CARVEOUT, TEXT_DA, 0, TEXT_SIZE, 0, 0, "IPU_MEM_DATA",
	},

	{
		TYPE_TRACE, TRACEBUFADDR, 0x8000, 0, "trace:sysm3",
	},

	{
		TYPE_DEVMEM, IPC_DA, IPC_PA, SZ_1M, 0, 0, "IPU_MEM_IPC",
	},

	{
		TYPE_DEVMEM,
		IPU_TILER_MODE_0_1, L3_TILER_MODE_0_1,
		SZ_256M, 0, 0, "IPU_TILER_MODE_0_1",
	},

	{
		TYPE_DEVMEM,
		IPU_TILER_MODE_2, L3_TILER_MODE_2,
		SZ_128M, 0, 0, "IPU_TILER_MODE_2",
	},

	{
		TYPE_DEVMEM,
		IPU_TILER_MODE_3, L3_TILER_MODE_3,
		SZ_128M, 0, 0, "IPU_TILER_MODE_3",
	},

	{
		TYPE_DEVMEM,
		IPU_PERIPHERAL_L4CFG, L4_PERIPHERAL_L4CFG,
		SZ_16M, 0, 0, "IPU_PERIPHERAL_L4CFG",
	},

	{
		TYPE_DEVMEM,
		IPU_PERIPHERAL_L4PER, L4_PERIPHERAL_L4PER,
		SZ_16M, 0, 0, "IPU_PERIPHERAL_L4PER",
	},

	{
		TYPE_DEVMEM,
		IPU_IVAHD_CONFIG, L3_IVAHD_CONFIG,
		SZ_16M, 0, 0, "IPU_IVAHD_CONFIG",
	},

	{
		TYPE_DEVMEM,
		IPU_IVAHD_SL2, L3_IVAHD_SL2,
		SZ_16M, 0, 0, "IPU_IVAHD_SL2",
	},
};

#endif /* _RSC_TABLE_IPU_H_ */