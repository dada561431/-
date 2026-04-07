/*
 * LED.c
 *
 *  Created on: 2025年11月13日
 *      Author: 14838
 */
#include "LED.h"
#include "cy_device_headers.h"

void set_led1(bool is_light) {
	if (is_light)
		*((unsigned int *)GPIO_PRT16) |= 0x80;
	else
 		*((unsigned int *)GPIO_PRT16) &= 0x7F;
}

void flip_led1() {
	*((unsigned int *)GPIO_PRT16) ^= 0x70;
}

void set_led2(bool is_light) {
	if (is_light)
		*((unsigned int *)GPIO_PRT16) |= 0x40;
	else
 		*((unsigned int *)GPIO_PRT16) &= 0xBF;
}

void flip_led2() {
	*((unsigned int *)GPIO_PRT16) ^= 0x40;
}

