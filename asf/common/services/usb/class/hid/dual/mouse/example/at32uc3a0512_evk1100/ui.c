/**
 * \file
 *
 * \brief User Interface
 *
 * Copyright (C) 2011-2015 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 */
/*
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */

#include <asf.h>
#include "ui.h"

//! Current USB mode
static bool ui_b_host_mode = false;

/**
 * \name Internal routines to manage asynchronous interrupt pin change
 * This interrupt is connected to a switch and allows to wakeup CPU in low sleep mode.
 * In USB device mode, this wakeup the USB host via a upstream resume.
 * In USB host mode, this wakeup the USB devices connected via a downstream resume.
 * @{
 */
static void ui_enable_asynchronous_interrupt(void);
static void ui_disable_asynchronous_interrupt(void);

/**
 * \brief Interrupt handler for interrupt pin change
 */
ISR(ui_wakeup_isr, AVR32_GPIO_IRQ_GROUP, 0)
{
	// Clear GPIO interrupt.
	gpio_clear_pin_interrupt_flag(GPIO_JOYSTICK_PUSH);

	// Clear External Interrupt Line else Wakeup event always enabled
	eic_clear_interrupt_line(&AVR32_EIC, EXT_NMI);

	if (ui_b_host_mode) {
		if (!uhc_is_suspend()) {
			// USB is not in suspend mode
			// Let's interrupt enable.
			return;
		}
		ui_disable_asynchronous_interrupt();

		// Wakeup the devices connected
		uhc_resume();
	} else {
		// In device mode, wakeup the USB host.
		udc_remotewakeup();
	}
}

/**
 * \brief Initializes and enables interrupt pin change
 */
static void ui_enable_asynchronous_interrupt(void)
{
	//! Structure holding the configuration parameters of the EIC low level driver.
	eic_options_t eic_options = {
			// Choose External Interrupt Controller Line
			.eic_line = EXT_NMI,
			// Enable level-triggered interrupt.
			.eic_mode = EIC_MODE_LEVEL_TRIGGERED,
			// Don't care value because the chosen eic mode is level-triggered.
			.eic_edge = 0,
			// Interrupt will trigger on low-level.
			.eic_level = EIC_LEVEL_LOW_LEVEL,
			// Enable filter.
			.eic_filter = EIC_FILTER_ENABLED,
			// For Wake Up mode, initialize in asynchronous mode
			.eic_async = EIC_ASYNCH_MODE};

	/* register joystick handler on level 0 */
	irqflags_t flags = cpu_irq_save();
	irq_register_handler(ui_wakeup_isr,
			AVR32_GPIO_IRQ_0 + (GPIO_JOYSTICK_PUSH / 8), 0);
	cpu_irq_restore(flags);

	/* configure joystick to produce IT on all state change */
	gpio_enable_pin_interrupt(GPIO_JOYSTICK_PUSH, GPIO_PIN_CHANGE);

	/*
	 * Configure pin change interrupt for asynchronous wake-up (required to
	 * wake up from the STATIC sleep mode).
	 *
	 * First, map the interrupt line to the GPIO pin with the right
	 * peripheral function.
	 */
	gpio_enable_module_pin(GPIO_JOYSTICK_PUSH, AVR32_EIC_EXTINT_8_FUNCTION);

	/*
	 * Enable the internal pull-up resistor on that pin (because the EIC is
	 * configured such that the interrupt will trigger on low-level, see
	 * eic_options.eic_level).
	 */
	gpio_enable_pin_pull_up(GPIO_JOYSTICK_PUSH);

	// Init the EIC controller with the set options.
	eic_init(&AVR32_EIC, &eic_options, sizeof(eic_options) /
			sizeof(eic_options_t));

	// Enable External Interrupt Controller Line
	eic_enable_line(&AVR32_EIC, EXT_NMI);
}

/**
 * \brief Disables interrupt pin change
 */
static void ui_disable_asynchronous_interrupt(void)
{
	eic_disable_line(&AVR32_EIC, EXT_NMI);

	/* Disable joystick input change ITs. */
	gpio_disable_pin_interrupt(GPIO_JOYSTICK_PUSH);
}
//! @}

/**
 * \name Main user interface functions
 * @{
 */
void ui_init(void)
{
	LED_Off(LED0);
	LED_Off(LED1);
	LED_Off(LED2);
	LED_Off(LED3);
	LED_Off(LED_BI0_GREEN);
	LED_Off(LED_BI0_RED);
	LED_Off(LED_BI1_GREEN);
}

