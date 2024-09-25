#pragma once

#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

class PWMControl {
public:
    PWMControl(int gpio_num = GPIO_NUM_2, int frequency = 5000, ledc_timer_bit_t resolution_bits = LEDC_TIMER_13_BIT, int duty = 4095)
        : gpio_num(gpio_num), frequency(frequency), resolution_bits(resolution_bits), duty(duty) {
        
        // Initialize the timer and channel
        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "Failed to initialize LEDC timer or channel");
        }
    }

    void setDutyCycle(int newDuty) {
        duty = newDuty;
        // Set the new duty cycle
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
        // Apply the duty cycle change
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    }

private:
    bool initializeLEDC() {
        // Configure the LEDC timer
        ledc_timer_config_t ledc_timer{};
        ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num        = LEDC_TIMER_0;
        ledc_timer.duty_resolution  = resolution_bits;   // Set duty resolution
        ledc_timer.freq_hz          = frequency;         // Set frequency
        ledc_timer.clk_cfg          = LEDC_AUTO_CLK;     // Use automatic clock configuration

        esp_err_t err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure LEDC timer: %s", esp_err_to_name(err));
            return false;
        }

        // Configure the LEDC channel
        ledc_channel_config_t ledc_channel{};
        ledc_channel.speed_mode     = LEDC_LOW_SPEED_MODE;
        ledc_channel.channel        = LEDC_CHANNEL_0;
        ledc_channel.timer_sel      = LEDC_TIMER_0;       // Attach channel to the configured timer
        ledc_channel.intr_type      = LEDC_INTR_DISABLE;   // Disable interrupts
        ledc_channel.gpio_num       = gpio_num;            // GPIO pin
        ledc_channel.duty           = 0;                   // Initialize with 0% duty cycle
        ledc_channel.hpoint         = 0;                   // Initialize hpoint to 0

        err = ledc_channel_config(&ledc_channel);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure LEDC channel: %s", esp_err_to_name(err));
            return false;
        }

        return true;
    }

private:
    int gpio_num;
    int frequency;
    ledc_timer_bit_t resolution_bits;
    int duty;
};

