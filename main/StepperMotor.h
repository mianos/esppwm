#pragma once

#include "driver/ledc.h"
#include "esp_err.h"
#include "driver/gpio.h"  // Include GPIO header

constexpr gpio_num_t STEP_MOTOR_GPIO_EN = GPIO_NUM_2;   // Enable pin (active low)
constexpr int STEP_MOTOR_GPIO_DIR = 0;  // Direction pin
constexpr int STEP_MOTOR_GPIO_STEP = 1; // Step pin

class StepperMotor {
private:
    ledc_channel_config_t ledc_channel;
    ledc_timer_config_t ledc_timer;
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_channel_t channel_num;
    ledc_timer_t timer_num;

    // Static variables to keep track of used timers and channels
    static ledc_timer_t next_timer_num;
    static ledc_channel_t next_channel_num;

    // Helper function to assign the next available timer and channel
    void assignTimerAndChannel();

public:
    StepperMotor(int gpio_num = STEP_MOTOR_GPIO_STEP,
                 ledc_mode_t speed_mode = LEDC_LOW_SPEED_MODE,
                 int frequency = 1000);

    void setFrequency(int frequency);
    void start();
    void stop();
};

