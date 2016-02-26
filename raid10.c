
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h> // for open flags
#include <assert.h>
#include <errno.h>
#include <string.h>

#define DEVICE_SIZE (1024*1024*256) // assume all devices identical in size

#define SECTOR_SIZE 512
#define SECTORS_PER_BLOCK 4
#define BLOCK_SIZE (SECTOR_SIZE * SECTORS_PER_BLOCK)

#define BUFFER_SIZE (BLOCK_SIZE * 2)

char	buf[BUFFER_SIZE];
int		num_dev, num_dev0, num_dev1;
int		*dev_fd;

void killDevice(int index0, int index1);
int findNotFaulty_seek(int dev0_num, int index1, int offset);
int findNotFaulty_r(int dev0_num, int index1, int size, int offset);
void do_raid10_rw(char* operation, int sector, int count);
int do_raid10_w(int dev0_num, int index1, int size, int offset, int sector_start, int sector_end);
void do_repear(int dev_num, char* new_dev);
int findNotFaultyDevice_repear(int index0, int index1, int dev_num);


int main(int argc, char** argv)
{
	if (!(argc > 3)){
		printf("Error: Wrong number of input arguments in command line\n");
		return;
	}
	int i, index0, index1;
	char line[1024];

	// number of devices == number of arguments (ignore 1st,2st)
	num_dev = argc - 2;
	int _dev_fd[num_dev];
	dev_fd = _dev_fd;


	num_dev1 = atoi(argv[1]);
	num_dev0 = num_dev / num_dev1;


	// open all devices
	for (i = 0; i < num_dev; ++i) {
		printf("Opening device %d: %s\n", i, argv[i + 2]);
		dev_fd[i] = open(argv[i + 2], O_RDWR);
		if (!(dev_fd[i] >= 0)){
			printf("Error opening device %d: %s\n:", i, strerror(errno));
			close(dev_fd[i]);
			dev_fd[i] = -1;

		}
	}



	// vars for parsing input line
	char operation[20];
	char third[100];
	int sector;
	int count_int;
	char *device;

	// read input lines to get command of type "OP <SECTOR> <COUNT>"
	while (fgets(line, 1024, stdin) != NULL) {
		if (!(sscanf(line, "%s %d %s", operation, &sector, third) == 3)){
			printf("Error: Wrong number of input arguments\n");
			return;
		}

		count_int = atoi(third);
		device = third;



		// KILL specified device
		if (!strcmp(operation, "KILL")) {
			close(dev_fd[sector]);
			dev_fd[sector] = -1;

		}

		// REPAIR
		else if (!strcmp(operation, "REPAIR")){
			do_repear(sector, device);
		}
		// READ / WRITE
		else {
			do_raid10_rw(operation, sector, count_int);
		}
	}


	for (i = 0; i < num_dev; i++) {
		if (dev_fd[i] >= 0)
			close(dev_fd[i]);
	}
}




void do_raid10_rw(char* operation, int sector, int count)
{
	int i = sector;
	int index1, ok = 1;

	while (i < sector + count)
	{
		index1 = 0;

		// find the relevant device for current sector
		int block_num = i / SECTORS_PER_BLOCK;
		int dev0_num = block_num % num_dev0;

		// make sure device didn't fail
		while (dev_fd[(dev0_num * num_dev1) + index1] == -1) {
			index1++;
			if (index1 > num_dev1 - 1){
				printf("Operation on bad device \n");
				return;
			}
		}


		// find offset of sector inside device
		int block_start = i / (num_dev0 * SECTORS_PER_BLOCK);
		int block_off = i % SECTORS_PER_BLOCK;
		int sector_start = block_start * SECTORS_PER_BLOCK + block_off;
		int offset = sector_start * SECTOR_SIZE;

		// try to write few sectors at once
		int num_sectors = SECTORS_PER_BLOCK - block_off;
		while (i + num_sectors > sector + count)
			--num_sectors;
		int sector_end = sector_start + num_sectors - 1;
		int size = num_sectors * SECTOR_SIZE;

		// validate calculations
		if ((!(num_sectors > 0)) || (!(size <= BUFFER_SIZE)) || (!(offset + size <= DEVICE_SIZE))){
			printf("Error: invalid input\n");
			return;
		}

		// seek in relevant device
		if (!(offset == lseek(dev_fd[(dev0_num * num_dev1) + index1], offset, SEEK_SET))){
			index1 = findNotFaulty_seek(dev0_num, index1, offset);
			if (index1 == -1){
				printf("Operation on bad device\n");
				return;
			}
		}

		if (!strcmp(operation, "READ")){
			if (!(size == read(dev_fd[(dev0_num * num_dev1) + index1], buf, size))){
				index1 = findNotFaulty_r(dev0_num, index1, size, offset);
			}
			if (index1 != -1){
				printf("Operation on device %d, sector %d-%d\n",
					(dev0_num * num_dev1) + index1, sector_start, sector_end);
			}
		}
		else if (!strcmp(operation, "WRITE")){
			index1 = do_raid10_w(dev0_num, index1, size, offset, sector_start, sector_end);
		}
		if (index1 == -1){
			printf("Operation on bad device\n");
			return;
		}


		i += num_sectors;
	}
}

