
#pragma once

#include <cmath>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Constants
static const int default_frequency = 5000;
static const int QUEUE_SIZE = 10;

// Struct to hold duty cycle and period
struct DutyCycleCommand {
    int duty;
    int period;
};

class PWMControl {
public:
    PWMControl(int frequency = default_frequency, double duty = 50.0,
               int gpio_num = GPIO_NUM_2,
               ledc_timer_bit_t resolution_bits = LEDC_TIMER_13_BIT)
        : gpio_num(gpio_num),
          frequency(static_cast<uint32_t>(frequency)),
          resolution_bits(resolution_bits),
          duty(duty),
          last_frequency(frequency),
          last_duty(duty) {

        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC initialization failed.");
        }

        initializeQueueAndTask();
        setDutyCyclePercentage(duty, 0);  // Start with initial duty cycle
    }

    float getCurrentPercentage() const {
        int max_duty = (1 << resolution_bits) - 1;
        float percentage = (static_cast<float>(duty) / max_duty) * 100.0f;
        return std::round(percentage * 10.0f) / 10.0f;
    }

    void setDutyCyclePercentage(float percentage, int period = 0) {
        if (percentage < 0.0f) percentage = 0.0f;
        if (percentage > 100.0f) percentage = 100.0f;

        int max_duty = (1 << resolution_bits) - 1;
        int newDuty = static_cast<int>((percentage / 100.0f) * max_duty);

        DutyCycleCommand command = {newDuty, period};
        if (xQueueSend(duty_cycle_queue, &command, portMAX_DELAY) != pdPASS) {
            ESP_LOGE("PWMControl", "Failed to send duty cycle command.");
        }
    }

    void setFrequency(int newFrequency) {
        if (newFrequency <= 0) {
            ESP_LOGE("PWMControl", "Invalid frequency, setting to 5000");
            newFrequency = 5000;
        }

        frequency = static_cast<uint32_t>(newFrequency);
        last_frequency = newFrequency;

        int currentDuty = duty;  // Save the current duty cycle

        if (!initializeLEDC()) {
            ESP_LOGE("PWMControl", "LEDC reinitialization failed.");
            return;
        }

        setDutyCycle(currentDuty);  // Reapply duty cycle
    }

private:
    void initializeQueueAndTask() {
        duty_cycle_queue = xQueueCreate(QUEUE_SIZE, sizeof(DutyCycleCommand));
        if (duty_cycle_queue == nullptr) {
            ESP_LOGE("PWMControl", "Failed to create queue.");
            return;
        }

        BaseType_t result = xTaskCreate(dutyCycleTask, "DutyCycleTask", 2048, this, 5, nullptr);
        if (result != pdPASS) {
            ESP_LOGE("PWMControl", "Failed to create task.");
        }
    }

    static void dutyCycleTask(void* pvParameter) {
        PWMControl* pwm = static_cast<PWMControl*>(pvParameter);
        DutyCycleCommand command;

        for (;;) {
            if (xQueueReceive(pwm->duty_cycle_queue, &command, portMAX_DELAY)) {
                ESP_LOGI("PWMControl", "Received duty: %d, period: %d", command.duty, command.period);
                pwm->setDutyCycle(command.duty);
            }
        }
    }

    bool initializeLEDC() {
        if (frequency == 0) {
            ESP_LOGE("PWMControl", "Invalid frequency (0), setting to %d", default_frequency);
            frequency = default_frequency;
        }

        ledc_timer_config_t ledc_timer{};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.duty_resolution = resolution_bits;
        ledc_timer.freq_hz = frequency;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;

        esp_err_t err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure timer: %s", esp_err_to_name(err));
            return false;
        }

        ledc_channel_config_t ledc_channel{};
        ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel.channel = LEDC_CHANNEL_0;
        ledc_channel.timer_sel = LEDC_TIMER_0;
        ledc_channel.intr_type = LEDC_INTR_DISABLE;
        ledc_channel.gpio_num = gpio_num;
        ledc_channel.duty = 0;
        ledc_channel.hpoint = 0;

        err = ledc_channel_config(&ledc_channel);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to configure channel: %s", esp_err_to_name(err));
            return false;
        }

        return true;
    }

    void setDutyCycle(int newDuty) {
        duty = newDuty;
        last_duty = newDuty;

        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to set duty: %s", esp_err_to_name(err));
        }

        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to update duty: %s", esp_err_to_name(err));
        }
    }

private:
    int gpio_num;
    uint32_t frequency;
    ledc_timer_bit_t resolution_bits;
    int duty;

    uint32_t last_frequency;
    int last_duty;

    QueueHandle_t duty_cycle_queue;
};

