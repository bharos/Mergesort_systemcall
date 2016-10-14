#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include "sys_xmergesort.h"

asmlinkage extern long (*sysptr)(void *arg, int arg_size);

// Uncomment below line and compile again to execute system
// call in debug mode

//#define DEBUG		1

/*
 * Read "len" bytes from "filename" into "buf".
 * "buf" is in kernel space.
 */
struct file* file_open(const char* path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);

#ifdef DEBUG
		printk(KERN_ALERT"error is : %d ", err);
#endif

		return NULL;
	}
	return filp;
}
//Function to close the file
void file_close(struct file* file) {
	filp_close(file, NULL);
}

void correct_offset(unsigned long long* read_offset, int* read_buf_offset,
                    int* read_result_offset, int* read_buf_end)
{
	*read_offset = 0;
	*read_buf_offset = 0;
	*read_result_offset = 0;
	*read_buf_end = 0;
}

int file_read(struct file* file, unsigned long long read_offset,
              unsigned char* read_buf,
              int* read_buf_offset, unsigned char* read_result_buf,
              int* read_buf_end, int* read_result_offset)
{
	mm_segment_t oldfs;
	int finish_read = 0;
	char c;
	*read_result_offset = 0;
	oldfs = get_fs();
	set_fs(get_ds());


	while (finish_read == 0)
	{
		// First time or when we need to read more from file
		if (*read_buf_offset == *read_buf_end)
		{
			*read_buf_end = vfs_read(file, read_buf, PAGE_SIZE, &read_offset);
			*read_buf_offset = 0;
			if (*read_buf_end == 0)
			{
				goto exit_read;
			}

		}

		//Here, read_buf has some content
		c = read_buf[*read_buf_offset];
		*read_buf_offset += 1;

		read_result_buf[*read_result_offset] = c;
		*read_result_offset += 1;

		if (c == '\n')
		{

			finish_read = 1;
		}
	}
	//Exit code
exit_read:
	set_fs(oldfs);

	//return current offset in file
	return read_offset;

}
int file_write(struct file* file, unsigned long long offset,
               unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int write_to_buffer(char* read_buf, int num_bytes, char* write_buf,
                    struct file* output_file, unsigned long long*  offset,
                    int* file_out_offset, int* count)
{
	int i = 0;
	mm_segment_t oldfs;
	oldfs = get_fs();
	set_fs(get_ds());

	for (i = 0; i < num_bytes; i++)
	{
		if (*offset >= PAGE_SIZE)
		{

			//Flush to outfile and make offset 0
			*file_out_offset +=
			    file_write(output_file, (int) * file_out_offset, write_buf,
			               (int) * offset);
			*offset = 0;
		}
		write_buf[*offset] = read_buf[i];
		*offset += 1;
	}
	*count += 1;
	set_fs(oldfs);
	return 1;
}
int verify_arguments(void *arg)

{
	struct kstat inputFileStat, inputFileStat2, outputFileStat;
	int i = 0, j = 0;

	int return_value =  0;
	int return_value2 = 0;
	struct myargs *ptr = (struct myargs*)arg;

#ifdef DEBUG
	printk("In verify\n");
#endif
	if ((ptr-> flags) == 0)
	{
		return_value = -EINVAL;
		printk(KERN_ALERT"\nEither u or a flag should be set\n");
		goto check_fail;
	}

//Check if input files are NULL
	for (i = 0; i < ptr->fileCount - 1; i++)
	{

		if (ptr->inputFiles[i] == NULL ||
		        !(access_ok(VERIFY_READ,
		                    ptr->inputFiles[i], sizeof(ptr->inputFiles[i])))) {
			return_value = -EINVAL;
			goto check_fail;
		}
	}
	if (ptr->outputFile == NULL) {
		return_value = -EINVAL;
		goto check_fail;
	}

	//Verify the flags

	if ((ptr-> flags & UNIQUE_FLAG) == UNIQUE_FLAG  &&
	        (ptr->flags & ALL_RECORDS_FLAG) == ALL_RECORDS_FLAG)
	{
		return_value = -EINVAL;

		printk(KERN_ALERT"\nu and a flags should not be set at the same time\n");
		goto check_fail;
	}


	if (!((ptr-> flags & UNIQUE_FLAG) == UNIQUE_FLAG)  &&
	        !((ptr->flags & ALL_RECORDS_FLAG) == ALL_RECORDS_FLAG))
	{
		return_value = -EINVAL;
		printk(KERN_ALERT"Either of u or a flag should be set");
		goto check_fail;
	}

	return_value = vfs_stat(ptr->outputFile, &outputFileStat);

	if (return_value) {
		//Check if the error is not ENOENT (No such file or directory),
		//because we will create the output file afterwards
		//But any other error should be reported

		if (return_value != -ENOENT)
		{
			printk(KERN_ALERT "Failed to stat output file.");
			printk("Error %d \n", return_value);
			goto check_fail;
		}
#ifdef DEBUG
		printk("outfile ENOENT, but proceed\n");
#endif
	}


	if (return_value != -ENOENT && !(S_ISREG(outputFileStat.mode))) {

		printk(KERN_ALERT "Output file is not a regular file %d\n",
		       return_value);

		return_value = -EINVAL;
		goto check_fail;
	}


	if ((ptr->flags & RETURN_COUNT_FLAG) == RETURN_COUNT_FLAG)
	{

		if (ptr->data == NULL)
		{
			return_value = -ENOENT;
			goto check_fail;
		}
	}
	for (i = 0; i < ptr->fileCount - 1; i++)
	{

		//stat the input file
		return_value = vfs_stat(ptr->inputFiles[i], &inputFileStat);
		if (return_value) {

			printk(KERN_ALERT "Failed to stat input file %d.Errno %d \n",
			       i + 1, return_value);

			return_value = -EINVAL;
			goto check_fail;
		}
//Check if input files are regular files
		if (!(S_ISREG(inputFileStat.mode))) {
			printk(KERN_ALERT "The input file %d is not a regular file \n", i + 1);
			return_value = -EINVAL;
			goto check_fail;
		}
		//Check if output file is same as input file
		if ((inputFileStat.dev == outputFileStat.dev)
		        && (inputFileStat.ino == outputFileStat.ino)) {

			printk("Input file %d and output file are the same..Error.\n",
			       i + 1);

			return_value = -EINVAL;
			goto check_fail;
		}

		for (j = i + 1; j < ptr->fileCount - 1; j++)
		{
			//Check if input files are same
			return_value2 = vfs_stat(ptr->inputFiles[j], &inputFileStat2);
			if (return_value) {

				printk("Failed to stat input file %d.Errno %d \n",
				       i + 1, return_value);

				return_value = -EINVAL;
				goto check_fail;
			}

			if ((inputFileStat.dev == inputFileStat2.dev)
			        && (inputFileStat.ino == inputFileStat2.ino)) {

				printk("Input file %d and Input file %d are the same..Error.\n",
				       i + 1, j + 1);

				return_value = -EINVAL;
				goto check_fail;
			}
		}
	}

	return 0;
check_fail:
	return return_value;

}
int initialise_arguments(void* arg, struct myargs* ptr, int arg_size)
{
	struct filename *outFileName = NULL;
	struct filename **inFileNames = NULL;
	int i = 0;
	int return_value = 0;

	return_value = copy_from_user(ptr, arg, arg_size);

	if (return_value > 0)
	{
		return_value = -EINVAL;
		printk(KERN_ALERT "Copy from user failed \n");
		goto init_fail;
	}

	//Copy the data variable to contain the number of lines when flag d is set

	if ((ptr->flags & RETURN_COUNT_FLAG) == RETURN_COUNT_FLAG)
	{
		return_value = copy_from_user(ptr->data, ptr->data, sizeof(int));

		if (return_value > 0)
		{
			return_value = -EINVAL;
			printk(KERN_ALERT "Copy from user failed \n");
			goto init_fail;
		}
	}

	//Get output file name to kernel space
	outFileName = getname(ptr->outputFile);

	if (IS_ERR(outFileName)) {
		return_value = -EINVAL;
		printk(KERN_ALERT "getname for outfile failed !\n");
		goto init_fail;
	}

	ptr->outputFile = outFileName->name;

	inFileNames = kmalloc(sizeof(char *) * (ptr->fileCount - 1), GFP_KERNEL);

	//Get input file names to kernel space
	for (i = 0; i < ptr->fileCount - 1; i++)
	{

		inFileNames[i] = getname(ptr->inputFiles[i]);
		if (IS_ERR(inFileNames[i])) {
			printk(KERN_ALERT "getname for input file failed !\n");
			goto init_fail;
		}

	}

	for (i = 0; i < ptr->fileCount - 1; i++)
	{
		ptr->inputFiles[i] = inFileNames[i]->name;
	}


init_fail:
	if (inFileNames)
	{

#ifdef DEBUG
		printk(KERN_ALERT"Free inFileNames used for getname\n");
#endif

		kfree(inFileNames);
	}
	return return_value;
}

asmlinkage long xmergesort(void *arg, int arg_size)
{

	struct file* input_file1 = NULL;
	struct file* input_file2 = NULL;
	struct file* output_file = NULL;
	struct file* temp_file = NULL;
	struct file* temp_file_to_unlink = NULL;
	char* temp_file_name = NULL;
	char* temp_file_name1 = NULL;
	char* temp_file_name2 = NULL;
	char* temp_file_name_to_unlink = NULL;

	umode_t	input_mode;
	umode_t	min_input_mode = 65535; //Max range of short

	unsigned long long read_offset1 = 0;
	unsigned long long read_offset2 = 0;
	unsigned long long write_offset = 0;
	int file_out_offset = 0;
	char *read_buf1 = NULL, *read_buf2 = NULL;

	char *write_buf = NULL;
	int cmp_length = 0;
	int cmp_result = 0;
	int numRecords = 0;
	int return_value = 0;

	char* read_result_buf1 = NULL;
	char *read_result_buf2 = NULL;
	int read_buf_offset1 = 0;
	int read_buf_offset2 = 0;
	int read_result_offset1 = 0;
	int read_result_offset2 = 0;
	char* last_write_buf = NULL;
	int last_write_length = 0;
	int read_buf_end1 = 0;
	int read_buf_end2 = 0;

	struct myargs * ptr = NULL;
	mm_segment_t oldfs;
	int err;
	int i = 0;


	int cmp_with_last_value = 0;
	/*  -EINVAL for NULL */
	if (arg == NULL)
		return_value = -EINVAL;


	return_value = verify_arguments(arg);

	if (return_value < 0)
	{

#ifdef DEBUG
		printk("Value received after verify is %d\n", return_value);
#endif
		goto exit_to_user;
	}

	ptr = kmalloc(sizeof(struct myargs), GFP_KERNEL);

	return_value = initialise_arguments(arg, ptr, arg_size);

	if (return_value < 0)
	{

		printk(KERN_ALERT "Value received after init is %d", return_value);
		goto exit_to_user;
	}

#ifdef DEBUG
	printk("Verify and init success %d\n", return_value);
#endif

	//Memory allocation for read and write buffers
	read_buf1 = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (read_buf1 == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}
	read_buf2 = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (read_buf2 == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}
	write_buf = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (write_buf == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}
	read_result_buf1 = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (read_result_buf1 == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}
	read_result_buf2 = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (read_result_buf2 == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}
	last_write_buf = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (last_write_buf == NULL)
	{
		return_value = -ENOMEM;
		goto unlink_output_file;
	}


	i = 0;

	//Open the file to write output
	temp_file_name = (char*)kmalloc(strlen(ptr->outputFile) + 6, GFP_KERNEL);

	temp_file_name1 = (char*)kmalloc(strlen(ptr->outputFile) + 6, GFP_KERNEL);
	strcpy(temp_file_name1, ptr->outputFile);
	temp_file_name2 = (char*)kmalloc(strlen(ptr->outputFile) + 6, GFP_KERNEL);
	strcpy(temp_file_name2, ptr->outputFile);

	strcat(temp_file_name1, ".tmp1");


	strcat(temp_file_name2, ".tmp2");
#ifdef DEBUG
	printk("Temp name = %s  %s\n", temp_file_name1, temp_file_name2);
#endif
	while (i < ptr->fileCount - 1)
	{
		numRecords = 0;

		input_file1 = file_open(ptr->inputFiles[i], O_RDONLY, 0);

		input_mode = input_file1->f_path.dentry->d_inode->i_mode;
		if (input_mode < min_input_mode)
		{
			min_input_mode = input_mode;

		}
#ifdef DEBUG
		printk("\n Input file 1 : %s", ptr->inputFiles[i]);
#endif

		if (i == 0)
		{
			input_file2 = file_open(ptr->inputFiles[i + 1], O_RDONLY, 0);

			input_mode = input_file2->f_path.dentry->d_inode->i_mode;

			if (input_mode < min_input_mode)
			{
				min_input_mode = input_mode;
			}

			strcpy(temp_file_name, temp_file_name1);
#ifdef DEBUG
			printk("\n Input file 2 : %s", ptr->inputFiles[i + 1]);
#endif

			i += 2;
		}



		else
		{

			if (i % 2 == 0)
			{
				input_file2 = file_open(temp_file_name1, O_RDONLY , 0);
				strcpy(temp_file_name, temp_file_name2);

#ifdef DEBUG
				printk("\n Input file 2 : %s", temp_file_name1);
#endif

				input_mode = input_file2->f_path.dentry->d_inode->i_mode;
			}
			else
			{
				input_file2 = file_open(temp_file_name2, O_RDONLY , 0);
				strcpy(temp_file_name, temp_file_name1);

#ifdef DEBUG
				printk("\n Input file 2 : %s", temp_file_name2);
#endif
			}

			i++;
		}

		if (input_file1 == NULL || input_file2 == NULL)
		{
			printk(KERN_ALERT "Input file not found\n");
			return_value = -ENOENT;
			goto exit_to_user;
		}

		//Check if input files are the same

		temp_file = file_open(temp_file_name,
		                      O_RDWR | O_CREAT | O_TRUNC, min_input_mode);
		if (temp_file == NULL)
		{
			return_value  = -ENOENT;
			goto unlink_output_file;
		}

#ifdef DEBUG
		printk("Output file : %s \n", temp_file_name);
#endif

		read_result_offset1 = 1;

		read_result_offset2 = 1;

		read_offset1  = file_read(input_file1, read_offset1, read_buf1,
		                          &read_buf_offset1, read_result_buf1,
		                          &read_buf_end1, &read_result_offset1);

		read_offset2 = file_read(input_file2, read_offset2,
		                         read_buf2, &read_buf_offset2, read_result_buf2,
		                         &read_buf_end2, &read_result_offset2);


		while (read_result_offset1 > 0 && read_result_offset2 > 0)

		{

			if (read_result_offset1 > read_result_offset2)
			{
				cmp_length = read_result_offset1;

			}
			else {
				cmp_length = read_result_offset2;

			}
			//Check if compare case-insensitive flag(i) is set
			if ((ptr->flags & CASE_INSENSITIVE_FLAG) == CASE_INSENSITIVE_FLAG)
			{
				cmp_result = strncasecmp(read_result_buf1,
				                         read_result_buf2,
				                         cmp_length);
			}
			//Else normal string compare
			else {
				cmp_result = strncmp(read_result_buf1,
				                     read_result_buf2,
				                     cmp_length);
			}

			//String from first file is lesser in alphabetical order
			if (cmp_result < 0)
			{
				//If unique flag is set,

				if ((ptr->flags & CASE_INSENSITIVE_FLAG) == CASE_INSENSITIVE_FLAG)
				{
					cmp_with_last_value =
					    strncasecmp(last_write_buf, read_result_buf1,
					                read_result_offset1 > last_write_length ?
					                last_write_length : read_result_offset1);
				}
				else {
					cmp_with_last_value =
					    strncmp(last_write_buf, read_result_buf1,
					            read_result_offset1 > last_write_length ?
					            last_write_length : read_result_offset1);
				}

				if (cmp_with_last_value > 0)
				{

					if ((ptr->flags & CHECK_SORT_FLAG) == CHECK_SORT_FLAG)
					{
						printk("Input file unsorted.Error \n");
						return_value = -EINVAL;
						goto unlink_output_file;
					}
					else
					{
#ifdef DEBUG
						printk("\n####COnflict 1!  ");
#endif

						read_offset1  =
						    file_read(input_file1, read_offset1, read_buf1,
						              &read_buf_offset1, read_result_buf1, &read_buf_end1,
						              &read_result_offset1);

					}

				}
				else if ((ptr->flags & UNIQUE_FLAG) == UNIQUE_FLAG &&
				         last_write_length == read_result_offset1 &&
				         cmp_with_last_value == 0)
				{
#ifdef DEBUG
					printk("\n####Duplicate 1!  ");
#endif
					read_offset1  =
					    file_read(input_file1, read_offset1, read_buf1,
					              &read_buf_offset1, read_result_buf1, &read_buf_end1,
					              &read_result_offset1);
				}
				else {
					write_to_buffer(read_result_buf1, read_result_offset1,
					                write_buf, temp_file, &write_offset, &file_out_offset,
					                &numRecords);

					strncpy(last_write_buf, read_result_buf1, read_result_offset1);

					last_write_length = read_result_offset1;

					read_offset1  =
					    file_read(input_file1, read_offset1,
					              read_buf1, &read_buf_offset1, read_result_buf1,
					              &read_buf_end1, &read_result_offset1);
				}


			}
			//String from second file is lesser or same in alphabetical order
			else //if(cmp_result > 0)
			{

				if ((ptr->flags & CASE_INSENSITIVE_FLAG) == CASE_INSENSITIVE_FLAG)
				{
					cmp_with_last_value =
					    strncasecmp(last_write_buf, read_result_buf2,
					                read_result_offset2 > last_write_length ?
					                last_write_length : read_result_offset2);
				}
				else {
					cmp_with_last_value =
					    strncmp(last_write_buf, read_result_buf2,
					            read_result_offset2 > last_write_length ?
					            last_write_length : read_result_offset2);
				}
				if (cmp_with_last_value > 0)
				{

					if ((ptr->flags & CHECK_SORT_FLAG) == CHECK_SORT_FLAG )
					{
						printk("Input file unsorted.Error \n");
						return_value = -EINVAL;
						goto unlink_output_file;

					}

					else
					{
#ifdef DEBUG
						printk("\n####COnflict 2! ");
#endif

						read_offset2 =
						    file_read(input_file2, read_offset2,
						              read_buf2, &read_buf_offset2, read_result_buf2,
						              &read_buf_end2, &read_result_offset2);


					}

				}
				else if ((ptr->flags & UNIQUE_FLAG) == UNIQUE_FLAG &&
				         last_write_length == read_result_offset2 &&
				         cmp_with_last_value == 0)
				{
#ifdef DEBUG
					printk("\n####Duplicate 2!  ");
#endif

					read_offset2 =
					    file_read(input_file2, read_offset2,
					              read_buf2, &read_buf_offset2, read_result_buf2,
					              &read_buf_end2, &read_result_offset2);

				} else {
					write_to_buffer(read_result_buf2, read_result_offset2,
					                write_buf, temp_file, &write_offset, &file_out_offset,
					                &numRecords);

					strncpy(last_write_buf, read_result_buf2, read_result_offset2);

					last_write_length = read_result_offset2;

					read_offset2 =
					    file_read(input_file2, read_offset2, read_buf2,
					              &read_buf_offset2, read_result_buf2, &read_buf_end2,
					              &read_result_offset2);
				}


			}
		}

#ifdef DEBUG
		printk("LOOP END %d  %d \n", read_result_offset1, read_result_offset2);
#endif

		while (read_result_offset1 > 0)
		{
			if ((ptr->flags & CASE_INSENSITIVE_FLAG) == CASE_INSENSITIVE_FLAG)
			{
				cmp_with_last_value =
				    strncasecmp(last_write_buf, read_result_buf1,
				                read_result_offset1 > last_write_length ?
				                last_write_length : read_result_offset1);
			}
			else {
				cmp_with_last_value =
				    strncmp(last_write_buf, read_result_buf1,
				            read_result_offset1 > last_write_length ?
				            last_write_length : read_result_offset1);
			}

			if (cmp_with_last_value > 0)
			{

				if ((ptr->flags & CHECK_SORT_FLAG) == CHECK_SORT_FLAG)
				{
					printk("Input file unsorted.Error \n");
					return_value = -EINVAL;
					goto unlink_output_file;
				}
				else
				{
#ifdef DEBUG
					printk("\n####COnflict 3!  ");
#endif

					read_offset1  =
					    file_read(input_file1, read_offset1, read_buf1,
					              &read_buf_offset1, read_result_buf1,
					              &read_buf_end1, &read_result_offset1);

				}

			}
			else if ((ptr->flags & UNIQUE_FLAG) == UNIQUE_FLAG &&
			         last_write_length == read_result_offset1 &&
			         cmp_with_last_value == 0)
			{
#ifdef DEBUG
				printk("\n####Duplicate 3!  ");
#endif


				read_offset1  =
				    file_read(input_file1, read_offset1, read_buf1,
				              &read_buf_offset1, read_result_buf1, &read_buf_end1,
				              &read_result_offset1);
			}
			else {
				write_to_buffer(read_result_buf1, read_result_offset1,
				                write_buf, temp_file, &write_offset, &file_out_offset,
				                &numRecords);

				strncpy(last_write_buf, read_result_buf1, read_result_offset1);
				last_write_length = read_result_offset1;

				read_offset1  =
				    file_read(input_file1, read_offset1, read_buf1,
				              &read_buf_offset1, read_result_buf1, &read_buf_end1,
				              &read_result_offset1);
			}

		}

		while (read_result_offset2 > 0)
		{
			if ((ptr->flags & CASE_INSENSITIVE_FLAG) == CASE_INSENSITIVE_FLAG)
			{
				cmp_with_last_value =
				    strncasecmp(last_write_buf, read_result_buf2,
				                read_result_offset2 > last_write_length ?
				                last_write_length : read_result_offset2);
			}
			else {
				cmp_with_last_value =
				    strncmp(last_write_buf, read_result_buf2,
				            read_result_offset2 > last_write_length ?
				            last_write_length : read_result_offset2);
			}
			if (cmp_with_last_value > 0)
			{

				if ((ptr->flags & CHECK_SORT_FLAG) == CHECK_SORT_FLAG )
				{
					printk("Input file unsorted.Error \n");
					return_value = -EINVAL;
					goto unlink_output_file;

				}

				else
				{
#ifdef DEBUG
					printk("\n####COnflict 4! ");
#endif

					read_offset2 =
					    file_read(input_file2, read_offset2,
					              read_buf2, &read_buf_offset2, read_result_buf2,
					              &read_buf_end2, &read_result_offset2);


				}

			}
			else if ((ptr->flags & UNIQUE_FLAG) == UNIQUE_FLAG &&
			         last_write_length == read_result_offset2 &&
			         cmp_with_last_value == 0)
			{
#ifdef DEBUG
				printk("\n####Duplicate 4 !  ");
#endif
				read_offset2 =
				    file_read(input_file2, read_offset2,
				              read_buf2, &read_buf_offset2, read_result_buf2,
				              &read_buf_end2, &read_result_offset2);
			}
			else
			{
				write_to_buffer(read_result_buf2, read_result_offset2,
				                write_buf, temp_file, &write_offset,
				                &file_out_offset, &numRecords);

				strncpy(last_write_buf, read_result_buf2, read_result_offset2);
				last_write_length = read_result_offset2;

				read_offset2 =
				    file_read(input_file2, read_offset2, read_buf2,
				              &read_buf_offset2, read_result_buf2, &read_buf_end2,
				              &read_result_offset2);
			}
		}

		//If there is some content remaining in write buffer,
		//write it to the output file
		file_write(temp_file, file_out_offset, write_buf, write_offset);


		correct_offset(&read_offset1, &read_buf_offset1
		               , &read_result_offset1, &read_buf_end1);
		correct_offset(&read_offset2, &read_buf_offset2,
		               &read_result_offset2, &read_buf_end2);
		write_offset = 0;
		file_out_offset = 0;
		last_write_length = 0;

//Cleanups in current loop

		if (input_file1)
		{
#ifdef DEBUG
			printk("Free input_file1");
#endif
			file_close(input_file1);
			input_file1 = NULL;
		}
		if (input_file2)
		{
#ifdef DEBUG
			printk("Free input_file2");
#endif
			file_close(input_file2);
			input_file2 = NULL;
		}

		if (temp_file)
		{
#ifdef DEBUG
			printk("Free temp file");
#endif
			file_close(temp_file);
			temp_file = NULL;
		}

	}

	//If data flag is set,
	//copy number of lines written to user provided variable
	if ((ptr->flags & RETURN_COUNT_FLAG) == RETURN_COUNT_FLAG)
	{

		if (copy_to_user(ptr->data, &numRecords, sizeof(int)))
		{
			printk(KERN_ALERT "Copy to user failed..");
			return_value = -EFAULT;
			goto unlink_output_file;
		}
	}

	goto rename_temp_file; //Success

// Merge process success .We need to rename temp file to output file.
rename_temp_file:
#ifdef DEBUG
	printk("\nIn rename  %s  \n", temp_file_name);
#endif

//Check if there were more than 2 input files
	if (ptr->fileCount - 1 > 2)
	{
#ifdef DEBUG
		printk("More than 2 i/p files\n");
#endif

		temp_file_name_to_unlink =
		    (char*)kmalloc(strlen(ptr->outputFile) + 6, GFP_KERNEL);

		strcpy(temp_file_name_to_unlink, ptr->outputFile);

		if ((ptr->fileCount - 1) % 2 == 0)
		{
			strcat(temp_file_name_to_unlink, ".tmp2");
		}
		else
		{
			strcat(temp_file_name_to_unlink, ".tmp1");
		}


		if (temp_file_name_to_unlink)
		{
#ifdef DEBUG
			printk("\n To unlink :  %s\n", temp_file_name_to_unlink);
#endif


			temp_file_to_unlink =
			    file_open(temp_file_name_to_unlink,
			              O_RDWR | O_CREAT, min_input_mode);
			if (temp_file_to_unlink == NULL)
			{
				return_value  = -ENOENT;
				goto exit_to_user;
			}

			oldfs = get_fs();
			set_fs(KERNEL_DS);

			err = vfs_unlink(temp_file_to_unlink->f_path.dentry->d_parent->d_inode,
			                 temp_file_to_unlink->f_path.dentry, NULL);
			if (err) {

				printk(KERN_ALERT "Error while unlinking.. Errno: %d \n", err);


			}

			set_fs(oldfs);


		}

	}
	else
	{
#ifdef DEBUG
		printk("\nNo unlink !\n");
#endif
	}



	temp_file = file_open(temp_file_name, O_RDWR, min_input_mode);
	if (temp_file == NULL)
	{
		return_value  = -ENOENT;
		goto unlink_output_file;
	}

	output_file =
	    file_open(ptr->outputFile, O_RDWR | O_CREAT, min_input_mode);

	if (output_file == NULL)
	{
		return_value  = -ENOENT;
		printk(KERN_ALERT "Failed to open output file\n");
		goto exit_to_user;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	err = vfs_rename(
	          temp_file->f_path.dentry->d_parent->d_inode,
	          temp_file->f_path.dentry,
	          output_file->f_path.dentry->d_parent->d_inode,
	          output_file->f_path.dentry, NULL, 0);

	if (err) {
		printk(KERN_ALERT
		       "Rename operation from temp file to output file failed.");
		printk("Errno: %d \n", err);
		goto unlink_output_file;
	}

	set_fs(oldfs);

	goto exit_to_user;

//Merge process failed somewhere,
//we need to unlink the temp file so that there won't be any output file
unlink_output_file:

#ifdef DEBUG
	printk("In Unlink");
#endif


//Check if there were more than 2 input files
	if (ptr->fileCount - 1 > 2)
	{
#ifdef DEBUG
		printk("More than 2 i/p files\n");
#endif
		temp_file_name_to_unlink =
		    (char*)kmalloc(strlen(ptr->outputFile) + 6, GFP_KERNEL);

		strcpy(temp_file_name_to_unlink, ptr->outputFile);

		if ((ptr->fileCount - 1) % 2 == 0)
		{
			strcat(temp_file_name_to_unlink, ".tmp2");
		}
		else
		{
			strcat(temp_file_name_to_unlink, ".tmp1");
		}


		if (temp_file_name_to_unlink)
		{
#ifdef DEBUG
			printk("\n To unlink :  %s\n", temp_file_name_to_unlink);
#endif

			temp_file_to_unlink =
			    file_open(temp_file_name_to_unlink,
			              O_RDWR | O_CREAT, 644);

			if (temp_file_to_unlink == NULL)
			{
				return_value  = -ENOENT;
				goto exit_to_user;
			}

			oldfs = get_fs();
			set_fs(KERNEL_DS);

			err = vfs_unlink(temp_file_to_unlink->f_path.dentry->d_parent->d_inode,
			                 temp_file_to_unlink->f_path.dentry, NULL);
			if (err) {

				printk(KERN_ALERT "Error while unlinking.. Errno: %d \n", err);


			}

			set_fs(oldfs);


		}

	}
	else
	{
#ifdef DEBUG
		printk("\nNo unlink !\n");
#endif
	}




	oldfs = get_fs();
	set_fs(KERNEL_DS);

	err = vfs_unlink(temp_file->f_path.dentry->d_parent->d_inode,
	                 temp_file->f_path.dentry, NULL);
	if (err) {

		printk(KERN_ALERT "Error while unlinking.. Errno: %d \n", err);
	}

	set_fs(oldfs);

exit_to_user:

	if (input_file1)
	{
#ifdef DEBUG
		printk("  close input_file1  ");
#endif
		file_close(input_file1);
	}
	if (input_file2)
	{
#ifdef DEBUG
		printk(" Close input_file2  ");
#endif
		file_close(input_file2);
	}
	if (output_file)
	{
#ifdef DEBUG
		printk("  close output_file ");
#endif
		file_close(output_file);
	}
	if (temp_file)
	{
#ifdef DEBUG
		printk(" Close temp file ");
#endif
		file_close(temp_file);
	}
	if (temp_file_to_unlink)
	{
#ifdef DEBUG
		printk(" Close temp_file_to_unlink \n");
#endif
		file_close(temp_file_to_unlink);
	}
	if (temp_file_name)
	{
		kfree(temp_file_name);
	}
	if (temp_file_name1)
	{
		kfree(temp_file_name1);
	}
	if (temp_file_name2)
	{
		kfree(temp_file_name2);
	}
	if (temp_file_name_to_unlink)
	{
#ifdef DEBUG
		printk(" free temp_file_to_unlink ");
#endif
		kfree(temp_file_name_to_unlink);
	}
	if (read_buf1)
	{
#ifdef DEBUG
		printk(" Free read_buf1 ");
#endif
		kfree(read_buf1);
	}
	if (read_buf2)
	{
#ifdef DEBUG
		printk(" Free read_buf2 ");
#endif
		kfree(read_buf2);
	}
	if (read_result_buf1)
	{
#ifdef DEBUG
		printk(" Free read_result_buf1 ");
#endif
		kfree(read_result_buf1);
	}
	if (read_result_buf2)
	{
#ifdef DEBUG
		printk(" Free read_result_buf2 ");
#endif
		kfree(read_result_buf2);
	}
	if (write_buf)
	{
#ifdef DEBUG
		printk(" Free write_buf ");
#endif
		kfree(write_buf);
	}
	if (last_write_buf)
	{
#ifdef DEBUG
		printk(" Free last_write_buf ");
#endif
		kfree(last_write_buf);
	}
	if (ptr)
	{
		kfree(ptr);
	}
#ifdef DEBUG
	printk("return value is :%d \n", return_value);
#endif

	return return_value;
}

static int __init init_sys_xmergesort(void)
{
	printk("installed new sys_xmergesort module\n");
	if (sysptr == NULL)
		sysptr = xmergesort;
	return 0;
}
static void  __exit exit_sys_xmergesort(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xmergesort module\n");
}
module_init(init_sys_xmergesort);
module_exit(exit_sys_xmergesort);
MODULE_LICENSE("GPL");

