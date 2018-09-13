#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <stdio_ext.h>
#include <time.h>
#include <math.h>
#include "test.h"


#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <wiringPi.h>


int main() {
	int fd;
	int i2c_fd;
	int address = 0x50;
	time_t start, end;
	double dif;

	//comparison strings
	char fct_str[] = "written successfully";
	char bd_str[] = "Successfully changed baud rate";
	char ping_str[] = "Successfully ping host";
	char flash_str[] = "NuttX-0.4.25";
	
	//open serial port 
	fd = open ("/dev/ttyS0", O_RDWR | O_NOCTTY  ); // O_NDELAY);
	if (fd == -1) 
		perror("Unable to open serial port\n");
	
	//Connect to Nucleo	
	if ((i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0) {
		printf("Failed to open the I2C bus\n");
		return -1;
	}
	if (ioctl(i2c_fd, I2C_SLAVE, address)) {
		printf("Failed to acquire bus access and/or talk to Nucleo.\n");
		return -1;
	}

	//set options for serial
	struct termios options;
	tcgetattr(fd, &options);
	options.c_iflag &=  ~(ICRNL);
	options.c_lflag |= ICANON; //ICANON flagallows us to turn off canonical mode. This means we will be reading input byte-by-byte, instead of line-by-line.
	//options.c_lflag |= (ICANON | ECHOE | ECHOK) ;
	options.c_lflag &= ~( ECHO | ECHOE | ECHOK | ECHONL |IEXTEN | ISIG);
	options.c_cflag = B38400 | CS8 |CLOCAL| CREAD;
	tcsetattr (fd,TCSANOW, &options);
		
	system ("gpio edge 17 rising");
	wiringPiSetupSys();

//	waitForInterrupt(17, -1);
//	for (; ;){
//	start_test(); //proverka na cifroviq vhod za VCC na loggeraa
	printf("=============");
	printf("\033[1;32m");
	printf("TEST BEGIN");
	printf("\033[0m");
	printf("=============\n\n");
	time(&start);

	if ( /* flash_logger(fd, flash_str) || fs_write(fd) ||
		mock_factory_write(fd, fct_str) || led_test(fd) ||
		gsm_test(fd, bd_str, ping_str, i2c_fd) ||
	       	inputs_test(fd, i2c_fd) ||*/
	      	factory_write(fd, fct_str))
	{
		printf("\033[1;31m");
		printf("Test failed\n");
		printf("\033[0;m");
		close(fd);
		return -1;
	}
	if (soft_rev_check(fd))
	       printf("Software revision doesn't match, must be 0.4.25\n");


	time(&end);
	dif = difftime(end, start);
	int seconds = (int)dif % 60;
	int minutes = (dif - seconds) / 60;

	printf("\n---------------------\n");
	printf("\033[1;32m");
	printf("Test ended successfully!\n");
	printf("\033[0m");
	printf("Test time = %d.%.2dm\n", minutes, seconds);
	close (fd);	
}

int compare_strings(char buf[], char compstr[]) {
//	printf("buf = %scompstr = %s\n",buf, compstr);
	char *pch = strstr(buf, compstr);	
	if (pch){
		return 0;
	}
	else return 1;
}

int write_to_logger (int fd, char str[]){
	int tx_length = write (fd, str , strlen(str));
	if (tx_length < 0) {
		perror("Write failed - ");
		return -1;
	} else return 0;
}

int read_from_logger (int fd, char comp_str[], int flush, int timeout){
	int retval;
	int rx_length;
	char buf[256];

	fd_set rfds;
	struct timeval tv;
	
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	tv.tv_sec = 0;
	tv.tv_usec = timeout;
	while(1){
		retval = select(fd+1, &rfds, NULL, NULL, &tv);
		if (retval == -1){
			perror("select()");
			return -1;
		}
			else if (FD_ISSET(fd, &rfds))
		{
			rx_length = read(fd, buf, 255);
		  	buf[rx_length] = 0;
			if (!flush) printf("%s", buf);
			if (comp_str != NULL)
			{	
				if (!compare_strings(buf, comp_str))
					return 0;
			}
		}
		else { 
		//	printf("No data within %d secs\n", timeout);
			return 1;
		}		
	} 
}

void flush(int fd){
	read_from_logger(fd, NULL, 1, 1000000 );
}

int start_test() {
	for (; ;) 
	{	
		if (waitForInterrupt(17, -1) > 0){
			printf("Begin test\n");		
			return 0;
		}
	}
}

int flash_check(int fd, char flash_str[]){
	if (!read_from_logger(fd, flash_str, 1, 15000000) &&
		!write_to_logger(fd, "md5c -f /dev/ifbank1 -e 255016\n")&&
		!read_from_logger(fd, "bc46e19c590d79455cc1d9c54967a862", 1, 5000000)) 
	{
		//printf("Programming successful\n");
		printf("\033[0;32m");
		printf("OK\n");
		printf("\033[0m");
		return 0;
	}
	else {
	//	printf("Programming failed\n");
		printf("\033[0;31m");
		printf("FAIL\n");
		printf("\033[0m");
		return -1;
	}
}

int flash_logger(int fd, char flash_str[]){
	printf("\n---------------------\n");
	int src_fd, dst_fd, n;
	unsigned char buf[4096];
	char print[] = "Programming logger -    ";
	write(1, print, sizeof(print)); 
	src_fd = open("/home/pi/sourceCodes/DL-MINI-BAT36-D2-3G-VB1.0-0.4.25.bin", O_RDONLY);
	dst_fd = open("/media/pi/NODE_L476RG/image.bin", O_CREAT | O_WRONLY);

	if (src_fd == -1 || dst_fd == -1) perror("Unable to open.\n ");
	while (1) {
		n = read( src_fd, buf, 4096);
		if ( n == -1) {
			printf("Error reading file.\n");
			break;
		}
		if (n == 0) {
			close(src_fd);
			close(dst_fd);
			return flash_check(fd, flash_str);
			break;
		}
		n = write(dst_fd, buf, n);
		if ( n == -1) {
			printf("Error writing.\n");
			break;
		}
	}
	printf("\033[1;31m");
	printf("Programming failed\n");
	printf("\033[0;m");
	close(src_fd);
	close(dst_fd);
	return -1;
}

//proverka dali ima neshto na vhoda na porta
//pri podavane na komandite ot fs_write
//int check_for_input (int fd) {
//	return  select(fd + 1, &rfds, NULL, NULL, &tv);
//}
int fs_write(int fd) {
	char strings[4][255] = {
		{ "mkfatfs /dev/mtdblock0\n"},
		{ "mount -t vfat /dev/mtdblock0 /mnt\n" },
		{ "mkfatfs /dev/ifbank2r\n" },
		{ "mount -t vfat /dev/ifbank2r /rsvd\n" }
	};	
	write_to_logger(fd, "umount /mnt\n");
	write_to_logger(fd, "umount /rsvd\n");	
	for (int i = 0; i < 4; i++){
		write_to_logger(fd, strings[i]);
	}
	flush(fd);
	return 0;	
	//read_from_logger ne vrashta nishto pri podavane na greshna komanda ot strings??
	//trqbva da se premahne echo-to na izhoda
	//return check_for_input(fd);
}

int mock_factory_write(int fd, char fct_str[]) {
	char mock_fcm[] = "factory -s 1801001 -r VB1.0 -p DL-MINI-BAT36-D2-3G -f\n"; 
	if (write_to_logger(fd, mock_fcm) ||
		read_from_logger(fd, fct_str, 1, 1000000) ||
		write_to_logger(fd, "factory -c\n") ||
		read_from_logger(fd, fct_str, 1, 1000000))
	{
		printf("Mock factory config write failed\n");
		return -1;
	}
	return 0;
}

int factory_write(int fd, char fct_str[]) {
	
	printf("\n---------------------\n");
	char ser_num[100] = {0} , hard_rev[100], prod_num[100];
	char fct_cmd[255];

	//factory input parameters	
	do {
		if (*ser_num)
		printf("Serial number must be less than 10 characters long\n");
		printf("Enter serial number: ");
		scanf("%s", ser_num);
	} while (strlen(ser_num) > 10);

	printf("Enter hardware revision: ");
	scanf("%s", hard_rev);
	printf("Enter product number: ");
       	scanf("%s", prod_num);	
	sprintf(fct_cmd, "factory -s %s -r %s -p %s -f\n",
			ser_num, hard_rev, prod_num);
	
	if ( !write_to_logger(fd, fct_cmd) && 
			!read_from_logger(fd, fct_str, 1, 2000000 ) &&
			!write_to_logger(fd, "factory -c\n") &&
			!read_from_logger(fd, fct_str, 1, 2000000))
	{
		printf("Factory config successfully written\n");
		return 0;	
	}
	else {
		printf("\033[0;31m");
		printf("Factory config write failed\n");
		printf("\033[0m");
		return -1;
	}
}

int led_test(int fd) {
	printf("\n---------------------\n");
	char response;	
	printf("Starting system LED\n");
	write_to_logger(fd, "gpio -o 1 /dev/gpout_systemstatusled\n");
	printf("Does the LED work? [y/n] ");
	scanf(" %c", &response);
	printf("LED test -    ");
	if (response == 'y' || response == 'Y' ) {
		printf("\033[0;32m");
		printf("OK\n");
		printf("\033[0m");
	//	printf("Stopping system LED\n");
		write_to_logger(fd, "gpio -o 0 /dev/gpout_systemstatusled\n");
		flush(fd);
		return 0;	
	} else {
		printf("\033[0;31m");
		printf("FAIL\n");
		printf("\033[0m");
		write_to_logger(fd, "gpio -o 0 /dev/gpout_systemstatusled\n");
		return 0;
	}
}

int measure_voltage(int fd, int i2c_fd) {
	char buf[20] = "Measure";
	char response[10] = {0};
	
	write(i2c_fd, buf, strlen(buf));
	sleep(1);
	read(i2c_fd, response, 7);
	int voltage = atoi(response);

	if (voltage > 3600) {
		printf("VCCGSM value is set - %d mV\n", voltage);
	       	return 0;
	}
	else {
		printf("VCCGSM is not set\n");
		write_to_logger(fd, "reset\n");
		return -1;
	}
}

int gsm_test(int fd, char bd_str[],char ping_str[], int i2c_fd){	
	printf("\n---------------------\n");
	char test_msg[] = "GSM test -    ";
	printf("Setting GSM module, please wait 1 minute...\n");
	if (!write_to_logger(fd, "modem_config -d /dev/ttyS0 -g 1 -ar 9600\n")
		        //&& !read_from_logger(fd, "nsh", 1, 1000000)	
		&& !read_from_logger(fd, bd_str, 1, 60000000))
		printf("Baud rate changed successfully\n");
	else { 
		printf("\033[1;31m");
		printf("Error while changing gsm baud rate\n");
		printf("\033[0m");
		write_to_logger(fd, "reset\n");
		return -1;
	}
	flush(fd);

//	printf("Measure VCCGSM - 3.8V\nPress ENTER to continue test ");
//	getchar();
//	getchar();

	if (measure_voltage(fd, i2c_fd))
		return -1;	
	
	//AT commands
	write_to_logger(fd, "echo \"AT&F\" > /dev/ttyS0\n");
	write_to_logger(fd, "echo \"AT+CFUN=1,1\" > /dev/ttyS0\n");
	printf("Pinging host 10.210.9.3 ...\n");
	write_to_logger(fd, "qftpc -i 10.210.9.3\n");
	write(1, test_msg, sizeof(test_msg)); 
	if (!read_from_logger(fd, ping_str, 1, 30000000)) {
		printf("\033[0;32m");
		printf("OK\n");
		printf("\033[0m");
	//	printf("Ping successful\n");
		return 0;
	} else {
		printf("\033[0;31m");
		printf("FAIL\n");
		printf("\033[0m");
	//	printf("Ping failed. Test ended\n");
		write_to_logger(fd, "reset\n");
		return 1;
	}

}

void inputs_config(int fd){
	char new_line[50] = "printf \"\\n\" >> /mnt/conf.cfg\n";
	char line[50];
	char buf[256];

	char cmd[][50] = {
		{ "lptim1_state enable" },
		{ "lptim1_init_value -1" },
		{ "lptim1_avr_gain 1.0" },
		
		{ "lptim1_count_gain 1.0" },
		{ "lptim1_log_avr enable" },
		{ "lptim1_log_count enable" },
		
		{ "lptim2_state enable" },
		{ "lptim2_init_value 0" },
		{ "lptim2_avr_gain 1.0" },

		{ "lptim2_count_gain 1.0" },
		{ "lptim2_log_avr enable" },
		{ "lptim2_log_count enable" }
	};


	printf("Setting the digital inputs' config, wait for 6 seconds\n");
	write_to_logger (fd, new_line);
	for ( int i = 0; i < 12 ; i++ ) {
		sprintf (line, "printf \"%s\" >> /mnt/conf.cfg\n", cmd[i]);
		write_to_logger(fd, line);
		write_to_logger(fd, new_line);
		read_from_logger(fd, NULL, 1, 500000);
	}
	printf("Inputs config written\n");
}

int generate_pulses(int fd, int i2c_fd) {
	char buf[20] = "Generate";
	char alarm_str[] = "Alarm Ampule-Reed received.";
	char print[] = "Inputs test -    ";

	printf("Starting alarm\n");
	write_to_logger(fd, "alarm -f\n");
	write(1, print, sizeof(print));
	if (!read_from_logger(fd, alarm_str, 1, 2000000)){
		write(i2c_fd, buf, strlen(buf));
		read_from_logger(fd, NULL, 1, 1500000);
		
		if (!write_to_logger(fd, "cat /dev/lptim1\n") &&
				!read_from_logger(fd, "96", 1, 500000) &&
				!write_to_logger(fd, "cat /dev/lptim2\n") &&
				!read_from_logger(fd, "95", 1, 500000))
		{
			printf("\033[0;32m");
			printf("OK\n");
			printf("\033[0m");
		//	printf("Pulse test successful, resetting device\n");
			write_to_logger(fd,"reset\n");
				flush(fd);
			return 0;				
		} else {
			printf("\033[0;31m");
			printf("FAIL\n");
			printf("\033[0m");
			write_to_logger(fd, "reset\n");
		//	printf("Pulse test failed\n");
			return -1;
		}		
	}	
}

int inputs_test(int fd, int i2c_fd) {
	printf("\n---------------------\n");
	inputs_config(fd);
	return generate_pulses(fd, i2c_fd);
}

int soft_rev_check(int fd) {
	write_to_logger(fd, "uname -a\n");
	return read_from_logger(fd, "0.4.25",1 , 500000);
}

