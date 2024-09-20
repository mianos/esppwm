#include "StepperMotor.h"
#include "esp_log.h"
#include "driver/gpio.h"  // Include GPIO header for controlling enable pin

static const char* TAG = "StepperMotor";

// Initialize static variables
ledc_timer_t StepperMotor::next_timer_num = LEDC_TIMER_0;
ledc_channel_t StepperMotor::next_channel_num = LEDC_CHANNEL_0;

// Helper function to assign the next available timer and channel
void StepperMotor::assignTimerAndChannel() {
    this->timer_num = next_timer_num;
    this->channel_num = next_channel_num;

    // Update next available channel
    if (next_channel_num < LEDC_CHANNEL_5) {
        next_channel_num = static_cast<ledc_channel_t>(next_channel_num + 1);
    } else {
        // Reset channel number and increment timer
        next_channel_num = LEDC_CHANNEL_0;
        if (next_timer_num < LEDC_TIMER_3) {
            next_timer_num = static_cast<ledc_timer_t>(next_timer_num + 1);
        } else {
            // All timers and channels used, log an error
            ESP_LOGE(TAG, "No more available timers and channels.");
        }
    }
}

// Constructor with automatic timer and channel assignment
StepperMotor::StepperMotor(int gpio_num,
                           ledc_mode_t speed_mode,
                           int frequency) {
    this->gpio_num = gpio_num;
    this->speed_mode = speed_mode;

    // Initialize the enable pin as output and set it to high (disabled)
    gpio_reset_pin(STEP_MOTOR_GPIO_EN);
    gpio_set_direction(STEP_MOTOR_GPIO_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(STEP_MOTOR_GPIO_EN, 1);  // Set enable pin high (inactive)

    assignTimerAndChannel(); // Assign timer and channel automatically
    ledc_timer = {};
    ledc_timer.speed_mode = speed_mode;
    ledc_timer.timer_num = timer_num;
    ledc_timer.duty_resolution = LEDC_TIMER_14_BIT;
    ledc_timer.freq_hz = frequency;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;

    ledc_channel = {};
    ledc_channel.speed_mode = speed_mode;
    ledc_channel.channel = channel_num;
    ledc_channel.timer_sel = timer_num;
    ledc_channel.intr_type = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num = gpio_num;

    // Set duty to 50% for square wave
    int max_duty = (1 << ledc_timer.duty_resolution) - 1;
    ledc_channel.duty = max_duty / 2;
    ledc_channel.hpoint = 0;

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void StepperMotor::setFrequency(int frequency) {
    ledc_timer_config_t updated_timer = ledc_timer; // Copy current timer config
    updated_timer.freq_hz = frequency;              // Update frequency
    ESP_ERROR_CHECK(ledc_timer_config(&updated_timer));
}

void StepperMotor::start() {
    // Set enable pin low (active) to start motor
    gpio_set_level(STEP_MOTOR_GPIO_EN, 0);
    
    ESP_ERROR_CHECK(ledc_timer_resume(speed_mode, timer_num));
}

void StepperMotor::stop() {
    ESP_ERROR_CHECK(ledc_timer_pause(speed_mode, timer_num));
    
    // Set enable pin high (inactive) to stop motor
    gpio_set_level(STEP_MOTOR_GPIO_EN, 1);
}
