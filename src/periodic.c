/*
 *  This file is part of SWT21 lab kit firmware.
 *
 *  SWT21 lab kit firmware is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SWT21 lab kit firmware is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with SWT21 lab kit firmware.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  Copyright 2021 Joachim Lublin, Binäs Teknik AB
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

#include "hci.h"
#include "led.h"

/*
 * Periodic configuration does not keep the actual configuration but only a copy
 * of the values it needs.
 */
static struct
{
	struct
	{
		uint32_t next;
		uint16_t period;
	} adc[2];
	struct
	{
		uint32_t next;
		uint16_t period;
		uint8_t state;
	} led;
	struct
	{
		uint8_t index;
		uint8_t last_index;
		uint8_t *sequence;
	} morse;
} periodic_conf;

static uint16_t run_flags;
static const uint16_t RUN_FLAG_ADC0 = 1 << 0;
//static const uint16_t RUN_FLAG_ADC1 = 1 << 1;
static const uint16_t RUN_FLAG_LED = 1 << 2;
static const uint16_t RUN_FLAG_MORSE = 1 << 3;

static QueueHandle_t periodic_queue;

struct periodic_event
{
	uint8_t event;
	union
	{
		struct
		{
			uint16_t period;
			uint16_t offset;
		} adc_periodic;
		struct
		{
			uint8_t state;
		} led_off;
		struct
		{
			uint16_t period;
			uint16_t offset;
		} led_blink;
		struct {
			uint8_t length;
			uint8_t *sequence;
		} led_morse;
	};
};

enum
{
	EVENT_ADC_OFF = 0,
	EVENT_ADC_PERIODIC,
	EVENT_LED_OFF,
	EVENT_LED_BLINK,
	EVENT_LED_MORSE
};

/*******************************************************************************
 *
 ******************************************************************************/
void adc_off()
{
	struct periodic_event event =
	{
		.event = EVENT_ADC_OFF,
	};

	xQueueSendToBack(periodic_queue, &event, 0);
}

/*******************************************************************************
 *
 ******************************************************************************/
void adc_periodic(uint16_t period, uint16_t offset)
{
	struct periodic_event event =
	{
		.event = EVENT_ADC_PERIODIC,
		.adc_periodic.period = period,
		.adc_periodic.offset = offset
	};

	xQueueSendToBack(periodic_queue, &event, 0);
}

/*******************************************************************************
 *
 ******************************************************************************/
void led_off(uint8_t state)
{
	struct periodic_event event =
	{
		.event = EVENT_LED_OFF,
		.led_off.state = state
	};

	xQueueSendToBack(periodic_queue, &event, 0);
}

/*******************************************************************************
 *
 ******************************************************************************/
void led_blink(uint16_t period, uint16_t offset)
{
	struct periodic_event event =
	{
		.event = EVENT_LED_BLINK,
		.led_blink.period = period,
		.led_blink.offset = offset
	};

	xQueueSendToBack(periodic_queue, &event, 0);
}

/*******************************************************************************
 *
 ******************************************************************************/
void led_morse(uint8_t *arr, uint8_t length)
{
	struct periodic_event event =
	{
		.event = EVENT_LED_MORSE,
		.led_morse.length = length,
		.led_morse.sequence = arr
	};
	// for (int i=0; i<length * 10; i++){
	// 		printf("%d\n", seq[i]);
	// 	}
	xQueueSendToBack(periodic_queue, &event, 0);
}

/*******************************************************************************
 *
 ******************************************************************************/
void periodic_thread(void *parameters)
{
	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	periodic_queue = xQueueCreate(10, sizeof(struct periodic_event));

	TickType_t current_tick = xTaskGetTickCount();
	TickType_t previous_tick;

	uint8_t morse_index;
	uint8_t dits;
	uint16_t dit_duration = 500;

	while(1)
	{
		previous_tick = current_tick;
		vTaskDelayUntil(&current_tick, 1);

		if(previous_tick + 1 != current_tick)
			printf("ERR: Periodic overrun!\n"); // Overrun

		if(run_flags & RUN_FLAG_ADC0)
		{
			if(current_tick >= periodic_conf.adc[0].next)
			{
				periodic_conf.adc[0].next += periodic_conf.adc[0].period;
				printf("ADC0: ...\n");
			}
		}

		if(run_flags & RUN_FLAG_LED)
		{
			if(current_tick >= periodic_conf.led.next)
			{
				if(run_flags & RUN_FLAG_MORSE){
					morse_index = periodic_conf.morse.index;
					dits = periodic_conf.morse.sequence[morse_index];

					// using led.period to set duration until next toggle
					periodic_conf.led.period = dits * dit_duration;
					periodic_conf.morse.index += 1;

					if (morse_index == periodic_conf.morse.last_index){
						free(periodic_conf.morse.sequence);
						run_flags &= ~RUN_FLAG_MORSE;
						run_flags &= ~RUN_FLAG_LED;
					}
				}
				periodic_conf.led.next += periodic_conf.led.period;

				periodic_conf.led.state = !periodic_conf.led.state;
				led_set_state(periodic_conf.led.state);
			}
		}

		struct periodic_event event;
		if(xQueueReceive(periodic_queue, &event, 0))
		{
			if(event.event == EVENT_ADC_OFF)
			{
				run_flags &= ~RUN_FLAG_ADC0;
				printf("OK\n");
			}

			else if(event.event == EVENT_ADC_PERIODIC)
			{
				uint16_t period = event.adc_periodic.period;
				uint16_t offset = event.adc_periodic.offset;

				uint32_t current_offset = current_tick % period;

				periodic_conf.adc[0].period = period;
				periodic_conf.adc[0].next = current_tick - current_offset + period + offset;

				run_flags |= RUN_FLAG_ADC0;
				printf("OK\n");
			}

			else if(event.event == EVENT_LED_OFF)
			{
				run_flags &= ~RUN_FLAG_LED;
				run_flags &= ~RUN_FLAG_MORSE;
				led_set_state(event.led_off.state);
				printf("OK\n");
			}

			else if(event.event == EVENT_LED_BLINK)
			{
				uint16_t period = event.led_blink.period;
				uint16_t offset = event.led_blink.offset;

				uint32_t current_offset = current_tick % period;

				periodic_conf.led.period = period;
				periodic_conf.led.next = current_tick - current_offset
				                         + period + offset;
				periodic_conf.led.state = 0;

				run_flags |= RUN_FLAG_LED;
				printf("OK\n");
			}
			else if(event.event == EVENT_LED_MORSE)
			{
				periodic_conf.morse.sequence = event.led_morse.sequence;
				periodic_conf.morse.index = 0;
				periodic_conf.morse.last_index = event.led_morse.length - 1;
				periodic_conf.led.state = 0;
				periodic_conf.led.next = current_tick;
				run_flags |= RUN_FLAG_LED;
				run_flags |= RUN_FLAG_MORSE;
				printf("OK\n");
			}
		}

	}
}
