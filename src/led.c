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
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "led.h"
#include "hci.h"

char characters[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M','N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U',
                      'V', 'W', 'X', 'Y', 'Z', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
char *morsecode[] = {".-","-...","-.-.","-..",".","..-.","--.","....","..",".---", "-.-",".-..","--","-.","---",".--.","--.-",
                     ".-.","...","-","..-", "...-",".--","-..-","-.--","--..", ".----","..---","...--","....-", ".....", "-....",
                     "--...","---..","----.","-----"};

void to_morse(char msg[], uint8_t *result){
    uint8_t i=0, j, last_index = 35;
	uint16_t k=0;
    char c;
    char *code;

    // for each character in the message:
    while (msg[i] != '\0'){
        c = msg[i];
        // convert to uppercase if needed
        if (c >= 97 && c <= 122) {
            c -= 32;
        }

        // find its index in the character list
        j = 0;
        while (j<=last_index && characters[j] != c){
            j++;
        }

        // no index found: unknown char, skip it
        if (j > last_index){
            i++;
            continue;
        }

        code = morsecode[j];
        j = 0;
        while (code[j] != '\0'){
            // duration for dit/dah
            if(code[j] == '.'){
                result[k] = 1;
            }
            else if (code[j] == '-'){
                result[k] = 3;
            }
            else {
                printf("Unknown morse char: %c\n", code[j]);
            }
            k++;
            // duration between dit/dah
            result[k] = 1;
            k++;
            j++;
        }
        // increase duration to 3 between letters
        result[k-1] += 2;
        i++;
    }
}

const int led_pin = GPIO_NUM_23;

static struct
{
	uint8_t flags; /* initialized */
} led_config;

const uint8_t LED_FLAG_INIT = 1 << 0;

int led_init()
{
	gpio_pad_select_gpio(led_pin);

	if(gpio_set_direction(led_pin, GPIO_MODE_OUTPUT) != ESP_OK)
		goto esp_err;

	led_config.flags |= LED_FLAG_INIT;

	return 0;

esp_err:
	printf("ERR LED init failed!\n");
	return -1;
}

void led_command()
{
	char *cmd = strtok(NULL, " ");

	/* Make sure we have a command */
	if(!cmd)
		goto einval;

	/*
	 * Compare commande against known commands and handle them, otherwise tell
	 * the user it was invalid.
	 */
	if(strcmp(cmd, "help") == 0)
	{
		printf("OK\n");
		printf(
			"Available commands:\n"
			"\n"
			"led on - \n"
			"led off - \n"
			"lin blink [half period] - half period in ms (int, default: 500)\n");
	}
	else if(strcmp(cmd, "on") == 0)
	{
		led_off(1);
	}
	else if(strcmp(cmd, "off") == 0)
	{
		led_off(0);
	}
	else if(strcmp(cmd, "blink") == 0)
	{
		const char *arg = strtok(NULL, " ");
		int period = 500;

		if(arg)
			period = atoi(arg);

		led_blink(period, 0);
	}
	else if(strcmp(cmd, "morse") == 0)
	{
		char *msg = strtok(NULL, " ");

		uint8_t *seq, msglen;
		msglen = strlen(msg);

		// each morse char needs up to 10 led toggles
		seq = (uint8_t *) calloc(msglen * 10, sizeof(uint8_t));

		to_morse(msg, seq);

		uint8_t length=0;
		for (int i=0; i<msglen * 10; i++){
			if (seq[i] == 0){
				break;
			}
			length++;
		}
		led_morse(seq, length);

	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void led_set_state(int state)
{
	gpio_set_level(led_pin, state);
}