void ui_usb_mode_change(bool b_host_mode)
{
	ui_init();
	ui_b_host_mode = b_host_mode;
	if (b_host_mode) {
		LED_On(LED0);
	}
}
//! @}

/**
 * \name Host mode user interface functions
 * @{
 */

//! Status of device enumeration
static uhc_enum_status_t ui_host_enum_status=UHC_ENUM_DISCONNECT;
//! Manages device mouse moving
static int8_t ui_host_x, ui_host_y, ui_host_scroll;

void ui_host_vbus_change(bool b_vbus_present)
{
	if (b_vbus_present) {
		LED_On(LED1);
	} else {
		LED_Off(LED1);
	}
}

void ui_host_vbus_error(void)
{
}

void ui_host_connection_event(uhc_device_t *dev, bool b_present)
{
	LED_Off(LED3);
	LED_Off(LED_BI0_GREEN);
	LED_Off(LED_BI0_RED);
	LED_Off(LED_BI1_GREEN);
	if (b_present) {
		LED_On(LED2);
	} else {
		LED_Off(LED2);
		ui_host_enum_status = UHC_ENUM_DISCONNECT;
	}
}

void ui_host_enum_event(uhc_device_t *dev, uhc_enum_status_t status)
{
	ui_host_enum_status = status;
	if (ui_host_enum_status == UHC_ENUM_SUCCESS) {
		ui_host_x = 0, ui_host_y = 0, ui_host_scroll = 0;
	}
}

void ui_host_wakeup_event(void)
{
	ui_disable_asynchronous_interrupt();
}

void ui_host_sof_event(void)
{
	bool b_btn_state;
	static bool btn_suspend = false;
	static bool btn_suspend_and_remotewakeup = false;
	static uint16_t counter_sof=0;

	if (ui_host_enum_status == UHC_ENUM_SUCCESS) {

		// Display device enumerated and in active mode
		if (++counter_sof == 500) {
			counter_sof = 0;
			LED_Toggle(LED3);
		}

		// Scan button to enter in suspend mode
		b_btn_state = !gpio_get_pin_value(GPIO_PUSH_BUTTON_0);
		if (b_btn_state != btn_suspend) {
			// Button have changed
			btn_suspend = b_btn_state;
			if (b_btn_state) {
				// Button has been pressed
				LED_Off(LED3);
				LED_Off(LED_BI0_GREEN);
				LED_Off(LED_BI0_RED);
				LED_Off(LED_BI1_GREEN);
				ui_enable_asynchronous_interrupt();
				uhc_suspend(false);
				return;
			}
		}

		// Scan button to enter in suspend mode and remote wakeup
		b_btn_state = !gpio_get_pin_value(GPIO_PUSH_BUTTON_1);
		if (b_btn_state != btn_suspend_and_remotewakeup) {
			// Button have changed
			btn_suspend_and_remotewakeup = b_btn_state;
			if (b_btn_state) {
				// Button has been pressed
				LED_Off(LED3);
				LED_Off(LED_BI0_GREEN);
				LED_Off(LED_BI0_RED);
				LED_Off(LED_BI1_GREEN);
				ui_enable_asynchronous_interrupt();
				uhc_suspend(true);
				return;
			}
		}

		// Power on a LED when the mouse move
		if (!ui_host_x && !ui_host_y && !ui_host_scroll) {
			LED_Off(LED_BI1_GREEN);
		}else{
			ui_host_x = ui_host_y = ui_host_scroll = 0;
			LED_On(LED_BI1_GREEN);
		}
	}
}

void ui_host_hid_mouse_btn_left(bool b_state)
{
	if (b_state) {
		LED_On(LED_BI0_GREEN);
	} else {
		LED_Off(LED_BI0_GREEN);
	}
}

void ui_host_hid_mouse_btn_right(bool b_state)
{
	if (b_state) {
		LED_On(LED_BI0_RED);
	} else {
		LED_Off(LED_BI0_RED);
	}
}

void ui_host_hid_mouse_btn_middle(bool b_state)
{
}

void ui_host_hid_mouse_move(int8_t x,int8_t y,int8_t scroll)
{
	ui_host_x = x;
	ui_host_y = y;
	ui_host_scroll = scroll;
}
//! @}

/**
 * \name Device mode user interface functions
 * @{
 */
