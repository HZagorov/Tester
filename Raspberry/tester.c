#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <linux/i2c-dev.h>

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <mysql/mysql.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>

#include "tester.h"


int 
main(int argc, char *argv[])
{
	char *apn = DEF_APN;
	char *hard_rev = DEF_HARD_REV;
	char *prod_num = DEF_PROD_NUM;
	int fd, i2c_fd, opt;
	time_t start;
	int (*ser_num_func) (char *) = serial_number_insert; 	
	
	char fct_str[] = "written successfully";	
	char apn_cmd[200];

	// Check for input parameters
	while ((opt = getopt(argc, argv, "a:h:p:s")) != -1) {
		switch (opt){
		case 'a':
			apn = optarg;
			break;
		case 'h':
			hard_rev = optarg;
			break;	
		case 'p':
			prod_num = optarg;
			break;
		case 's':
			ser_num_func = manual_serial_number_insert;
			break;	
		default:
			fprintf(stderr, "Usage: %s [-a apn] "
				"[-h hardware revision] "
				"[-p product number] -s\n"
				"Where: -s: Manually "
				"insert serial number\n", argv[0]);
			return -1;
		}
	}

	snprintf(apn_cmd, sizeof(apn_cmd), 
		 "printf \"umts_apn %s\" >> /rsvd/default.cfg\n"
		 "printf \"\\n\" >> rsvd/default.cfg\n", apn);

	power_devices();
	if (setup_devices(&fd, &i2c_fd)){
		power_off(0);
		return -1;
	}
	setup_termios(fd); // Set options for serial port
	if (check_serial_number(fd)){
		close_fds(2, fd, i2c_fd);
		power_off(0);
		return -1;
	}
	begin_test(&start);	


	// Test conditions
	if (flash_logger(fd)
		|| fs_write(fd) // File system config
		|| mock_factory_write(fd, fct_str, apn_cmd) 
		|| led_test(fd) 
		|| gsm_test(fd, i2c_fd, apn) 
		|| inputs_test(fd, i2c_fd) 
		|| factory_write(fd, fct_str, apn_cmd, hard_rev, prod_num,
				ser_num_func )) {
	
		close_fds(2, fd, i2c_fd);
		power_off(CAP_DISCH_TIME);
		print_error_msg("Test failed\n");
		return -1;
	}
		
	soft_ver_check(fd);
	close_fds(2, fd, i2c_fd);
	power_off(CAP_DISCH_TIME);
	end_test(&start);	
	return 0;
}

void 
power_devices()
{
	char fail_path[100];

	snprintf(fail_path, sizeof(fail_path), "%s/FAIL.TXT", NUCLEO_PATH);

	wiringPiSetupGpio();
	pinMode(PWR, OUTPUT);
	digitalWrite(PWR, HIGH);
	pinMode(JMP, OUTPUT);
	pinMode(REED, OUTPUT);
	pinMode(CAP, OUTPUT);
	digitalWrite(CAP, LOW);
	digitalWrite(JMP, HIGH);

	// Check if Nucleo file system appears 
	if (open(NUCLEO_PATH, O_RDONLY) < 0) {
		reset_nucleo(1);
	} 	
	// Check for FAIL.TXT
	if (open(fail_path, O_RDONLY) > 0) {
		reset_nucleo(1);
	}
	// Reset if Nucleo free space is insufficient
	if (get_available_space(NUCLEO_PATH) < IMAGE_BLOCKS){
		reset_nucleo(1);
	}
}

int 
get_available_space(char *path)
{
	struct statvfs statfs;

	statvfs(path, &statfs);
	return statfs.f_bfree;
}

int 
setup_devices(int *fd, int *i2c_fd)
{
	// Open serial port 
	if ((*fd = open(UART_PORT, O_RDWR | O_NOCTTY)) == -1) { // O_NDELAY
		perror("Unable to open serial port");
		return -1;
	}

	// Connect to Nucleo	
	if ((*i2c_fd = open(I2C_PORT, O_RDWR)) < 0) {
		printf("Failed to open the I2C bus\n");
		close(*fd);
		return -1;
	}

	// Set I2C Nucleo address
	if (ioctl(*i2c_fd, I2C_SLAVE, I2C_ADDRESS)) {
		printf("Failed to acquire bus access and/or talk to Nucleo.\n");
		close_fds(2, *fd, *i2c_fd);
		return -1;
	}
}

