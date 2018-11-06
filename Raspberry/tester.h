#define PUPIN	9	//pullup input
#define NPWR	11	//nucleo power
#define CAP 	17	//capacitor
#define REED 	22	//reed ampule
#define JMP 	24	//console jumper
#define PWR 	27	//DUT power

#define BOOT_CHECK_TIMEOUT 20

#define NUCLEO_PATH	"/media/pi/NODE_L476RG"
#define IMAGE_PATH	"/home/pi/sourceCodes/test/" \
			"DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin" 
#define SOFT_VER	"0.4.27"
#define UART_PORT	"/dev/ttyS0"
#define I2C_PORT	"/dev/i2c-1"

#define DONT_FLUSH 0
#define FLUSH 1

void setup_devices();
void setup_termios(int fd);
int write_to_logger(int fd, char *str);
int read_from_logger(int fd, char *comp_str, int flush, float timeout);
void flush(int fd);
int flash_check(int fd);
int flash_logger(int fd);
int fs_write(int fd);
int mock_factory_write(int fd, char *fct_comp_str, char *apn);
int factory_write(int fd, char *fct_comp_str, char *apn);
int led_test(int fd);
int measure_voltage(int fd, int i2c_fd);
int gsm_test(int fd, int i2c_fd, char *apn);
int inputs_config(int fd);
int generate_pulses(int fd, int i2c_fd);
int inputs_test(int fd, int i2c_fd);
int soft_ver_check(int fd, char *soft_ver);
void print_ok();
void print_fail();
void power_off(int fd, int i2c_fd);
double calculate_time(time_t *start);
void reset_logger();
void reset_nucleo(int sleeptime);
