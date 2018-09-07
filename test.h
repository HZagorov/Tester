int compare_strings(char buf[], char compstr[]);
int write_to_logger(int fd, char str[]);
int read_from_logger(int fd, char comp_str[], int flush, int timeout);
void flush(int fd);
int start();
int flash_check(int fd, char flash_str[]);
int flash_logger(int fd, char flash_str[]);
int fs_write(int fd);
int mock_factory_write(int fd, char fct_str[]);
int factory_write(int fd, char fct_str[]);
int led_test(int fd);
int measure_voltage(int fd, int i2c_fd);
int gsm_test(int fd, char bd_str[], char ping_str[], int i2c_fd);
void inputs_config(int fd);
int generate_pulses(int fd, int i2c_fd);
int inputs_test(int fd, int i2c_fd);
int soft_rev_check(int fd);

