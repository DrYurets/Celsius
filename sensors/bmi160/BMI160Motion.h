#ifndef CELSIUS_BMI160_MOTION_H
#define CELSIUS_BMI160_MOTION_H

#include <Wire.h>

#define BMI160_CHIP_ID_REG 0x00
#define BMI160_CHIP_ID 0xD1
#define BMI160_CMD_REG 0x7E
/** Команды PMU в 0x7E (даташит Bosch BMI160). */
#define BMI160_CMD_ACC_SUSPEND 0x10
#define BMI160_CMD_ACC_NORMAL 0x11
#define BMI160_ACC_CONF_REG 0x40
#define BMI160_ACC_RANGE_REG 0x41
#define BMI160_ACC_DATA_REG 0x12
#define BMI160_INT_EN_0_REG 0x50
#define BMI160_INT_OUT_CTRL_REG 0x53
#define BMI160_INT_LATCH_REG 0x54
#define BMI160_INT_MAP_0_REG 0x55
#define BMI160_INT_MOTION_0_REG 0x5F
#define BMI160_INT_MOTION_1_REG 0x60

struct BMI160AccelSample {
  int16_t x;
  int16_t y;
  int16_t z;
};

// Адрес I²C после успешного init: 0x68 или 0x69; 0 — не найден.
inline uint8_t &bmi160I2cAddrRef() {
  static uint8_t sAddr = 0;
  return sAddr;
}

#define gBmi160I2cAddr bmi160I2cAddrRef()

/** 0 = ещё не нашли BMI160 на шине (после успешного init — 0x68 или 0x69). */
inline uint8_t bmi160GetI2cAddr() {
  return bmi160I2cAddrRef();
}

inline bool bmi160ReadRegAt(uint8_t i2cAddr, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(i2cAddr, (uint8_t)1) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

inline bool bmi160WriteRegAt(uint8_t i2cAddr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(i2cAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

inline bool bmi160WriteReg(uint8_t reg, uint8_t value) {
  const uint8_t a = gBmi160I2cAddr;
  if (a == 0) {
    return false;
  }
  return bmi160WriteRegAt(a, reg, value);
}

inline bool bmi160ReadReg(uint8_t reg, uint8_t &value) {
  const uint8_t a = gBmi160I2cAddr;
  if (a == 0) {
    return false;
  }
  return bmi160ReadRegAt(a, reg, value);
}

inline bool bmi160ReadRegs(uint8_t startReg, uint8_t *buf, uint8_t len) {
  const uint8_t a = gBmi160I2cAddr;
  if (!buf || len == 0 || a == 0) {
    return false;
  }
  Wire.beginTransmission(a);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(a, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

/**
 * Инициализация BMI160 по I²C: ищем чип на 0x68 и 0x69, затем soft-reset и только акселерометр.
 * Прерывания на INT1 отключены.
 */
inline bool initBMI160Sensor() {
  gBmi160I2cAddr = 0;

  const uint8_t addrs[] = {0x68, 0x69};
  for (uint8_t ai = 0; ai < sizeof(addrs); ai++) {
    const uint8_t a = addrs[ai];
    uint8_t chipId = 0;
    if (!bmi160ReadRegAt(a, BMI160_CHIP_ID_REG, chipId)) {
      Serial.printf("[BMI160] 0x%02X: нет ответа I2C\n", a);
      continue;
    }
    Serial.printf("[BMI160] 0x%02X: CHIP_ID=0x%02X (ожид. 0x%02X)\n", a, chipId, BMI160_CHIP_ID);
    if (chipId != BMI160_CHIP_ID) {
      continue;
    }
    gBmi160I2cAddr = a;
    break;
  }

  if (gBmi160I2cAddr == 0) {
    Serial.println("[BMI160] не найден: проверьте SDA/SCL, питание 3V3, AD0 (адрес 0x68/0x69), модель чипа (нужен BMI160, ID=0xD1)");
    return false;
  }

  // После нахождения адреса — мягкий сброс в известное состояние (рекомендация Bosch).
  bmi160WriteReg(BMI160_CMD_REG, 0xB6);
  delay(25);
  bmi160WriteReg(BMI160_CMD_REG, BMI160_CMD_ACC_NORMAL);
  delay(15);

  bmi160WriteReg(BMI160_ACC_CONF_REG, 0x28);
  bmi160WriteReg(BMI160_ACC_RANGE_REG, 0x05);

  bmi160WriteReg(BMI160_INT_EN_0_REG, 0);
  bmi160WriteReg(BMI160_INT_MAP_0_REG, 0);
  bmi160WriteReg(BMI160_INT_LATCH_REG, 0);
  bmi160WriteReg(BMI160_INT_OUT_CTRL_REG, 0);
  bmi160WriteReg(BMI160_INT_MOTION_0_REG, 0);
  bmi160WriteReg(BMI160_INT_MOTION_1_REG, 0);

  uint8_t regAccConf = 0, regAccRange = 0;
  bmi160ReadReg(BMI160_ACC_CONF_REG, regAccConf);
  bmi160ReadReg(BMI160_ACC_RANGE_REG, regAccRange);
  Serial.printf("[BMI160] OK @0x%02X accConf=0x%02X accRange=0x%02X\n", gBmi160I2cAddr, regAccConf, regAccRange);

  return true;
}

/**
 * Перевод акселерометра в suspend перед deep sleep MCU.
 * Имеет смысл, если BMI160 остаётся под питанием 3V3, пока ESP спит — в normal режиме ток заметно выше.
 * После пробуждения `initBMI160Sensor()` снова поднимает аксель (soft reset + NORMAL).
 */
inline bool bmi160SuspendAccelForSleep() {
  if (gBmi160I2cAddr == 0) {
    return false;
  }
  if (!bmi160WriteReg(BMI160_CMD_REG, BMI160_CMD_ACC_SUSPEND)) {
    return false;
  }
  delay(5);
  return true;
}

inline bool readBMI160Accel(BMI160AccelSample &sample) {
  if (gBmi160I2cAddr == 0) {
    return false;
  }
  uint8_t raw[6];
  if (!bmi160ReadRegs(BMI160_ACC_DATA_REG, raw, sizeof(raw))) {
    return false;
  }
  sample.x = (int16_t)((((uint16_t)raw[1]) << 8) | raw[0]);
  sample.y = (int16_t)((((uint16_t)raw[3]) << 8) | raw[2]);
  sample.z = (int16_t)((((uint16_t)raw[5]) << 8) | raw[4]);
  return true;
}

#endif