//This function execute writing. return -1 if write failed.
int do_raid10_w(int dev0_num, int index1, int size, int offset, int sector_start, int sector_end){
	int cnt_writings = 0;
	while (index1 < num_dev1){
		if (dev_fd[(dev0_num * num_dev1) + index1] == -1){//skip faulty devices.
			index1++;
			continue;
		}
		else if (!(offset == lseek(dev_fd[(dev0_num * num_dev1) + index1], offset, SEEK_SET))){//if lseek function fail, find other device.
			index1 = findNotFaulty_seek(dev0_num, index1, offset);
		}
		else if (!(size == write(dev_fd[(dev0_num * num_dev1) + index1], buf, size))){//if write function fail, kill the device.
			printf("Error writing to device %d: %s\n", (dev0_num * num_dev1) + index1, strerror(errno));
			killDevice(dev0_num, index1);
		}
		else{//nothing failed, lseek and write function worked on not faulty device.
			printf("Operation on device %d, sector %d-%d\n",
				(dev0_num * num_dev1) + index1, sector_start, sector_end);
			cnt_writings++;
		}
		index1++;

	}
	if (cnt_writings == 0){//the function didn't write to any device, so the operation failed.
		return -1;
	}
	else{
		return 0;
	}
}

//This function kill device, close it and put -1 in the array.
void killDevice(int index0, int index1){
	int indx = (index0 * num_dev1) + index1;
	close(dev_fd[indx]);
	dev_fd[indx] = -1;
}


//This function find the first device that lseek doesn't fail. return -1 if it didn't find.
int findNotFaulty_seek(int dev0_num, int index1, int offset){
	while (index1 < num_dev1){
		if (dev_fd[(dev0_num * num_dev1) + index1] == -1){
			index1++;
			continue;
		}
		else if (!(offset == lseek(dev_fd[(dev0_num * num_dev1) + index1], offset, SEEK_SET))){//if lseek doesn't work, kill the device.
			printf("Error lseek in device %d: %s\n", (dev0_num * num_dev1) + index1, strerror(errno));
			killDevice(dev0_num, index1);
			index1++;
		}
		else{
			break;
		}
	}
	if (index1 >= num_dev1)//we didn't find device.
		return -1;
	return index1;
}

//This function find the first device that read doesn't fail. return -1 if it didn't find.
int findNotFaulty_r(int dev0_num, int index1, int size, int offset){
	while (index1 < num_dev1){
		if (dev_fd[(dev0_num * num_dev1) + index1] == -1){
			index1++;
			continue;
		}
		else if (!(offset == lseek(dev_fd[(dev0_num * num_dev1) + index1], offset, SEEK_SET))){//if lseek function fail, find other device.
			index1 = findNotFaulty_seek(dev0_num, index1, offset);
			if (index1 == -1){
				return -1;
			}
		}

		else if (!(size == read(dev_fd[(dev0_num * num_dev1) + index1], buf, size))){//if read doesn't work, kill the device.
			printf("Error reading from device %d: %s\n", (dev0_num * num_dev1) + index1, strerror(errno));
			killDevice(dev0_num, index1);
			index1++;
		}
		else{
			break;
		}


	}
	if (index1 >= num_dev1)//we didn't find device.
		return -1;
	return index1;
}

//This is the repear function
void do_repear(int dev_num, char* new_dev){
	int fd = open(new_dev, O_RDWR);
	if (fd < 0){
		printf("Error opening device %s: %s\n", new_dev, strerror(errno));
		return;
	}
	int index0, index1, offset = 0, for_read, for_write, dev_num_copy = dev_num;

	if (dev_fd[dev_num] != -1){//we assume the original device is faulty. 
		index0 = dev_num_copy / num_dev1;
		index1 = dev_num_copy % num_dev1;
		killDevice(index0, index1);
	}

	while (DEVICE_SIZE-offset > 0){
		index0 = dev_num_copy / num_dev1;
		index1 = dev_num_copy % num_dev1;

		if (dev_fd[dev_num_copy] == -1){

			//find not faulty disk from the same RAID1 
			dev_num_copy = findNotFaultyDevice_repear(index0, index1, dev_num_copy);

			if (dev_num_copy == -1){
				break;
			}
			index1 = dev_num_copy % num_dev1;
		}

		if (lseek(dev_fd[dev_num_copy], offset, SEEK_SET) != offset){
			printf("Error lseek in device %d: %s\n", dev_num_copy, strerror(errno));
			killDevice(index0, index1);
			continue;
		}
		for_read = read(dev_fd[dev_num_copy], buf, BUFFER_SIZE);
		if (for_read == -1){
			printf("Error reading from device %d: %s\n", dev_num_copy, strerror(errno));
			killDevice(index0, index1);
			continue;
		}
		else if (for_read == 0){
			break;
		}

		for_write = write(fd, buf, for_read);

		if ((for_write == -1) || (for_write < for_read)){
			close(fd);
			printf("Error writing to device %s: %s\n", new_dev, strerror(errno));
			return;

		}

		offset += for_read;


	}
	dev_fd[dev_num] = fd;


}


//find not faulty disk from the same RAID1 , for repear.
int findNotFaultyDevice_repear(int index0, int index1, int dev_num){
	int i;
	for (i = index1 - 1; 0 < i; i--){
		if (dev_fd[i] != -1){
			return (index0*num_dev1 + i);
		}
	}
	for (i = index1 + 1; i < num_dev1; i++){
		if (dev_fd[i] != -1){
			return (index0*num_dev1 + i);
		}
	}

	return -1;
}