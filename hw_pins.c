/*
 * Copyright (c) 2021 Krivorot Oleg <krivorot.oleg@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gsm.h"

#if CONFIG_GSM_MODULE_LIB_PIN_POWER_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), power_gpios)
__weak void gsm_hw_pin_power_press(void)
{
}

__weak void gsm_hw_pin_power_release(void)
{
}
#endif

#if CONFIG_GSM_MODULE_LIB_PIN_ENABLE_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), enable_gpios)
__weak void gsm_hw_pin_enable_press(void)
{
}

__weak void gsm_hw_pin_enable_release(void)
{
}
#endif

#if CONFIG_GSM_MODULE_LIB_PIN_RESET_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), reset_gpios)
__weak void gsm_hw_pin_reset_press(void)
{
}

__weak void gsm_hw_pin_reset_release(void)
{
}
#endif

#if CONFIG_GSM_MODULE_LIB_PIN_SIM_SELECT_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), pin_sim_select_gpios)
__weak void gsm_hw_pin_sim_select_press(void)
{
}

__weak void gsm_hw_pin_sim_select_release(void)
{
}
#endif

#if CONFIG_GSM_MODULE_LIB_PIN_SPEAKER_ON_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), pin_spk_enable)
__weak void gsm_hw_pin_spk_enable_press(void)
{
}

__weak void gsm_hw_pin_spk_enable_release(void)
{
}
#endif

__weak void gsm_hw_pin_status_read(void)
{
}

__weak void gsm_hw_pin_ring_read(void)
{
}

void gsm_set_cb_hw_key(hw_pins_t type, bool press, void *func)
{
	switch (type) {
#if CONFIG_GSM_MODULE_LIB_PIN_POWER_ENABLE
	case pin_power:
		if (press) {
			gsm.hw_pins.hw_pin_power_press = func;
		} else {
			gsm.hw_pins.hw_pin_power_release = func;
		}
		break;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_RESET_ENABLE
	case pin_reset:
		if (press) {
			gsm.hw_pins.hw_pin_reset_press = func;
		} else {
			gsm.hw_pins.hw_pin_reset_release = func;
		}
		break;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_ENABLE_ENABLE
	case pin_enable:
		if (press) {
			gsm.hw_pins.hw_pin_enable_press = func;
		} else {
			gsm.hw_pins.hw_pin_enable_release = func;
		}
		break;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_SIM_SELECT_ENABLE
	case pin_sim_select:
		if (press) {
			gsm.hw_pins.hw_pin_sim_select_press = func;
		} else {
			gsm.hw_pins.hw_pin_sim_select_release = func;
		}
		break;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_SPEAKER_ON_ENABLE
	case pin_spk_enable:
		if (press) {
			gsm.hw_pins.hw_pin_spk_on_press = func;
		} else {
			gsm.hw_pins.hw_pin_spk_on_release = func;
		}
		break;
#endif
	default:
		break;
	}
}

void gsm_hw_pins_init(void)
{
#if CONFIG_GSM_MODULE_LIB_PIN_POWER_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), power_gpios)
	gsm.hw_pins.hw_pin_power_press = gsm_hw_pin_power_press;
	gsm.hw_pins.hw_pin_power_release = gsm_hw_pin_power_release;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_RESET_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), reset_gpios)
	gsm.hw_pins.hw_pin_reset_press = gsm_hw_pin_reset_press;
	gsm.hw_pins.hw_pin_reset_release = gsm_hw_pin_reset_release;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_ENABLE_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), enable_gpios)
	gsm.hw_pins.hw_pin_enable_press = gsm_hw_pin_enable_press;
	gsm.hw_pins.hw_pin_enable_release = gsm_hw_pin_enable_release;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_SIM_SELECT_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), sim_select_gpios)
	gsm.hw_pins.hw_pin_sim_select_press = gsm_hw_pin_sim_select_press;
	gsm.hw_pins.hw_pin_sim_select_release = gsm_hw_pin_sim_select_release;
#endif
#if CONFIG_GSM_MODULE_LIB_PIN_SPEAKER_ON_ENABLE && DT_NODE_HAS_PROP(DT_ALIAS(gsm_at), spk_on_gpios)
	gsm.hw_pins.hw_pin_spk_on_press = gsm_hw_pin_spk_on_press;
	gsm.hw_pins.hw_pin_spk_on_release = gsm_hw_pin_spk_on_release;
#endif
}