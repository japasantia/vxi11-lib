#include "vxi11_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <thread>

/* OPEN FUNCTIONS */

int Vxi11_OpenDevice(VxiHandle* pHandle, const char* pIp, char* pDevice)
{
    pHandle->pClient = clnt_create(pIp, DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");

    if (pHandle->pClient == NULL)
    {
        clnt_pcreateerror(pIp);
        return -1;
    }

    return Vxi11_OpenLink(pIp, pDevice, pHandle);
}

int Vxi11_OpenDevice(VxiHandle* pHandle, const char* pIp)
{
    char device[6];
    strncpy(device,"inst0",6);

    return  Vxi11_OpenDevice(pHandle, pIp, device);
}

int Vxi11_OpenLink(VxiHandle* pHandle, const char* pIp, char* pDevice)
{
    Create_LinkParms linkParms;

    linkParms.clientId = (long)(pHandle->pClient);
    linkParms.lockDevice = 0;
    linkParms.lock_timeout = VXI11_DEFAULT_TIMEOUT;
    linkParms.device = pDevice;

    pHandle->pLink = (Create_LinkResp*) calloc(1, sizeof(Create_LinkResp));

    if (create_link_1(&linkParms, pHandle->pLink, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, pIp);
        return -2;
    }

    return 0;
}

/* CLOSE FUNCTIONS */

int Vxi11_CloseDevice(VxiHandle* pHandle, const char* pIp)
{
    int ret = Vxi11_CloseLink(pIp, pHandle);

    clnt_destroy(pHandle->pClient);

    return ret;
}

int Vxi11_CloseLink(VxiHandle* pHandle, const char* pIp)
{
    Device_Error deviceError;
    memset(&deviceError, 0, sizeof(Device_Error));

    if (destroy_link_1(&pHandle->pLink->lid, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, pIp);
        return -1;
    }

    return 0;
}

/* SEND FUNCTIONS */

/* We still need the version of the function where the length is set explicitly
 * though, for when we are sending fixed length data blocks. */
int Vxi11_Send(VxiHandle* pHandle, const char* pData, unsigned long len)
{
    Device_WriteParms writeParms;
    unsigned int bytesLeft = len;
    char *pSendData;

    pSendData = new char[len];
    memcpy(pSendData, pData, len);

    writeParms.lid          = pHandle->pLink->lid;
    writeParms.io_timeout   = VXI11_DEFAULT_TIMEOUT;
    writeParms.lock_timeout = VXI11_DEFAULT_TIMEOUT;

    /* We can only write (link->maxRecvSize) bytes at a time, so we sit in a loop,
     * writing a chunk at a time, until we're done. */

    do
    {
        Device_WriteResp writeResp;
        memset(&writeResp, 0, sizeof(writeResp));

        if (bytesLeft <= pHandle->pLink->maxRecvSize)
        {
            writeParms.flags = 8;
            writeParms.data.data_len = bytesLeft;
        }
        else
        {
            writeParms.flags = 0;
            /* We need to check that maxRecvSize is a sane value (ie >0). Believe it
             * or not, on some versions of Agilent Infiniium scope firmware the scope
             * returned "0", which breaks Rule B.6.3 of the VXI-11 protocol. Nevertheless
             * we need to catch this, otherwise the program just hangs. */
            if (pHandle->pLink->maxRecvSize > 0)
            {
                writeParms.data.data_len = pHandle->pLink->maxRecvSize;
            }
            else
            {
                writeParms.data.data_len = 4096; /* pretty much anything should be able to cope with 4kB */
            }
        }
        writeParms.data.data_val = pSendData + (len - bytesLeft);

        if (device_write_1(&writeParms, &writeResp, pHandle->pClient) != RPC_SUCCESS)
        {
            delete[] pSendData;
            return -VXI11_NULL_WRITE_RESP; /* The instrument did not acknowledge the write, just completely
                              dropped it. There was no vxi11 comms error as such, the
                              instrument is just being rude. Usually occurs when the instrument
                              is busy. If we don't check this first, then the following
                              line causes a seg fault */
        }
        if (writeResp.error != 0)
        {
            printf("vxi11_user: write error: %d\n", (int)writeResp.error);
            delete[] pSendData;
            return -(writeResp.error);
        }
        bytesLeft -= writeResp.size;
    }
    while (bytesLeft > 0);

    delete[] pSendData;

    return 0;
}

/* RECEIVE FUNCTIONS */

// It appeared that this function wasn't correctly dealing with more data available than specified in len.
// This patch attempts to fix this issue.	RDP 2007/8/13

/* wrapper, for default timeout */

long Vxi11_Receive(VxiHandle *pHandle, char *pReceiveData, unsigned long len)
{
    return Vxi11_Receive(pHandle, pReceiveData, len, VXI11_READ_TIMEOUT);
}

#define RCV_END_BIT	0x04	// An end indicator has been read
#define RCV_CHR_BIT	0x02	// A termchr is set in flags and a character which matches termChar is transferred
#define RCV_REQCNT_BIT	0x01	// requestSize bytes have been transferred.  This includes a request size of zero.

long Vxi11_Receive(VxiHandle* pHandle, char* pReceiveData, unsigned long len, unsigned long timeout)
{
    Device_ReadParms readParms;
    Device_ReadResp  readResp;
    unsigned long curPos = 0;

    readParms.lid		   = pHandle->pLink->lid;
    readParms.requestSize  = len;
    readParms.io_timeout   = timeout;	/* in ms */
    readParms.lock_timeout = timeout;	/* in ms */
    readParms.flags = VXI_ENDW_FLAG;
    readParms.termChar = 0;

    do
    {
        memset(&readResp, 0, sizeof(readResp));

        readResp.data.data_val = pReceiveData + curPos;
        readParms.requestSize = len - curPos;	// Never request more total data than originally specified in len

        if (device_read_1(&readParms, &readResp, pHandle->pClient) != RPC_SUCCESS)
        {
            return -VXI11_NULL_READ_RESP;
            /* there is nothing to read. Usually occurs after sending a query
                which times out on the instrument. If we don't check this first,
                then the following line causes a seg fault */
        }
        if (readResp.error != 0)
        {
            /* Read failed for reason specified in error code.
            *  (From published VXI-11 protocol, section B.5.2)
            *  0	no error
            *  1	syntax error
            *  3	device not accessible
            *  4	invalid link identifier
            *  5	parameter error
            *  6	channel not established
            *  8	operation not supported
            *  9	out of resources
            *  11	device locked by another link
            *  12	no lock held by this link
            *  15	I/O timeout
            *  17	I/O error
            *  21	invalid address
            *  23	abort
            *  29	channel already established
            */

            printf("vxi11_user: read error: %d\n", (int)readResp.error);
            return -(readResp.error);
        }

        if ((curPos + readResp.data.data_len) <= len)
        {
            curPos += readResp.data.data_len;
        }
        if ( (readResp.reason & RCV_END_BIT) || (readResp.reason & RCV_CHR_BIT) )
        {
            break;
        }
        else if ( curPos == len )
        {
            printf("xvi11_user: read error: buffer too small. Read %d bytes without hitting terminator.\n", (int)curPos );
            return -100;
        }
    }
    while (1);

    return (curPos); /*actual number of bytes received*/
}

/* STATUS FUNCTIONS */

int Vxi11_ReadSTB(VxiHandle* pHandle, unsigned char* pStb, unsigned long timeout)
{
    Device_GenericParms genericParms;
    Device_ReadStbResp readStbResp;

    // TODO: investigar flags de structs parms
    genericParms.flags = VXI_ENDW_FLAG;

    genericParms.lid = pHandle->pLink->lid;
    genericParms.io_timeout = timeout;
    genericParms.lock_timeout = timeout;

    memset(&genericParms, 0, sizeof(Device_ReadParms));

    if (device_readstb_1(&genericParms, &readStbResp, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while reading STB");
        return -1;
    }

    *pStb = readStbResp.stb;

    return 0;
}

int Vxi11_DeviceTrigger(VxiHandle* pHandle, unsigned long timeout)
{
    Device_GenericParms genericParms;
    Device_Error deviceError;

    genericParms.flags = VXI_ENDW_FLAG;
    genericParms.lid = pHandle->pLink->lid;
    genericParms.io_timeout = timeout;
    genericParms.lock_timeout = timeout;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_trigger_1(&genericParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device trigger");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceClear(VxiHandle* pHandle, unsigned long timeout)
{
    Device_GenericParms genericParms;
    Device_Error deviceError;

    genericParms.flags = VXI_ENDW_FLAG;
    genericParms.lid = pHandle->pLink->lid;
    genericParms.io_timeout = timeout;
    genericParms.lock_timeout = timeout;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_clear_1(&genericParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device clear");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceRemote(VxiHandle* pHandle, unsigned long timeout)
{
    Device_GenericParms genericParms;
    Device_Error deviceError;

    genericParms.flags = VXI_ENDW_FLAG;
    genericParms.lid = pHandle->pLink->lid;
    genericParms.io_timeout = timeout;
    genericParms.lock_timeout = timeout;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_remote_1(&genericParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device remote");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceLocal(VxiHandle* pHandle, unsigned long timeout)
{
    Device_GenericParms genericParms;
    Device_Error deviceError;

    genericParms.flags = VXI_ENDW_FLAG;
    genericParms.lid = pHandle->pLink->lid;
    genericParms.io_timeout = timeout;
    genericParms.lock_timeout = timeout;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_remote_1(&genericParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device local");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceLock(VxiHandle* pHandle, unsigned long lockTimeout)
{
    Device_LockParms lockParms;
    Device_Error deviceError;

    lockParms.flags = VXI_ENDW_FLAG;
    lockParms.lid = pHandle->pLink->lid;
    lockParms.lock_timeout = lockTimeout;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_lock_1(&lockParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device lock");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceUnlock(VxiHandle* pHandle)
{
    Device_Error deviceError;

    memset(&deviceError, 0, sizeof(Device_Error));

    // TODO: investigar parametro Device_Link
    if (device_unlock_1((Device_Link*) pHandle->pLink, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing device unlock");
        return -1;
    }

    return 0;
}

int Vxi11_DeviceEnableSRQ(VxiHandle* pHandle, bool_t enable)
{
    Device_EnableSrqParms enableSrqParms;
    Device_Error deviceError;

    enableSrqParms.enable = enable;
    enableSrqParms.lid = pHandle->pLink->lid;
    // enableSrqParms.handle // TODO: Que es handle?

    memset(&deviceError, 0, sizeof(Device_Error));

    if (device_enable_srq_1(&enableSrqParms, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while executing SRQ enable");
        return -1;
    }

    return 0;
}

int Vxi11_CreateIntrChannel(VxiHandle* pHandle)
{
    Device_RemoteFunc remoteFunc;
    Device_Error deviceError;

    remoteFunc.hostAddr = 0;
    remoteFunc.hostPort = 0;
    remoteFunc.progFamily = DEVICE_TCP;
    remoteFunc.progNum = DEVICE_INTR;
    remoteFunc.progVers = DEVICE_INTR_VERSION;

    memset(&deviceError, 0, sizeof(Device_Error));

    if (create_intr_chan_1(&remoteFunc, &deviceError, pHandle->pClient) != RPC_SUCCESS)
    {
        clnt_perror(pHandle->pClient, "Error while creating intr channel");
        return -1;
    }

    return 0;
}

/*
enum clnt_stat
destroy_intr_chan_1(void *argp, Device_Error *clnt_res, CLIENT *clnt)
{
    return (clnt_call(clnt, destroy_intr_chan,
        (xdrproc_t) xdr_void, (caddr_t) argp,
        (xdrproc_t) xdr_Device_Error, (caddr_t) clnt_res,
        TIMEOUT));
}
*/

int (*Vxi11_SRQHandler)(char* arg);

int Vxi11_RegisterSRQHandler(int (*callback)(char* arg))
{
    Vxi11_SRQHandler = callback;

    return 0;
}


char* device_intr_srq_1_svc(char* MyDevice_SrqParms, struct svc_req* Mysvc_req)
{
    printf("SRQ received...\n");

    (*Vxi11_SRQHandler)(MyDevice_SrqParms);

    return(NULL);
}

static void device_intr_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
union {
    Device_SrqParms device_intr_srq_1_arg;
    } argument;
    char *result;

xdrproc_t _xdr_argument, _xdr_result;
    char *(*local)(char *, struct svc_req *);
switch (rqstp->rq_proc)
{
    case NULLPROC:
        (void) svc_sendreply (transp, (xdrproc_t) xdr_void, (char *)NULL);
        return;
    case device_intr_srq:
        _xdr_argument = (xdrproc_t) xdr_Device_SrqParms;
        _xdr_result = (xdrproc_t) xdr_void;
        local = (char *(*)(char *, struct svc_req *)) device_intr_srq_1_svc;
        break;
    default:
        svcerr_noproc (transp);
        return;
}
    memset ((char *)&argument, 0, sizeof (argument));
    if (!svc_getargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument))
    {
        svcerr_decode (transp);
        return;
    }
    result = (*local)((char *)&argument, rqstp);
    if (result != NULL && !svc_sendreply(transp, (xdrproc_t) _xdr_result, result))
    {
        svcerr_systemerr (transp);
    }
    if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument))
    {
        fprintf (stderr, "%s", "unable to free arguments");
        exit (1);
    }
    return;
}

// int main (int argc, char **argv)
int Vxi11_InitializeSRQService()
{
    register SVCXPRT *transp;
    pmap_unset (DEVICE_INTR, DEVICE_INTR_VERSION);
    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL)
    {
        fprintf (stderr, "%s", "cannot create udp service.");
        exit(1);
    }
    printf("UDP Socket for VXI-11 interrupt channel: %d\n", transp->xp_port);
    if (!svc_register(transp, DEVICE_INTR, DEVICE_INTR_VERSION, device_intr_1, IPPROTO_UDP))
    {
        fprintf (stderr, "%s","unable to register (DEVICE_INTR, DEVICE_INTR_VERSION, udp).");
        exit(1);
    }
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL)
    {
        fprintf (stderr, "%s", "cannot create tcp service.");
        exit(1);
    }
    printf("TCP Socket for VXI-11 interrupt channel: %d\n", transp->xp_port);
    if(!svc_register(transp, DEVICE_INTR, DEVICE_INTR_VERSION, device_intr_1, IPPROTO_TCP))
    {
        fprintf (stderr, "%s","unable to register (DEVICE_INTR, DEVICE_INTR_VERSION, tcp).");
        exit(1);
    }
    svc_run ();
    fprintf (stderr, "%s", "svc_run returned");
    exit (1);
    // NOTREACHED
}

int Vxi11_StartServiceSRQ()
{
    std::thread srqThread(&Vxi11_InitializeSRQService);
}



