#ifndef FORTDBG_H
#define FORTDBG_H

#include "fortdrv.h"

#define FORT_DEBUG_VERSION 1

typedef enum FORT_FUNC_ID {
    FORT_FUNC_UNKNOWN = 1,
    FORT_DEVICE_CANCEL_PENDING,
    FORT_CALLOUT_ALE_CLASSIFY,
    FORT_CALLOUT_TRANSPORT_CLASSIFY,
    FORT_CALLOUT_FLOW_DELETE,
    FORT_CALLOUT_DISCARD_CLASSIFY,
    FORT_CALLOUT_INSTALL,
    FORT_CALLOUT_REMOVE,
    FORT_CALLOUT_FORCE_REAUTH,
    FORT_CALLOUT_TIMER,
    FORT_DEVICE_CREATE,
    FORT_DEVICE_CLOSE,
    FORT_DEVICE_CLEANUP,
    FORT_DEVICE_CONTROL,
    FORT_DEVICE_SHUTDOWN,
    FORT_DEVICE_LOAD,
    FORT_DEVICE_UNLOAD,
    FORT_PACKET_INJECT_COMPLETE,
    FORT_SYSCB_POWER,
    FORT_SYSCB_TIME,
    FORT_TIMER_CALLBACK,
    FORT_WORKER_CALLBACK,
} FORT_FUNC_ID;

#if defined(FORT_DEBUG_STACK)
#    define FORT_CHECK_STACK(func_id) fort_check_stack(__func__, func_id)
#else
#    define FORT_CHECK_STACK(func_id)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

FORT_API void fort_check_stack(const char *func_name, FORT_FUNC_ID func_id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FORTDBG_H
