#include "CST816D.h"

CST816D::CST816D(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
    : _sda(sda_pin), _scl(scl_pin), _rst(rst_pin), _int(int_pin) {}

void CST816D::begin(void) {
  if (_sda != -1 && _scl != -1) {
    Wire.begin(_sda, _scl);
  } else {
    Wire.begin();
  }

  if (_int != -1) {
    pinMode(_int, OUTPUT);
    digitalWrite(_int, HIGH);
    delay(1);
    digitalWrite(_int, LOW);
    delay(1);
  }

  if (_rst != -1) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(300);
  }

  // Disable automatic low-power mode to keep touch reads predictable.
  i2c_write(0xFE, 0xFF);
}

bool CST816D::getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture) {
  const bool finger_detected = static_cast<bool>(i2c_read(0x02));

  *gesture = i2c_read(0x01);
  if (!(*gesture == SlideUp || *gesture == SlideDown)) {
    *gesture = None;
  }

  uint8_t data[4];
  i2c_read_continuous(0x03, data, 4);
  *x = ((data[0] & 0x0F) << 8) | data[1];
  *y = ((data[2] & 0x0F) << 8) | data[3];

  return finger_detected;
}

uint8_t CST816D::i2c_read(uint8_t addr) {
  uint8_t read_data = 0;
  uint8_t read_count = 0;

  do {
    Wire.beginTransmission(I2C_ADDR_CST816D);
    Wire.write(addr);
    Wire.endTransmission(false);
    read_count = Wire.requestFrom(I2C_ADDR_CST816D, static_cast<uint8_t>(1));
  } while (read_count == 0);

  while (Wire.available()) {
    read_data = Wire.read();
  }
  return read_data;
}

uint8_t CST816D::i2c_read_continuous(uint8_t addr, uint8_t *data,
                                     uint32_t length) {
  Wire.beginTransmission(I2C_ADDR_CST816D);
  Wire.write(addr);
  if (Wire.endTransmission(true)) return static_cast<uint8_t>(-1);

  Wire.requestFrom(I2C_ADDR_CST816D, length);
  for (uint32_t index = 0; index < length; ++index) {
    *data++ = Wire.read();
  }
  return 0;
}

void CST816D::i2c_write(uint8_t addr, uint8_t data) {
  Wire.beginTransmission(I2C_ADDR_CST816D);
  Wire.write(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t CST816D::i2c_write_continuous(uint8_t addr, const uint8_t *data,
                                      uint32_t length) {
  Wire.beginTransmission(I2C_ADDR_CST816D);
  Wire.write(addr);
  for (uint32_t index = 0; index < length; ++index) {
    Wire.write(*data++);
  }
  if (Wire.endTransmission(true)) return static_cast<uint8_t>(-1);
  return 0;
}
