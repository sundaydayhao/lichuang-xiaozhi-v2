#pragma once

#include <esp_log.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static constexpr const char* kGreenLampTag = "绿灯事件：";

// 舵机控制参数（参考 otto_robot）
#define SERVO_MIN_PULSEWIDTH_US 500           // 最小脉宽（微秒）
#define SERVO_MAX_PULSEWIDTH_US 2500          // 最大脉宽（微秒）
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000  // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD 20000           // 20000 ticks, 20ms

class Green_Lamp  {
private:
    bool power_ = false;                 // 是否处于“按摩中”
    gpio_num_t gpio_num_;
    ledc_channel_t ledc_channel_;
    ledc_mode_t ledc_speed_mode_;
    bool is_attached_ = false;
    int current_position_ = 90;          // 当前角度（0-180）
    int speed_level_ = 0;                // 0=停止，1=低，2=中，3=高
    inline static Green_Lamp* instance_ = nullptr; // 全局实例指针，便于本地STT直接控制

    // 按摩计时
    TimerHandle_t massage_timer_ = nullptr;   // 一次性定时器
    int massage_duration_minutes_ = 3;        // 默认3分钟

    // 设置舵机角度（0-180度）
    void SetServoAngle(int angle) {
        if (!is_attached_) return;

        angle = (angle < 0) ? 0 : (angle > 180 ? 180 : angle);
        current_position_ = angle;

        // 将角度转换为PWM占空比（LEDC 13bit, 50Hz）
        uint32_t duty = (uint32_t)(((angle / 180.0) * 2.0 + 0.5) * 8191 / 20.0);

        ESP_ERROR_CHECK(ledc_set_duty(ledc_speed_mode_, ledc_channel_, duty));
        ESP_ERROR_CHECK(ledc_update_duty(ledc_speed_mode_, ledc_channel_));

        ESP_LOGI(kGreenLampTag, "设置舵机角度: %d度", angle);
    }

    // 档位 -> 角度映射（>90 为单向旋转；如需反向可将数值改到 <90）
    int AngleForLevel(int level) {
        switch (level) {
            case 1: return 100; // 慢
            case 2: return 120; // 中
            case 3: return 140; // 快
            default: return 90; // 停止
        }
    }

    // 根据 power_ 与 speed_level_ 应用速度
    void ApplySpeed() {
        if (!power_ || speed_level_ == 0) {
            SetServoAngle(90); // 停止
            ESP_LOGI(kGreenLampTag, "停止按摩（90°）");
            return;
        }
        int angle = AngleForLevel(speed_level_);
        SetServoAngle(angle);
        ESP_LOGI(kGreenLampTag, "开始/调整按摩：%d 档（角度=%d）", speed_level_, angle);
    }

    // 初始化LEDC PWM
    void InitializeServo() {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 50,  // 标准舵机 50Hz
            .clk_cfg = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_ = LEDC_CHANNEL_0;
        ledc_speed_mode_ = LEDC_LOW_SPEED_MODE;

        ledc_channel_config_t ledc_channel = {
            .gpio_num = gpio_num_,
            .speed_mode = ledc_speed_mode_,
            .channel = ledc_channel_,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_1,
            .duty = 0,
            .hpoint = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        is_attached_ = true;
        SetServoAngle(90); // 停止位
        ESP_LOGI(kGreenLampTag, "舵机初始化完成，GPIO=%d", gpio_num_);
    }

    // 计时到点后的回调：自动停止
    static void MassageTimerCallback(TimerHandle_t xTimer) {
        auto self = static_cast<Green_Lamp*>(pvTimerGetTimerID(xTimer));
        if (!self) return;
        self->power_ = false;
        self->speed_level_ = 0;
        self->ApplySpeed();
        ESP_LOGI(kGreenLampTag, "按摩计时结束，已自动停止");
    }

    void StartMassageTimer(int minutes) {
        if (minutes <= 0) return;
        if (massage_timer_ == nullptr) {
            massage_timer_ = xTimerCreate("massage_timer", pdMS_TO_TICKS(60000), pdFALSE, this, MassageTimerCallback);
        }
        if (massage_timer_) {
            xTimerStop(massage_timer_, 0);
            xTimerChangePeriod(massage_timer_, pdMS_TO_TICKS(minutes * 60000), 0);
            xTimerStart(massage_timer_, 0);
            ESP_LOGI(kGreenLampTag, "按摩计时启动：%d 分钟", minutes);
        }
    }

    void StopMassageTimer() {
        if (massage_timer_) {
            xTimerStop(massage_timer_, 0);
            ESP_LOGI(kGreenLampTag, "按摩计时取消");
        }
    }

public:
    explicit Green_Lamp(gpio_num_t gpio_num) : gpio_num_(gpio_num) {
        instance_ = this;
        InitializeServo();
    }

    ~Green_Lamp() {
        // 停止舵机
        if (is_attached_) {
            SetServoAngle(90);
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_ERROR_CHECK(ledc_stop(ledc_speed_mode_, ledc_channel_, 0));
            is_attached_ = false;
        }
    }

public:
    // 本地STT快速路径：先动再说
    static void StartMassage() {
        if (instance_) {
            instance_->power_ = true;
            if (instance_->speed_level_ < 1) instance_->speed_level_ = 1;
            instance_->ApplySpeed();
            instance_->StartMassageTimer(instance_->massage_duration_minutes_);
        }
    }
    static void StopMassage() {
        if (instance_) {
            instance_->power_ = false;
            instance_->speed_level_ = 0;
            instance_->ApplySpeed();
            instance_->StopMassageTimer();
        }
    }
    static void IncreaseLevel() {
        if (instance_) {
            if (!instance_->power_) { instance_->power_ = true; instance_->speed_level_ = 1; }
            else if (instance_->speed_level_ < 3) { instance_->speed_level_++; }
            instance_->ApplySpeed();
        }
    }
    static void DecreaseLevel() {
        if (instance_) {
            if (instance_->power_) {
                if (instance_->speed_level_ > 1) instance_->speed_level_--;
                if (instance_->speed_level_ < 1) instance_->speed_level_ = 1;
            } else {
                instance_->speed_level_ = 0;
            }
            instance_->ApplySpeed();
        }
    }

    // 设定默认按摩时长（分钟）
    static void SetMassageDurationMinutes(int minutes) {
        if (instance_ && minutes > 0) {
            instance_->massage_duration_minutes_ = minutes;
            ESP_LOGI(kGreenLampTag, "设定按摩默认时长：%d 分钟", minutes);
        }
    }

    // 直接按指定分钟开始按摩
    static void StartMassageWithMinutes(int minutes) {
        if (instance_ && minutes > 0) {
            instance_->power_ = true;
            if (instance_->speed_level_ < 1) instance_->speed_level_ = 1;
            instance_->ApplySpeed();
            instance_->StartMassageTimer(minutes);
        }
    }

    static bool IsMassageRunning() {
        return instance_ != nullptr && instance_->power_;
    }

    static bool IsAvailable() {
        return instance_ != nullptr;
    }
};
