#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>

#include "tester.h"

#define	PUPIN 9
#define	NPWR 11
#define	CAP 17
#define	REED 22
#define	JMP 24
#define	PWR 27

#define IMAGE_PATH "/home/pi/sourceCodes/test/DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin" 
#define NUCLEO_PATH "/media/pi/NODE_L476RG"

int 
main(int argc, char *argv[]){
	char  *apn = "internet.vivacom.bg";
	int fd, i2c_fd, address = 0x50, opt, seconds, minutes;
	time_t start, end;
	double dif;
	
	//comparison strings
	char fct_str[] = "written successfully";
	char bd_str[] = "Successfully changed baud rate";
	char ping_str[] = "Successfully ping host";
	char flash_str[] = "NuttX-0.4.27";
	char apn_cmd[200];

	//check for input parameters - apn
	while ((opt = getopt(argc, argv, "a:")) != -1) 
	{
		switch (opt){
		case 'a':
			apn = optarg;
			break;
		default:
			fprintf(stderr, "Usage %s [-a apn]\n", argv[0]);
			return 0;
		}
	}
	
	snprintf(apn_cmd, 200, "printf \"umts_apn %s\" >> /rsvd/default.cfg\n"
			 "printf \"\\n\" >> rsvd/default.cfg\n", apn);

	setup_devices(); //power up Nucleo and DUT
	
	//open serial port 
	if ((fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY)) == -1) // O_NDELAY
	{
		perror("Unable to open serial port\n");
	}

	//Connect to Nucleo	
	if ((i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0) 
	{
		printf("Failed to open the I2C bus\n");
		return -1;
	}

	//set I2C Nucleo address
	if (ioctl(i2c_fd, I2C_SLAVE, address)) 
	{
		printf("Failed to acquire bus access and/or talk to Nucleo.\n");
		return -1;
	}

	setup_termios(fd);//set options for serial port
	
	printf("=============");
	printf("\033[1;32m");
	printf("TEST BEGIN");
	printf("\033[0m");
	printf("=============\n\n");
	time(&start);

	//test conditions
	if (flash_logger(fd, flash_str)
		|| fs_write(fd) //file system config
		|| mock_factory_write(fd, fct_str, apn_cmd) 
		|| led_test(fd) 
		|| gsm_test(fd, bd_str, ping_str, i2c_fd, apn) 
		|| inputs_test(fd, i2c_fd) 
		|| factory_write(fd, fct_str, apn_cmd)) 
	{
		power_off(fd);
		printf("\033[1;31m");
		printf("Test failed\n");
		printf("\033[0;m");
		return -1;
	}
	
	if (soft_rev_check(fd))
	{
	       printf("\nSoftware revision doesn't match, must be 0.4.27\n");
	}

	power_off(fd);
	time(&end);
	dif = difftime(end, start);
	seconds = (int)dif % 60;
	minutes = (dif - seconds) / 60;
	printf("\n---------------------\n");
	printf("\033[1;32m");
	printf("Test ended successfully!\n");
	printf("\033[0m");
	printf("Total test time = %d.%.2dm\n", minutes, seconds);
	
	return 0;
}

//setup GPIO pins and Nucleo
void 
setup_devices()
{
	//system ("gpio edge ?? rising");
	wiringPiSetupGpio();
	pinMode(PWR, OUTPUT);	
	digitalWrite(PWR, HIGH);
	pinMode(JMP, OUTPUT);
	pinMode(REED, OUTPUT);
	pinMode(CAP, OUTPUT);
	digitalWrite(CAP, LOW);
	digitalWrite(JMP, HIGH);

	if (open(NUCLEO_PATH, O_RDONLY) < 0) 
	{
		reset_nucleo(1);

	} 
	if (open(NUCLEO_PATH "/FAIL.TXT", O_RDONLY) > 0)
	{
		reset_nucleo(1);
	}

}

void 
setup_termios(int fd)
{
	struct termios options;

	tcgetattr(fd, &options);
	options.c_iflag &=  ~(ICRNL);
	options.c_lflag |= ICANON; //ICANON flagallows us to turn off canonical mode. This means we will be reading input byte-by-byte, instead of line-by-line.
	//options.c_lflag |= (ICANON | ECHOE | ECHOK) ;
	options.c_lflag &= ~( ECHO | ECHOE | ECHOK | ECHONL |IEXTEN | ISIG);
	options.c_cflag = B38400 | CS8 |CLOCAL| CREAD;
	tcsetattr (fd,TCSANOW, &options);	
}