void 
setup_termios(int fd)
{
	struct termios options;

	tcgetattr(fd, &options);
	options.c_iflag &=  ~(ICRNL);
	options.c_lflag |= ICANON; /*ICANON flag allows us to turn off
			   	     canonical mode.This means we will
			   	     be reading input byte-by-byte,
			   	     instead of line-by-line.*/
	//options.c_lflag |= (ICANON | ECHOE | ECHOK);
	options.c_lflag &= ~( ECHO | ECHOE | ECHOK | ECHONL |IEXTEN | ISIG);
	options.c_cflag = B38400 | CS8 |CLOCAL| CREAD;
	tcsetattr (fd,TCSANOW, &options);	
}

int
check_serial_number(int fd)
{
	char serial_cmd[] = "cat /rsvd/factory.txt\n";
	char serial_line[20] = "serial";
	char serial_number[10];
	int c, j = 0;

	if (!read_from_logger(fd, BOOT_STR, BOOT_TIMEOUT, NONE)
	  	&& !write_to_logger(fd, serial_cmd)
		&& !read_from_logger(fd, serial_line, 1, STORE)) {

		for (int i = 0; i < strlen(serial_line); i++) {
			if (isdigit(serial_line[i])) {
				serial_number[j++] = serial_line[i];
			}
		}
		serial_number[j] = '\0';

		if (serial_number[0] != '\0'){
			printf("Device has a serial number - %s\n",
				serial_number);
			printf("Begin test or quit? [y/n] ");
			c = getchar();
			if (c != 'y' && c != 'Y') {
				return -1;	
			}
		}
	}
	return 0;
}	

void 
begin_test(time_t *start)
{
	printf("=============");
	printf("\033[1;32m");
	printf("TEST BEGIN");
	printf("\033[0m");
	printf("=============\n\n");
	time(start);
}

int 
write_to_logger (int fd, char *str)
{
	int str_size = strlen(str);
	int retval = write(fd, str, str_size);

	if (retval == str_size) {
		return 0;
	} else { 
		perror("Write failed - ");
		return - 1;
	}
}

int
read_from_logger (int fd, char *comp_str, float timeout, int flags)
{
	int retval;
	int rx_length;
	char buf[256];

	fd_set rfds;
	struct timeval tv;
	
	FD_ZERO(&rfds);		// Clears the file descriptor set
	FD_SET(fd, &rfds);	// Add fd to the set

	tv.tv_sec = 0;
	tv.tv_usec = (int) (timeout * 1000000);
	
	while(1) {
		retval = select(fd+1, &rfds, NULL, NULL, &tv);
		if (retval == -1) {
			perror("select()");
			return -1;
		}
		else if (FD_ISSET(fd, &rfds)) {
			rx_length = read(fd, buf, 255);
		  	buf[rx_length] = '\0';
			if (flags & PRINT) { 
				printf("%s", buf);
			}
			if (comp_str != NULL) {	
				char *pch = strstr(buf,comp_str);
				//printf("comp_str is %s\n", comp_str);
				if (pch) {
					if (flags & STORE){
						strcpy(comp_str, buf);
					}
					return 0;
				}
			}
		}
		else { 
		//	printf("No data within %d secs\n", timeout);
			return 1;
		}		
	} 
}

// Flush serial input
void 
flush(int fd)
{
	read_from_logger(fd, NULL, FLUSH_TIMEOUT, NONE);
}

void 
calculate_md5sum(char *md5sum, size_t size)
{
	char md5sum_cmd[100];
	int i, ch;
	
	snprintf(md5sum_cmd, sizeof(md5sum_cmd), "md5sum %s", IMAGE_PATH);
	FILE *p = popen(md5sum_cmd, "r");

	for (i = 0; i < size - 1  && isxdigit(ch = fgetc(p)); i++) {
		*md5sum++ = ch;
	}

	*md5sum = '\0';
	pclose(p);
}

