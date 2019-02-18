#include "mbed.h"
#include "string.h"
#define DIVIDE_COEF 3
//I2CSlave (I2C_SDA, I2C_SCL);

I2CSlave slave (D14, D15);
Serial pc(SERIAL_TX, SERIAL_RX);
DigitalOut digitalPin1(D12);
DigitalOut digitalPin2(D13);
DigitalIn powerVoltage(D4);
AnalogIn analog_value(A1);

char voltage[10];

void generate_pulses()
{
    for (int i = 0; i < 100 ; i++) {
        digitalPin1 = 1;
        digitalPin2 = 1;
        wait_ms(5);
        digitalPin1 = 0;
        digitalPin2 = 0;
        wait_ms(5);
    }
}

void measure_voltage()
{
    float meas = analog_value.read(); // Converts and read the analog input value (value from 0.0 to 1.0)
    meas = meas * 3300 * DIVIDE_COEF; // Change the value to be in the 0 to 3300 range
    printf("Measured voltage = %.2f mV\r\n", meas);
    sprintf(voltage, "%.2f", meas);
}

int main()
{

    char buf[20];
    char msg1[20] = "Generate";
    char word[] = "Word";
    char msg2[20] = "Measure";

    slave.address(0xA0);
    printf("I2C program started\r\n");

    
    while (1) {
        int i = slave.receive();
        switch (i) {
            case I2CSlave::ReadAddressed:
                //slave.write(word, 5);
                slave.write(voltage, 8);
                break;
            case I2CSlave::WriteGeneral:
                slave.read(buf,10);
                printf("Read G: %s\r\n", buf);
                break;
            case I2CSlave::WriteAddressed:
                slave.read(buf, 20);
                //printf("msg = %s\r\nbuf = %s\r\n", msg1, buf);
                if (strcmp (buf, msg1) == 0) {
                    generate_pulses();
                }
                if (strcmp (buf, msg2) == 0) {
                    measure_voltage();
                }
                break;
        }
        for (int i = 0; i < 10; i++) buf[i] = 0; //clear buf
    }
}




