#pragma once

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

class PWMControl {
public:
    PWMControl(int gpio_num = GPIO_NUM_2, int frequency = 5000, ledc_timer_bit_t resolution_bits = LEDC_TIMER_13_BIT, int duty = 4095)
        : gpio_num(gpio_num), frequency(static_cast<uint32_t>(frequency)), resolution_bits(resolution_bits), duty(duty) {
        
        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC initialization failed.");
        }
    }

    // Set the duty cycle as a raw integer value (0 to max based on resolution)
    void setDutyCycle(int newDuty) {
        duty = newDuty;
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    }

    // Set the duty cycle as a percentage (0.0 to 100.0)
    void setDutyCyclePercentage(float percentage) {
        if (percentage < 0.0f) {
            percentage = 0.0f;  // Clamp percentage to 0% if below range
        } else if (percentage > 100.0f) {
            percentage = 100.0f;  // Clamp percentage to 100% if above range
        }

        // Convert percentage to duty cycle based on the resolution
        int max_duty = (1 << resolution_bits) - 1;  // For 13-bit resolution, this is 8191
        int newDuty = static_cast<int>((percentage / 100.0f) * max_duty);

        setDutyCycle(newDuty);
    }

private:
    bool initializeLEDC() {
        ledc_timer_config_t ledc_timer{};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.duty_resolution = resolution_bits;
        ledc_timer.freq_hz = frequency;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;

        esp_err_t err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure LEDC timer: %s", esp_err_to_name(err));
            return false;
        }

        ledc_channel_config_t ledc_channel{};
        ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel.channel = LEDC_CHANNEL_0;
        ledc_channel.timer_sel = LEDC_TIMER_0;
        ledc_channel.intr_type = LEDC_INTR_DISABLE;
        ledc_channel.gpio_num = gpio_num;
        ledc_channel.duty = 0;  // Start with 0% duty to prevent sudden output
        ledc_channel.hpoint = 0;

        err = ledc_channel_config(&ledc_channel);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure LEDC channel: %s", esp_err_to_name(err));
            return false;
        }

        return true;
    }

private:
    int gpio_num;
    uint32_t frequency;
    ledc_timer_bit_t resolution_bits;  // Use ledc_timer_bit_t instead of int for resolution bits
    int duty;
};
