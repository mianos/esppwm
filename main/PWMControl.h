#pragma once

#include <cmath>
#include <optional>
#include <algorithm>  // For std::clamp
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class PWMControl {
public:
    PWMControl(int frequency = 5000, double duty = 50.0, int gpio_num = GPIO_NUM_2,
               ledc_timer_bit_t resolution_bits = LEDC_TIMER_13_BIT)
        : gpio_num(gpio_num),
          frequency(static_cast<uint32_t>(frequency)),
          resolution_bits(resolution_bits),
          duty(static_cast<int>((duty / 100.0f) * ((1 << resolution_bits) - 1))),
          last_frequency(frequency),
          last_duty(duty),
          timer_handle(nullptr) {

        // Create a mutex for thread safety
        mutex = xSemaphoreCreateMutex();

        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC initialization failed.");
        }

        // Start at the duty cycle provided in the constructor
        setDutyCyclePercentage(duty);
    }

    ~PWMControl() {
        // Clean up timer and mutex
        if (timer_handle != nullptr) {
            esp_timer_stop(timer_handle);
            esp_timer_delete(timer_handle);
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
        }
    }

    // Get the current duty cycle percentage
    float getCurrentPercentage() const {
        int max_duty = (1 << resolution_bits) - 1;
        float percentage = (static_cast<float>(duty) / max_duty) * 100.0f;
        return std::round(percentage * 10.0f) / 10.0f;
    }

    // Set the duty cycle as a raw integer value (0 to max based on resolution)
    void setDutyCycle(int newDuty) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
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

            xSemaphoreGive(mutex);
        }
    }

    // Set the duty cycle as a percentage (0.0 to 100.0)
    void setDutyCyclePercentage(float percentage, int durationMs = 0, std::optional<float> returnPercentage = std::nullopt) {
        // Clamp the percentage to the valid range [0.0f, 100.0f]
        percentage = std::clamp(percentage, 0.0f, 100.0f);

        if (durationMs > 0) {
            if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
                // Use returnPercentage if provided; otherwise, default to 0.0f
                restore_duty = returnPercentage.value_or(0.0f);

                // Set the temporary percentage
                applyDutyCycle(percentage);

                // Create and start a timer to restore the duty cycle after the specified duration
                startRestoreTimer(durationMs);

                xSemaphoreGive(mutex);
            }
        } else {
            // Permanent change
            applyDutyCycle(percentage);
        }
    }

    // Set a new PWM frequency and reinitialize LEDC
    void setFrequency(int newFrequency) {
        if (newFrequency <= 0) {
            ESP_LOGE("PWMControl", "Invalid frequency: %d", newFrequency);
            return;
        }

        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            frequency = static_cast<uint32_t>(newFrequency);
            last_frequency = newFrequency;

            // Save current duty cycle so it can be reapplied after reinitializing the timer
            int currentDuty = duty;

            // Reinitialize the LEDC with the new frequency
            if (!initializeLEDC()) {
                ESP_LOGE("PWMControl", "LEDC reinitialization with new frequency failed.");
                xSemaphoreGive(mutex);
                return;
            }

            // Restore the duty cycle after frequency change
            setDutyCycle(currentDuty);

            xSemaphoreGive(mutex);
        }
    }

private:
    void applyDutyCycle(float percentage) {
        int max_duty = (1 << resolution_bits) - 1;
        int newDuty = static_cast<int>((percentage / 100.0f) * max_duty);
        setDutyCycle(newDuty);
    }

    void startRestoreTimer(int durationMs) {
        // If a timer already exists, stop and delete it
        if (timer_handle != nullptr) {
            esp_timer_stop(timer_handle);
            esp_timer_delete(timer_handle);
            timer_handle = nullptr;
        }

        // Configure the timer
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = &PWMControl::timerCallback;
        timer_args.arg = this;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "restore_pwm_timer";
        timer_args.skip_unhandled_events = false;

        esp_err_t err = esp_timer_create(&timer_args, &timer_handle);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to create timer: %s", esp_err_to_name(err));
            return;
        }

        // Start the timer
        esp_timer_start_once(timer_handle, durationMs * 1000);  // Convert ms to us
    }

    static void timerCallback(void* arg) {
        PWMControl* pwm = static_cast<PWMControl*>(arg);
        if (xSemaphoreTake(pwm->mutex, portMAX_DELAY) == pdTRUE) {
            pwm->applyDutyCycle(pwm->restore_duty);

            // Clean up the timer
            if (pwm->timer_handle != nullptr) {
                esp_timer_stop(pwm->timer_handle);
                esp_timer_delete(pwm->timer_handle);
                pwm->timer_handle = nullptr;
            }

            xSemaphoreGive(pwm->mutex);
        }
    }

    bool initializeLEDC() {
        if (frequency == 0) {
            ESP_LOGE("PWMControl", "Invalid frequency (0) during LEDC initialization.");
            return false;
        }

        // LEDC Timer configuration
        ledc_timer_config_t ledc_timer = {};
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

        // LEDC Channel configuration
        ledc_channel_config_t ledc_channel = {};
        ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel.channel = LEDC_CHANNEL_0;
        ledc_channel.timer_sel = LEDC_TIMER_0;
        ledc_channel.intr_type = LEDC_INTR_DISABLE;
        ledc_channel.gpio_num = gpio_num;
        ledc_channel.duty = 0;
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

    uint32_t last_frequency;
    int last_duty;

    float restore_duty;
    esp_timer_handle_t timer_handle;
    SemaphoreHandle_t mutex;
};

