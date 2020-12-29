#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

//Added functions
int copy_file(const char *src, const char *dst, mode_t permissions)
{
    //read and open are of type ssize_t ¯\_(ツ)_/¯
    int src_descriptor;
    int dst_descriptor;
    char buffer[4096];
    ssize_t file_reader;
    ssize_t file_writer;

    //Try to open the source file...
    //We only want to read the source file so use the O_RDONLY flag
    src_descriptor = open(src, O_RDONLY);

    if (src_descriptor < 0)
    {
        syserror(open, src);
    }

    //Now open the destination file...
    //We want to write, and create the file, if the file already exists then overwrite it
    //Don't forget to add all the permissions
    dst_descriptor = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC);

    if (dst_descriptor < 0)
    {
        syserror(open, src);
    }

    //Now go through the source file and copy its contents to the new destination file
    //To do this read 4096 bytes at a time into the buffer, if it fails file_reader becomes negative
    //File reader contains the number of bytes read
    while ((file_reader = read(src_descriptor, buffer, sizeof(buffer))) > 0)
    {
        //Now that part of the file is in the buffer we need to write it to file
        ssize_t written = 0;
        char *writing_buffer = buffer;

        //Continue writing to the file until the amount written is equal to the amount
        //read in by the file_reader
        while (written < file_reader)
        {
            //File writer will contain the number of bytes written to the destination file
            //However you have to move the pointer to the buffer by the amount written
            file_writer = write(dst_descriptor, writing_buffer, file_reader - written);

            //An error has occurred then throw an error
            if (file_writer < 0)
            {
                syserror(write, dst);
            }

            if (file_writer >= 0)
            {
                //Increase the written counter
                //This basically just tells us how much we've written so far
                written += file_writer;
                writing_buffer += written;
            }
        }
    }

    //Check if the file read had an error
    if (file_reader < 0)
    {
        syserror(read, src);
    }

    //Now close the files...
    if (close(dst_descriptor) < 0)
    {
        syserror(close, src);
    }

    if (close(src_descriptor) < 0)
    {
        syserror(close, dst);
    }

    return 0;
}

int recursive_copy(const char *src, const char *dst)
{
    //Open the directory
    DIR *directory;
    struct dirent *entries = (struct dirent *)malloc(sizeof(struct dirent));
    struct stat *file_type = (struct stat *)malloc(sizeof(struct stat));

    directory = opendir(src);

    //Read each entry then either copy the file directly
    //or call recursive copy  again if it's a directory
    while ((entries = readdir(directory)) != NULL)
    {
        if (!(strcmp(entries->d_name, ".") == 0 || strcmp(entries->d_name, "..") == 0))
        {
            //Get the path to the entry
            char *entry_location = (char *)malloc((strlen(src) + strlen(entries->d_name) + 2) * sizeof(char));
            strcpy(entry_location, src);
            strcat(entry_location, "/");
            strcat(entry_location, entries->d_name);

            //Get the path to the destination
            char *entry_desination = (char *)malloc((strlen(dst) + strlen(entries->d_name) + 2) * sizeof(char));
            strcpy(entry_desination, dst);
            strcat(entry_desination, "/");
            strcat(entry_desination, entries->d_name);

            stat(entry_location, file_type);

            //If it's a directory then call recursive_copy again
            if (S_ISDIR(file_type->st_mode) == 1)
            {
                //Make the directory...
                mode_t temp_permission = 0777;
                if (mkdir(entry_desination, temp_permission) < 0)
                {
                    syserror(mkdir, entry_desination);
                }

                //Now recursively go through it
                recursive_copy(entry_location, entry_desination);

                //Give it the correct permissions
                chmod(entry_desination, file_type->st_mode);
            }
            else if (S_ISREG(file_type->st_mode) == 1)
            {
                //If it's a file just simply copy it!
                copy_file(entry_location, entry_desination, file_type->st_mode);
                chmod(entry_desination, file_type->st_mode);
            }

            free(entry_location);
            free(entry_desination);
        }
    }

    if (closedir(directory) < 0)
    {
        syserror(closedir, src);
    }

    free(file_type);
    free(entries);
    return 0;
}

void usage()
{
    fprintf(stderr, "Usage: cpr srcdir dstdir\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        usage();
    }

    //Given a source and destination figure out if it's a folder or a file...
    char *source = argv[1];
    char *destination = argv[2];

    //Figure out if the source is a file or director...
    struct stat *statbuf = (struct stat *)malloc(sizeof(struct stat));
    stat(source, statbuf);

    if (S_ISREG(statbuf->st_mode) == 1)
    {
        //Now simply copy the file from source to destination!
        copy_file(source, destination, statbuf->st_mode);
        chmod(destination, statbuf->st_mode);
    }
    else if (S_ISDIR(statbuf->st_mode) == 1)
    {
        //First make the directory
        if (mkdir(destination, statbuf->st_mode) < 0)
        {
            syserror(mkdir, destination);
        }

        //Now copy recursively
        recursive_copy(source, destination);

        //Now give it permissions
        chmod(destination, statbuf->st_mode);
    }
    else
    {
        syserror(stat, source);
    }

    free(statbuf);

    return 0;
}
