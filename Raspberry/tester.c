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
	/* Assign default values and clear variables without such, in case of
	 * empty column database insertion. 
	 */
	struct logger_s logger = {
		.module = GSM,
		.apn = DEF_APN,
		.hard_rev = DEF_HARD_REV,
		.soft_ver = {0},
		.prod_num = DEF_PROD_NUM,
		.ser_num = {0},
		.imsi = {0},
		.ccid = {0},
		.module_rev = {0}
	};
	
	/* Comparison string for factory command */
	char fct_str[] = "written successfully";	
	char apn_cmd[200] = {0};
	int fd, i2c_fd, opt, t_flags = 0, fail_flag = 1;
	time_t start;
	int manual_serial_flag = 0;
	int (*module_test) (int fd, int i2c_fd, struct logger_s *logger);
	module_test = gsm_test;

	/* Check for input parameters */
	while ((opt = getopt(argc, argv, "a:h:p:smiflrdenc:")) != -1) {
		switch (opt) {
		case 'a':
			strcpy(logger.apn, optarg);
			break;
		case 'h':
			strcpy(logger.hard_rev, optarg);
			break;	
		case 'p':
			strcpy(logger.prod_num, optarg);
			break;
		case 's':
			manual_serial_flag = 1;
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
		case 'e': /* Faultless mode */
			fail_flag = 0;
			break;
		case 'n': /* NB-IoT */
			strcpy(logger.apn, "iot-test");
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

	setup_termios(fd); /* Set options for serial port */
	if (check_serial_number(fd)) {
		read_from_logger(fd, NULL, 0, CLOSE);
		close_fds(2, fd, i2c_fd);
		power_off(0);
		return -1;
	}

	begin_test(&start);	

	/* Test conditions */
	if ((t_flags & T_FLASH && (flash_logger(fd) ||  
	     mount_fs(fd) || 	/* File system mount */ 
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
		/* If inputs test isn't executed alarm is not set */
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

	/* Don't execute factory_write in faultless mode */
	if (fail_flag && (t_flags & T_FACTORY &&
	    factory_write(fd, fct_str, apn_cmd, &logger,
			   manual_serial_flag))) {
			
		end_test(fd, i2c_fd, CAP_DISCH_TIME, FAILURE, &start);
		return -1;
	}
		
	end_test(fd, i2c_fd, CAP_DISCH_TIME, SUCCESS, &start);
	return 0;
}

/***************************************************************************
 * Name: power_devices
 * 
 * Description:
 *   Power DUT and Nucleo. 
 ***************************************************************************/ 
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

	/* Check if Nucleo file system appears, i.e., if Nucleo is powered */
	if (open(NUCLEO_PATH, O_RDONLY) < 0) {
		reset_nucleo(1);
	}

	/* Check for FAIL.TXT */
	if (open(fail_path, O_RDONLY) > 0) {
		reset_nucleo(1);
	}

	/* Reset if Nucleo free space is insufficient */
	if (get_available_space(NUCLEO_PATH) < IMAGE_BLOCKS){
		reset_nucleo(1);
	}
}

/***************************************************************************
 * Name: get_available_space
 *
 * Description:
 *   Get the number of free blocks of a mounted file system.
 ***************************************************************************/ 
int 
get_available_space(char *path)
{
	struct statvfs statfs;

	statvfs(path, &statfs);
	return statfs.f_bfree;
}

/***************************************************************************
 * Name: setup_devices
 *
 * Description:
 *   Get UART, I2C and log file descriptors.
 ***************************************************************************/ 
int 
setup_devices(int *fd, int *i2c_fd)
{
	/* Open serial port */
	if ((*fd = open(UART_PORT, O_RDWR | O_NOCTTY)) == -1) { 
		perror("Unable to open serial port");
		return -1;
	}

	/* Connect to Nucleo via I2C */
	if ((*i2c_fd = open(I2C_PORT, O_RDWR)) < 0) {
		perror("Unable to open the I2C bus");
		close(*fd);
		return -1;
	}

	/* Set Nucleo I2C address */
	if (ioctl(*i2c_fd, I2C_SLAVE, I2C_ADDRESS)) {
		perror("Unable to acquire bus access and/or talk to Nucleo");
		close_fds(2, *fd, *i2c_fd);
		return -1;
	}

	/* Open log file */
	read_from_logger(*fd, NULL, 0, OPEN);
	
	return 0;
}

/***************************************************************************
 * Name: setup_termios
 *
 * Description:
 *   Setup UART attributes.
 ***************************************************************************/ 
void 
setup_termios(int fd)
{
	struct termios options;

	tcgetattr(fd, &options);
	options.c_iflag &=  ~(ICRNL);
	options.c_lflag |= ICANON; /* ICANON flag allows us to turn off
			   	      canonical mode.This means we will
			   	      be reading input byte-by-byte,
			   	      instead of line-by-line. */
	// options.c_lflag |= (ICANON | ECHOE | ECHOK) 

	/* Dissable all echo attributes */
	options.c_lflag &= ~( ECHO | ECHOE | ECHOK | ECHONL |IEXTEN | ISIG);
	options.c_cflag = B38400 | CS8 |CLOCAL| CREAD;
	tcsetattr (fd,TCSANOW, &options);	
}

/***************************************************************************
 * Name: extract_number
 *
 * Description:
 *   Extract the number in the string, passed as a line argument.
 ***************************************************************************/ 
void
extract_number(char *line, char *number)
{
	int i, k;
		
	for (i = 0, k = 0; i < strlen(line); i++) {
		if (isdigit(line[i])) {
			number[k++] = line[i];
		}
	}
	number[k] = '\0';
}

/***************************************************************************
 * Name: check_serial_number
 *
 * Description:
 *   Check if DUT has a serial number and ask the user to 
 *   continue with the testing.
 ***************************************************************************/ 
int
check_serial_number(int fd)
{
	char serial_cmd[] = "cat /rsvd/factory.txt\n";
	char serial_line[20] = "serial"; /* Compare string for serial number */
	char serial_number[10];
	int c;

	if (!read_from_logger(fd, BOOT_STR, BOOT_TIMEOUT, NONE) &&
	    !write_to_logger(fd, serial_cmd) &&
	    !read_from_logger(fd, serial_line, DEF_TIMEOUT, STORE)) {

		extract_number(serial_line, serial_number);
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

/***************************************************************************
 * Name: write_to_logger
 *
 * Description:
 *   Function for writing to DUT over UART.
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: read_from_logger
 *
 * Description:
 *   Function for reading from DUT over UART.
 *
 * Input Parameters:
 *   fd - DUT file descriptor
 *   comp_str - string to match with the line read from DUT
 *   timeout - timeout in seconds for select
 *   flags - flags for read handling and log file operations 
 *
 * Returned Value:
 *   0 if comp_str matches the read line or after one line is read
 *   if LINE is specified in flags
 *   1 when timeout has expired
 *   -1 on error
 ***************************************************************************/ 
int
read_from_logger(int fd, char *comp_str, float timeout, int flags)
{
	int retval;
	int rx_length;
	char buf[256];
	static FILE *log_fp = NULL;

	fd_set rfds;
	struct timeval tv;
	
	FD_ZERO(&rfds);		/* Clears the file descriptor set */
	FD_SET(fd, &rfds);	/* Add DUT fd to the set */

	/* Open log file */
	if (flags & OPEN) {
		if ((log_fp = fopen(LOG_PATH, "wat")) == NULL) {
			perror("Unable to open log file");
			return -1;	
		}
	} /* Close log file */
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
		
			/* Insert into log */
			if (log_fp != NULL) {
				fprintf(log_fp, "%s", buf);
			}

			/* Print the read buf content */
			if (flags & PRINT) { 
				printf("%s", buf);
			}
			
			/* Read one line, write in comp_str and exit */
			if (flags & LINE) {
				strcpy(comp_str, buf);
				return 0;
			}

			/* Check if there is a string for comparison */
			if (comp_str != NULL) {
				/* Check for comp_str substring occurrence in buf */	
				char *pch = strstr(buf,comp_str);
				if (pch) {
					/* Store in comp_str if specified */
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

/***************************************************************************
 * Name: flush_logger
 *
 * Description:
 *   Flush serial input.
 ***************************************************************************/ 
void 
flush_logger(int fd)
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

/***************************************************************************
 * Name: get_md5sum
 *
 * Description:
 *  Retrieve image md5 checksum. 
 ***************************************************************************/ 
void 
get_md5sum(char *md5sum, size_t size)
{
	char md5sum_cmd[100];
	int i, ch;
	
	snprintf(md5sum_cmd, sizeof(md5sum_cmd), "md5sum %s", IMAGE_PATH);
	/* Pass the md5sum command to the shell 
	 * and extract the checksum. 
	 */
	FILE *p = popen(md5sum_cmd, "r");
	for (i = 0; i < size - 1 && isxdigit(ch = fgetc(p)); i++) {
		*md5sum++ = ch;
	}

	*md5sum = '\0';
	pclose(p);
}

/***************************************************************************
 * Name: get_file_size
 *
 * Description:
 *   Returns the file size in bytes.
 ***************************************************************************/ 
size_t
get_file_size(const char *filepath)
{
	struct stat st;

	stat(filepath, &st);
	return st.st_size;
}

/***************************************************************************
 * Name: flash_check
 *
 * Description:
 *   Check md5sum to verify the burned image.
 ***************************************************************************/ 
int 
flash_check(int fd)
{
	size_t image_size;
	char md5c_cmd[100];
	char md5sum[MD5SUM_HASH_SIZE];
	
	flush_logger(fd);

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

/***************************************************************************
 * Name: flash_logger
 *
 * Description:
 *   Write image to DUT and perform check.
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: mount_fs 
 *
 * Description:
 *   Mount mnt and rsvd file systems.
 ***************************************************************************/ 
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

	flush_logger(fd);
	return 0;	
}

/***************************************************************************
 * Name: mock_factory_write 
 *
 * Description:
 *   Use mock factory settings and default configuration for testing. 
 ***************************************************************************/ 
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

		//	printf("Probably DUT is in sleep mode\n");
		print_error_msg("Mock factory config write failed\n");
		return -1;
	}

	return 0;
}

/***************************************************************************
 * Name: led_test 
 *
 * Description:
 *   Function for testing system status LED.   
 ***************************************************************************/ 
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
	flush_logger(fd);
	return 0;
}

/***************************************************************************
 * Name: measure_voltage 
 *
 * Description:
 *   Measure and check if GSM voltage is set. 
 ***************************************************************************/ 
int 
measure_voltage(int fd, int i2c_fd) 
{
	char measure_cmd[] = "Measure";
	char response[10] = {0};
	int voltage;

	/* Send measure command to Nucleo */
	write(i2c_fd, measure_cmd, strlen(measure_cmd));
	sleep(1);	/* Wait for Nucleo to make measurement */
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

/***************************************************************************
 * Name: AT_sim 
 *
 * Description:
 *   Obtain SIM params using cu and AT commands.
 ***************************************************************************/ 
int
AT_sim(int fd, struct logger_s *logger)
{
	char imsi_cmd[] = "AT+CIMI\n";
	char ccid_cmd[] = "AT+CCID\n";
	char rev_cmd[] = "ATI\n";
	char ccid_str[30] = "CCID:";	/* Comparison string for CCID */
	char imsi_str[30] = {0};	/* No comparison string for IMSI */
	char rev_str[30] = "Revision";	/* Comparison string for module rev */

	/* Enter cu and write AT commands for IMSI and CCID */
	if (write_to_logger(fd, "cu\n") ||
	    write_to_logger(fd, ccid_cmd) || 

	    /* The line that includes ccid_str contains the CCID */
	    read_from_logger(fd, ccid_str, DEF_TIMEOUT, STORE) ||

	    /* AT+CCID returns OK if successful */
	    read_from_logger(fd, "OK", DEF_TIMEOUT, NONE) ||	
	    write_to_logger(fd, imsi_cmd) ||

	    /* The second line returned by AT+CIMI contains only the
	     * IMSI number without any substrings to make comparison
	     * to match the line. So the two lines are read and the
	     * second, which contains the IMSI, is stored in imsi_str.
	     */
	    read_from_logger(fd, imsi_str, DEF_TIMEOUT, LINE) ||
	    read_from_logger(fd, imsi_str, DEF_TIMEOUT, LINE)) {
	   
		print_error_msg("Failed reading IMSI and CCID number\n");	
	} else {	
		/* Although IMSI contains only the number, it has /r/n 
		 * at the end, which inserted in the database will 
		 * result in column dislocation, so the imsi_str 
		 * must be parsed with extract_number.
	       	 */
		extract_number(imsi_str, logger->imsi);
		extract_number(ccid_str, logger->ccid);
	}

	/* Write AT command for module revision */
	if (write_to_logger(fd, rev_cmd) ||
	    read_from_logger(fd, rev_str, DEF_TIMEOUT, STORE)) {

		print_error_msg("Failed reading module revision\n");
	} else {
		/* Search for : in the line containing the revision */
		char *pch = strstr(rev_str, ":");
		int i;	
		
		/* The character preceding the module revision is a whitespace
		 * so increment the pointer to refer to the first digit */
		*pch++;
		for (i = 0; *pch != '\r'; i++) {
			logger->module_rev[i] = *pch++;
		}
		logger->module_rev[i] = '\0';
	}
}

/***************************************************************************
 * Name: qftpc_sim 
 *
 * Description:
 *   Obtain SIM params using qftpc commands.
 ***************************************************************************/ 
void
qftpc_sim(int fd, struct logger_s *logger)
{
	char imsi_str[20] = "IMSI";	/* Comparison string for IMSI */
	char ccid_str[30] = "CCID:";    /* Comparison string for CCID */
	char rev_str[30] = "Revision";	/* Comparison string for mod rev */
	char qftpc_gsm[] = "qftpc -j -e 0 -b 0\n";
	char qftpc_nbiot[] = "qftpc -j -e 5 -b 10\n";
	char *qftpc_cmd = qftpc_gsm;
	char mod_rev_cmd[100] = "qftpc -v -e 0 -b 0\n";
	int timeout = QFTPC_TIMEOUT;
	
	/* If module is NB-IoT, use NB-IoT timeout and command.
	 * With GSM devices the module has already been powered
	 * in gsm_test, as with NB-IoT it isn't, so timeout 
	 * is used to wait for the powerup. 
	 */
	if (logger->module & NBIOT) {
		qftpc_cmd = qftpc_nbiot;
		timeout = NBIOT_TIMEOUT;
	}

	/* Write qftpc command for obtaining IMSI and CCID
	 * and capture the lines containing the two numbers
	 */
	if (write_to_logger(fd, qftpc_cmd) ||
    	    read_from_logger(fd, imsi_str, timeout, STORE) ||
	    read_from_logger(fd, ccid_str, QFTPC_TIMEOUT, STORE)) {
	
		print_error_msg("Failed reading IMSI and CCID number\n");	
		
	} else {
		extract_number(imsi_str, logger->imsi);
		extract_number(ccid_str, logger->ccid);
	}

	/* Write qftpc module revision command */
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

/***************************************************************************
 * Name: get_sim_info
 *
 * Description:
 *   Get CCID, IMSI and module revision.
 ***************************************************************************/ 
void
get_sim_info(int fd, struct logger_s *logger)
{	
	char soft_ver[10];

	/* If software version is 0.4.27 use cu and AT commands to get SIM
	 * parameters. Then discharge cap and reset device to exit from cu.
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

/***************************************************************************
 * Name: gsm_test 
 *
 * Description:
 *   Module test for GSM devices.
 *
 *   Charge cap, boot module, set baud rate, mode, get SIM parameters
 *   and module revision, write AT commands. Set APN and run ping. 
 ***************************************************************************/ 
int
gsm_test(int fd, int i2c_fd, struct logger_s *logger)
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

	/* Compose qftpc command for setting APN and ping */
	snprintf(qftpc_cmd, sizeof(qftpc_cmd), 
		 "qftpc -a %s -e 0 -b 20 -i %s\n", logger->apn, ip);

	printf("\n---------------------\n");
	printf("Setting GSM module, please wait 1 minute...\n");

	/* Boot module, change baud rate and set modem G mode */
	if (!write_to_logger(fd, bd_cmd) &&
	   
       	    /* Make sure baud rate and pins are set */
	    !read_from_logger(fd, bd_comp_str, UMTS_BAUD_TIMEOUT, NONE) &&
	    !read_from_logger(fd, umts_boot_str, UMTS_BOOT_TIMEOUT, NONE)) {

		printf("Baud rate changed successfully\n");
	} else { 
		print_error_msg("Error while changing gsm baud rate\n");
		return -1;
	}

	/* Check if VCCGSM is set */
	if (measure_voltage(fd, i2c_fd)) {
		return -1;	
	}
	
	/* Get SIM card parameters - IMSI, CCID - and the module revision */
	get_sim_info(fd, logger);

	/* AT commands for module configuration */
	if (write_to_logger(fd, "echo \"AT&F\" > /dev/ttyS0\n") ||
	    write_to_logger(fd, "echo \"AT+CFUN=1,1\" > /dev/ttyS0\n")) {

		print_error_msg("Error writing AT commands\n");
		return -1;
	}
	printf("Pinging host %s ...\n", ip);	
	
	/* Set apn and run ping */
	if (write_to_logger(fd, qftpc_cmd)) {	
		print_error_msg("Error writing qftpc ping command\n");
		return -1;
	}

	write(1, test_msg, sizeof(test_msg)); 
#if 0
	/* Check for UMTS error */
	if (!read_from_logger(fd, error_msg, UMTS_ERROR_TIMEOUT, STORE | PRINT)) {
		err = 1 - err;	
	}
	else
#endif
	/* Make sure ping is successful */
	if (!read_from_logger(fd, ping_comp_str, PING_TIMEOUT, NONE)) {
		print_ok();
		return 0;
	}
	
	write_to_logger(fd, "\nreset\n");
	print_fail();
	flush_logger(fd);

	/* Retest if UMTS error occurred.
	 * In some cases the subsequent test is successful.
	 */
	if (err) {
		print_error_msg(error_msg);
	} else	{
		printf("Retest GSM? [y/n] ");
		scanf(" %c", &response);
		if (response == 'y' || response == 'Y') {
			return gsm_test(fd, i2c_fd, logger);
		}
	}

	return -1;
}

/***************************************************************************
 * Name: gsm_test 
 *
 * Description:
 *   Module test for NB-IoT devices.
 *
 *   Charge cap, boot module, set baud rate, mode, get SIM parameters
 *   and module revision, write AT commands. Set APN and run ping. 
 ***************************************************************************/ 
int
nbiot_test(int fd, int i2c_fd, struct logger_s *logger)
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

	/* Get SIM card parameters - IMSI, CCID - and the module revision */
	get_sim_info(fd, logger);
	
	/* Check if VCCGSM is set */
	if (measure_voltage(fd, i2c_fd)) {
		return -1;	
	}
	
	/* Disable autoconnect option */
	if (write_to_logger(fd, autocon_cmd) ||
	    read_from_logger(fd, autocon_cmp_str, AUTOCON_TIMEOUT, NONE)) {

		write_to_logger(fd, "reset\n");
		print_error_msg("Error while removing autoconnect\n");
		return -1;
	}	
	printf("Pinging host %s ...\n", ip);
	
	/* Set apn and run ping */
	if (write_to_logger(fd, ping_cmd)) {	
		print_error_msg("Error writing qftpc ping command\n");
		return -1;
	}
	write(1, test_msg, sizeof(test_msg)); 	

	/* Make sure ping is successful */	
	if (!read_from_logger(fd, ping_comp_str, PING_TIMEOUT, NONE)) {
		print_ok();
		return 0;
	} else {
		write_to_logger(fd, "reset\n");
		print_fail();
		flush_logger(fd);
		return -1;
	}
}

/***************************************************************************
 * Name: inputs_config 
 *
 * Description:
 *   Write configuration for the digital inputs.
 ***************************************************************************/ 
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
		/* Wait 0.5s before the next write */
		read_from_logger(fd, NULL, INPUTS_CONF_SLEEP, NONE);
	}

	printf("Inputs config written\n");
	return 0;
}

/***************************************************************************
 * Name: generate_pulses 
 *
 * Description:
 *   Send 100 pulses to each digital input.
 ***************************************************************************/ 
int 
generate_pulses(int fd, int i2c_fd) 
{
	char generate_cmd[] = "Generate";
	char alarm_str[] = "Alarm Ampule-Reed received.";
	char test_msg[] = "Inputs test -    ";

	printf("Starting alarm\n");
	if (write_to_logger(fd, "alarm -f\n") ||
	    read_from_logger(fd, alarm_str, ALARM_TIMEOUT, NONE)) {    
		print_error_msg("Error while starting alarm\n");
		return -1;
	}

	write(1, test_msg, sizeof(test_msg));	
	
	/* Send generate command to Nucleo */
	write(i2c_fd, generate_cmd, strlen(generate_cmd));
	sleep(1); /* Wait a second for Nucleo to generate the pulses */	

	/* Check the stored values in the lptims */
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

/***************************************************************************
 * Name: inputs_test 
 *
 * Description:
 *   Inputs test consists of inputs configuration and pulse (de)generation.
 ***************************************************************************/ 
int 
inputs_test(int fd, int i2c_fd) 
{
	printf("\n---------------------\n");
	return (inputs_config(fd) || generate_pulses(fd, i2c_fd));
}

/***************************************************************************
 * Name: reed_test 
 *
 * Description:
 *   Perform reed ampule test.  
 *   
 *   Alarm must be triggered before the test. After the device falls
 *   asleep, the electromagnet is turned on. If it fails to activate, 
 *   the function waits 20 seconds for manual activation by hand. 
 ***************************************************************************/ 
int 
reed_test(int fd, int module) 
{
	char test_msg[] = "Reed test -    ";
	char sleep_str[] ="AT+QPOWD=1"; 	/* Comparison string for GSM */
	char reed_str[] = "Reed clicked for 3s.";

	if (module  == NBIOT) {
		strcpy(sleep_str, "Send time");
	}	

	flush_logger(fd);	
	printf("\n---------------------\n");
	printf("Wait for DUT to enter sleep mode...\n");

	/* Wait for device to fall asleep and turn on the electromagnet */
	if (!read_from_logger(fd, sleep_str, SLEEP_TIMEOUT, NONE)) {
		printf("Turning magnet on\n");	
		write(1, test_msg, sizeof(test_msg));
		digitalWrite(REED, HIGH);
	
		/* If reed_str is read the test is successful */
		if (!read_from_logger(fd, reed_str, EM_TIMEOUT, NONE)) {
			digitalWrite(REED, LOW);
			print_ok();
			write_to_logger(fd, "reset\n");
			
			/* Return on reset */
			return read_from_logger(fd, BOOT_STR,
						BOOT_TIMEOUT, NONE);

		} else {	
			digitalWrite(REED, LOW);
			print_fail();

			/* Wait for the ampule to be activated by hand */
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

/***************************************************************************
 * Name: setup_mysql 
 *
 * Description:
 *   Setup mysql connection.  
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: insert_serial_number 
 *
 * Description:
 *   Ask the user to type in the serial number.  
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: database_insert 
 *
 * Description:
 *   Perform a database insert.    
 ***************************************************************************/ 
int 
database_insert(struct logger_s *logger)
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

/***************************************************************************
 * Name: factory_write 
 *
 * Description:
 *   Perform database insert and execute factory command. 
 ***************************************************************************/ 
int 
factory_write(int fd, char *fct_comp_str, char *apn_cmd,
	      struct logger_s *logger, int manual_flag) 
{	
	char fct_cmd[100];

	printf("\n---------------------\n");

	/* Obtain serial number from user */
	if (manual_flag){
		insert_serial_number(logger->ser_num);
	}
	
	/* Obtain software version */
	if (check_soft_ver(fd, logger->soft_ver)) {
		return -1;
	}
	
	/* Insert into database */
	if (database_insert(logger)) {
		print_error_msg("Database write failed\n");
		return -1;
	}

	sprintf(fct_cmd, "factory -s %s -r %s -p %s -f\n",
		logger->ser_num, logger->hard_rev, logger->prod_num);
	flush_logger(fd);

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

/***************************************************************************
 * Name: read_soft_ver 
 *
 * Description:
 *   Obtain DUT software version. 
 ***************************************************************************/ 
int
read_soft_ver(int fd, char *soft_ver)
{
	char uname_str[50] = "NuttX/Smartcom"; /* Comparison string */ 
	
	if (write_to_logger(fd, "uname -a\n") ||
	    read_from_logger(fd, uname_str, DEF_TIMEOUT, STORE)) {
		print_error_msg("Error while reading software version\n");
		return -1;
	}
	
	sscanf(uname_str, "%*s %s", soft_ver);
	return 0;
}

/***************************************************************************
 * Name: check_soft_ver 
 *
 * Description:
 *   Compare image and DUT software version.
 *
 * PREREQUISITES!!!:
 *   The image filename must contain the software version as in
 *   DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin
 ***************************************************************************/ 
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

	flush_logger(fd);
	return 0;
}

/***************************************************************************
 * Name: read_UID 
 *
 * Description:
 *   Obtain DUT UID. 
 ***************************************************************************/ 
int
read_UID(int fd, char *UID)
{
	char UID_str[50] = "UID"; /* Comparison string */
	char *pch;
	int i;

	/* The UID is printed on start sequence */
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

/***************************************************************************
 * Name: log_into_db 
 *
 * Description:
 *   Insert log file into failed_devices table in db.
 *
 *   Prepared statement is used because DUT prints some non-ASCII characters
 *   which break the query. 
 ***************************************************************************/ 
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

	/* Open log file */
	if ((log_fd = open(LOG_PATH, O_RDONLY)) == -1) {
		perror("Unable to open log file");
		return;
	}

	/* mmap the log file into log buffer */
	log = (char *)mmap(NULL, filesize, PROT_READ, MAP_SHARED, log_fd, 0);
	if (log == MAP_FAILED) {
		perror("Unable to map log file");
		close(log_fd);
		return;
	}

	/* Read the device UID */	
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

/***************************************************************************
 * Name: print_ok 
 *
 * Description:
 *   Print OK in green.
 ***************************************************************************/ 
void 
print_ok()
{
	printf("\033[0;32m");
	printf("OK\n");
	printf("\033[0m");
}

/***************************************************************************
 * Name: print_fail 
 *
 * Description:
 *   Print fail message.
 ***************************************************************************/ 
void 
print_fail() 
{
	print_error_msg("FAIL\n");
}

/***************************************************************************
 * Name: print_fail 
 *
 * Description:
 *   Print text in red.
 ***************************************************************************/ 
void
print_error_msg(char *err_msg)
{
	printf("\033[0;31m");
	printf("%s", err_msg);
	printf("\033[0m");
}

/***************************************************************************
 * Name: discharge_cap 
 *
 * Description:
 *   Discharge the capacitor.
 *
 * Input parameters:
 *   disch_time - specifies the time for discharge in seconds.
 ***************************************************************************/ 
void
discharge_cap(int disch_time)
{
	digitalWrite(CAP, HIGH);
	sleep(disch_time);
	digitalWrite(CAP, LOW);
}

/***************************************************************************
 * Name: power_off 
 *
 * Description:
 *   Shutdown DUT and discharge cap.
 *
 * Input parameters:
 *   disch_time - specifies the time for cap discharge in seconds.
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: close_fds 
 *
 * Description:
 *  Variadic function for closing file descriptors. 
 *
 * Input parameters:
 *   fd_count - number of file descriptors passed as arguments. 
 ***************************************************************************/ 
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

/***************************************************************************
 * Name: calculate_time 
 *
 * Description:
 *  Calculate test time. 
 ***************************************************************************/ 
double 
calculate_time(time_t *start)
{
	time_t end;
	time(&end);
	return difftime(end, *start);
}

/***************************************************************************
 * Name: reset_logger 
 ***************************************************************************/ 
void 
reset_logger()
{
	digitalWrite(PWR, LOW);
	sleep(1);
	digitalWrite(PWR, HIGH);
	sleep(2);
}

/***************************************************************************
 * Name: reset_nucleo 
 ***************************************************************************/ 
void 
reset_nucleo(int sleeptime)
{
	pinMode(NPWR, OUTPUT);
	digitalWrite(NPWR, LOW);
	sleep(sleeptime);
	digitalWrite(NPWR, HIGH);
	pinMode(PUPIN, INPUT);
	pullUpDnControl(PUPIN, PUD_UP);
	
	/* Wait until Nucleo file system appears */
	do {
		;
	} while (open(NUCLEO_PATH, O_RDONLY) < 0);
}

/***************************************************************************
 * Name: end_test 
 ***************************************************************************/ 
void 
end_test(int fd, int i2c_fd, int cap_time, int test_result, time_t *start)
{
	/* Close log file descriptor */
	read_from_logger(fd, NULL, 0, CLOSE); 

	/* If test has failed insert log into db */	
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

