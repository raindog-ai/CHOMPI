#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define USBD_MAX_NUM_INTERFACES 5
#define USBD_MAX_NUM_CONFIGURATION 1
#define USBD_SUPPORT_USER_STRING 1
#define USBD_SELF_POWERED 0
#define USBD_LPM_ENABLED 0
#define USBD_MAX_STR_DESC_SIZ 512
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_malloc malloc
#define USBD_free free
#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)
#define UNUSED(x) (void)(x)
#define DEVICE_HS 1
#define DEVICE_FS 0
#define USBD_Delay(x) (void)(x)
#define UID_BASE mock_uid
extern uint32_t mock_uid[3];
typedef struct { int unused; } PCD_HandleTypeDef;
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __DSB(void) {}
static inline void __set_PRIMASK(uint32_t mask) { (void)mask; }
static inline void HAL_PWREx_EnableUSBVoltageDetector(void) {}
