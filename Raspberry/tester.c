#define _GNU_SOURCE
#include <sys/mman.h>
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
	struct logger logger;
	char apn_cmd[200] = {0};
	char fct_str[] = "written successfully";	
	int fd, i2c_fd, opt, t_flags = 0, fail_flag = 1;
	time_t start;
	int manual_flag = 0;
	int (*module_test) (int fd, int i2c_fd, struct logger *logger);

	// Asign default values and clear variables without such in case of
	// empty column database insertion
	logger.apn = DEF_APN;
	logger.hard_rev = DEF_HARD_REV;
	logger.prod_num = DEF_PROD_NUM;
	logger.imsi[0] = '\0';
	logger.ccid[0] = '\0';
	logger.module_rev[0] ='\0';
	logger.module = GSM;
	module_test = gsm_test;
	
	// Check for input parameters
	while ((opt = getopt(argc, argv, "a:h:p:smiflrdenc:")) != -1) {
		switch (opt) {
		case 'a':
			logger.apn = optarg;
			break;
		case 'h':
			logger.hard_rev = optarg;
			break;	
		case 'p':
			logger.prod_num = optarg;
			break;
		case 's':
			manual_flag = 1;
			break;
		case 'm':
			t_flags |= T_MODULE;
			break;
		case 'i':
			t_flags |= T_INPUTS;
			break;
		case 'f':
			t_flags |= T_FLASH;
			break;
		case 'l':
			t_flags |= T_LED;
			break;
		case 'r':
			t_flags |= T_REED;
			break;
		case 'd':
			t_flags |= T_FACTORY;
			break;
		case 'e':
			fail_flag = 0;
			break;
		case 'n':
			logger.apn = "iot-test";
			logger.module = NBIOT;
			module_test = nbiot_test;
			break;
		case 'c':
			wiringPiSetupGpio();
			pinMode(CAP, OUTPUT);
			power_off(atoi(optarg));
			return 0;	
		default:
			fprintf(stderr, "Usage:\t%s [-a apn] "
				"[-h hardware revision] [-p product number]\n\t"
				"OPTIONS: [-a] [-h] [-p] [-s] [-f] [-l] [-m] "
			       	"[-n] [-i] [-r] [-c] [-d]\n\t[-e]\n"
				"Where:\t-a apn: Sets the APN\n\t"
				"-h hr: Sets the hardware revision\n\t"
				"-p pn: Sets the product number\n\t"
				"-s: Manually insert serial number\n"
				"\t-f: Flash DUT\n\t-l: Test LED\n\t"
				"-m: Test communication module\n\t"
				"-n: Test for NB-IoT module\n\t"
				"-i: Test lptim inputs\n\t" 
				"-r: Test reed ampule\n\t"
				"-d: Write serial number and module "
				"parameters to database \n\t"
				"-c discharge time: Discharge capacitor\n\t"
			       	"-e: Execute test without interruption "
			        "in case of failure\n", argv[0]);
			return -1;
		}
	}

	if (t_flags == 0) {
		t_flags = 255;
	}
	if (module_test == gsm_test) {
		snprintf(apn_cmd, sizeof(apn_cmd), 
			 "printf \"umts_apn %s\" >> /rsvd/default.cfg\n"
			 "printf \"\\n\" >> rsvd/default.cfg\n", logger.apn);
	}

	power_devices();
	if (setup_devices(&fd, &i2c_fd)) {
		power_off(0);
		return -1;
	}
	setup_termios(fd); // Set options for serial port
	if (check_serial_number(fd)) {
		read_from_logger(fd, NULL, 0, CLOSE);
		close_fds(2, fd, i2c_fd);
		power_off(0);
		return -1;
	}

	begin_test(&start);	

	// Test conditions
	if ((t_flags & T_FLASH && (flash_logger(fd) ||  
	     mount_fs(fd) || 	// File system mount 
	     mock_factory_write(fd, fct_str, apn_cmd) ))) {
		
		if (fail_flag) {
			end_test(fd, i2c_fd, NO_DISCH, FAILURE, &start);
			return -1;
		}
	}
	if (t_flags & T_LED && led_test(fd) ||
	   (t_flags & T_MODULE && module_test(fd, i2c_fd, &logger ))) {
		
		if (fail_flag) {
			end_test(fd, i2c_fd, CAP_DISCH_TIME, FAILURE, &start);
			return -1;
		}	
	}

	if ((t_flags & T_INPUTS && inputs_test(fd, i2c_fd))) {
		if (fail_flag) {
			end_test(fd, i2c_fd, CAP_DISCH_TIME, FAILURE, &start);
			return -1;
		}
	}

	if ((t_flags & T_REED)) {
		if ((t_flags & T_INPUTS) == 0){
			write_to_logger(fd, "alarm -f\n");
		}
		if (reed_test(fd, logger.module)) {
			if (fail_flag) {
				end_test(fd, i2c_fd, CAP_DISCH_TIME,
					 FAILURE, &start);
				return -1;
			}
		}
	}

	//Don't execute factory_write in case fail_flag or t_flag is unset
	if (fail_flag && (t_flags & T_FACTORY &&
	    factory_write(fd, fct_str, apn_cmd, &logger,
			   manual_flag))) {
			
		end_test(fd, i2c_fd, CAP_DISCH_TIME, FAILURE, &start);
		return -1;
	}
		
	end_test(fd, i2c_fd, CAP_DISCH_TIME, SUCCESS, &start);
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

	// Connect to Nucleo via I2C	
	if ((*i2c_fd = open(I2C_PORT, O_RDWR)) < 0) {
		perror("Unable to open the I2C bus");
		close(*fd);
		return -1;
	}

	// Set Nucleo I2C address
	if (ioctl(*i2c_fd, I2C_SLAVE, I2C_ADDRESS)) {
		perror("Unable to acquire bus access and/or talk to Nucleo");
		close_fds(2, *fd, *i2c_fd);
		return -1;
	}

	// Open log file
	read_from_logger(*fd, NULL, 0, OPEN);
	
	return 0;
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