int 
compare_strings(char buf[], char compstr[]) 
{
	char *pch = strstr(buf, compstr);	

//	printf("buf = %scompstr = %s\n",buf, compstr);
	if (pch)
	{
		return 0;
	} else return 1;
}

int 
write_to_logger (int fd, char str[])
{
	int tx_length = write (fd, str , strlen(str));

	if (tx_length < 0) 
	{
		perror("Write failed - ");
		return -1;
	} else return 0;
}

int
read_from_logger (int fd, char comp_str[], int flush, int timeout){
	int retval;
	int rx_length;
	char buf[256];

	fd_set rfds;
	struct timeval tv;
	
	FD_ZERO(&rfds); //clears the file descriptor set
	FD_SET(fd, &rfds); //add fd to the set

	tv.tv_sec = 0;
	tv.tv_usec = timeout;
	
	while(1)
	{
		retval = select(fd+1, &rfds, NULL, NULL, &tv);
		if (retval == -1)
		{
			perror("select()");
			return -1;
		}
		else if (FD_ISSET(fd, &rfds))
		{
			rx_length = read(fd, buf, 255);
		  	buf[rx_length] = 0;
			if (!flush)
			{ 
				printf("%s", buf);
			}
			if (comp_str != NULL)
			{	
				if (!compare_strings(buf, comp_str))
				{
					char *pch = strstr(comp_str, "ERROR UMTS:");
					if (pch) strcpy(comp_str, buf);
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

//flush serial input
void 
flush(int fd)
{
	read_from_logger(fd, NULL, 1, 1000000 );
}

int 
flash_check(int fd, char flash_str[])
{

	flush(fd);

	if (read_from_logger(fd, flash_str, 1, 20000000)) 
	{
		reset_logger();
	}	

	if (!write_to_logger(fd, "md5c -f /dev/ifbank1 -e 264996\n")
		&& !read_from_logger(fd, "073eecc8ecf31fe57f9862bc74a1e86b", 1, 10000000)) 
	{
		print_ok();
		return 0;
	}
	else 
	{
		print_fail();
		printf("Flash check failed\n");
		return -1;
	}
}

int 
flash_logger(int fd, char flash_str[])
{
	int src_fd, dst_fd, n, m;
	unsigned char buf[4096];
	char print[] = "Programming DUT -    ";

	printf("\n---------------------\n");
	write(1, print, sizeof(print)); 	

	src_fd = open(IMAGE_PATH, O_RDONLY);		
	dst_fd = open(NUCLEO_PATH "/image.bin", O_CREAT | O_WRONLY);
	
	if (src_fd == -1 || dst_fd == -1)
	{
		perror("Unable to open.\n ");
	}

	while (1) 
	{
		n = read( src_fd, buf, 4096);
		
		if (n == -1) 
		{
			printf("Error reading file.\n");
			break;
		}
		else if (n == 0) 
		{
			close(src_fd);
			close(dst_fd);
			return flash_check(fd, flash_str);
			break;
		}
		
		m = write(dst_fd, buf, n);
		
		if (m == -1) 
		{
			close(src_fd);
			close(dst_fd);
			printf("Before reset_nucleo\n");
			reset_nucleo(1);
			src_fd = open(IMAGE_PATH, O_RDONLY);		
			dst_fd = open(NUCLEO_PATH "/image.bin", O_CREAT | O_WRONLY);
			continue;
		}
	}
	printf("\033[0;31m");
	printf("Programming failed\n");
	printf("\033[0;m");
	close(src_fd);
	close(dst_fd);
	return -1;
}

int 
fs_write(int fd)
{
	char strings[4][255] = 
	{
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
}

int
mock_factory_write(int fd, char fct_str[], char apn_cmd[]) 
{
	char mock_fcm[] = "factory -s 1801001 -r VB1.0 -p DL-MINI-BAT36-D2-3G -f\n";
    
	if (write_to_logger(fd, mock_fcm) 
		|| read_from_logger(fd, fct_str, 1, 2000000) 
		|| write_to_logger(fd, apn_cmd) 
		|| write_to_logger(fd, "factory -c\n") 
		||read_from_logger(fd, fct_str, 1, 2000000))
	{
		printf("\033[0;31m");
		printf("Mock factory config write failed\n");
		//printf("Probably DUT is in sleep mode\n");
		printf("\033[0;m");
		return -1;
	}

	return 0;
}

int 
factory_write(int fd, char fct_str[], char apn_cmd[]) 
{	
	char ser_num[100] = {0};
    char hard_rev[] = "VB1.0";
	char prod_num[] = "DL-MINI-BAT36-D2-3G";
	char fct_cmd[255];

	printf("\n---------------------\n");

	//factory input parameters	
	do 
	{
		if (*ser_num)
		{
			printf("Serial number must be less than 10 characters long\n");
		}
		printf("Enter serial number: ");
		scanf("%s", ser_num);
	} while (strlen(ser_num) > 10);

	sprintf(fct_cmd, "factory -s %s -r %s -p %s -f\n",
			ser_num, hard_rev, prod_num);
	
	if (!write_to_logger(fd, fct_cmd) 
		&& !read_from_logger(fd, fct_str, 1, 2000000 ) 
		&& !write_to_logger(fd, apn_cmd) 
		&& !write_to_logger(fd, "factory -c\n") 
		&& !read_from_logger(fd, fct_str, 1, 2000000))
	{
		printf("Factory config successfully written\n");
		return 0;	
	}
	else 
	{
		printf("\033[0;31m");
		printf("Factory config write failed\n");
		printf("\033[0m");
		return -1;
	}
}

int 
led_test(int fd) 
{
	char response;	
	
	printf("\n---------------------\n");
	printf("Starting system LED\n");
	
	write_to_logger(fd, "gpio -o 1 /dev/gpout_systemstatusled\n");
	printf("Does the LED work? [y/n] ");
	
	scanf(" %c", &response);
	printf("LED test -    ");

	(response == 'y' || response == 'Y') ? print_ok() : print_fail();

	write_to_logger(fd, "gpio -o 0 /dev/gpout_systemstatusled\n");
	flush(fd);
	return 0;
}

int 
measure_voltage(int fd, int i2c_fd) 
{
	char buf[20] = "Measure";
	char response[10] = {0};
	int voltage;

	write(i2c_fd, buf, strlen(buf));
	sleep(1); //wait for nucleo to make measurement
	read(i2c_fd, response, 7);
	voltage = atoi(response);

	if (voltage > 3600) 
	{
		printf("VCCGSM value is set - %d mV\n", voltage);
	    return 0;
	}
	else 
	{
		printf("VCCGSM is not set - %d mV\n", voltage);
		write_to_logger(fd, "reset\n");
		return -1;
	}
}

int
gsm_test(int fd, char *bd_str,char *ping_str, int i2c_fd, char *apn)
{	
	int err = 0;
	char response;
	char test_msg[] = "GSM test -    ";
	char error_msg[] = "ERROR UMTS:";
	char qftpc_cmd[200];
	char mondis_ip[] = "10.210.9.3";
	char google_ip[] = "8.8.8.8";
	char *ip = google_ip;
	
	if (!strcmp(apn, "cp-mondis")) ip = "10.210.9.3";
	snprintf(qftpc_cmd, 200, "qftpc -a %s -e 15 -b 20 -i %s\n",
			 apn, ip);

	printf("\n---------------------\n");
	printf("Setting GSM module, please wait 1 minute...\n");
	if (!write_to_logger(fd, 
		"modem_config -d /dev/ttyS0 -g 1 -ar 9600 -e 15 -b 20\n")
		&& !read_from_logger(fd, bd_str, 0, 40000000))
	{
		printf("Baud rate changed successfully\n");
	}
	else 
	{ 
		printf("\033[1;31m");
		printf("Error while changing gsm baud rate\n");
		printf("\033[0m");
		return -1;
	}

//	printf("Measure VCCGSM - 3.8V\nPress ENTER to continue test ");
//	getchar();
//	getchar();

	//check VCCGSM
	if (measure_voltage(fd, i2c_fd))
	{
		return -1;	
	}

	//AT commands
	write_to_logger(fd, "echo \"AT&F\" > /dev/ttyS0\n");
	write_to_logger(fd, "echo \"AT+CFUN=1,1\" > /dev/ttyS0\n");

	printf("Pinging host %s ...\n", ip);
	write_to_logger(fd, qftpc_cmd);
	write(1, test_msg, sizeof(test_msg)); 
	
	if (!read_from_logger(fd, error_msg , 0, 25000000))
	{
		err = 1 - err;
	}
	else if (!read_from_logger(fd, ping_str, 0, 35000000))
   	{
		print_ok();
		return 0;
	}	

	write_to_logger(fd, "reset\n");
	print_fail();
	flush(fd);
	
	if (err)
	{
		printf("\033[0;31m");
		printf("%s", error_msg);
		printf("\033[0m");
	}
	else 
	{
		printf("Retest GSM? [y/n] ");
		scanf(" %c", &response);
		if (response == 'y' || response == 'Y')
		{
			return gsm_test(fd, bd_str, ping_str, i2c_fd, apn);
		}
	}

	return 1;
}

void 
inputs_config(int fd)
{
	char new_line[50] = "printf \"\\n\" >> /mnt/conf.cfg\n";
	char line[50];
	char buf[256];

	char cmd[][50] = 
	{
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
	
	for ( int i = 0; i < 12 ; i++ ) 
	{
		sprintf (line, "printf \"%s\" >> /mnt/conf.cfg\n", cmd[i]);
		write_to_logger(fd, line);
		write_to_logger(fd, new_line);
		read_from_logger(fd, NULL, 1, 500000);
	}

	printf("Inputs config written\n");
}

int 
reed_test(int fd) 
{
	char print[] = "Reed test -    ";
	char alarm_str[] ="AT+QPOWD=1";
	char reed_str[] = "Reed clicked for 3s.";
	
	printf("\n---------------------\n");
	printf("Wait for DUT to enter sleep mode...\n");
	if (!read_from_logger(fd, alarm_str, 1, 200000000))
	{
		printf("Turning magnet on\n");	
		write(1, print, sizeof(print));
		digitalWrite(22, HIGH);
		
		if (!read_from_logger(fd, reed_str, 1, 10000000))
		{
			print_ok();
			write_to_logger(fd, "reset\n");
			return 0;
		}
	   	else 
		{
			print_fail();
			printf("Activate reed ampule by hand\n");
			if (!read_from_logger(fd, reed_str, 1, 20000000))
			{
				write(1, print, sizeof(print));
				print_ok();
				write_to_logger(fd, "reset\n");
				return 0;
			}
		}
	}  

	write(1, print, sizeof(print));
	print_fail();
	write_to_logger(fd, "reset\n");
	return -1;	 
}

int 
generate_pulses(int fd, int i2c_fd) 
{
	char buf[20] = "Generate";
	char alarm_str[] = "Alarm Ampule-Reed received.";
	char print[] = "Inputs test -    ";

	printf("Starting alarm\n");
	write_to_logger(fd, "alarm -f\n");
	write(1, print, sizeof(print));
	if (!read_from_logger(fd, alarm_str, 1, 2000000))
	{
		write(i2c_fd, buf, strlen(buf));
		read_from_logger(fd, NULL, 1, 1500000);
			
		if (!write_to_logger(fd, "cat /dev/lptim1\n") 
			&& !read_from_logger(fd, "96", 1, 500000) 
			&& !write_to_logger(fd, "cat /dev/lptim2\n") 
			&& !read_from_logger(fd, "95", 1, 500000))
		{	
			print_ok();
			flush(fd);
			return 0;				
		} 
		else 
		{
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
	inputs_config(fd);
	return (generate_pulses(fd, i2c_fd) 
			|| reed_test(fd));
}

int 
soft_rev_check(int fd)
{
	write_to_logger(fd, "uname -a\n");
	return read_from_logger(fd, "0.4.27", 1 , 500000);
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
	printf("\033[0;31m");
	printf("FAIL\n");
	printf("\033[0m");
}	

void 
power_off(int fd)
{
	close(fd);
	digitalWrite(PWR, LOW);
	digitalWrite(JMP, LOW);
	digitalWrite(REED, LOW);
	digitalWrite(CAP, HIGH);
	printf("\nWait 30 seconds for cap to discharge\n");
	sleep(30);
	digitalWrite(CAP, LOW);
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
	//sleep(2);
	//time_t start;
	//time (&start);
	do 
	{
		;
	//	if (calculate_time(&start) > 3){
	//		printf("Running script\n");
	//		system("sudo /home/pi/sourceCodes/test/mount_nucleo");
	//	}		
	} while (open(NUCLEO_PATH, O_RDONLY) < 0);
}

void 
open_fds(int *src_fd, int *dst_fd )
{
//	if (*src_fd || *dst_fd ){
//		close(*src_fd);
//		close(*dst_fd);
//	}		
	*src_fd = open("/home/pi/sourceCodes/test/DL-MINI-BAT36-D2-3G-VB1.0-0.4.27.bin", O_RDONLY);		
	*dst_fd = open("/media/pi/NODE_L476RG/image.bin", O_CREAT | O_WRONLY);
	//if (*src_fd == -1 || *dst_fd == -1) perror("Unable to open.\n ");

}

