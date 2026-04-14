#include "mpu.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t i2c_mutex;

static inline bool i2cLock(TickType_t timeout) {
    if (i2c_mutex == NULL) {
        return true;
    }
    return xSemaphoreTake(i2c_mutex, timeout) == pdTRUE;
}

static inline void i2cUnlock() {
    if (i2c_mutex != NULL) {
        xSemaphoreGive(i2c_mutex);
    }
}

// constructor
MPU6050::MPU6050(uint8_t address, uint32_t accel_fs_range, uint32_t gyro_fs_range)
        : _address(address),
            _accel_fs_range(accel_fs_range),
            _gyro_fs_range(gyro_fs_range),
            mpu_dmp(address, &Wire),
            dmp_ready(false),
            dmp_packet_valid(false),
            packet_size(0),
            last_dmp_update_ms(0) {}

// initialize the MPU6050 
uint8_t  MPU6050::init() {
    // initialize the MPU6050 
    if (!i2cLock(pdMS_TO_TICKS(100))) {
        Serial.println(F("[-]MPU6050 init failed - I2C busy."));
        return 0;
    }

    bool x = Wire.begin(static_cast<int>(SDA), static_cast<int>(SCL));
    Wire.setTimeOut(50);
    Wire.beginTransmission(this->_address);
    Wire.write(PWR_MNGMT_1); // power on the device 
    Wire.write(RESET);
    Wire.endTransmission(true);
    delay(50);

    // configure the gyroscope
    Wire.beginTransmission(this->_address);
    Wire.write(GYRO_CONFIG);
    if(this->_gyro_fs_range == 250) {
        Wire.write(SET_GYRO_FS_250);
    } else if(this->_gyro_fs_range == 500) {
        Wire.write(SET_GYRO_FS_500);
    } else if (this->_gyro_fs_range == 1000) {
        Wire.write(SET_GYRO_FS_1000);
    } else if (this->_gyro_fs_range == 2000) {
        Wire.write(SET_GYRO_FS_2000);
    }
    Wire.endTransmission(true);
    delay(50);

    // configure the accelerometer
    Wire.beginTransmission(this->_address);
    Wire.write(ACCEL_CONFIG);

    if(this->_accel_fs_range == 2) {
        Wire.write(SET_ACCEL_FS_2G);
    } else if (this->_accel_fs_range == 4) {
         Wire.write(SET_ACCEL_FS_4G);
    }  else if (this->_accel_fs_range== 8) {
         Wire.write(SET_ACCEL_FS_8G);
    }  else if (this->_accel_fs_range == 16) {
         Wire.write(SET_ACCEL_FS_16G);
    }
    Wire.endTransmission(true);

    // TODO: ceck initialization properly
    if (x) {
        Serial.println(F("[+]MPU6050 init OK."));
        i2cUnlock();
        return 1;
    }
    else {
        Serial.println(F("[-]MPU6050 init failed."));
        i2cUnlock();
        return 0;
    }
    
}

/**
 * Read X axiS acceleration
*/
float MPU6050::readXAcceleration() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->acc_x_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(ACCEL_XOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, static_cast<int>(WIRE_SEND_STOP));
    this->acc_x = Wire.read()<<8 | Wire.read();

    // divide by the respective factors
    if(this->_accel_fs_range == 2) {
        this->acc_x_real = (float) acc_x / ACCEL_FACTOR_2G;
    } else if(this->_accel_fs_range == 4) {
        this->acc_x_real = (float) acc_x / ACCEL_FACTOR_4G; 
    } else if(this->_accel_fs_range == 8) {
        this->acc_x_real = (float) acc_x / ACCEL_FACTOR_8G;
    } else if(this->_accel_fs_range == 16) {
        this->acc_x_real = (float) acc_x / ACCEL_FACTOR_16G;
    }

    i2cUnlock();
    return this->acc_x_real;

}

/**
 * Read Y acceleration
*/
float MPU6050::readYAcceleration() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->acc_y_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(ACCEL_YOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, WIRE_SEND_STOP);
    this->acc_y = Wire.read()<<8 | Wire.read();

    // divide by the respective factors
    if(this->_accel_fs_range == 2) {
        this->acc_y_real = (float) acc_y / ACCEL_FACTOR_2G;
    } else if(this->_accel_fs_range == 4) {
        this->acc_y_real = (float) acc_y / ACCEL_FACTOR_4G; 
    } else if(this->_accel_fs_range == 8) {
        this->acc_y_real = (float) acc_y / ACCEL_FACTOR_8G;
    } else if(this->_accel_fs_range == 16) {
        this->acc_y_real = (float) acc_y / ACCEL_FACTOR_16G;
    }

    i2cUnlock();
    return this->acc_y_real;
    
}

