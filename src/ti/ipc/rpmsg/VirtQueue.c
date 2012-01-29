/*
 * Copyright (c) 2011, Texas Instruments Incorporated
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
/** ============================================================================
 *  @file       VirtQueue.c
 *
 *  @brief      Virtio Queue implementation for BIOS
 *
 *  Differences between BIOS version and Linux kernel (include/linux/virtio.h):
 *  - Renamed module from virtio.h to VirtQueue_Object.h to match the API prefixes;
 *  - BIOS (XDC) types and CamelCasing used;
 *  - virtio_device concept removed (i.e, assumes no containing device);
 *  - simplified scatterlist from Linux version;
 *  - The notify function is implicit in the implementation, and not provided
 *    by the client, as it is in Linux virtio.
 *
 *  All VirtQueue operations can be called in any context.
 *
 *  The virtio header should be included in an application as follows:
 *  @code
 *  #include <ti/ipc/rpmsg/VirtQueue.h>
 *  @endcode
 *
 */

#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <xdc/runtime/Log.h>
#include <xdc/runtime/Diags.h>

#include <ti/sysbios/hal/Hwi.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/hal/Cache.h>

#include <ti/ipc/rpmsg/InterruptM3.h>
#include <ti/pm/IpcPower.h>

#include <ti/ipc/MultiProc.h>

#include <string.h>

#include <ti/ipc/rpmsg/virtio_ring.h>
#include <ti/ipc/rpmsg/VirtQueue.h>
#include <ti/ipc/rpmsg/_VirtQueue.h>

/*
 * Define APPM3_IS_HOST to enable VirtioTransport test where appM3 is host and
 * sysM3 is slave.  Otherwise, VirtQueue defaults to Linux/A9 as host, and
 * appM3 and sysM3 as slaves (where sysM3 routes interrupts to appM3).
 */
//#define APPM3_IS_HOST

/* Used for defining the size of the virtqueue registry */
#define NUM_QUEUES                      5

/*
 * enum - Predefined Mailbox Messages
 *
 * @RP_MSG_MBOX_READY: informs the M3's that we're up and running. will be
 * followed by another mailbox message that carries the A9's virtual address
 * of the shared buffer. This would allow the A9's drivers to send virtual
 * addresses of the buffers.
 *
 * @RP_MSG_MBOX_STATE_CHANGE: informs the receiver that there is an inbound
 * message waiting in its own receive-side vring. please note that currently
 * this message is optional: alternatively, one can explicitly send the index
 * of the triggered virtqueue itself. the preferred approach will be decided
 * as we progress and experiment with those design ideas.
 *
 * @RP_MSG_MBOX_CRASH: this message indicates that the BIOS side is unhappy
 *
 * @RP_MBOX_ECHO_REQUEST: this message requests the remote processor to reply
 * with RP_MBOX_ECHO_REPLY
 *
 * @RP_MBOX_ECHO_REPLY: this is a reply that is sent when RP_MBOX_ECHO_REQUEST
 * is received.
 *
 * @RP_MBOX_ABORT_REQUEST:  tells the M3 to crash on demand
 */
enum {
    RP_MSG_MBOX_READY           = (Int)0xFFFFFF00,
    RP_MSG_MBOX_STATE_CHANGE    = (Int)0xFFFFFF01,
    RP_MSG_MBOX_CRASH           = (Int)0xFFFFFF02,
    RP_MBOX_ECHO_REQUEST        = (Int)0xFFFFFF03,
    RP_MBOX_ECHO_REPLY          = (Int)0xFFFFFF04,
    RP_MBOX_ABORT_REQUEST       = (Int)0xFFFFFF05,
    RP_MSG_FLUSH_CACHE          = (Int)0xFFFFFF06,
    RP_MSG_HIBERNATION          = (Int)0xFFFFFF07
};

#define DIV_ROUND_UP(n,d)   (((n) + (d) - 1) / (d))
#define RP_MSG_BUFS_SPACE   (RP_MSG_NUM_BUFS * RP_MSG_BUF_SIZE * 2)

#define PAGE_SIZE           (4096)
/*
 * The alignment to use between consumer and producer parts of vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your BIOS image as well
 */
#define RP_MSG_VRING_ALIGN  (4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RP_MSG_RING_SIZE    ((DIV_ROUND_UP(vring_size(RP_MSG_NUM_BUFS, \
                            RP_MSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

/* The total IPC space needed to communicate with a remote processor */
#define RPMSG_IPC_MEM   (RP_MSG_BUFS_SPACE + 2 * RP_MSG_RING_SIZE)

