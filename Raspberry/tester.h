#define PUPIN	9	//pullup input
#define NPWR	11	//nucleo power
#define CAP 	17	//capacitor
#define REED 	22	//reed ampule
#define JMP 	24	//console jumper
#define PWR 	27	//DUT power


#define NUCLEO_PATH	"/media/pi/NODE_L476RG"
#define IMAGE_PATH	"/home/pi/sourceCodes/test/" \
			"DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin" 
#define SOFT_VER	"0.4.27"
#define UART_PORT	"/dev/ttyS0"
#define I2C_PORT	"/dev/i2c-1"
#define DEF_APN		"mtm.tag.com"
#define I2C_ADDRESS	0x50
#define DEF_HARD_REV	"VB1.0"
#define DEF_PROD_NUM	"DL-MINI-BAT36-D2-3G"

#define DONT_FLUSH 0
#define FLUSH 1

#define MD5SUM_HASH_SIZE 33

#define FLUSH_TIMEOUT 1
#define BOOT_CHECK_TIMEOUT 20
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

void setup_devices();
void setup_termios(int fd);
int write_to_logger(int fd, char *str);
int read_from_logger(int fd, char *comp_str, int flush, float timeout);
void flush(int fd);
int flash_check(int fd);
int flash_logger(int fd);
int fs_write(int fd);
int mock_factory_write(int fd, char *fct_comp_str, char *apn);
int factory_write(int fd, char *fct_comp_str, char *apn,
		  char *hard_rev, char *prod_num);
int led_test(int fd);
int measure_voltage(int fd, int i2c_fd);
int gsm_test(int fd, int i2c_fd, char *apn);
int inputs_config(int fd);
int generate_pulses(int fd, int i2c_fd);
int inputs_test(int fd, int i2c_fd);
int soft_ver_check(int fd, char *soft_ver);
void print_ok();
void print_fail();
void print_error_msg(char *err_msg);
void power_off(int fd, int i2c_fd);
double calculate_time(time_t *start);
void reset_logger();
void reset_nucleo(int sleeptime);