/**
 * Read Z acceleration
*/
float MPU6050::readZAcceleration() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->acc_z_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(ACCEL_ZOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, WIRE_SEND_STOP);
    this->acc_z = Wire.read()<<8 | Wire.read();

    // divide by the respective factors
    if(this->_accel_fs_range == 2) {
        this->acc_z_real = (float) acc_z / ACCEL_FACTOR_2G;
    } else if(this->_accel_fs_range == 4) {
        this->acc_z_real = (float) acc_z / ACCEL_FACTOR_4G; 
    } else if(this->_accel_fs_range == 8) {
        this->acc_z_real = (float) acc_z / ACCEL_FACTOR_8G;
    } else if(this->_accel_fs_range == 16) {
        this->acc_z_real = (float) acc_z / ACCEL_FACTOR_16G;
    }

    i2cUnlock();
    return this->acc_z_real;
    
}

/**
 * compute the pitch angle
 * angle along the transverse axis 
 * return roll angle in degrees
*/
float MPU6050::getRoll() {
    // convert the imu readings to m/s^2
    this->acc_y_ms = this->readYAcceleration() * ONE_G;
    this->acc_z_ms = this->readZAcceleration() * ONE_G;

    this->roll_angle = atan2(this->acc_y_ms, this->acc_z_ms);

    return this->roll_angle * TO_DEG_FACTOR;    

}

/**
 * compute the roll angle
 * angle along the longitudinal axis
 * return pitch angle in degrees
*/
float MPU6050::getPitch() {

    // convert the imu readings to m/s^2
    this->acc_x_ms = this->readXAcceleration() * ONE_G;

    double u = this->acc_x_ms / ONE_G;

    // clip to [-1, +1] bound before passing to arcsine
    if( ! ( (u > 1) || (u < -1) )) {
        this->pitch_angle = asin(this->acc_x_ms/ONE_G);
    }

    return this->pitch_angle * TO_DEG_FACTOR;
}

float MPU6050::readXAngularVelocity() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->ang_vel_x_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(GYRO_XOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, static_cast<int>(WIRE_SEND_STOP));
    this->ang_vel_x = Wire.read() << 8 | Wire.read();

    // divide by the configured settings 
    if(this->_gyro_fs_range == 250) {
        this->ang_vel_x_real = (float) ang_vel_x / GYRO_FACTOR_250; 
    } else if (this->_gyro_fs_range == 500) {
        this->ang_vel_x_real = (float) ang_vel_x / GYRO_FACTOR_500; 
    } else if(this->_gyro_fs_range == 1000) {
        this->ang_vel_x_real = (float) ang_vel_x / GYRO_FACTOR_1000; 
    } else if(this->_gyro_fs_range == 2000) {
        this->ang_vel_x_real = (float) ang_vel_x / GYRO_FACTOR_2000; 
    }

    i2cUnlock();
    return this->ang_vel_x_real;
}

float MPU6050::readYAngularVelocity() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->ang_vel_y_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(GYRO_YOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, static_cast<int>(WIRE_SEND_STOP));
    this->ang_vel_y = Wire.read() << 8 | Wire.read();

    // divide by the confiured settings 
    if(this->_gyro_fs_range == 250) {
        this->ang_vel_y_real = (float) ang_vel_y / GYRO_FACTOR_250; 
    } else if (this->_gyro_fs_range == 500) {
        this->ang_vel_y_real = (float) ang_vel_y / GYRO_FACTOR_500; 
    } else if(this->_gyro_fs_range == 1000) {
        this->ang_vel_y_real = (float) ang_vel_y / GYRO_FACTOR_1000; 
    } else if(this->_gyro_fs_range == 2000) {
        this->ang_vel_y_real = (float) ang_vel_y / GYRO_FACTOR_2000; 
    }

    i2cUnlock();
    return this->ang_vel_y_real;
}

float MPU6050::readZAngularVelocity() {
    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return this->ang_vel_z_real;
    }
    Wire.beginTransmission(this->_address);
    Wire.write(GYRO_ZOUT_H);
    Wire.endTransmission(true);

    Wire.requestFrom(static_cast<int>(this->_address), 2, static_cast<int>(WIRE_SEND_STOP));
    this->ang_vel_z= Wire.read() << 8 | Wire.read();

    // divide by the confiured settings 
    if(this->_gyro_fs_range == 250) {
        this->ang_vel_z_real = (float) ang_vel_z / GYRO_FACTOR_250; 
    } else if (this->_gyro_fs_range == 500) {
        this->ang_vel_z_real = (float) ang_vel_z / GYRO_FACTOR_500; 
    } else if(this->_gyro_fs_range == 1000) {
        this->ang_vel_z_real = (float) ang_vel_z / GYRO_FACTOR_1000; 
    } else if(this->_gyro_fs_range == 2000) {
        this->ang_vel_z_real = (float) ang_vel_z / GYRO_FACTOR_2000; 
    }

    i2cUnlock();
    return this->ang_vel_z_real;
}