#define ID_SYSM3_TO_A9      0
#define ID_A9_TO_SYSM3      1
#define ID_APPM3_TO_A9      2
#define ID_A9_TO_APPM3      3

static UInt numQueues = 0;
static struct VirtQueue_Object *queueRegistry[NUM_QUEUES];

static UInt16 hostProcId;
static UInt16 dspProcId;
static UInt16 sysm3ProcId;
static UInt16 appm3ProcId;

#ifdef APPM3_IS_HOST
static UInt16 synced = 0;
#endif

static inline Void * mapPAtoVA(UInt pa)
{
    return (Void *)((pa & 0x000fffffU) | 0xa0000000U);
}

static inline UInt mapVAtoPA(Void * va)
{
    return ((UInt)va & 0x000fffffU) | 0xa9000000U;
}

/*!
 * ======== VirtQueue_kick ========
 */
Void VirtQueue_kick(VirtQueue_Handle vq)
{
    /* For now, simply interrupt remote processor */
    if (vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT) {
        Log_print0(Diags_USER1,
                "VirtQueue_kick: no kick because of VRING_AVAIL_F_NO_INTERRUPT\n");
        return;
    }

    Log_print2(Diags_USER1,
            "VirtQueue_kick: Sending interrupt to proc %d with payload 0x%x\n",
            (IArg)vq->procId, (IArg)vq->id);
    InterruptM3_intSend(vq->procId, vq->id);
}

/*!
 * ======== VirtQueue_addUsedBuf ========
 */
Int VirtQueue_addUsedBuf(VirtQueue_Handle vq, Int16 head)
{
    struct vring_used_elem *used;

    Log_print3(Diags_USER1,
       "addUsedBuf vq->id: %d head: %d used->idx: %d",
        vq->id, head, vq->vring.used->idx);

    if ((head > vq->vring.num) || (head < 0)) {
        Error_raise(NULL, Error_E_generic, 0, 0);
    }

    /*
    * The virtqueue contains a ring of used buffers.  Get a pointer to the
    * next entry in that used ring.
    */
    used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
    used->id = head;
    used->len = RP_MSG_BUF_SIZE;

    vq->vring.used->idx++;

    return (0);
}

/*!
 * ======== VirtQueue_addAvailBuf ========
 */
Int VirtQueue_addAvailBuf(VirtQueue_Object *vq, Void *buf)
{
    UInt16 avail;

    Log_print3(Diags_USER1,
       "addAvailBuf vq->id: %d vq->num_free: %d avail->idx: %d",
        vq->id, vq->num_free, vq->vring.avail->idx);

    if (vq->num_free == 0) {
        /* There's no more space */
        Error_raise(NULL, Error_E_generic, 0, 0);
    }
    vq->num_free--;

    avail =  vq->vring.avail->idx++ % vq->vring.num;

    vq->vring.desc[avail].addr = mapVAtoPA(buf);
    vq->vring.desc[avail].len = RP_MSG_BUF_SIZE;

    return (vq->num_free);
}

/*!
 * ======== VirtQueue_getUsedBuf ========
 */
Void *VirtQueue_getUsedBuf(VirtQueue_Object *vq)
{
    UInt16 head;
    Void *buf;

    Log_print3(Diags_USER1,
       "getUsedBuf vq->id: %d last_used_idx: %d used->idx: %d",
        vq->id, vq->last_used_idx, vq->vring.used->idx);

    /* There's nothing available? */
    if (vq->last_used_idx == vq->vring.used->idx) {
        return (NULL);
    }

    head = vq->vring.used->ring[vq->last_used_idx % vq->vring.num].id;
    vq->last_used_idx++;
    vq->num_free++;

    buf = mapPAtoVA(vq->vring.desc[head].addr);

    return (buf);
}

/*!
 * ======== VirtQueue_getAvailBuf ========
 */
Int16 VirtQueue_getAvailBuf(VirtQueue_Handle vq, Void **buf)
{
    UInt16 head;

    Log_print5(Diags_USER1,
       "getAvailBuf vq->id: %d last_avail_idx: %d avail->idx: %d num: %d 0x%x",
        vq->id, vq->last_avail_idx, vq->vring.avail->idx, vq->vring.num,
        (IArg)&vq->vring.avail);

    /* There's nothing available? */
    if (vq->last_avail_idx == vq->vring.avail->idx) {
        if (VirtQueue_isHost(vq))  {
            /* We need to know about added buffers */
            vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;
            /* check again after setting flag */
            if (vq->last_avail_idx == vq->vring.avail->idx)  {
                return (-1);
            }
       }
       else {
            return (-1);
       }
    }

    /* No need to be kicked about added buffers anymore */
    if (VirtQueue_isHost(vq))  {
        vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
    }

    /*
     * Grab the next descriptor number they're advertising, and increment
     * the index we've seen.
     */
#ifndef APPM3_IS_HOST  /* This line works with Linux rpmsg Host: */
    head = vq->vring.avail->ring[vq->last_avail_idx++ % vq->vring.num];
#else  /* This line works with both Linux and appM3 as rpmsg Hosts: */
    head = vq->last_avail_idx++ % vq->vring.num;
#endif

    *buf = mapPAtoVA(vq->vring.desc[head].addr);

    return (head);
}

