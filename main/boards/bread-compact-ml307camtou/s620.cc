#include "s620.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "S620"

S620::S620(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
    ESP_LOGI(TAG, "S620 created at address 0x%02X", addr);
}

bool S620::Initialize() {
    if (initialized_) {
        return true;
    }
    
    uint8_t data;
    
    WriteReg(REG_LOW_POWER, 0x01);
    
    // 2. 等待退出低功耗模式完成（检查Bit 1）
    int timeout = 100;  // 最多等待100次（1秒）
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        data = ReadReg(REG_LOW_POWER);
        timeout--;
    } while ((data & 0x02) == 0 && timeout > 0);
    
    if (timeout <= 0) {
        ESP_LOGW(TAG, "退出低功耗模式超时（可能芯片本来就不在低功耗模式）");
    } else {
        ESP_LOGI(TAG, "退出低功耗模式成功");
    }
    
    // 3. 重要：根据手册，退出低功耗后需要延时100ms才能读写寄存器
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 4. 写入0x12寄存器的三个值（I2C写操作使能序列）
    // 手册要求：写 REG0x12=0x20 -> 0x40 -> 0x80
    WriteReg(REG_I2C_ENABLE, 0x20);

    vTaskDelay(pdMS_TO_TICKS(10));
    
    WriteReg(REG_I2C_ENABLE, 0x40);

    vTaskDelay(pdMS_TO_TICKS(10));
    
    WriteReg(REG_I2C_ENABLE, 0x80);

    vTaskDelay(pdMS_TO_TICKS(10));
    
    initialized_ = true;
    ESP_LOGD(TAG, "S620初始化完成（I2C写操作已使能）");
    return true;
}

int S620::GetBatteryVoltage() {
    if (!initialized_) {
        if (!Initialize()) {
            return 0;
        }
    }
    
    // 配置ADC读取电池电压（REG0x40 bit3-0 = 2：电池电压，1.2mV/step）
    WriteReg(REG_ADC_CONFIG, 0x02);
    
    vTaskDelay(pdMS_TO_TICKS(10));  // 等待ADC转换
    
    // 读取ADC数据低8位（REG0x41）
    uint8_t adc_low = ReadReg(REG_ADC_LOW);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 读取ADC数据高4位（REG0x42）
    uint8_t adc_high = ReadReg(REG_ADC_HIGH);
    
    // 计算电池电压：Adc_data[11:0] * 1.2mV
    uint16_t adc_value = ((adc_high & 0x0F) << 8) | adc_low;
    int voltage_mv = (int)(adc_value * 1.2f);  // 转换为mV
    
    ESP_LOGD(TAG, "ADC值: 0x%03X, 电池电压: %d mV", adc_value, voltage_mv);
    
    return voltage_mv;
}

bool S620::IsCharging() {
    if (!initialized_) {
        if (!Initialize()) {
            return false;
        }
    }
    
    // REG0x0A bit0：充电状态指示位
    uint8_t status = ReadReg(REG_STATUS);
    return (status & 0x01) != 0;
}

bool S620::IsDischarging() {
    if (!initialized_) {
        if (!Initialize()) {
            return false;
        }
    }
    
    // REG0x0A bit1：放电状态指示位
    uint8_t status = ReadReg(REG_STATUS);
    return (status & 0x02) != 0;
}

int S620::GetBatteryLevel() {
    if (!initialized_) {
        if (!Initialize()) {
            return 0;
        }
    }
    
    // 优先使用库仑计电量（REG0x58：最终电量，1%/step）
    //uint8_t coulomb_level = ReadReg(REG_FINAL_LEVEL);
    //coulomb_level &= 0x7F;  // 清除最高位
    //
    //if (coulomb_level > 0 && coulomb_level <= 100) {
    //    ESP_LOGI(TAG, "使用库仑计电量: %d%%", coulomb_level);
    //    return coulomb_level;
    //}
    
    // 如果库仑计不可用，使用电压估算
    int voltage = GetBatteryVoltage();
    if (voltage < VOLTAGE_MIN) {
        return 0;
    }
    if (voltage > VOLTAGE_MAX) {
        return 100;
    }
    
    // 线性计算电量百分比
    int level = ((voltage - VOLTAGE_MIN) * 100) / (VOLTAGE_MAX - VOLTAGE_MIN);
    level = level > 100 ? 100 : level;
    
    //ESP_LOGI(TAG, "使用电压估算电量: %d%% (电压: %d mV)", level, voltage);
    return level;
}