/**
 * perform sensor fusion
 * perfom complementary filter to remove accelerometer high frequrecny noise 
 * remove low frequency noise from gyroscope and fuse the sensors 
*/
void MPU6050::filterImu() {
    // complementary filter formula 
    // return this value as the final correct value from the IMU
    

}

// float MPU6050::readTemperature() {
//     // write to temp register
//     Wire.beginTransmission(this->_address);
//     Wire.write(TEMP_OUT_H);
//     Wire.endTransmission(true);

//     Wire.requestFrom(static_cast<int>(this->_address), 2, WIRE_SEND_STOP);
//     this->temp = Wire.read()<<8 | Wire.read();

//     // temperature conversion formula 
//     // temp = (TEMP_OUT_VALUE as a signed quantity)/340 +36.53
// }

// ═══════════════════════════════════════════════════════════════════════════════════════
// DMP (Digital Motion Processor) Implementation
// ═══════════════════════════════════════════════════════════════════════════════════════

/**
 * @brief Initialize DMP (Digital Motion Processor) for quaternion-based angle estimation
 * @return 0 = success, non-zero = DMP initialization error code
 */
uint8_t MPU6050::initDMP() {
    Serial.println(F("[DMP] Initializing DMP..."));

    if (!i2cLock(pdMS_TO_TICKS(200))) {
        Serial.println(F("[DMP] Initialization failed (I2C busy)"));
        dmp_ready = false;
        return 1;
    }
    
    uint8_t devStatus = mpu_dmp.dmpInitialize();
    
    if (devStatus != 0) {
        Serial.print(F("[DMP] Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
        dmp_ready = false;
        i2cUnlock();
        return devStatus;
    }
    
    // Align full-scale ranges with configured settings before calibration.
    uint8_t accel_range = MPU6050_ACCEL_FS_2;
    switch (_accel_fs_range) {
        case 4:  accel_range = MPU6050_ACCEL_FS_4; break;
        case 8:  accel_range = MPU6050_ACCEL_FS_8; break;
        case 16: accel_range = MPU6050_ACCEL_FS_16; break;
        default: accel_range = MPU6050_ACCEL_FS_2; break;
    }
    mpu_dmp.setFullScaleAccelRange(accel_range);

    uint8_t gyro_range = MPU6050_GYRO_FS_250;
    switch (_gyro_fs_range) {
        case 500:  gyro_range = MPU6050_GYRO_FS_500; break;
        case 1000: gyro_range = MPU6050_GYRO_FS_1000; break;
        case 2000: gyro_range = MPU6050_GYRO_FS_2000; break;
        default:   gyro_range = MPU6050_GYRO_FS_250; break;
    }
    mpu_dmp.setFullScaleGyroRange(gyro_range);

    i2cUnlock();

    // Calibrate sensors (blocking by design as requested)
    Serial.println(F("[DMP] Calibrating accel/gyro..."));
    calibrateSensors();

    if (!i2cLock(pdMS_TO_TICKS(200))) {
        Serial.println(F("[DMP] Enable failed (I2C busy)"));
        dmp_ready = false;
        return 1;
    }

    // Enable DMP
    Serial.println(F("[DMP] Enabling DMP..."));
    mpu_dmp.setDMPEnabled(true);
    
    // Set packet size and enable DMP
    packet_size = mpu_dmp.dmpGetFIFOPacketSize();
    dmp_ready = true;
    dmp_packet_valid = false;

    i2cUnlock();
    
    Serial.println(F("[DMP] DMP initialized successfully!"));
    return 0;
}

/**
 * @brief Calibrate accelerometer and gyroscope offsets
 * Performs 6-iteration Accel and Gyro calibration
 */
void MPU6050::calibrateSensors() {
    Serial.println(F("[DMP] Starting sensor calibration..."));
    Serial.println(F("[DMP] Please keep device stationary..."));

    if (!i2cLock(pdMS_TO_TICKS(500))) {
        Serial.println(F("[DMP] Calibration skipped (I2C busy)"));
        return;
    }
    
    // Calibrate Accel (6 iterations)
    mpu_dmp.CalibrateAccel(6);
    
    // Calibrate Gyro (6 iterations)
    mpu_dmp.CalibrateGyro(6);
    
    // Print active offsets for verification
    mpu_dmp.PrintActiveOffsets();

    i2cUnlock();
    
    Serial.println(F("[DMP] Calibration complete"));
}

/**
 * @brief Poll FIFO buffer for latest DMP packet
 * Extracts yaw/pitch/roll and angular velocities from quaternion and gravity
 * @param packet - DMPPacket struct to store results
 * @return true if new data available, false otherwise
 */
bool MPU6050::pollFIFO(DMPPacket& packet) {
    if (!dmp_ready) {
        return false;
    }

    if (!i2cLock(pdMS_TO_TICKS(20))) {
        return false;
    }
    
    // Get FIFO count
    uint16_t fifo_count = mpu_dmp.getFIFOCount();
    
    // Check for overflow (shouldn't happen if polling often enough)
    if (fifo_count == 1023) {
        // reset so we can continue cleanly
        mpu_dmp.resetFIFO();
        i2cUnlock();
        return false;
    }
    
    // Non-blocking behavior: if not enough bytes yet, return and try again next cycle.
    // Blocking here can starve other RTOS tasks and look like a startup freeze.
    if (fifo_count < packet_size) {
        i2cUnlock();
        return false;
    }
    
    // Read packet from FIFO (returns 0 when no valid data, >0 on success)
    if (mpu_dmp.dmpGetCurrentFIFOPacket(fifo_buffer) > 0) {
        // Get quaternion
        mpu_dmp.dmpGetQuaternion(&q, fifo_buffer);
        
        // Get gravity vector
        mpu_dmp.dmpGetGravity(&gravity, &q);
        
        // Get yaw/pitch/roll angles in radians
        mpu_dmp.dmpGetYawPitchRoll(ypr, &q, &gravity);
        
        // Convert to degrees and store in packet
        packet.yaw = ypr[0] * 180.0f / M_PI;      // Z rotation (heading)
        packet.pitch = ypr[1] * 180.0f / M_PI;    // Y rotation (pitch)
        packet.roll = ypr[2] * 180.0f / M_PI;     // X rotation (roll)
        
        // Get raw gyro rates from the DMP packet
        VectorInt16 gyro_raw;
        mpu_dmp.dmpGetGyro(&gyro_raw, fifo_buffer);
        
        // Convert gyro raw values to deg/s based on full-scale range
        if (_gyro_fs_range == 250) {
            packet.gx = gyro_raw.x / GYRO_FACTOR_250;
            packet.gy = gyro_raw.y / GYRO_FACTOR_250;
            packet.gz = gyro_raw.z / GYRO_FACTOR_250;
        } else if (_gyro_fs_range == 500) {
            packet.gx = gyro_raw.x / GYRO_FACTOR_500;
            packet.gy = gyro_raw.y / GYRO_FACTOR_500;
            packet.gz = gyro_raw.z / GYRO_FACTOR_500;
        } else if (_gyro_fs_range == 1000) {
            packet.gx = gyro_raw.x / GYRO_FACTOR_1000;
            packet.gy = gyro_raw.y / GYRO_FACTOR_1000;
            packet.gz = gyro_raw.z / GYRO_FACTOR_1000;
        } else {  // 2000 dps
            packet.gx = gyro_raw.x / GYRO_FACTOR_2000;
            packet.gy = gyro_raw.y / GYRO_FACTOR_2000;
            packet.gz = gyro_raw.z / GYRO_FACTOR_2000;
        }
        
        // Update local gyro readings for backward compatibility
        ang_vel_x_real = packet.gx;
        ang_vel_y_real = packet.gy;
        ang_vel_z_real = packet.gz;
        
        // Update local pitch/roll for backward compatibility
        pitch_angle = packet.pitch;
        roll_angle = packet.roll;
        dmp_packet_valid = true;
        last_dmp_update_ms = millis();

        // Update accel cache from the DMP packet to avoid extra I2C reads.
        VectorInt16 accel_raw;
        mpu_dmp.dmpGetAccel(&accel_raw, fifo_buffer);
        float accel_scale = ACCEL_FACTOR_2G;
        if (_accel_fs_range == 4) {
            accel_scale = ACCEL_FACTOR_4G;
        } else if (_accel_fs_range == 8) {
            accel_scale = ACCEL_FACTOR_8G;
        } else if (_accel_fs_range == 16) {
            accel_scale = ACCEL_FACTOR_16G;
        }
        acc_x_real = accel_raw.x / accel_scale;
        acc_y_real = accel_raw.y / accel_scale;
        acc_z_real = accel_raw.z / accel_scale;

        i2cUnlock();
        return true;
    }
    i2cUnlock();
    return false;
}

bool MPU6050::hasFreshDMPPacket(uint32_t maxAgeMs) const {
    if (!dmp_packet_valid) {
        return false;
    }
    if (maxAgeMs == 0) {
        return true;
    }
    uint32_t age_ms = millis() - last_dmp_update_ms;
    return age_ms <= maxAgeMs;
}


