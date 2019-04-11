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

To run the tester program write
```
tester 
```
Without any options the program will test every module and will exit if any failure has occured.
The default testing program is for GSM module. If the device is NB-IoT use the -n option.
```
tester -n
```

For faultless testing, without interrupting or stopping the program when test failures occurre, use the -e option
```
tester -e
```

For separate module testing use the following options
```
-s Manually insert serial number
-f Flash DUT
-l Test LED
-m Test communication module
-i Test lptim inputs
-r Test reed ampule
-d Write serial number and module parameters to database
```

Note that when in faultless mode the database insertion is omitted.
For example if you want to flash the DUT, test the communication module, manually write serial number and insert module parameters to database, use:
```
tester -f -m -d -s 
```