void
parse_number(char *line, char *number)
{
	int i, k;
		
	for (i = 0, k = 0; i < strlen(line); i++) {
		if (isdigit(line[i])) {
			number[k++] = line[i];
		}
	}
	number[k] = '\0';
}

int
check_serial_number(int fd)
{
	char serial_cmd[] = "cat /rsvd/factory.txt\n";
	char serial_line[20] = "serial";
	char serial_number[10];
	int c;

	if (!read_from_logger(fd, BOOT_STR, BOOT_TIMEOUT, NONE) &&
	    !write_to_logger(fd, serial_cmd) &&
	    !read_from_logger(fd, serial_line, DEF_TIMEOUT, STORE)) {

		parse_number(serial_line, serial_number);
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

int 
write_to_logger(int fd, char *str)
{
	int str_size = strlen(str);
	int retval = write(fd, str, str_size);

	if (retval == str_size) {
		return 0;
	} else { 
		perror("Write failed - ");
		return -1;
	}
}

int
read_from_logger(int fd, char *comp_str, float timeout, int flags)
{
	int retval;
	int rx_length;
	char buf[256];
	static FILE *log_fp = NULL;

	fd_set rfds;
	struct timeval tv;
	
	FD_ZERO(&rfds);		// Clears the file descriptor set
	FD_SET(fd, &rfds);	// Add fd to the set

	if (flags & OPEN) {
		if ((log_fp = fopen(LOG_PATH, "wat")) == NULL) {
			perror("Unable to open log file");
			return -1;	
		}
	}
	else if (flags & CLOSE) {
		fclose(log_fp);
		log_fp = NULL;
	}
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
			
			if (log_fp != NULL) {
				fprintf(log_fp, "%s", buf);
			}		
			if (flags & PRINT) { 
				printf("%s", buf);
			}
			
			if (flags & LINE) {
				strcpy(comp_str, buf);
				return 0;
			}

			if (comp_str != NULL) {	
				char *pch = strstr(buf,comp_str);
				if (pch) {
					if (flags & STORE) {
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
begin_test(time_t *start)
{
	printf("=============");
	printf("\033[1;32m");
	printf("TEST BEGIN");
	printf("\033[0m");
	printf("=============\n\n");
	time(start);
}

void 
get_md5sum(char *md5sum, size_t size)
{
	char md5sum_cmd[100];
	int i, ch;
	
	snprintf(md5sum_cmd, sizeof(md5sum_cmd), "md5sum %s", IMAGE_PATH);
	FILE *p = popen(md5sum_cmd, "r");
	for (i = 0; i < size - 1 && isxdigit(ch = fgetc(p)); i++) {
		*md5sum++ = ch;
	}

	*md5sum = '\0';
	pclose(p);
}

size_t
get_file_size(const char *filename)
{
	struct stat st;

	stat(filename, &st);
	return st.st_size;
}

int 
flash_check(int fd)
{
	size_t image_size;
	char md5c_cmd[100];
	char md5sum[MD5SUM_HASH_SIZE];
	
	flush(fd);

	image_size = get_file_size(IMAGE_PATH);
	snprintf(md5c_cmd, sizeof(md5c_cmd), 
		 "md5c -f /dev/ifbank1 -e %d\n", image_size);	
	get_md5sum(md5sum, sizeof(md5sum));

	if (read_from_logger(fd, BOOT_STR, FLASH_BOOT_TIMEOUT, NONE)) {
		reset_logger();
	}	

	if (!write_to_logger(fd, md5c_cmd) &&
	    !read_from_logger(fd, md5sum, MD5SUM_TIMEOUT, NONE )) {

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
mount_fs(int fd)
{
	char umount_mnt_cmd[] = "umount /mnt\n";
	char umount_rsvd_cmd[] = "umount /rsvd\n";
	char *fs_cmds[4] = {
		"mkfatfs /dev/mtdblock0\n",
		"mount -t vfat /dev/mtdblock0 /mnt\n",
		"mkfatfs /dev/ifbank2r\n",
		"mount -t vfat /dev/ifbank2r /rsvd\n"
	};
	
	if (write_to_logger(fd, umount_mnt_cmd) ||
	    write_to_logger(fd, umount_rsvd_cmd)) {

		print_error_msg("File system config write failed\n");
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		if (write_to_logger(fd, fs_cmds[i])) {
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
    
	if (write_to_logger(fd, mock_fct_cmd) || 
	    read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE) ||
	    write_to_logger(fd, apn_cmd) ||
	    write_to_logger(fd, "factory -c\n") || 
	    read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE)) {

		//printf("Probably DUT is in sleep mode\n");
		print_error_msg("Mock factory config write failed\n");
		return -1;
	}
	return 0;
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

// Get SIM params using cu and AT commands
int
AT_sim(int fd, struct logger *logger)
{
	char imsi_cmd[] = "AT+CIMI\n";
	char ccid_cmd[] = "AT+CCID\n";
	char rev_cmd[] = "ATI\n";
	char ccid_str[30] = "CCID:";
	char imsi_str[30] = {0};
	char rev_str[30] = "Revision";

	if (write_to_logger(fd, "cu\n") ||
	    write_to_logger(fd, ccid_cmd) || 
	    read_from_logger(fd, ccid_str, DEF_TIMEOUT, STORE) ||
	    read_from_logger(fd, "OK", DEF_TIMEOUT, NONE) ||
	    write_to_logger(fd, imsi_cmd) ||
	    read_from_logger(fd, imsi_str, DEF_TIMEOUT, LINE) ||
	    read_from_logger(fd, imsi_str, DEF_TIMEOUT, LINE)) {
	   
		print_error_msg("Failed reading IMSI and CCID number\n");	
	} else {	
		parse_number(imsi_str, logger->imsi);
		parse_number(ccid_str, logger->ccid);
	}

	if (write_to_logger(fd, rev_cmd) ||
	    read_from_logger(fd, rev_str, DEF_TIMEOUT, STORE)) {

		print_error_msg("Failed reading module revision\n");
	} else {
		char *pch = strstr(rev_str, ":");
		int i;	
		
		*pch++;
		for (i = 0; *pch != '\r'; i++) {
			logger->module_rev[i] = *pch++;
		}
		logger->module_rev[i] = '\0';
	}
}

// Get SIM params using qftpc command
void
qftpc_sim(int fd, struct logger *logger)
{
	char imsi_str[20] = "IMSI";
	char ccid_str[30] = "CCID:";
	char qftpc_gsm[] = "qftpc -j -e 0 -b 0\n";
	char qftpc_nbiot[] = "qftpc -j -e 5 -b 10\n";
	char *qftpc_cmd = qftpc_gsm;
	char rev_str[30] = "Revision";
	char mod_rev_cmd[100];
	int timeout = QFTPC_TIMEOUT;
	
	// If module is NB-IoT, use NB-IoT timeout and command
	if (logger->module & NBIOT) {
		qftpc_cmd = qftpc_nbiot;
		timeout = NBIOT_TIMEOUT;
	}
	
	if (write_to_logger(fd, qftpc_cmd) ||
    	    read_from_logger(fd, imsi_str, timeout, STORE) ||
	    read_from_logger(fd, ccid_str, QFTPC_TIMEOUT, STORE)) {
	
		print_error_msg("Failed reading IMSI and CCID number\n");	
		
	} else {
		parse_number(imsi_str, logger->imsi);
		parse_number(ccid_str, logger->ccid);
	}

	snprintf(mod_rev_cmd, sizeof(mod_rev_cmd), "qftpc -v -e 0 -b 0\n"); 
	
	if (write_to_logger(fd, mod_rev_cmd) ||
	    read_from_logger(fd, rev_str, QFTPC_TIMEOUT, STORE)) {

		print_error_msg("Failed reading module revision\n");
	} else {
		char *pch = strstr(rev_str, ":");
		int i;	

		*pch++;
		for (i = 0; *pch != '\r'; i++) {
			logger->module_rev[i] = *pch++;
		}
		logger->module_rev[i] = '\0';
	}
}

void
get_sim_info(int fd, struct logger *logger)
{	
	char soft_ver[10];

	/* If software version is 0.4.27 use cu and AT commands to get SIM
	 * parameters. Thereafter discharge cap, reset device to exit from
	 * cu. 
	*/
	read_soft_ver(fd, soft_ver);
	if ((logger->module & GSM) && !strcmp("0.4.27", soft_ver)) {
		AT_sim(fd, logger);
		digitalWrite(PWR, LOW);
		discharge_cap(10);
		reset_logger();
	} else {
		qftpc_sim(fd,logger);
	}
}

int
gsm_test(int fd, int i2c_fd, struct logger *logger)
{	
	int err = 0;
	char response;
	char umts_boot_str[] = "WARNING: Pins are set!";
	char bd_comp_str[] = "Successfully changed baud rate";
	char ping_comp_str[] = "Successfully ping host";
	char test_msg[] = "Module test -    ";
	char error_msg[] = "ERROR UMTS:";
	char qftpc_cmd[200];
	char bd_cmd[] = "modem_config -d /dev/ttyS0 -g 1 "
			"-ar 9600 -e 10 -b 20\n";
	char mondis_ip[] = "10.210.9.3";
	char google_ip[] = "8.8.8.8";
	char *ip = google_ip;
	
	if (!strcmp(logger->apn, "cp-mondis")) {
		ip = mondis_ip;
	}

	// Compose qftpc command for setting APN and ping
	snprintf(qftpc_cmd, sizeof(qftpc_cmd), 
		 "qftpc -a %s -e 0 -b 20 -i %s\n", logger->apn, ip);

	printf("\n---------------------\n");
	printf("Setting GSM module, please wait 1 minute...\n");

	// Boot module, change baud rate and set modem G mode
	if (!write_to_logger(fd, bd_cmd) &&
	    !read_from_logger(fd, bd_comp_str, UMTS_BAUD_TIMEOUT, NONE) &&
	    !read_from_logger(fd, umts_boot_str, UMTS_BOOT_TIMEOUT, NONE)) {

		printf("Baud rate changed successfully\n");
	} else { 
		print_error_msg("Error while changing gsm baud rate\n");
		return -1;
	}

	// Check VCCGSM
	if (measure_voltage(fd, i2c_fd)) {
		return -1;	
	}
	
	// Get SIM card parameters - IMSI, CCID - and the module revision
	get_sim_info(fd, logger);

	// AT commands
	if (write_to_logger(fd, "echo \"AT&F\" > /dev/ttyS0\n") ||
	    write_to_logger(fd, "echo \"AT+CFUN=1,1\" > /dev/ttyS0\n")) {

		print_error_msg("Error writing AT commands\n");
		return -1;
	}
	printf("Pinging host %s ...\n", ip);	
	
	// Set apn and ping 
	if (write_to_logger(fd, qftpc_cmd)) {	
		print_error_msg("Error writing qftpc ping command\n");
		return -1;
	}
	write(1, test_msg, sizeof(test_msg)); 	
//	if (!read_from_logger(fd, error_msg, UMTS_ERROR_TIMEOUT, STORE | PRINT)) {
//		err = 1 - err;	
//	}
//	else 
	if (!read_from_logger(fd, ping_comp_str, PING_TIMEOUT, NONE)) {
		print_ok();
		return 0;
	}
	
	write_to_logger(fd, "\nreset\n");
	print_fail();
	flush(fd);

	// Retest if UMTS error occurred
	if (err) {
		print_error_msg(error_msg);
	} else {
		printf("Retest GSM? [y/n] ");
		scanf(" %c", &response);
		if (response == 'y' || response == 'Y') {
			return gsm_test(fd, i2c_fd, logger);
		}
	}
	return -1;
}

int
nbiot_test(int fd, int i2c_fd, struct logger *logger)
{	
	char ping_comp_str[] = "Successfully ping host";
	char autocon_cmp_str[] = "AUTOCONNECT,FALSE";
	char test_msg[] = "Module test -    ";
	char autocon_cmd[] = "qftpc -e 0 -b 0 -k AUTOCONNECT FALSE\n";
	char ping_cmd[200];
	char ip[] = "8.8.8.8";
	
	snprintf(ping_cmd, sizeof(ping_cmd), 
		 "qftpc -a %s -e 5 -b 10 -i %s\n", logger->apn, ip);

	printf("\n---------------------\n");
	printf("Setting NB-IoT module, please wait %d seconds...\n",
		NBIOT_TIMEOUT);

	//Get SIM card parameters - IMSI, CCID - and the module revision
	get_sim_info(fd, logger);
	
	// Check VCCGSM
	if (measure_voltage(fd, i2c_fd)) {
		return -1;	
	}
	
	//Disable autoconnect option
	if (write_to_logger(fd, autocon_cmd) ||
	    read_from_logger(fd, autocon_cmp_str, AUTOCON_TIMEOUT, NONE)) {

		write_to_logger(fd, "reset\n");
		print_error_msg("Error while removing autoconnect\n");
		return -1;
	}	
	printf("Pinging host %s ...\n", ip);
	
	// Set apn and ping 
	if (write_to_logger(fd, ping_cmd)) {	
		print_error_msg("Error writing qftpc ping command\n");
		return -1;
	}
	write(1, test_msg, sizeof(test_msg)); 	
	
	if (!read_from_logger(fd, ping_comp_str, PING_TIMEOUT, NONE)) {
		print_ok();
		return 0;
	} else {
		write_to_logger(fd, "reset\n");
		print_fail();
		flush(fd);
		return -1;
	}
}

int 
inputs_config(int fd)
{
	char new_line[] = "printf \"\\n\" >> /mnt/conf.cfg\n";
	char line[50];
	char *cfg[12] = {
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
	for (int i = 0; i < sizeof(cfg) / sizeof(cfg[0]) ; i++) {
		sprintf(line, "printf \"%s\" >> /mnt/conf.cfg\n", cfg[i]);	
		if (write_to_logger(fd, line) ||
		    write_to_logger(fd, new_line)) {

			print_error_msg("Inputs config write failed\n");
			return -1;
		}
		// Wait 0.5s
		read_from_logger(fd, NULL, INPUTS_CONF_SLEEP, NONE);
	}
	printf("Inputs config written\n");
	return 0;
}

int 
generate_pulses(int fd, int i2c_fd) 
{
	char buf[] = "Generate";
	char alarm_str[] = "Alarm Ampule-Reed received.";
	char test_msg[] = "Inputs test -    ";

	printf("Starting alarm\n");
	if (write_to_logger(fd, "alarm -f\n") ||
	    read_from_logger(fd, alarm_str, ALARM_TIMEOUT, NONE)) {    
		print_error_msg("Error while starting alarm\n");
		return -1;
	}

	write(1, test_msg, sizeof(test_msg));	
	// Tell Nucleo to generate pulses
	write(i2c_fd, buf, strlen(buf));
	sleep(1); // Wait for Nucleo to generate pulses	

	if (!write_to_logger(fd, "cat /dev/lptim1\n") &&
	    !read_from_logger(fd, LPTIM1_PULSES, PULSE_TIMEOUT, NONE) && 
	    !write_to_logger(fd, "cat /dev/lptim2\n") &&
	    !read_from_logger(fd, LPTIM2_PULSES, PULSE_TIMEOUT, NONE)) {

		print_ok();
		return 0;				
	} else {
		print_fail();
		return -1;
	}		
}

int 
inputs_test(int fd, int i2c_fd) 
{
	printf("\n---------------------\n");
	return (inputs_config(fd) || generate_pulses(fd, i2c_fd));
}

int 
reed_test(int fd, int module) 
{
	char test_msg[] = "Reed test -    ";
	char sleep_str[] ="AT+QPOWD=1"; 	//Comprase string for GSM
	char reed_str[] = "Reed clicked for 3s.";

	if (module  == NBIOT) {
		strcpy(sleep_str, "Send time");
	}	

	flush(fd);	
	printf("\n---------------------\n");
	printf("Wait for DUT to enter sleep mode...\n");

	if (!read_from_logger(fd, sleep_str, SLEEP_TIMEOUT, NONE)) {
		printf("Turning magnet on\n");	
		write(1, test_msg, sizeof(test_msg));
		digitalWrite(REED, HIGH);
		
		if (!read_from_logger(fd, reed_str, EM_TIMEOUT, NONE)) {
			digitalWrite(REED, LOW);
			print_ok();
			write_to_logger(fd, "reset\n");
			return read_from_logger(fd, BOOT_STR,
						BOOT_TIMEOUT, NONE);
		} else { 
			digitalWrite(REED, LOW);
			print_fail();

			printf("Activate reed ampule by hand\n");
			if (!read_from_logger(fd, reed_str,
					      REED_CHECK_TIMEOUT, NONE)) {
				write(1, test_msg, sizeof(test_msg));
				print_ok();
				write_to_logger(fd, "reset\n");
				return read_from_logger(fd, BOOT_STR,
							BOOT_TIMEOUT, NONE);
			}
		}
	}  
	write(1, test_msg, sizeof(test_msg));
	print_fail();
	write_to_logger(fd, "reset\n");
	return -1;	 
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
				database, 0, NULL, 0)) {
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;
	}
	return 0;
}

void
insert_serial_number(char *serial_number)
{
	char input[255] = {0};

	do {
		if (*input) {
			printf("Serial number must be 10 or less "
			       "characters long\n\n");
		}
		printf("Enter serial number: ");
		scanf("%s", input);
	} while (strlen(input) > 10);
	strcpy(serial_number, input);
}

int 
database_insert(struct logger *logger)
{
	MYSQL *con = mysql_init(NULL);
	char query[255];
	unsigned long id;

	setup_mysql(con);
	snprintf(query, sizeof(query),
		 "INSERT INTO %s values('%s', '%s', '%s', '%s', '%s', '%s', "
		 "'%s', UNIX_TIMESTAMP())", DB_TABLE, logger->ser_num,  
		 logger->hard_rev,logger-> prod_num, logger->soft_ver,
		 logger->imsi, logger->ccid, logger->module_rev);
	if (mysql_query(con, query)) {
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;	
	}		
	
	id = mysql_insert_id(con);
	snprintf(logger->ser_num, 11, "%lu", id);

	mysql_close(con);	
	return 0;	
}

int 
factory_write(int fd, char *fct_comp_str, char *apn_cmd, struct logger *logger,
	      int manual_flag) 
{	
	char fct_cmd[100];

	printf("\n---------------------\n");

	//Get serial version
	if (manual_flag){
		insert_serial_number(logger->ser_num);
	}
	//Get software version
	if (check_soft_ver(fd, logger->soft_ver)) {
		return -1;
	}
	//Insert into database
	if (database_insert(logger)) {
		print_error_msg("Database write failed\n");
		return -1;
	}

	sprintf(fct_cmd, "factory -s %s -r %s -p %s -f\n",
		logger->ser_num, logger->hard_rev, logger->prod_num);
	flush(fd);

	if (!write_to_logger(fd, fct_cmd) && 
	    !read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE) &&
	    !write_to_logger(fd, apn_cmd) &&
	    !write_to_logger(fd, "factory -c\n") &&
	    !read_from_logger(fd, fct_comp_str, FACTORY_TIMEOUT, NONE)) {

		printf("Factory config successfully written\n");
		printf("Inserted serial number is %s\n", logger->ser_num);
		sleep(1);
		return 0;	
	}
	else {
		print_error_msg("Factory config write failed\n");
		return -1;
	}
}

int
read_soft_ver(int fd, char *soft_ver)
{
	char uname_str[50] = "NuttX/Smartcom";
	
	if (write_to_logger(fd, "uname -a\n") ||
	    read_from_logger(fd, uname_str, DEF_TIMEOUT, STORE)) {
		print_error_msg("Error while reading software version\n");
		return -1;
	}
	
	sscanf(uname_str, "%*s %s", soft_ver);
	return 0;
}

int 
check_soft_ver(int fd, char *soft_ver)
{
	char uname_str[50] = "NuttX/Smartcom";
	char *pch;

	if (read_soft_ver(fd, soft_ver)) {
		return -1;
	}

	pch = strstr(IMAGE_PATH, soft_ver);
	if (pch == NULL){
		printf("\nSoftware version and image name don't match, "
		       "software version is %s\n", soft_ver);
	}

	flush(fd);
	return 0;
}

int
read_UID(int fd, char *UID)
{
	char UID_str[50] = "UID";
	char *pch;
	int i;

	if (write_to_logger(fd, "reset\n") ||
	    read_from_logger(fd, UID_str, RESET_TIMEOUT, STORE)) {

			print_error_msg("Failed reading UID\n");
			return -1;
	}
		
	pch = strstr(UID_str, " ");
	*pch++;
	for (i = 0; *pch != '\r' ; i++) {
		UID[i] = *pch++;
	}	
	UID[i] = '\0';
	return 0;
}

void
log_into_db(int fd)
{	
	int log_fd, i;
	unsigned long uid_length;
	unsigned long log_length;
	size_t filesize;
	char *log, *statement, UID[50];	
	MYSQL *con;
	MYSQL_STMT *stmt;
	MYSQL_BIND bind[2];

	filesize = get_file_size(LOG_PATH);

	if ((log_fd = open(LOG_PATH, O_RDONLY)) == -1) {
		perror("Unable to open log file");
		return;
	}
	log = (char *)mmap(NULL, filesize, PROT_READ, MAP_SHARED, log_fd, 0);
	if (log == MAP_FAILED) {
		perror("Unable to map log file");
		close(log_fd);
		return;
	}
	
	if (read_UID(fd, UID)) {
		close(log_fd);
		return;	
	}

	con = mysql_init(NULL);
	setup_mysql(con);
	stmt = mysql_stmt_init(con);
	memset(bind, 0, sizeof(bind));

	uid_length = strlen(UID);
	log_length = strlen(log);
	statement = "INSERT INTO `failed_devices` (`UID`,`log`," 
		    "`submission_date`) values(?,?,UNIX_TIMESTAMP())";	
	mysql_stmt_prepare(stmt, statement, strlen(statement));

	bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = UID;
	bind[0].buffer_length = sizeof(UID);
        bind[0].is_null = 0;
        bind[0].length = &uid_length;

        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = log;
        bind[1].buffer_length = sizeof(log);
        bind[1].is_null = 0;
        bind[1].length = &log_length;

	mysql_stmt_bind_param(stmt, bind);
	if (mysql_stmt_execute(stmt)) {
		fprintf(stderr, "%s\n", mysql_error(con));
	}

	int unmap=munmap(log, filesize);
	if (unmap == -1) {
		perror("Unable to munmap");
	}

	mysql_stmt_close(stmt);
	mysql_close(con);
	close(log_fd);
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
discharge_cap(int disch_time)
{
	digitalWrite(CAP, HIGH);
	sleep(disch_time);
	digitalWrite(CAP, LOW);
}

void 
power_off(int disch_time)
{
	digitalWrite(PWR, LOW);
	digitalWrite(JMP, LOW);
	digitalWrite(REED, LOW);
	if (disch_time > 0){
		printf("\nWait %d seconds for cap to discharge\n", disch_time);
		discharge_cap(disch_time);
	}
}

void
close_fds(int fd_count, ...)
{
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
end_test(int fd, int i2c_fd, int cap_time, int test_result, time_t *start)
{
	read_from_logger(fd, NULL, 0, CLOSE); 
	
	if (!test_result) {
		print_error_msg("Test failed\n");
		log_into_db(fd);
		close_fds(2, fd, i2c_fd);
		power_off(cap_time);
		return;
	}
	
	close_fds(2, fd, i2c_fd);
	power_off(cap_time);
	double test_time = calculate_time(start);
	int seconds = (int)test_time % 60;
	int minutes = (test_time - seconds) / 60;

	printf("\n---------------------\n");
	printf("\033[1;32m");
	printf("Test ended successfully!\n");
	printf("\033[0m");
	printf("Total test time = %d.%.2dm\n", minutes, seconds);
}
