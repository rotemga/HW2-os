#define _GNU_SOURCE

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h> // for open flags
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


#define FILE_SIZE 128*1024*1024
#define MB 1024*1024
#define KB 1024



int randomIndex(int write_size);
int fillRandom(int fd, char* buf);
int fillBufRandom(char* randombuf);

int main(int argc, char** argv)
{
	if (argc != 4){
		printf("Error: Wrong number of input arguments in command line\n");
		return;
	}



	int ok = 1, fd, i = 0;
	int write_size = atoi(argv[3]);
	int o_direct = atoi(argv[2]);
	char *path_file = argv[1];
	static char buf[1024 * 1024] __attribute__((__aligned__(4096)));// Allocate a 1MB buffer





	struct stat statbuf;
	int statReturn = stat(path_file, &statbuf);

	if ((statbuf.st_mode & S_IFMT) != S_IFBLK)//Check whether the input file is a block device
	{

		//Make sure the input file exists and is of size exactly 128MB.

		//The input file does not exists
		if ((statReturn < 0) || (errno == ENOENT)){
			fd = open(path_file, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO); //create it

			if (fd < 0){
				printf("Error opening file: %s\n", strerror(errno));
				return errno;
			}

			if (fillRandom(fd, buf) == -1){ //write random data to it until it has 128MB of data.
				exit(0);
			}
			close(fd);
		}
		//if the file is symbolic link or has additional hard links
		else if (((statbuf.st_mode & S_IFMT) == S_IFLNK) || (statbuf.st_nlink > 1)){
			printf("Error: The file is a symbolic link or has additional hard links\n");
			exit(0);

		}


		//If it exists, verify it is of the correct size. If it isn’t, truncate it, and write to it random data to it until it has 128MB of data.
		else if (statbuf.st_size != FILE_SIZE){


			fd = open(path_file, O_TRUNC | O_RDWR);


			if (fd < 0){
				printf("Error opening file: %s\n", strerror(errno));
				return errno;
			}
			if (fillRandom(fd, buf) == -1){
				exit(0);
			}
			close(fd);


		}




	}
	// fill the alocated buffer with random data
	if (fillBufRandom(buf) == -1){
		exit(0);
	}

	struct timeval start, end;
	long mtime, seconds, useconds;
	// start time measurement
	gettimeofday(&start, NULL);

	//Open the input file with or without the O_DIRECT flag as indicated by the 2nd argument
	if (o_direct == 1){
		fd = open(path_file, O_RDWR | O_DIRECT, S_IRWXU | S_IRWXG | S_IRWXO);
		if (fd < 0){
			printf("Error opening file: %s\n", strerror(errno));
			return errno;
		}
	}
	else if (o_direct == 0){
		fd = open(path_file, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
		if (fd < 0){
			printf("Error opening file: %s\n", strerror(errno));
			return errno;
		}
	}



	//repeatedly write from the buffer to the file, at random locations in the file
	double div = (double)(KB / (double)write_size);
	int iterNum = (int)(div * 128);//number of writes until writing 128MB of total data to the file
	int offset;

	for (i = 0; i < iterNum; i++){
		
		offset = randomIndex(write_size); // a random aligned offset (i.e., multiples of the write size)
		if (lseek(fd, offset, SEEK_SET) != offset){ //lseek system call with a random offset.
			printf("Error in lseek function: %s\n", strerror(errno));
			return errno;
		}
		if (write(fd, buf, (write_size * KB)) != (write_size * KB)){//a write with write length according to the write size.
			printf("Error in write function: %s\n", strerror(errno));
			return errno;
		}
	
	}




	close(fd);
	// end time measurement and print result
	gettimeofday(&end, NULL);
	seconds = end.tv_sec - start.tv_sec;
	useconds = end.tv_usec - start.tv_usec;
	mtime = ((seconds)* 1000 + useconds / 1000.0) + 0.5;

	//For every run, it should print the throughput of the run, i.e., how much MB/second did the program write, on average:
	double throughput = (128 * 1000) / (double)mtime;
	printf("The throughput of the run: %lf\n", throughput); //delete print iterNum, div, milliseconds


	return 0;


}
//This function find a random aligned offset (i.e., multiples of the write size)
int randomIndex(int write_size){
	int res, ok = 1;
	int ws_kb = write_size*KB;
	res = random();
	res = res % FILE_SIZE;
	res = res - (res % ws_kb);
	return res;
}

//write 128MB of random data, to the file with fd. Return -1 on error, 0 in succsess.
int fillRandom(int fd, char* buf){
	int i, j;
	for (i = 0; i < 128; i++){
		if (fillBufRandom(buf) == -1){
			return -1;
		}
		if (write(fd, buf, MB) != MB){
			printf("Error in write function: %s\n", strerror(errno));
			return -1;
		}
	}
	return 0;
}
//fill buffer that has size 1MB with random data. Return -1 on error, 0 in succsess.
int fillBufRandom(char* randombuf){
	int j;
	for (j = 0; j < (4 * KB); j++){ //init randombuf that has size 1MB=(1024^2)bytes, with random data.
		if (initstate(time(NULL), &(randombuf[j]), 256) == NULL) {
			printf("Error: initstate function\n");
			return -1;
		}
	}
	return 0;

}