#define  MOUSE_MOVE_RANGE  3
static bool ui_device_b_mouse_enable = false;

void ui_device_suspend_action(void)
{
	ui_init();
}

void ui_device_resume_action(void)
{
	LED_On(LED1);
}

void ui_device_remotewakeup_enable(void)
{
	ui_enable_asynchronous_interrupt();
}

void ui_device_remotewakeup_disable(void)
{
	ui_disable_asynchronous_interrupt();
}

bool ui_device_mouse_enable(void)
{
	ui_device_b_mouse_enable = true;
	return true;
}

void ui_device_mouse_disable(void)
{
	ui_device_b_mouse_enable = false;
}

void ui_device_sof_action(void)
{
	bool b_btn_state;
	uint16_t framenumber;
	static bool btn_left_last_state = HID_MOUSE_BTN_UP;
	static bool btn_right_last_state = HID_MOUSE_BTN_UP;
	static bool btn_middle_last_state = HID_MOUSE_BTN_UP;
	static uint8_t cpt_sof = 0;

	if (!ui_device_b_mouse_enable)
		return;

	framenumber = udd_get_frame_number();
	if ((framenumber % 1000) == 0) {
		LED_On(LED2);
	}
	if ((framenumber % 1000) == 500) {
		LED_Off(LED2);
	}
	// Scan process running each 2ms
	cpt_sof++;
	if (2 > cpt_sof)
		return;
	cpt_sof = 0;

	// Scan buttons on switch 0 (left), 1 (middle), 2 (right)
	b_btn_state = (!gpio_get_pin_value(GPIO_PUSH_BUTTON_0)) ?
			HID_MOUSE_BTN_DOWN : HID_MOUSE_BTN_UP;
	if (b_btn_state != btn_left_last_state) {
		udi_hid_mouse_btnleft(b_btn_state);
		btn_left_last_state = b_btn_state;
	}
	b_btn_state = (!gpio_get_pin_value(GPIO_PUSH_BUTTON_1)) ?
			HID_MOUSE_BTN_DOWN : HID_MOUSE_BTN_UP;
	if (b_btn_state != btn_middle_last_state) {
		udi_hid_mouse_btnmiddle(b_btn_state);
		btn_middle_last_state = b_btn_state;
	}
	b_btn_state = (!gpio_get_pin_value(GPIO_PUSH_BUTTON_2)) ?
			HID_MOUSE_BTN_DOWN : HID_MOUSE_BTN_UP;
	if (b_btn_state != btn_right_last_state) {
		udi_hid_mouse_btnright(b_btn_state);
		btn_right_last_state = b_btn_state;
	}
	// Joystick used to move mouse
	if (is_joystick_right())
		udi_hid_mouse_moveX(MOUSE_MOVE_RANGE);
	if (is_joystick_left())
		udi_hid_mouse_moveX(-MOUSE_MOVE_RANGE);
	if (is_joystick_up())
		udi_hid_mouse_moveY(-MOUSE_MOVE_RANGE);
	if (is_joystick_down())
		udi_hid_mouse_moveY(MOUSE_MOVE_RANGE);
}
//! @}

/**
 * \defgroup UI User Interface
 *
 * Human interface on EVK1100 :
 * - PWR led is on when power present
 * - Led 0 is on in host mode (when USB OTG cable is pluged)
 * - device mode:
 *   - Led 1 is on when USB line is in IDLE mode, and off in SUSPEND mode
 *   - Led 2 blinks when USB host has checked and enabled HID mouse interface
 *   - Mouse buttons are linked to switch PB0 (left), PB1 (middle), PB2 (right)
 *   - Joystick are used to move mouse
 *   - Only Push joystick button can be used to wakeup USB Host in remote wakeup mode.
 * - host mode:
 *   - Led 1 is on when Vbus is present
 *   - Led 2 is on when a device is connected
 *   - Led 3 blinks when the device is enumerated and USB in idle mode
 *   - Led 4 green is on when the mouse left button is down
 *   - Led 4 red is on when the mouse right button is down
 *   - Led 5 green is on when the mouse move
 *   - Switch PB0 allows to enter the device in suspend mode
 *   - Switch PB1 allows to enter the device in suspend mode with remote wakeup feature authorized
 *   - Only Push joystick button can be used to wakeup the USB devices in suspend mode
 *
 * Note: On board the LED labels are incremented of one.
 * E.g. Led0 in software corresponding at Led1 label on board.
 */
