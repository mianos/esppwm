#pragma once

#include <cmath>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const int default_frequency = 5000;
class PWMControl {
public:
    PWMControl(int frequency = default_frequency, double duty = 50.0, int gpio_num = GPIO_NUM_2, ledc_timer_bit_t resolution_bits = LEDC_TIMER_13_BIT)
        : gpio_num(gpio_num), frequency(static_cast<uint32_t>(frequency)), resolution_bits(resolution_bits), duty(duty), last_frequency(frequency), last_duty(duty) {

        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC initialization failed.");
        }

        // Start at the duty cycle provided in the constructor
        setDutyCyclePercentage(duty);
    }



	float getCurrentPercentage() const {
		int max_duty = (1 << resolution_bits) - 1;  // For 13-bit resolution, this is 8191
		float percentage = (static_cast<float>(duty) / max_duty) * 100.0f;
		return std::round(percentage * 10.0f) / 10.0f;
	}

    // Set the duty cycle as a raw integer value (0 to max based on resolution)
    void setDutyCycle(int newDuty) {
        duty = newDuty;
        last_duty = newDuty;

        // Reapply the duty cycle
        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to set duty cycle: %s", esp_err_to_name(err));
        }

        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to update duty cycle: %s", esp_err_to_name(err));
        }
    }

    // Set the duty cycle as a percentage (0.0 to 100.0)
    void setDutyCyclePercentage(float percentage, int period=0) {
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

    // Set a new PWM frequency and reinitialize LEDC
    void setFrequency(int newFrequency) {
        if (newFrequency <= 0) {
            ESP_LOGE("PWMControl", "Invalid frequency setting to 5000");
            newFrequency = 5000;
        }

        frequency = static_cast<uint32_t>(newFrequency);
        last_frequency = newFrequency;

        // Save current duty cycle so it can be reapplied after reinitializing the timer
        int currentDuty = duty;

        // Reinitialize the LEDC with the new frequency
        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC reinitialization with new frequency failed.");
            return;
        }

        // Restore the duty cycle after frequency change
        setDutyCycle(currentDuty);
    }

private:
    bool initializeLEDC() {
        if (frequency == 0) {
            ESP_LOGE("PWMControl", "Invalid frequency (0) during LEDC initialization setting to %d", default_frequency);
            frequency = default_frequency;
        }

        // LEDC Timer configuration
        ledc_timer_config_t ledc_timer{};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.duty_resolution = resolution_bits;
        ledc_timer.freq_hz = frequency;  // Ensure frequency is not zero
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;

        esp_err_t err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure LEDC timer: %s", esp_err_to_name(err));
            return false;
        }

        // LEDC Channel configuration
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
    ledc_timer_bit_t resolution_bits;
    int duty;

    // Track the last frequency and duty cycle
    uint32_t last_frequency;
    int last_duty;
};
