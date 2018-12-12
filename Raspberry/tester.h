//GPIO macros
#define PUPIN	9	//pullup input
#define NPWR	11	//nucleo power
#define CAP 	17	//capacitor
#define REED 	22	//reed ampule
#define JMP 	24	//console jumper
#define PWR 	27	//DUT power

#define FAILURE		0
#define SUCCESS		1
#define BOOT_STR	"NuttShell"
#define DEF_APN		"mtm.tag.com"
#define CAP_DISCH_TIME	30
#define NO_DISCH	0
#define LPTIM1_PULSES	"96"
#define LPTIM2_PULSES	"95"

//Test flags
#define T_FLASH		0x01
#define T_LED		0x02
#define T_MODULE	0x04
#define T_INPUTS	0x08
#define T_REED		0x10
#define T_FACTORY	0x20

//Read flags
#define NONE		0x00
#define PRINT 		0x01	//print what's read
#define STORE 		0x02	//store the matched string from read()

//Programming macros
#define SOFT_VER	"0.4.27"
#define IMAGE_PATH	"/home/pi/sourceCodes/test/" \
			"DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin" 

//Factory macros
#define DEF_HARD_REV	"VB1.0"
#define DEF_PROD_NUM	"DL-MINI-BAT36-D2-3G"

//Database macros
#define DB_SERVER	"localhost"
#define DB_USER		"raspberry"
#define DB_PASS		"smartcom"
#define DATABASE	"tester"
#define DB_TABLE	"test"

//Device macros
#define NUCLEO_PATH	"/media/pi/NODE_L476RG"
#define UART_PORT	"/dev/ttyS0"
#define I2C_PORT	"/dev/i2c-1"
#define I2C_ADDRESS	0x50


//Flush macros
#define DONT_FLUSH 0
#define FLUSH 1

//MD5 macros
#define MD5SUM_HASH_SIZE 33
#define IMAGE_BLOCKS 65

//Timeout macros
#define FLUSH_TIMEOUT 1
#define BOOT_TIMEOUT 2
#define FLASH_BOOT_TIMEOUT 20
#define MD5SUM_TIMEOUT 2
#define FACTORY_TIMEOUT 2
#define UMTS_TIMEOUT 40
#define UMTS_ERROR_TIMEOUT 30
#define PING_TIMEOUT 90
#define INPUTS_CONF_SLEEP 0.5
#define SLEEP_TIMEOUT 120
#define EM_TIMEOUT 15	//electromagnet timeout
#define REED_CHECK_TIMEOUT 20 
#define ALARM_TIMEOUT 2
#define PULSE_TIMEOUT 0.5

int check_serial_number(int fd);

void power_devices();
int setup_devices(int *fd, int *i2c_fd);
int get_available_space(char *path);
void setup_termios(int fd);
int check_serial_number(int fd);
void begin_test(time_t *start);
int write_to_logger(int fd, char *str);
int read_from_logger(int fd, char *comp_str, float timeout, int flags);
void flush(int fd);
int flash_check(int fd);
int flash_logger(int fd);
int fs_config(int fd);
int mock_factory_write(int fd, char *fct_comp_str, char *apn);
int setup_mysql(MYSQL *con);
int manual_serial_number_insert(char *serial_number);
int serial_number_insert(char *serial_number);
int factory_write(int fd, char *fct_comp_str, char *apn,
		  char *hard_rev, char *prod_num, int (*func)(char *));
int led_test(int fd);
int measure_voltage(int fd, int i2c_fd);
int module_test(int fd, int i2c_fd, char *apn);
int inputs_config(int fd);
int generate_pulses(int fd, int i2c_fd);
int inputs_test(int fd, int i2c_fd);
int reed_test(int fd);
int soft_ver_check(int fd);
void print_ok();
void print_fail();
void print_error_msg(char *err_msg);
void close_fds(int fd_count, ...);
void power_off(int disch_time);
double calculate_time(time_t *start);
void reset_logger();
void reset_nucleo(int sleeptime);
void end_test(int fd, int i2c_fd, int cap_time, int test_result, time_t *start);