/*!
 * ======== VirtQueue_disableCallback ========
 */
Void VirtQueue_disableCallback(VirtQueue_Object *vq)
{
    //TODO
    Log_print0(Diags_USER1, "VirtQueue_disableCallback called.");
}

/*!
 * ======== VirtQueue_enableCallback ========
 */
Bool VirtQueue_enableCallback(VirtQueue_Object *vq)
{
    Log_print0(Diags_USER1, "VirtQueue_enableCallback called.");

    //TODO
    return (FALSE);
}

#ifdef APPM3_IS_HOST
/*!
 * ======== VirtQueue_isr ========
 * Note 'arg' is ignored: it is the Hwi argument, not the mailbox argument.
 */
Void VirtQueue_isr(UArg msg)
{
    VirtQueue_Object *vq;

    Log_print1(Diags_USER1, "VirtQueue_isr received msg = 0x%x\n", msg);

    if (MultiProc_self() == sysm3ProcId) {
        switch(msg) {
            case (UInt)RP_MSG_MBOX_READY:
                return;

            case (UInt)RP_MBOX_ECHO_REQUEST:
                InterruptM3_intSend(appm3ProcId, (UInt)(RP_MBOX_ECHO_REPLY));
                synced = 1;
                return;

            case (UInt)RP_MBOX_ABORT_REQUEST:
                {
                    Fxn f = (Fxn)0x0;
                    Log_print0(Diags_USER1, "Crash on demand ...\n");
                    f();
                }
                return;

            case (UInt)RP_MSG_FLUSH_CACHE:
                Cache_wbAll();
                return;

            case (UInt)RP_MSG_HIBERNATION:
                /* Notify Core1 */
                InterruptM3_intSend(appm3ProcId, (UInt)(RP_MSG_HIBERNATION));
                IpcPower_suspend();
                return;

            default:
                /*
                 *  If the message isn't one of the above, it's either part of the
                 *  2-message synchronization sequence or it a virtqueue message
                 */
                break;
        }
    }
    else if (msg & 0xFFFF0000) {
        if (msg == (UInt)RP_MSG_HIBERNATION) {
            IpcPower_suspend();
        }
        if ((MultiProc_self() == appm3ProcId) &&
             (msg == (UInt)(RP_MBOX_ECHO_REPLY)))  {
           synced = 1;
        }
        return;
    }

    vq = queueRegistry[msg];
    if (vq) {
        vq->callback(vq);
    }
}

#else

/*!
 * ======== VirtQueue_isr ========
 * Note 'arg' is ignored: it is the Hwi argument, not the mailbox argument.
 */
Void VirtQueue_isr(UArg msg)
{
    VirtQueue_Object *vq;

    Log_print1(Diags_USER1, "VirtQueue_isr received msg = 0x%x\n", msg);

    if (MultiProc_self() == sysm3ProcId) {
        switch(msg) {
            case (UInt)RP_MSG_MBOX_READY:
                return;

            case (UInt)RP_MBOX_ECHO_REQUEST:
                InterruptM3_intSend(hostProcId, (UInt)(RP_MBOX_ECHO_REPLY));
                return;

            case (UInt)RP_MBOX_ABORT_REQUEST:
                {
                    Fxn f = (Fxn)0x0;
                    Log_print0(Diags_USER1, "Crash on demand ...\n");
                    f();
                }
                return;

            case (UInt)RP_MSG_FLUSH_CACHE:
                Cache_wbAll();
                return;

            case (UInt)RP_MSG_HIBERNATION:
                /* Notify Core1 */
                InterruptM3_intSend(appm3ProcId, (UInt)(RP_MSG_HIBERNATION));
                IpcPower_suspend();
                return;

            default:
                /*
                 *  If the message isn't one of the above, it's either part of the
                 *  2-message synchronization sequence or it a virtqueue message
                 */
                break;
        }
    }
    else if (msg & 0xFFFF0000) {
        if (msg == (UInt)RP_MSG_HIBERNATION) {
            IpcPower_suspend();
        }
        return;
    }

    if (MultiProc_self() == sysm3ProcId &&
            (msg == ID_A9_TO_APPM3 || msg == ID_APPM3_TO_A9)) {
        InterruptM3_intSend(appm3ProcId, (UInt)msg);
    }
    else {
        vq = queueRegistry[msg];
        if (vq) {
            vq->callback(vq);
        }
    }
}
#endif

