#ifndef _GSMCONFIG_H
#define _GSMCONFIG_H


#include "autoconf.h"

//  board config
#if CONFIG_GSM_MODULE_LIB_DEBUG_ENABLE
#define _GSM_DEBUG 1
#else
#define _GSM_DEBUG 0
#endif

#define _GSM_USART USART3
#define _GSM_POWERKEY_GPIO GSM_KEY_GPIO_Port
#define _GSM_POWERKEY_PIN GSM_KEY_Pin

//  enable/disable
#if CONFIG_GSM_MODULE_LIB_ENABLE_CALL
#define _GSM_CALL_ENABLE 1
#else
#define _GSM_CALL_ENABLE 0
#endif

#if CONFIG_GSM_MODULE_LIB_ENABLE_SMS
#define _GSM_MSG_ENABLE 1
#else
#define _GSM_MSG_ENABLE 0
#endif

#if CONFIG_GSM_MODULE_LIB_ENABLE_DTMF_DETECT
#define _GSM_DTMF_DETECT_ENABLE 1
#else
#define _GSM_DTMF_DETECT_ENABLE 0
#endif

#if CONFIG_GSM_MODULE_LIB_ENABLE_GPRS
#define _GSM_GPRS_ENABLE 1
#else
#define _GSM_GPRS_ENABLE 0
#endif

#if CONFIG_GSM_MODULE_LIB_ENABLE_BLUETOOTH
#define _GSM_BLUETOOTH_ENABLE 0 //  not support yet
#else
#define _GSM_BLUETOOTH_ENABLE 0
#endif

//  do not change
#define _GSM_AT_MAX_ANSWER_ITEMS 5
#define _GSM_TASKSIZE 512
#define _GSM_BUFFTASKSIZE 128
#define _GSM_RXSIZE 512
#define _GSM_RXTIMEOUT 10

#endif