int 
flash_check(int fd)
{
	int image_size;
	char md5c_cmd[100];
	char md5sum[MD5SUM_HASH_SIZE];
	struct stat image_stat;
	
	flush(fd);

	stat(IMAGE_PATH, &image_stat);
	image_size = image_stat.st_size;
	snprintf(md5c_cmd, sizeof(md5c_cmd), 
		 "md5c -f /dev/ifbank1 -e %d\n", image_size);
	
	calculate_md5sum(md5sum, sizeof(md5sum));
	
	if (read_from_logger(fd, BOOT_STR, FLASH_BOOT_TIMEOUT, NONE)) {
		reset_logger();
	}	

	if (!write_to_logger(fd, md5c_cmd)
		&& !read_from_logger(fd, md5sum, MD5SUM_TIMEOUT, NONE )) {

		print_ok();
		return 0;
	} else {
		print_fail();
		print_error_msg("Flash check failed\n");
		return -1;
	}
}

int 
flash_logger(int fd)
{
	int src_fd, dst_fd, nread, nwrite;
	unsigned char buf[4096];
	char test_msg[] = "Programming DUT -    ";
	char nucleo_path[100];
	
	snprintf(nucleo_path, sizeof(nucleo_path),
		 "%s%s", NUCLEO_PATH, "/image.bin");

	printf("\n---------------------\n");
	
	write(1, test_msg, sizeof(test_msg)); 	
	
	src_fd = open(IMAGE_PATH, O_RDONLY);		
	dst_fd = open(nucleo_path, O_CREAT | O_WRONLY);
	
	if (src_fd == -1 || dst_fd == -1) {
		print_fail();
		perror("Unable to open");
	} else {
		while (1) {
			nread = read(src_fd, buf, 4096);
		
			if (nread == -1) {
				print_fail();
				printf("Error reading file\n");
				break;
			}
			else if (nread == 0) {
				close_fds(2, src_fd, dst_fd);
				return flash_check(fd);
			}
		
			nwrite = write(dst_fd, buf, nread);	
			if (nwrite == -1) {
				print_fail();
				print_error_msg("Error writing\n");
				break;
			}
		}
	}
	print_error_msg("Programming failed\n");
	close_fds(2, src_fd, dst_fd);
	return -1;
}

int 
fs_write(int fd)
{
	char umount_mnt_cmd[] = "umount /mnt\n";
	char umount_rsvd_cmd[] = "umount /rsvd\n";
	char *fs_cmds[4] = {
		"mkfatfs /dev/mtdblock0\n",
		"mount -t vfat /dev/mtdblock0 /mnt\n",
		"mkfatfs /dev/ifbank2r\n",
		"mount -t vfat /dev/ifbank2r /rsvd\n"
	};
	
	if (write_to_logger(fd, umount_mnt_cmd)
		||write_to_logger(fd, umount_rsvd_cmd)){

		print_error_msg("File system config write failed\n");
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		if (write_to_logger(fd, fs_cmds[i])){
			print_error_msg("File system config write failed\n");
			return -1;
		}
	}
	flush(fd);
	return 0;	
}

int
mock_factory_write(int fd, char *fct_comp_str, char *apn_cmd) 
{
	char mock_fct_cmd[] = "factory -s 1801001 -r VB1.0 "
		   	      "-p DL-MINI-BAT36-D2-3G -f\n";
    
	if (write_to_logger(fd, mock_fct_cmd) 
		|| read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE) 
		|| write_to_logger(fd, apn_cmd) 
		|| write_to_logger(fd, "factory -c\n") 
		|| read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE)) {

		//printf("Probably DUT is in sleep mode\n");
		print_error_msg("Mock factory config write failed\n");
		return -1;
	}
	return 0;
}

int
setup_mysql(MYSQL *con)
{
	char server[] = DB_SERVER;
	char user[] = DB_USER;
	char pass[] = DB_PASS;
	char database[] = DATABASE;

	if (con == NULL){
		fprintf(stderr, "%s\n", mysql_error(con));
		return -1;
	}

	if (!mysql_real_connect(con, server, user, pass,
				database, 0, NULL, 0)){
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;
	}
	return 0;
}

