#include <pwr_power.h>
#include "gsm.h"
#include "stdio.h"
#include <drivers/uart.h>

//#############################################################################################
struct k_mutex gsmMutex;

#if (_GSM_DTMF_DETECT_ENABLE == 1)
osMessageQId gsmDtmfQueueHandle;
#endif
Gsm_t gsm = { .uart_dev = NULL };

K_HEAP_DEFINE(gsm_heap, 8096);

const char *_GSM_ALWAYS_SEARCH[] = {
	"\r\n+CLIP:", //  0
	"POWER DOWN\r\n", //  1
	"\r\n+CMTI:", //  2
	"\r\nNO CARRIER\r\n", //  3
	"\r\n+DTMF:", //  4
	"\r\n+CREG:", //  5
	"\r\nCLOSED\r\n", //  6
	"\r\n+CIPRXGET: 1\r\n", //  7

};
//#############################################################################################
void gsm_init_config(void);
//#############################################################################################

static void gsm_uart_isr(const struct device *dev, void *user_data)
{
	(void)dev;
	const struct device *uart_dev = gsm.uart_dev;
	int rx;
	uint8_t tmp;

	/* get all the data off UART as fast as we can */
	while (uart_irq_update(uart_dev) && uart_irq_is_pending(uart_dev)) {
		if (uart_irq_rx_ready(uart_dev)) {
			// rx isr
			rx = uart_fifo_read(uart_dev, &tmp, sizeof(tmp));
			if (rx > 0) {
				if (gsm.at.index < _GSM_RXSIZE - 1) {
					gsm.at.buff[gsm.at.index] = tmp;
					gsm.at.index++;
				}
				gsm.at.rxTime = sys_clock_timeout_end_calc(K_MSEC(_GSM_RXTIMEOUT));
			}
		}
		if (uart_irq_tx_ready(uart_dev)) {
			// tx isr
			uint8_t buffer;
			int send_len;

			if (gsm.tx.count == gsm.tx.sent) {
				uart_irq_tx_disable(uart_dev);
				k_sem_give(&gsm.tx.sem);
			} else {
				buffer = *(gsm.tx.buffer + gsm.tx.sent);
				send_len = uart_fifo_fill(uart_dev, &buffer, 1);
				if (send_len != 1) {
					//LOG_ERR("Drop 1 bytes");
				}
				gsm.tx.sent++;
			}
		}
	}
}
//#############################################################################################
void gsm_at_checkRxBuffer(void)
{
	if ((gsm.at.index > 0) && (gsm.at.rxTime < k_uptime_ticks())) {
#if _GSM_DEBUG == 1
		printf("%s", (const char *)gsm.at.buff);
#endif
		//  +++ search answer at-command
		for (uint8_t i = 0; i < _GSM_AT_MAX_ANSWER_ITEMS; i++) {
			if (gsm.at.answerSearch[i] != NULL) {
				char *found = strstr((char *)gsm.at.buff, gsm.at.answerSearch[i]);
				if (found != NULL) {
					gsm.at.answerFound = i;
					if (gsm.at.answerString != NULL)
						strncpy(gsm.at.answerString, found,
							gsm.at.answerSize);
					break;
				}
			} else
				break;
		}
		//  --- search answer at-command

		//  +++ search always
		for (uint8_t i = 0; i < sizeof(_GSM_ALWAYS_SEARCH) / 4; i++) {
			char *str = strstr((char *)gsm.at.buff, _GSM_ALWAYS_SEARCH[i]);
			if (str != NULL) {
				switch (i) {
				case 0: //  found   "\r\n+CLIP:"
#if (_GSM_CALL_ENABLE == 1)
					if (sscanf(str, "\r\n+CLIP: \"%[^\"]\"", gsm.call.number) ==
					    1)
						gsm.call.ringing = 1;
#endif
					break;
				case 1: //  found   "POWER DOWN\r\n"
					gsm.power = 0;
					gsm.started = 0;
					break;
				case 2: //  found   "\r\n+CMTI:"
#if (_GSM_MSG_ENABLE == 1)
					str = strchr(str, ',');
					if (str != NULL) {
						str++;
						gsm.msg.newMsg = atoi(str);
						break;
					}
#endif
					break;
				case 3: //  found   "\r\nNO CARRIER\r\n"
#if (_GSM_CALL_ENABLE == 1)
					gsm.call.callbackEndCall = 1;
#endif
					break;
				case 4: //  found   "\r\n+DTMF:"
#if ((_GSM_DTMF_DETECT_ENABLE == 1) && (_GSM_CALL_ENABLE == 1))
					if (sscanf(str, "\r\n+DTMF: %c\r\n", &gsm.call.dtmf) == 1)
						osMessagePut(gsmDtmfQueueHandle, gsm.call.dtmf, 10);
#endif
					break;
				case 5: //  found   "\r\n+CREG:"
					if (strstr(str, "\r\n+CREG: 1\r\n") != NULL)
						gsm.registred = 1;
					else
						gsm.registred = 0;
					break;
				case 6: //  found   "\r\nCLOSED\r\n"
#if (_GSM_GPRS_ENABLE == 1)
					gsm.gprs.tcpConnection = 0;
#endif
					break;
				case 7: //  found   "\r\n+CIPRXGET: 1\r\n"
#if (_GSM_GPRS_ENABLE == 1)
					gsm.gprs.gotData = 1;
#endif
					break;
				default:
					break;
				}
			}
			//  --- search always
		}
		memset(gsm.at.buff, 0, _GSM_RXSIZE);
		gsm.at.index = 0;
	}
}
//#############################################################################################
void gsm_at_sendString(const char *string)
{
	gsm_at_sendData(string, strlen(string));
}
//#############################################################################################
void gsm_at_sendData(const uint8_t *data, uint16_t len)
{
	gsm.tx.count = len;
	gsm.tx.buffer = data;
	gsm.tx.sent = 0;
	uart_irq_tx_enable(gsm.uart_dev);
	k_sem_take(&gsm.tx.sem, K_FOREVER);
	// ToDo: Написати вихід через таймаут якщо не прийшов семафор
}
//#############################################################################################
uint8_t gsm_at_sendCommand(const char *command, uint32_t waitMs, char *answer,
			   uint16_t sizeOfAnswer, uint8_t items, ...)
{
	do {
		k_sleep(K_MSEC(10));
	} while (k_mutex_lock(&gsmMutex, K_SECONDS(10)) != 0);
	va_list tag;
	va_start(tag, items);
	for (uint8_t i = 0; i < items; i++) {
		char *str = va_arg(tag, char *);
		gsm.at.answerSearch[i] = k_heap_alloc(&gsm_heap, strlen(str)+1, K_MSEC(100));
		if (gsm.at.answerSearch[i] != NULL)
			strcpy(gsm.at.answerSearch[i], str);
	}
	va_end(tag);

	if ((answer != NULL) && (sizeOfAnswer > 0)) {
		gsm.at.answerSize = sizeOfAnswer;
		gsm.at.answerString = k_heap_alloc(&gsm_heap, sizeOfAnswer + 1, K_MSEC(100));
		memset(gsm.at.answerString, 0, sizeOfAnswer);
	}

	gsm.at.answerFound = -1;
	uint64_t startInterval = sys_clock_timeout_end_calc(K_MSEC(waitMs));

	gsm_at_sendString(command);

	while (startInterval > k_uptime_ticks()) {
		if (gsm.at.answerFound != -1)
			break;
		k_sleep(K_MSEC(10));
	}

	for (uint8_t i = 0; i < items; i++) {
		k_heap_free(&gsm_heap, gsm.at.answerSearch[i]);
		gsm.at.answerSearch[i] = NULL;
	}

	if ((answer != NULL) && (sizeOfAnswer > 0)) {
		if (gsm.at.answerFound >= 0)
			strncpy(answer, gsm.at.answerString, sizeOfAnswer);
		k_heap_free(&gsm_heap, gsm.at.answerString);
		gsm.at.answerString = NULL;
	}

	k_mutex_unlock(&gsmMutex);
	return gsm.at.answerFound + 1;
}
//#############################################################################################
//#############################################################################################
//#############################################################################################
bool gsm_power(bool on_off)
{
	if (on_off) {
		//  power on
#if CONFIG_GSM_MODULE_LIB_PIN_POWER_ENABLE
		if (gsm.hw_pins.hw_pin_power_press != NULL) {
			gsm.hw_pins.hw_pin_power_press();
		}
		k_sleep(K_MSEC(200));
#endif
		if (gsm_at_sendCommand("AT\r\n", 500, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") ==
		    1) {
			gsm.power = 1;
			gsm_init_config();
			gsm.started = 1;
			return true;
		}
#if CONFIG_GSM_MODULE_LIB_PIN_ENABLE_ENABLE
		if (gsm.hw_pins.hw_pin_enable_press != NULL) {
			gsm.hw_pins.hw_pin_enable_press();
		}
		k_sleep(K_MSEC(1200));
		if (gsm.hw_pins.hw_pin_enable_release != NULL) {
			gsm.hw_pins.hw_pin_enable_release();
		}
		k_sleep(K_MSEC(2000));
#endif
		for (uint8_t i = 0; i < 10; i++) {
			if (gsm_at_sendCommand("AT\r\n", 500, NULL, 0, 1, "\r\nOK\r\n") == 1) {
				gsm.power = 1;
				k_sleep(K_MSEC(4000));
				gsm_init_config();
				gsm.started = 1;
				return true;
			}
		}
		gsm.power = 0;
		gsm.started = 0;
		return false;
	} else {
		//  power off
#if CONFIG_GSM_MODULE_LIB_PIN_POWER_ENABLE
		if (gsm.hw_pins.hw_pin_power_release != NULL) {
			gsm.hw_pins.hw_pin_power_release();
		}
		k_sleep(K_MSEC(500));
		return true;
#else
		if (gsm_at_sendCommand("AT\r\n", 500, NULL, 0, 1, "\r\nOK\r\n") == 0) {
			gsm.power = 0;
			gsm.started = 0;
			return true;
		}
#if CONFIG_GSM_MODULE_LIB_PIN_ENABLE_ENABLE
		if (gsm.hw_pins.hw_pin_enable_press != NULL) {
			gsm.hw_pins.hw_pin_enable_press();
		}
		k_sleep(K_MSEC(1200));
		if (gsm.hw_pins.hw_pin_enable_release != NULL) {
			gsm.hw_pins.hw_pin_enable_release();
		}
		k_sleep(K_MSEC(2000));
#endif
		if (gsm_at_sendCommand("AT\r\n", 500, NULL, 0, 1, "\r\nOK\r\n") == 0) {
			gsm.power = 0;
			gsm.started = 0;
			return true;
		} else {
			gsm.power = 1;
			return false;
		}
#endif
	}
}
//#############################################################################################
bool gsm_setDefault(void)
{
	if (gsm_at_sendCommand("AT&F0\r\n", 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return true;
	return false;
}
//#############################################################################################
bool gsm_saveProfile(void)
{
	if (gsm_at_sendCommand("AT&W\r\n", 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return true;
	return false;
}
//#############################################################################################
bool gsm_enterPinPuk(const char *string)
{
	char str[32];
	if (string == NULL)
		return false;
	sprintf(str, "AT+CPIN=%s\r\n", string);
	if (gsm_at_sendCommand(str, 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return false;
	return true;
}
//#############################################################################################
bool gsm_getIMEI(char *string, uint8_t sizeOfString)
{
	if ((string == NULL) || (sizeOfString < 15))
		return false;
	char str[32];
	if (gsm_at_sendCommand("AT+GSN\r\n", 1000, str, sizeof(str), 2, "AT+GSN",
			       "\r\nERROR\r\n") != 1)
		return false;
	if (sscanf(str, "\r\nAT+GSN\r\n %[^\r\n]", string) != 1)
		return false;
	return true;
}
//#############################################################################################
bool gsm_getVersion(char *string, uint8_t sizeOfString)
{
	if (string == NULL)
		return false;
	char str1[16 + sizeOfString];
	char str2[sizeOfString + 1];
	if (gsm_at_sendCommand("AT+CGMR\r\n", 1000, str1, sizeof(str1), 2, "AT+GMM",
			       "\r\nERROR\r\n") != 1)
		return false;
	if (sscanf(str1, "\r\nAT+CGMR\r\n %[^\r\n]", str2) != 1)
		return false;
	strncpy(string, str2, sizeOfString);
	return true;
}
//#############################################################################################
bool gsm_getModel(char *string, uint8_t sizeOfString)
{
	if (string == NULL)
		return false;
	char str1[16 + sizeOfString];
	char str2[sizeOfString + 1];
	if (gsm_at_sendCommand("AT+GMM\r\n", 1000, str1, sizeof(str1), 2, "AT+GMM",
			       "\r\nERROR\r\n") != 1)
		return false;
	if (sscanf(str1, "\r\nAT+GMM\r\n %[^\r\n]", str2) != 1)
		return false;
	strncpy(string, str2, sizeOfString);
	return true;
}
//#############################################################################################
bool gsm_getServiceProviderName(char *string, uint8_t sizeOfString)
{
	if (string == NULL)
		return false;
	char str1[16 + sizeOfString];
	char str2[sizeOfString + 1];
	if (gsm_at_sendCommand("AT+CSPN?\r\n", 1000, str1, sizeof(str1), 2,
			       "\r\n+CSPN:", "\r\nERROR\r\n") != 1)
		return false;
	if (sscanf(str1, "\r\n+CSPN: \"%[^\"]\"", str2) != 1)
		return false;
	strncpy(string, str2, sizeOfString);
	return true;
}
//#############################################################################################
uint8_t gsm_getSignalQuality_0_to_100(void)
{
	return 12;
	char str[32];
	uint8_t p1, p2;
	if (gsm_at_sendCommand("AT+CSQ\r\n", 1000, str, sizeof(str), 2,
			       "\r\n+CSQ:", "\r\nERROR\r\n") != 1)
		return 0;
	if (sscanf(str, "\r\n+CSQ: %hhd,%hhd\r\n", &p1, &p2) != 2)
		return 0;
	if (p1 == 99)
		gsm.signal = 0;
	else
		gsm.signal = (p1 * 100) / 31;
	return gsm.signal;
}
//#############################################################################################
#if CONFIG_GSM_MODULE_LIB_ENABLE_USSD
bool gsm_ussd(char *command, char *answer, uint16_t sizeOfAnswer, uint8_t waitSecond)
{
	if (command == NULL) {
		if (gsm_at_sendCommand("AT+CUSD=2\r\n", 1000, NULL, 0, 2, "\r\nOK\r\n",
				       "\r\nERROR\r\n") != 1)
			return false;
		return true;
	} else if (answer == NULL) {
		char str[16 + strlen(command)];
		sprintf(str, "AT+CUSD=0,\"%s\"\r\n", command);
		if (gsm_at_sendCommand(str, waitSecond * 1000, NULL, 0, 2, "\r\nOK\r\n",
				       "\r\nERROR\r\n") != 1)
			return false;
		return true;
	} else {
		char str[16 + strlen(command)];
		char str2[sizeOfAnswer + 32];
		sprintf(str, "AT+CUSD=1,\"%s\"\r\n", command);
		if (gsm_at_sendCommand(str, waitSecond * 1000, str2, sizeof(str2), 2,
				       "\r\n+CUSD:", "\r\nERROR\r\n") != 1) {
			gsm_at_sendCommand("AT+CUSD=2\r\n", 1000, NULL, 0, 2, "\r\nOK\r\n",
					   "\r\nERROR\r\n");
			return false;
		}
		char *start = strstr(str2, "\"");
		char *end = strstr(str2, "\", ");
		if (start != NULL && end != NULL) {
			start++;
			strncpy(answer, start, end - start);
			return true;
		} else
			return false;
	}
}
#endif
//#############################################################################################
bool gsm_waitForStarted(uint8_t waitSecond)
{
	uint64_t startInterval = sys_clock_timeout_end_calc(K_SECONDS(waitSecond));
	//uint32_t startTime = HAL_GetTick();
	//while (HAL_GetTick() - startTime < (waitSecond * 1000)) {
	while (startInterval > k_uptime_ticks()) {
		k_sleep(K_MSEC(100));
		if (gsm.started == 1)
			return true;
	}
	return false;
}
//#############################################################################################
bool gsm_waitForRegister(uint8_t waitSecond)
{
	uint64_t startInterval = sys_clock_timeout_end_calc(K_SECONDS(waitSecond));
	//uint32_t startTime = HAL_GetTick();
	uint8_t idx = 0;
	//while (HAL_GetTick() - startTime < (waitSecond * 1000)) {
	while (startInterval > k_uptime_ticks()) {
		k_sleep(K_MSEC(100));
		if (gsm.registred == 1)
			return true;
		idx++;
		if (idx % 10 == 0) {
			if (gsm_at_sendCommand("AT+CREG?\r\n", 1000, NULL, 0, 1,
					       "\r\n+CREG: 1,1\r\n") == 1)
				gsm.registred = 1;
		}
	}
	return false;
}
//#############################################################################################
bool gsm_tonePlay(Gsm_Tone_t Gsm_Tone_, uint32_t durationMiliSecond, uint8_t level_0_100)
{
	char str[32];
	sprintf(str, "AT+SNDLEVEL=0,%d\r\n", level_0_100);
	if (gsm_at_sendCommand(str, 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return false;
	sprintf(str, "AT+STTONE=1,%d,%d\r\n", Gsm_Tone_, (unsigned)durationMiliSecond);
	if (gsm_at_sendCommand(str, 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return false;
	return true;
}
//#############################################################################################
bool gsm_toneStop(void)
{
	if (gsm_at_sendCommand("AT+STTONE=0\r\n", 5000, NULL, 0, 2, "\r\nOK\r\n",
			       "\r\nERROR\r\n") != 1)
		return true;
	return false;
}
//#############################################################################################
bool gsm_dtmf(char *string, uint32_t durationMiliSecond)
{
	char str[32];
	sprintf(str, "AT+VTS=\"%s\",%d\r\n", string, (unsigned)durationMiliSecond / 100);
	if (gsm_at_sendCommand(str, 5000, NULL, 0, 2, "\r\nOK\r\n", "\r\nERROR\r\n") != 1)
		return false;
	return true;
}
//#############################################################################################
void gsm_init_config(void)
{
	char str1[64];
	char str2[16];
	gsm_setDefault();
	gsm_at_sendCommand("ATE1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
	gsm_at_sendCommand("AT+COLP=1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
	gsm_at_sendCommand("AT+CLIP=1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
	gsm_at_sendCommand("AT+CREG=1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
	gsm_at_sendCommand("AT+FSHEX=0\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
#if (_GSM_GPRS_ENABLE == 1)
	gsm_at_sendCommand("AT+CIPSHUT\r\n", 65000, NULL, 0, 2, "\r\nSHUT OK\r\n", "\r\nERROR\r\n");
	gsm_at_sendCommand("AT+CIPHEAD=0\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
	gsm_at_sendCommand("AT+CIPRXGET=1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
#endif
	gsm_getVersion(str1, sizeof(str1));
	k_sleep(K_MSEC(2000));
	for (uint8_t i = 0; i < 5; i++) {
		if (gsm_at_sendCommand("AT+CPIN?\r\n", 1000, str1, sizeof(str1), 2,
				       "\r\n+CPIN:", "\r\nERROR\r\n") == 1) {
			if (sscanf(str1, "\r\n+CPIN: %[^\r\n]", str2) == 1) {
				if (strcmp(str2, "READY") == 0) {
					gsm_callback_simcardReady();
					break;
				}
				if (strcmp(str2, "SIM PIN") == 0) {
					gsm_callback_simcardPinRequest();
					break;
				}
				if (strcmp(str2, "SIM PUK") == 0) {
					gsm_callback_simcardPukRequest();
					break;
				}
			}
		} else {
			gsm_callback_simcardNotInserted();
		}
		k_sleep(K_MSEC(2000));
	}
#if (_GSM_MSG_ENABLE == 1)
	gsm_msg_textMode(true);
	gsm_msg_selectStorage(Gsm_Msg_Store_MODULE);
	gsm_msg_selectCharacterSet(Gsm_Msg_ChSet_IRA);
#endif
#if (_GSM_DTMF_DETECT_ENABLE == 1)
	gsm_at_sendCommand("AT+DDET=1\r\n", 1000, NULL, 0, 1, "\r\nOK\r\n");
#endif
}
//#############################################################################################
//#############################################################################################
//#############################################################################################
void FUNC_NORETURN gsm_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	static uint64_t gsm10sTimer = 0;
	static uint64_t gsm60sTimer = 0;
	static uint8_t gsmError = 0;
	char str[32];

	gsm_power(true);
#if (_GSM_MSG_ENABLE == 1)
	if (gsm.msg.storageUsed > 0) {
		for (uint16_t i = 0; i < 150; i++) {
			if (gsm_msg_read(i)) {
				gsm_callback_newMsg(gsm.msg.number, gsm.msg.time,
						    (char *)gsm.msg.buff);
				gsm_msg_delete(i);
			}
		}
	}
#endif
	while (1) {
		if (gsm.power == 1) {
#if (_GSM_DTMF_DETECT_ENABLE == 1)
			osEvent event = osMessageGet(gsmDtmfQueueHandle, 0);
			if (event.status == osEventMessage) {
				gsm.taskBusy = 1;
				gsm_callback_dtmf((char)event.value.v);
				gsm.taskBusy = 0;
			}
#endif
#if (_GSM_CALL_ENABLE == 1)
			if (gsm.call.callbackEndCall == 1) // \r\nNO CARRIER\r\n detect
			{
				gsm.taskBusy = 1;
				gsm.call.callbackEndCall = 0;
				gsm_callback_endCall();
				gsm.call.busy = 0;
				gsm.taskBusy = 0;
			}
#endif
#if (_GSM_GPRS_ENABLE == 1)
			if (gsm.gprs.gotData == 1) {
				gsm.taskBusy = 1;
				gsm.gprs.gotData = 0;
				memset(gsm.gprs.buff, 0, sizeof(gsm.gprs.buff));
				sprintf(str, "AT+CIPRXGET=2,%d\r\n", sizeof(gsm.gprs.buff) - 16);
				if (gsm_at_sendCommand(str, 1000, (char *)gsm.gprs.buff,
						       sizeof(gsm.gprs.buff), 2, "\r\n+CIPRXGET: 2",
						       "\r\nERROR\r\n") == 1) {
					uint16_t len = 0, read = 0;
					if (sscanf((char *)gsm.gprs.buff,
						   "\r\n+CIPRXGET: 2,%hd,%hd\r\n", &len,
						   &read) == 2) {
						gsm_callback_gprsGotData(gsm.gprs.buff, len);
					}
				}
				gsm.taskBusy = 0;
			}
#endif
			if (gsm10sTimer < k_uptime_ticks()) //  10 seconds timer
			{
				gsm10sTimer = sys_clock_timeout_end_calc(K_SECONDS(10));
				gsm.taskBusy = 1;
				if ((gsm_getSignalQuality_0_to_100() < 20) ||
				    (gsm.registred == 0)) //  update signal value every 10 seconds
				{
					gsmError++;
					//  restart after 60 seconds while low signal
					if (gsmError == 6) {
						gsm_power(false);
						k_sleep(K_MSEC(2000));
						gsm_power(true);
						gsmError = 0;
					}
					gsm.taskBusy = 0;
				} else
					gsmError = 0;
#if (_GSM_GPRS_ENABLE == 1)
				gsm.taskBusy = 1;
				if (gsm_at_sendCommand("AT+SAPBR=2,1\r\n", 1000, str, sizeof(str),
						       2, "\r\n+SAPBR: 1,", "\r\nERROR\r\n") == 1) {
					if (sscanf(str, "\r\n+SAPBR: 1,1,\"%[^\"\r\n]",
						   gsm.gprs.ip) == 1) {
						if (gsm.gprs.connectedLast == false) {
							gsm.gprs.connected = true;
							gsm.gprs.connectedLast = true;
							gsm_callback_gprsConnected();
						}
					} else {
						if (gsm.gprs.connectedLast == true) {
							gsm.gprs.connected = false;
							gsm.gprs.connectedLast = false;
							gsm_callback_gprsDisconnected();
						}
					}
				} else {
					if (gsm.gprs.connectedLast == true) {
						gsm.gprs.connected = false;
						gsm.gprs.connectedLast = false;
						gsm_callback_gprsDisconnected();
					}
				}
				gsm.taskBusy = 0;
#endif
			}

			if (gsm60sTimer < k_uptime_ticks()) //  60 seconds timer
			{
				gsm60sTimer = sys_clock_timeout_end_calc(K_SECONDS(60));
#if (_GSM_MSG_ENABLE == 1)
				gsm.taskBusy = 1;
				//  check sms memory every 60 seconds
				if (gsm_msg_getStorageUsed() > 0) {
					for (uint16_t i = 0; i < 150; i++) {
						if (gsm_msg_read(i)) {
							gsm_callback_newMsg(gsm.msg.number,
									    gsm.msg.time,
									    (char *)gsm.msg.buff);
							gsm_msg_delete(i);
						}
					}
				}
				gsm.taskBusy = 0;
#endif
			}
#if (_GSM_MSG_ENABLE == 1)
			gsm.taskBusy = 1;
			if (gsm.msg.newMsg != -1) {
				if (gsm_msg_read(gsm.msg.newMsg)) {
					gsm_callback_newMsg(gsm.msg.number, gsm.msg.time,
							    (char *)gsm.msg.buff);
					gsm_msg_delete(gsm.msg.newMsg);
				}
				gsm.msg.newMsg = -1;
			}
			gsm.taskBusy = 0;
#endif
#if (_GSM_CALL_ENABLE == 1)
			gsm.taskBusy = 1;
			if (gsm.call.ringing == 1) {
				gsm_callback_newCall(gsm.call.number);
				gsm.call.ringing = 0;
			}
			gsm.taskBusy = 0;
#endif
		} else {
			gsm.taskBusy = 1;
			if (gsm.enabled) {
				gsm_power(true); //  turn on again, after power down
			}
			gsm.taskBusy = 0;
		}
		k_sleep(K_MSEC(10));
	}
}

K_THREAD_STACK_DEFINE(gsm_main_stack_area, CONFIG_GSM_MODULE_LIB_MAIN_TH_STACK_SIZE);
struct k_thread gsm_main_th;
k_tid_t gsm_main_th_tid;

K_THREAD_STACK_DEFINE(gsm_buffer_stack_area, CONFIG_GSM_MODULE_LIB_BUFFER_TH_STACK_SIZE);
struct k_thread gsm_buffer_th;
k_tid_t gsm_buffer_th_tid;

//#############################################################################################
void FUNC_NORETURN gsmBuffer_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	uart_irq_rx_enable(gsm.uart_dev);
	while (1) {
		gsm_at_checkRxBuffer();
		k_sleep(K_MSEC(1));
	}
}
//#############################################################################################

bool gsm_init(void)
{
	if (gsm.inited == 1)
		return true;
	gsm.inited = 1;
	gsm.enabled = 0;

	gsm.uart_dev = device_get_binding(DT_LABEL(DT_BUS(DT_ALIAS(gsm_at))));
	if (!gsm.uart_dev) {
		//LOG_ERR("SIMCOM GSM device not found");
		gsm.inited = 0;
		return false;
	}

	k_sem_init(&gsm.tx.sem, 0, 1);
	k_mutex_init(&gsmMutex);

	uart_irq_rx_disable(gsm.uart_dev);
	uart_irq_tx_disable(gsm.uart_dev);
	uart_irq_callback_user_data_set(gsm.uart_dev, gsm_uart_isr, &gsm);

	gsm_hw_pins_init();

	gsm_main_th_tid =
		k_thread_create(&gsm_main_th, gsm_main_stack_area,
				K_THREAD_STACK_SIZEOF(gsm_main_stack_area), gsm_task, NULL, NULL,
				NULL, CONFIG_GSM_MODULE_LIB_MAIN_TH_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gsm_main_th, "gsm_main");

	gsm_buffer_th_tid =
		k_thread_create(&gsm_buffer_th, gsm_buffer_stack_area,
				K_THREAD_STACK_SIZEOF(gsm_buffer_stack_area), gsmBuffer_task, NULL,
				NULL, NULL, CONFIG_GSM_MODULE_LIB_BUFFER_TH_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gsm_buffer_th, "gsm_buffer");

#if (_GSM_DTMF_DETECT_ENABLE == 1)
	osMessageQDef(GsmDtmfQueue, 8, uint8_t);
	gsmDtmfQueueHandle = osMessageCreate(osMessageQ(GsmDtmfQueue), NULL);
	if (gsmDtmfQueueHandle == NULL)
		break;
#endif

	return true;
}

bool gsm_enable(bool enable)
{
	gsm.enabled = enable;
	return enable;
}
