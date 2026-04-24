#ifndef CELSIUS_BMI160_MOTION_H
#define CELSIUS_BMI160_MOTION_H

#include <Wire.h>

#define BMI160_I2C_ADDR 0x68
#define BMI160_CHIP_ID_REG 0x00
#define BMI160_CHIP_ID 0xD1
#define BMI160_CMD_REG 0x7E
#define BMI160_ACC_CONF_REG 0x40
#define BMI160_ACC_RANGE_REG 0x41
#define BMI160_INT_EN_0_REG 0x50
#define BMI160_INT_OUT_CTRL_REG 0x53
#define BMI160_INT_LATCH_REG 0x54
#define BMI160_INT_MAP_0_REG 0x55
#define BMI160_INT_MOTION_0_REG 0x5F
#define BMI160_INT_MOTION_1_REG 0x60
#define BMI160_INT_STATUS_0_REG 0x1C

inline bool bmi160WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BMI160_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

inline bool bmi160ReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(BMI160_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(BMI160_I2C_ADDR, (uint8_t)1) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

inline bool initBMI160MotionWake() {
  uint8_t chipId = 0;
  if (!bmi160ReadReg(BMI160_CHIP_ID_REG, chipId) || chipId != BMI160_CHIP_ID) {
    return false;
  }

  bmi160WriteReg(BMI160_CMD_REG, 0xB6);
  delay(15);
  bmi160WriteReg(BMI160_CMD_REG, 0x11);
  delay(5);

  bmi160WriteReg(BMI160_ACC_CONF_REG, 0x28);
  bmi160WriteReg(BMI160_ACC_RANGE_REG, 0x05);
  bmi160WriteReg(BMI160_INT_MOTION_0_REG, 0x02);
  bmi160WriteReg(BMI160_INT_MOTION_1_REG, 0x14);
  bmi160WriteReg(BMI160_INT_EN_0_REG, 0x07);
  bmi160WriteReg(BMI160_INT_OUT_CTRL_REG, 0x05);
  bmi160WriteReg(BMI160_INT_LATCH_REG, 0x0F);
  bmi160WriteReg(BMI160_INT_MAP_0_REG, 0x04);

  uint8_t regAccConf = 0, regAccRange = 0, regIntEn0 = 0, regIntOut = 0, regLatch = 0, regMap0 = 0, regStat0 = 0;
  bmi160ReadReg(BMI160_ACC_CONF_REG, regAccConf);
  bmi160ReadReg(BMI160_ACC_RANGE_REG, regAccRange);
  bmi160ReadReg(BMI160_INT_EN_0_REG, regIntEn0);
  bmi160ReadReg(BMI160_INT_OUT_CTRL_REG, regIntOut);
  bmi160ReadReg(BMI160_INT_LATCH_REG, regLatch);
  bmi160ReadReg(BMI160_INT_MAP_0_REG, regMap0);
  bmi160ReadReg(BMI160_INT_STATUS_0_REG, regStat0);
  Serial.printf("BMI160 regs accConf=0x%02X accRange=0x%02X intEn0=0x%02X intOut=0x%02X latch=0x%02X map0=0x%02X stat0=0x%02X\n",
                regAccConf, regAccRange, regIntEn0, regIntOut, regLatch, regMap0, regStat0);

  return true;
}

#endif
