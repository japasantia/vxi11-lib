#ifndef VXI11_LIB_H
#define VXI11_LIB_H

#include <rpc/rpc.h>
#include "vxi11.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

#define EXPORT_C extern "C"

#define	VXI11_DEFAULT_TIMEOUT	10000	/* in ms */
#define	VXI11_READ_TIMEOUT      2000	/* in ms */
#define	VXI11_NULL_READ_RESP	50      /* vxi11_receive() return value if a query
                                        * times out ON THE INSTRUMENT (and so we have
                                         * to resend the query again) */
#define	VXI11_NULL_WRITE_RESP	51      /* vxi11_send() return value if a sent command
                                         * times out ON THE INSTURMENT. */

/* FLAGS */
#define WAITLOCK_FLAG       1
#define WRITE_END_CHAR_FLAG 8
#define READ_END_CHAR_FLAG  0x80
#define VXI_ENDW_FLAG       (WAITLOCK_FLAG | WRITE_END_CHAR_FLAG)

typedef struct tagVxiHandle
{
    CLIENT* pClient;
    Create_LinkResp* pLink;
} VxiHandle;

EXPORT_C
int Vxi11_OpenDevice(VxiHandle* pHandle, const char* pIp, char* pDevice);

int Vxi11_OpenDevice(VxiHandle* pHandle, const char* pIp);

int Vxi11_OpenLink(const char* pIp, char* pDevice, VxiHandle* pHandle);

EXPORT_C
int Vxi11_CloseDevice(const char* pIp, VxiHandle* pHandle);

int Vxi11_CloseLink(const char* pIp, VxiHandle* pHandle);

EXPORT_C
int Vxi11_Send(VxiHandle* pHandle, const char* pData, unsigned long len);

long Vxi11_Receive(VxiHandle *pHandle, char *pReceiveData, unsigned long len);

EXPORT_C
long Vxi11_Receive(VxiHandle* pHandle, char* pReceiveData, unsigned long len, unsigned long timeout);

EXPORT_C
int Vxi11_ReadSTB(VxiHandle* pHandle, unsigned char* pStb, unsigned long timeout);

EXPORT_C
int Vxi11_DeviceTrigger(VxiHandle* pHandle, unsigned long timeout);

EXPORT_C
int Vxi11_DeviceClear(VxiHandle* pHandle, unsigned long timeout);

EXPORT_C
int Vxi11_DeviceRemote(VxiHandle* pHandle, unsigned long timeout);

EXPORT_C
int Vxi11_DeviceLocal(VxiHandle* pHandle, unsigned long timeout);

EXPORT_C
int Vxi11_DeviceLock(VxiHandle* pHandle, unsigned long lockTimeout);

EXPORT_C
int Vxi11_DeviceUnlock(VxiHandle* pHandle);

EXPORT_C
int Vxi11_DeviceEnableSRQ(VxiHandle* pHandle, bool_t enable);

// #ifdef __cplusplus
// }
// #endif

#endif // VXI11_LIB_H