int
manual_serial_number_insert(char *serial_number)
{
	MYSQL *con = mysql_init(NULL);
	char query[100];

	setup_mysql(con);
	do {
		if (*serial_number) {
			printf("Serial number must be less "
			       "than 10 characters long\n\n");
		}
		printf("Enter serial number: ");
		scanf("%s", serial_number);
	} while (strlen(serial_number) > 10);
	
	snprintf(query, sizeof(query),
		 "Insert into %s values (%s, CURTIME())",
		 DB_TABLE, serial_number);

	if (mysql_query(con, query)){
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;	
	}		
	
	mysql_close(con);
	return 0;
}

int
serial_number_insert(char *serial_number)
{
	MYSQL *con = mysql_init(NULL);
	char query[100];
	int id;
	
	setup_mysql(con);
	snprintf(query, sizeof(query),
		 "Insert into %s (submission_date) \
		 values (CURTIME())", DB_TABLE);

	if (mysql_query(con, query)){
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;	
	}		
	
	id = mysql_insert_id(con);
	snprintf(serial_number, sizeof(serial_number), "%d", id);

	mysql_close(con);	
	return 0;	
}

int 
factory_write(int fd, char *fct_comp_str, char *apn_cmd, char *hard_rev,
	      char *prod_num, int (*func)(char *)) 
{	
	char fct_cmd[100];
	char ser_num[10] = {0};

	printf("\n---------------------\n");

	func(ser_num);
	sprintf(fct_cmd, "factory -s %s -r %s -p %s -f\n",
		ser_num, hard_rev, prod_num);

	flush(fd);
	if (!write_to_logger(fd, fct_cmd) 
		&& !read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE) 
		&& !write_to_logger(fd, apn_cmd) 
		&& !write_to_logger(fd, "factory -c\n") 
		&& !read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE)){

		printf("Factory config successfully written\n");
		return 0;	
	}
	else {
		print_error_msg("Factory config write failed\n");
		return -1;
	}
}

int 
led_test(int fd) 
{
	char response;	
	char led_on_cmd[] = "gpio -o 1 /dev/gpout_systemstatusled\n";
	char led_off_cmd[] = "gpio -o 0 /dev/gpout_systemstatusled\n";

	printf("\n---------------------\n");
	printf("Starting system LED\n");
	
	if (write_to_logger(fd, led_on_cmd)) {
		printf("\033[0;31m");
		printf("Starting system LED failed\n");
		printf("\033[0;m");
		return -1;
	}

	printf("Does the LED work? [y/n] ");
	scanf(" %c", &response);
	printf("LED test -    ");

	(response == 'y' || response == 'Y') ? print_ok() : print_fail();

	if (write_to_logger(fd, led_off_cmd)) {	
		print_fail();
		print_error_msg("Stoping system LED failed\n");
		return -1;
	}
	flush(fd);
	return 0;
}

int 
measure_voltage(int fd, int i2c_fd) 
{
	char buf[] = "Measure";
	char response[10] = {0};
	int voltage;

	write(i2c_fd, buf, strlen(buf));
	sleep(1);	//Wait for Nucleo to make measurement
	read(i2c_fd, response, 7);
	voltage = atoi(response);

	if (voltage > 3600) {
		printf("VCCGSM value is set - %d mV\n", voltage);
		return 0;
	}
	else {
		printf("VCCGSM is NOT set - %d mV\n", voltage);
		write_to_logger(fd, "reset\n");
		return -1;
	}
}

