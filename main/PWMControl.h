#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "SettingsManager.h"

struct DutyCycleCommand {
    int duty;
    int period;
};

class PWMControl {
public:
    static constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_13_BIT;
    // The LEDC source clock divided by the counter range; above this the
    // timer can't be configured at 13-bit resolution.
    static constexpr int kMaxFrequency = 80000000 / (1 << kResolution);

    PWMControl(SettingsManager& settings, int gpio_num = GPIO_NUM_2)
        : settings(settings), gpio_num(gpio_num), frequency(settings.frequency) {
        if (frequency <= 0 || frequency > kMaxFrequency) frequency = 1000;
        if (!configureLEDC(frequency)) {
            ESP_LOGE("PWMControl", "LEDC initialization failed.");
        }
        initializeQueueAndTask();
        setDutyCyclePercentage(settings.duty, 0);
    }

    void setDutyCyclePercentage(float percentage, int period = 0) {
        if (percentage < 0.0f) percentage = 0.0f;
        if (percentage > 100.0f) percentage = 100.0f;
        if (settings.invert) percentage = 100.0f - percentage;
        const int max_duty = (1 << kResolution) - 1;
        DutyCycleCommand command = {static_cast<int>((percentage / 100.0f) * max_duty), period};
        if (xQueueSend(duty_cycle_queue, &command, portMAX_DELAY) != pdPASS) {
            ESP_LOGE("PWMControl", "Failed to send duty cycle command.");
        }
    }

    // Keeps the running configuration if the new frequency is rejected.
    bool setFrequency(int newFrequency) {
        if (newFrequency <= 0 || newFrequency > kMaxFrequency) {
            ESP_LOGE("PWMControl", "frequency %d out of range (1..%d)", newFrequency, kMaxFrequency);
            return false;
        }
        if (!configureLEDC(newFrequency)) {
            configureLEDC(frequency);
            setDutyCycle(duty);
            return false;
        }
        frequency = newFrequency;
        setDutyCycle(duty);
        return true;
    }

private:
    void initializeQueueAndTask() {
        duty_cycle_queue = xQueueCreate(10, sizeof(DutyCycleCommand));
        if (duty_cycle_queue == nullptr) {
            ESP_LOGE("PWMControl", "Failed to create queue.");
            return;
        }
        if (xTaskCreate(dutyCycleTask, "DutyCycleTask", 2048, this, 5, nullptr) != pdPASS) {
            ESP_LOGE("PWMControl", "Failed to create task.");
        }
    }

    static void dutyCycleTask(void* pvParameter) {
        auto* pwm = static_cast<PWMControl*>(pvParameter);
        DutyCycleCommand command;
        for (;;) {
            if (xQueueReceive(pwm->duty_cycle_queue, &command, portMAX_DELAY)) {
                ESP_LOGI("PWMControl", "duty %d, period %d", command.duty, command.period);
                if (command.period == 0) {
                    pwm->setDutyCycle(command.duty);
                } else {
                    const int previousDuty = pwm->duty;
                    pwm->setDutyCycle(command.duty);
                    vTaskDelay(pdMS_TO_TICKS(command.period));
                    pwm->setDutyCycle(previousDuty);
                }
            }
        }
    }

    bool configureLEDC(int freq) {
        ledc_timer_config_t ledc_timer{};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.duty_resolution = kResolution;
        ledc_timer.freq_hz = static_cast<uint32_t>(freq);
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
        ledc_channel.gpio_num = gpio_num;
        ledc_channel.duty = static_cast<uint32_t>(duty);
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
        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        if (err != ESP_OK) {
            ESP_LOGE("PWMControl", "Failed to set duty: %s", esp_err_to_name(err));
        }
    }

    SettingsManager& settings;
    int gpio_num;
    int frequency;
    int duty = 0;
    QueueHandle_t duty_cycle_queue = nullptr;
};
