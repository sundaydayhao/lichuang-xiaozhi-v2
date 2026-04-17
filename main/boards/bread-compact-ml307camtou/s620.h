#ifndef __S620_H__
#define __S620_H__

#include "i2c_device.h"

class S620 : public I2cDevice {
public:
    S620(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x3C);
    
    // 初始化S620（退出低功耗模式并使能I2C写操作）
    bool Initialize();
    
    // 读取电池电压（单位：mV）
    int GetBatteryVoltage();
    
    // 是否正在充电
    bool IsCharging();
    
    // 是否在放电
    bool IsDischarging();
    
    // 获取电量百分比（0-100），优先使用库仑计，否则使用电压估算
    int GetBatteryLevel();

private:
    // S620寄存器地址
    static constexpr uint8_t REG_VERSION = 0x01;      // 版本寄存器
    static constexpr uint8_t REG_STATUS = 0x0A;       // 系统状态指示（bit0=充电，bit1=放电）
    static constexpr uint8_t REG_ADC_CONFIG = 0x40;   // ADC配置
    static constexpr uint8_t REG_ADC_LOW = 0x41;      // ADC数据低8位
    static constexpr uint8_t REG_ADC_HIGH = 0x42;     // ADC数据高4位
    static constexpr uint8_t REG_COULOMB_CURRENT = 0x56; // 库仑计当前电量（1%/step）
    static constexpr uint8_t REG_FINAL_LEVEL = 0x58;   // 最终电量（1%/step）
    static constexpr uint8_t REG_LOW_POWER = 0x2D;     // 低功耗控制
    static constexpr uint8_t REG_I2C_ENABLE = 0x12;    // I2C写操作使能
    
    // 电压计算参数（根据实际电池规格调整）
    static constexpr int VOLTAGE_MIN = 3000;  // 最低电压（mV）
    static constexpr int VOLTAGE_MAX = 4200;  // 最高电压（mV）
    
    bool initialized_ = false;
};

#endif

