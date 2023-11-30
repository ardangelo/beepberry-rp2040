#include "pi.h"
#include "reg.h"
#include "keyboard.h"
#include "gpioexp.h"
#include "backlight.h"
#include "hardware/adc.h"
#include <hardware/pwm.h>

#include <pico/stdlib.h>

enum pi_state
{
	PI_STATE_OFF = 0,
	PI_STATE_ON = 1,
};

static enum pi_state state;

void pi_power_init(void)
{
	adc_init();
	adc_gpio_init(PIN_BAT_ADC);
	adc_select_input(0);

	gpio_init(PIN_PI_PWR);
	gpio_set_dir(PIN_PI_PWR, GPIO_OUT);
	gpio_put(PIN_PI_PWR, 0);
	state = PI_STATE_OFF;
}

void pi_power_on(enum power_on_reason reason)
{
	if (state == PI_STATE_ON) {
		return;
	}

	gpio_put(PIN_PI_PWR, 1);
	state = PI_STATE_ON;

	// LED green while booting until driver loaded
    reg_set_value(REG_ID_LED, 1);
    reg_set_value(REG_ID_LED_R, 0);
    reg_set_value(REG_ID_LED_G, 128);
    reg_set_value(REG_ID_LED_B, 0);
	led_sync();

	// Update startup reason
	reg_set_value(REG_ID_STARTUP_REASON, reason);
}

void pi_power_off(void)
{
	if (state == PI_STATE_OFF) {
		return;
	}

	gpio_put(PIN_PI_PWR, 0);
	state = PI_STATE_OFF;
}

static int64_t pi_power_on_alarm_callback(alarm_id_t _, void* __)
{
	pi_power_on(POWER_ON_REWAKE);

	return 0;
}

void pi_schedule_power_on(uint32_t ms)
{
	add_alarm_in_ms(ms, pi_power_on_alarm_callback, NULL, true);
}

static int64_t pi_power_off_alarm_callback(alarm_id_t _, void* __)
{
	pi_power_off();

	return 0;
}

void pi_schedule_power_off(uint32_t ms)
{
	add_alarm_in_ms(ms, pi_power_off_alarm_callback, NULL, true);
}

void led_init(void)
{
    // Set up PWM channels
    gpio_set_function(PIN_LED_R, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_G, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_B, GPIO_FUNC_PWM);

    //default off
    reg_set_value(REG_ID_LED, 0);

    led_sync();
}

void led_sync(void){
    // Set the PWM slice for each channel
    uint slice_r = pwm_gpio_to_slice_num(PIN_LED_R);
    uint slice_g = pwm_gpio_to_slice_num(PIN_LED_G);
    uint slice_b = pwm_gpio_to_slice_num(PIN_LED_B);

    // Calculate the PWM value for each channel
    uint16_t pwm_r = (0xFF - reg_get_value(REG_ID_LED_R)) * 0x101;
    uint16_t pwm_g = (0xFF - reg_get_value(REG_ID_LED_G)) * 0x101;
    uint16_t pwm_b = (0xFF - reg_get_value(REG_ID_LED_B)) * 0x101;

    // Set the PWM duty cycle for each channel
    if(reg_get_value(REG_ID_LED) == 0){
        pwm_set_gpio_level(PIN_LED_R, 0xFFFF);
        pwm_set_gpio_level(PIN_LED_G, 0xFFFF);
        pwm_set_gpio_level(PIN_LED_B, 0xFFFF);
    } else {
        pwm_set_gpio_level(PIN_LED_R, pwm_r);
        pwm_set_gpio_level(PIN_LED_G, pwm_g);
        pwm_set_gpio_level(PIN_LED_B, pwm_b);
    }

    // Enable PWM channels
    pwm_set_enabled(slice_r, true);
    pwm_set_enabled(slice_g, true);
    pwm_set_enabled(slice_b, true);
}
