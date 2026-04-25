#include "mpu.h"
#include <Arduino.h>

// constructor
MPU6050::MPU6050(uint8_t address, uint32_t accel_fs_range, uint32_t gyro_fs_range) {
    this->_address = address;
    this->_accel_fs_range = accel_fs_range;
    this->_gyro_fs_range = gyro_fs_range;

}

// initialize the MPU6050 
uint8_t  MPU6050::init() {
    // NOTE: Wire.begin() is intentionally NOT called here.
    // The I2C bus is initialized once in setup() before any tasks start.
    // Calling Wire.begin() from inside a task resets the I2C peripheral and
    // causes all concurrent I2C devices (BMP, ADS1115) to return 0x0000.
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

    // Verify the device responds by reading WHO_AM_I register (0x75).
    // MPU6050 returns 0x68 when healthy.
    Wire.beginTransmission(this->_address);
    Wire.write(0x75); // WHO_AM_I register
    Wire.endTransmission(false);
    Wire.requestFrom(static_cast<int>(this->_address), 1, static_cast<int>(true));
    uint8_t who_am_i = Wire.available() ? Wire.read() : 0x00;

    if (who_am_i == 0x68 || who_am_i == 0x72) { // 0x68 = MPU6050, 0x72 = MPU6500
        Serial.println(F("[+]MPU6050 init OK."));
        return 1;
    } else {
        Serial.print(F("[-]MPU6050 init failed. WHO_AM_I=0x"));
        Serial.println(who_am_i, HEX);
        return 0;
    }
}

/**
 * Read X axiS acceleration
*/
float MPU6050::readXAcceleration() {
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

    return this->acc_x_real;

}

/**
 * Read Y acceleration
*/
float MPU6050::readYAcceleration() {
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

    return this->acc_y_real;
    
}

/**
 * Read Z acceleration
*/
float MPU6050::readZAcceleration() {
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

    return this->ang_vel_x_real;
}

float MPU6050::readYAngularVelocity() {
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

    return this->ang_vel_y_real;
}

float MPU6050::readZAngularVelocity() {
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