int
gsm_test(int fd, int i2c_fd, char *apn)
{	
	int err = 0;
	char response;
	char bd_comp_str[] = "Successfully changed baud rate";
	char ping_comp_str[] = "Successfully ping host";
	char test_msg[] = "GSM test -    ";
	char error_msg[] = "ERROR UMTS:";
	char qftpc_cmd[200];
	char bd_cmd[] = "modem_config -d /dev/ttyS0 -g 1 "
       			"-ar 9600 -e 15 -b 20\n";
	char mondis_ip[] = "10.210.9.3";
	char google_ip[] = "8.8.8.8";
	char *ip = google_ip;
	
	if (!strcmp(apn, "cp-mondis")) {
		ip = mondis_ip;
	}
	snprintf(qftpc_cmd, sizeof(qftpc_cmd), 
		 "qftpc -a %s -e 15 -b 20 -i %s\n", apn, ip);

	printf("\n---------------------\n");
	printf("Setting GSM module, please wait 1 minute...\n");

	if (!write_to_logger(fd, bd_cmd)
		&& !read_from_logger(fd, bd_comp_str, UMTS_TIMEOUT, NONE)) {

		printf("Baud rate changed successfully\n");
	} else { 
		print_error_msg("Error while changing gsm baud rate\n");
		return -1;
	}

	// Check VCCGSM
	if (measure_voltage(fd, i2c_fd)) {
		return -1;	
	}

	// AT commands
	if (write_to_logger(fd, "echo \"AT&F\" > /dev/ttyS0\n")
		||write_to_logger(fd, "echo \"AT+CFUN=1,1\" > /dev/ttyS0\n")) {

		print_error_msg("Error while writing AT commands\n");
		return -1;
	}

	printf("Pinging host %s ...\n", ip);

	if (write_to_logger(fd, qftpc_cmd)) {	
		print_error_msg("Error writing qftpc command\n");

	}
	write(1, test_msg, sizeof(test_msg)); 
	
	if (!read_from_logger(fd, error_msg, UMTS_ERROR_TIMEOUT, STORE)) {
		err = 1 - err;
		
	}
	else if (!read_from_logger(fd, ping_comp_str, PING_TIMEOUT, NONE)) {
		print_ok();
		return 0;
	}	

	write_to_logger(fd, "reset\n");
	print_fail();
	flush(fd);
	
	if (err) {
		print_error_msg(error_msg);
	} else {
		printf("Retest GSM? [y/n] ");
		scanf(" %c", &response);
		if (response == 'y' || response == 'Y') {
			return gsm_test(fd, i2c_fd, apn);
		}
	}
	return -1;
}

int 
inputs_config(int fd)
{
	char new_line[] = "printf \"\\n\" >> /mnt/conf.cfg\n";
	char line[50];
	char buf[256];
	int ret;

	char *cmd[12] = {
		"lptim1_state enable",
		"lptim1_init_value -1",
		"lptim1_avr_gain 1.0",
		
		"lptim1_count_gain 1.0",
		"lptim1_log_avr enable",
		"lptim1_log_count enable",
		
		"lptim2_state enable",
		"lptim2_init_value 0",
		"lptim2_avr_gain 1.0",

		"lptim2_count_gain 1.0",
		"lptim2_log_avr enable",
		"lptim2_log_count enable"
	};


	printf("Setting the digital inputs' config, wait for 6 seconds\n");

	if (write_to_logger (fd, new_line)) {		
		print_error_msg("Inputs config write failed\n");
		return -1;
	}
	
	for ( int i = 0; i < sizeof(cmd) / sizeof(cmd[0]) ; i++ ) {
		sprintf (line, "printf \"%s\" >> /mnt/conf.cfg\n", cmd[i]);	
	       	if (write_to_logger(fd, line) 
			|| write_to_logger(fd, new_line)) {

			print_error_msg("Inputs config write failed\n");
			return -1;
		}
		//Wait 0.5s
		read_from_logger(fd, NULL, INPUTS_CONF_SLEEP, NONE);
	}
	printf("Inputs config written\n");
	return 0;
}

int 
reed_test(int fd) 
{
	char test_msg[] = "Reed test -    ";
	char alarm_str[] ="AT+QPOWD=1";
	char reed_str[] = "Reed clicked for 3s.";
	
	flush(fd);	
	printf("\n---------------------\n");
	printf("Wait for DUT to enter sleep mode...\n");

	if (!read_from_logger(fd, alarm_str, SLEEP_TIMEOUT, NONE)) {
		printf("Turning magnet on\n");	
		write(1, test_msg, sizeof(test_msg));
		digitalWrite(REED, HIGH);
		
		if (!read_from_logger(fd, reed_str, EM_TIMEOUT, NONE)) {
			digitalWrite(REED, LOW);
			print_ok();
			return write_to_logger(fd, "reset\n");
		} else { 
			digitalWrite(REED, LOW);
			print_fail();

			printf("Activate reed ampule by hand\n");
			if (!read_from_logger(fd, reed_str,
					      REED_CHECK_TIMEOUT, NONE)) {
				write(1, test_msg, sizeof(test_msg));
				print_ok();
				return	write_to_logger(fd, "reset\n");
			}
		}
	}  
	write(1, test_msg, sizeof(test_msg));
	print_fail();
	write_to_logger(fd, "reset\n");
	return -1;	 
}

