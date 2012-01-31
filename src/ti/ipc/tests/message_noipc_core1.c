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
/*
 *  ======== message_core1.c ========
 *  message example on a multiprocessor system
 *
 *  This is an example program that uses MessageQ to pass a message
 *  from one core to another.
 *
 *  Each processor creates its own MessageQ first and then will try to open
 *  a remote processor's MessageQ.  Each processor will either create a
 *  MessageQTransportShm object or open an object that was created by a
 *  remote processor.  The HeapBuf for MessageQ will only be created
 *  by one processor and opened by all other processors.
 */

#include <xdc/std.h>
#include <string.h>

/*  -----------------------------------XDC.RUNTIME module Headers    */
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Memory.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/IHeap.h>
#include <xdc/runtime/Diags.h>

/*  ----------------------------------- IPC module Headers           */
#include <ti/ipc/MessageQ.h>
#include <ti/ipc/MultiProc.h>
#include <ti/ipc/transports/TransportVirtioSetup.h>

/*  ----------------------------------- BIOS6 module Headers         */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/heaps/HeapBuf.h>
#include <ti/sysbios/knl/Task.h>

/*  ----------------------------------- To get globals from .cfg Header */
#include <xdc/cfg/global.h>

/*
 * TBD: This to get the TransportVirtio_attach() called in lieu of "correct"
 * way to hook in to BIOS Ipc_start().
 * Must be done after BIOS_start(), as TransportVirtio startup relies on
 * passing an interrupt handshake.
 */
void myIpcStart()
{
    Int     status;
    UInt    sysm3ProcId = MultiProc_getId("CORE0");

    /* call TransportCircSetup attach to remote processor */
    status = TransportVirtioSetup_attach(sysm3ProcId, 0);

    Assert_isTrue(status >= 0, NULL);
}


/*
 *  ======== tsk1_func ========
 *  Receive and return messages
 */
Void tsk1_func(UArg arg0, UArg arg1)
{
    MessageQ_Msg     msg;
    MessageQ_QueueId remoteQueueId;
    MessageQ_Msg     getMsg;
    MessageQ_Handle  messageQ;
    Int              status;
    UInt16           msgId = 0;
    UInt             sysm3ProcId = MultiProc_getId("CORE0");

    System_printf("tsk1_func: In tsk1_func.\n");

    /* Get our Transport loaded in absence of Ipc module: */
    myIpcStart();

    /* Create a message queue. Using SyncSem as the synchronizer */
    messageQ = MessageQ_create(SLAVE_MESSAGEQNAME, NULL);
    if (messageQ == NULL) {
        System_abort("MessageQ_create failed\n" );
    }

    /* No NameServer yet, so assume QueueId is same on both M3's: */
    remoteQueueId = MessageQ_getQueueId(messageQ);
    System_printf("tsk1_func: created messageQ: QueueID: 0x%x\n",
                   remoteQueueId);

    /* Force procId to be the destination: */
    remoteQueueId = (remoteQueueId & 0x0000FFFF) | (sysm3ProcId << 16);

#if 0
    /* Open the remote message queue. Spin until it is ready. */
    do {
        status = MessageQ_open(HOST_MESSAGEQNAME, &remoteQueueId);
    }
    while (status != MessageQ_S_SUCCESS);

    System_printf("tsk1_func: opened remote messageQ.\n");
#else
    /*
     * Give enough time for other side to create his messageQ.
     * Remove this hack once MessageQ_open() is implemented, which will
     * provide the expected sync mechanism over NameServer.
     */
    System_printf("Task Sleep...\n");
    Task_sleep(1000);
#endif

    /* Send the message to the core1 and wait for a message from core1 */
    System_printf("Start the main loop\n");
    while (msgId < NUMLOOPS) {
        /* Ping-pong the same message around the processors */
        msg = MessageQ_alloc(HEAPID, HEAP_MSGSIZE);
        if (msg == NULL) {
           System_abort("MessageQ_alloc failed\n" );
        }

        /* Allow this message to be traced as it goes between processors: */
        MessageQ_setMsgTrace(msg, TRUE);

        /* Increment...the remote side will check this */
        msgId++;
        MessageQ_setMsgId(msg, msgId);

        System_printf("Sending a message #%d to core %d\n", msgId, sysm3ProcId);

        status = MessageQ_put(remoteQueueId, msg);
        if (status != MessageQ_S_SUCCESS) {
           System_abort("MessageQ_put had a failure/error\n");
        }

        /* Get a message */
        status = MessageQ_get(messageQ, &getMsg, MessageQ_FOREVER);
        if (status != MessageQ_S_SUCCESS) {
           System_abort("This should not happen since timeout is forever\n");
        }

        System_printf("Received message #%d from core %d\n",
                       MessageQ_getMsgId(getMsg), sysm3ProcId);

        /* test id of message received */
        if (MessageQ_getMsgId(getMsg) != msgId) {
            System_abort("The id received is incorrect!\n");
        }
        MessageQ_free(getMsg);
    }

    System_printf("Test complete!\n");
    System_exit(0);
}

/*
 *  ======== main ========
 */
Int main(Int argc, Char* argv[])
{
    Error_Block            eb;
    Ptr                    buf;
    HeapBuf_Handle         heapHandle;
    HeapBuf_Params         heapBufParams;

    /* Initialize the Error_Block. This is required before using it */
    Error_init(&eb);

    System_printf("main: MultiProc id = %d\n", MultiProc_self());

    buf = Memory_alloc(0, (HEAP_NUMMSGS * HEAP_MSGSIZE) + HEAP_ALIGN, 8, &eb);

    /*
     *  Create the heap that will be used to allocate messages.
     */
    HeapBuf_Params_init(&heapBufParams);
    heapBufParams.align          = 8;
    heapBufParams.numBlocks      = HEAP_NUMMSGS;
    heapBufParams.blockSize      = HEAP_MSGSIZE;
    heapBufParams.bufSize        = HEAP_NUMMSGS * HEAP_MSGSIZE;
    heapBufParams.buf            = buf;
    heapHandle = HeapBuf_create(&heapBufParams, &eb);
    if (heapHandle == NULL) {
        System_abort("HeapBuf_create failed\n" );
    }

    /* Register this heap with MessageQ */
    MessageQ_registerHeap((IHeap_Handle)(heapHandle), HEAPID);

    BIOS_start();
    return (0);
}
