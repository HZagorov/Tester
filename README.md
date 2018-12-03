# Tester
Automatic tester for DL-MINI.

## Prerequisites
In order to compile the tester.c program you need the wiringPi C library used in all Raspberry Pi,
as well as the libmariadbclient library.
To build the Nucleo program first go to the repository
```
https://os.mbed.com/users/HZagorov/code/Nucleo_i2c_raspberry/
```
Click "Export to desktop IDE" and choose toolchain (Sw4STM32). After loading the downloaded project in Sw4STM32 you must remove all --wrap linker options. Go to Project -> C/C++ Build -> Settings. From Tool Settings click MCU G++ Linker -> Miscellaneous. The Linker flags box must include
```
-DMBED_DEBUG -DMBED_TRAP_ERRORS_ENABLED=1 -Wl,-n -Wl,--start-group -lstdc++ -lsupc++ -lm -lc -lgcc -lnosys -Wl,--end-group
```
After this the project is ready to build.
## Compiling
To compile the tester.c program run the following compile command
```
gcc -o tester tester.c -lwiringPi `mysql_config --libs`
```