int 
generate_pulses(int fd, int i2c_fd) 
{
	char buf[] = "Generate";
	char alarm_str[] = "Alarm Ampule-Reed received.";
	char test_msg[] = "Inputs test -    ";

	printf("Starting alarm\n");
	if (write_to_logger(fd, "alarm -f\n")) {
		print_error_msg("Alarm write failed\n");
		return -1;
	}

	write(1, test_msg, sizeof(test_msg));

	if (!read_from_logger(fd, alarm_str, ALARM_TIMEOUT, NONE)) {
		write(i2c_fd, buf, strlen(buf));
		sleep(1); // Wait for Nucleo to generate pulses	

		if (!write_to_logger(fd, "cat /dev/lptim1\n") 
			&& !read_from_logger(fd, LPTIM1_PULSES, 
					     PULSE_TIMEOUT, NONE) 
			&& !write_to_logger(fd, "cat /dev/lptim2\n") 
			&& !read_from_logger(fd, LPTIM2_PULSES,
			       		     PULSE_TIMEOUT, NONE)) {

			print_ok();
			return 0;				
		} else {
			print_fail();
			return -1;
		}		
	}
	print_fail();
	return -1;			
}


int 
inputs_test(int fd, int i2c_fd) 
{
	printf("\n---------------------\n");
	return (inputs_config(fd) || generate_pulses(fd, i2c_fd) 
		|| reed_test(fd));
}

int 
soft_ver_check(int fd)
{
	write_to_logger(fd, "uname -a\n");
	if (read_from_logger(fd, SOFT_VER, 0.5, NONE)){
		printf("\nSoftware version doesn't match, "
		       "must be %s\n", SOFT_VER);
	}
}

void 
print_ok()
{
	printf("\033[0;32m");
	printf("OK\n");
	printf("\033[0m");
}

void 
print_fail() 
{
	print_error_msg("FAIL\n");
}

void
print_error_msg(char *err_msg)
{
	printf("\033[0;31m");
	printf("%s", err_msg);
	printf("\033[0m");
}

void 
power_off(int disch_time)
{
	digitalWrite(PWR, LOW);
	digitalWrite(JMP, LOW);
	digitalWrite(REED, LOW);
	if (disch_time > 0){
		digitalWrite(CAP, HIGH);
		printf("\nWait %d seconds for cap to discharge\n", disch_time);
		sleep(disch_time);
	}
	digitalWrite(CAP, LOW);
}

void
close_fds(int fd_count, ...){
	va_list ap;

	va_start(ap, fd_count);
	for (int i = 0; i < fd_count; i++){
		close(va_arg(ap, int));
	}
	va_end(ap);
}


double 
calculate_time(time_t *start)
{
	time_t end;
	time(&end);
	return difftime(end, *start);
}

void 
reset_logger()
{
	digitalWrite(PWR, LOW);
	sleep(1);
	digitalWrite(PWR, HIGH);
	sleep(2);
}


void 
reset_nucleo(int sleeptime)
{
	pinMode(NPWR, OUTPUT);
	digitalWrite(NPWR, LOW);
	sleep(sleeptime);
	digitalWrite(NPWR, HIGH);
	pinMode(PUPIN, INPUT);
	pullUpDnControl(PUPIN, PUD_UP);
	do {
		;
	} while (open(NUCLEO_PATH, O_RDONLY) < 0);
}

void 
end_test(time_t *start)
{	
	double test_time = calculate_time(start);
	int seconds = (int) test_time % 60;
	int minutes = (test_time - seconds) / 60;

	printf("\n---------------------\n");
	printf("\033[1;32m");
	printf("Test ended successfully!\n");
	printf("\033[0m");
	printf("Total test time = %d.%.2dm\n", minutes, seconds);
}