/*!
 * ======== VirtQueue_create ========
 */
VirtQueue_Object *VirtQueue_create(VirtQueue_callback callback,
        UInt16 remoteProcId, UArg arg)
{
    VirtQueue_Object *vq;
    void *vring_phys;
    Error_Block eb;

    Error_init(&eb);

    vq = Memory_alloc(NULL, sizeof(VirtQueue_Object), 0, &eb);
    if (!vq) {
        return (NULL);
    }

    vq->callback = callback;
    vq->id = numQueues++;
    vq->procId = remoteProcId;
    vq->last_avail_idx = 0;
    vq->last_used_idx = 0;
    vq->num_free = RP_MSG_NUM_BUFS;
    vq->arg = arg;

#ifndef APPM3_IS_HOST
    if (MultiProc_self() == appm3ProcId) {
        vq->id += 2;
    }
#endif

    switch (vq->id) {
        case ID_SYSM3_TO_A9:
            vring_phys = (struct vring *) IPU_MEM_VRING0;
            break;
        case ID_A9_TO_SYSM3:
            vring_phys = (struct vring *) IPU_MEM_VRING1;
            break;
        case ID_APPM3_TO_A9:
            vring_phys = (struct vring *) IPU_MEM_VRING2;
            break;
        case ID_A9_TO_APPM3:
            vring_phys = (struct vring *) IPU_MEM_VRING3;
            break;
    }

    Log_print3(Diags_USER1,
            "vring: %d 0x%x (0x%x)", vq->id, (IArg)vring_phys,
            RP_MSG_RING_SIZE);

    vring_init(&(vq->vring), RP_MSG_NUM_BUFS, vring_phys, RP_MSG_VRING_ALIGN);

    queueRegistry[vq->id] = vq;

    return (vq);
}

/*!
 * ======== VirtQueue_startup ========
 */
Void VirtQueue_startup(Bool isHost)
{
    hostProcId      = MultiProc_getId("HOST");
    dspProcId       = MultiProc_getId("DSP");
    sysm3ProcId     = MultiProc_getId("CORE0");
    appm3ProcId     = MultiProc_getId("CORE1");

    /* Initilize the IpcPower module */
    IpcPower_init();

    if (isHost)  {
       /* Host is responsible for zeroing out vring memory: */
       memset((void *)IPU_MEM_VRING0, 0,
                RP_MSG_RING_SIZE * 2 + RP_MSG_BUFS_SPACE);
    }

    if (MultiProc_self() == appm3ProcId) {
        InterruptM3_intRegister(VirtQueue_isr);
    }
    else if (MultiProc_self() == sysm3ProcId)  {
        InterruptM3_intRegister(VirtQueue_isr);
    }
    else if (MultiProc_self() == dspProcId)  {
        InterruptM3_intRegister(VirtQueue_isr);
    }

    if (isHost)  {
       /* Host sends init sequence to sync slave processor (sysM3): */
       InterruptM3_intSend(sysm3ProcId, (UInt)RP_MSG_MBOX_READY);
       InterruptM3_intSend(sysm3ProcId, (UInt)RP_MBOX_ECHO_REQUEST);
    }

#ifdef APPM3_IS_HOST
    /* Busy wait until host and slaves have synced: */
    while (!synced);
#endif
    Log_print0(Diags_USER1, "Passed VirtQueue_startup\n");
}

/*!
 * ======== postCrashToMailbox ========
 */
Void postCrashToMailbox(Error_Block * eb)
{
    Error_print(eb);
    InterruptM3_intSend(0, (UInt)RP_MSG_MBOX_CRASH);
}


#define CACHE_WB_TICK_PERIOD    5

/*!
 * ======== VirtQueue_cacheWb ========
 *
 * Used for flushing SysMin trace buffer.
 */
Void VirtQueue_cacheWb()
{
    static UInt32 oldticks;

    if (Clock_getTicks() >= (oldticks + CACHE_WB_TICK_PERIOD)) {
        /* Don't keep flushing cache */
        return;
    }

    /* Flush the cache */
    Cache_wbAll();
}
