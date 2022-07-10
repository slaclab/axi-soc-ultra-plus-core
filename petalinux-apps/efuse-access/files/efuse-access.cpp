/******************************************************************************
* Copyright (c) 2021 Xilinx, Inc. All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <stdbool.h>
 
typedef struct {
    u_int32_t offset;
    u_int32_t size;
}EfuseLookupTable;
 
/* Xilinx error codes */
#define EFUSE_RD_FAILED     1026
#define EFUSE_WR_FAILED     1027
 
#define SYS_PATH    "/sys/bus/nvmem/devices/zynqmp-nvmem0/nvmem"
 
#define EFUSE_MAX_ROWS  16
 
static void print_help();
static u_int32_t get_length(u_int32_t offset);
static u_int32_t remove_initial_0x(char *str);
static u_int32_t validate_offset(char *str);
static int32_t read_efuse(int fd, u_int32_t offset);
static int32_t write_efuse(int fd, u_int32_t offset, char* value, u_int32_t val_len);
static u_int32_t convert_char_to_nibble(char in_char, unsigned char *num);
static u_int32_t convert_string_to_hex_be(const char *str, unsigned char *buf,
    u_int32_t len);
static u_int32_t convert_string_to_hex_le(const char *str, unsigned char *buf,
    u_int32_t len);
 
 
int main(int argc, char* argv[])
{
    int fd;
    u_int32_t offset = 0;
    u_int32_t bytes = 0;
    int32_t readflag = 0;
    int32_t writeflag = 0;
    int32_t helpflag = 0;
    char* value = NULL;
    int32_t c;
    int32_t long_index = 0;
    int32_t status;
 
    static struct option long_options[] = {
        {"help",      no_argument,       0,  'h' },
        {"read",      no_argument,       0,  'r' },
        {"write",     no_argument,       0,  'w' },
        {0,           0,                 0,   0  }
    };
 
    while ((c = getopt_long(argc, argv, "hrw", long_options, &long_index)) != -1) {
        switch (c) {
            case 'h':
                helpflag++;
                break;
            case 'r':
                readflag++;
                break;
            case 'w':
                writeflag++;
                break;
            default:
                print_help();
                abort ();
                break;
        }
    }
 
    if (((readflag + writeflag + helpflag) > 1) ||
        (readflag == true && argc != 3) ||
        (writeflag == true && argc != 4)) {
        fprintf (stderr, "Invalid syntax\n");
        print_help();
        return EINVAL;
    }
 
    if (helpflag == true) {
        print_help();
        return 0;
    }
 
    fd = open(SYS_PATH, O_RDWR);
    if(fd <= 0) {
        printf("Opening SYS FS NVMEM file is failed\n");
        return errno;
    }
 
    if (readflag == true) {
        status = validate_offset(argv[2]);
        if (status != 0) {
            return status;
        }
        offset = strtoul(argv[2], NULL, 16);
        status = read_efuse(fd, offset);
        return status;
    }
 
    if (writeflag == true) {
        status = validate_offset(argv[2]);
        if (status != 0) {
            return status;
        }
        offset = strtoul(argv[2], NULL, 16);
        value = argv[3];
        u_int32_t length = remove_initial_0x(value);
        status = write_efuse(fd, offset, value, length);
        return status;
    }
 
    close(fd);
 
    return 0;
}
 
/*
 * Prints help on the syntax and supported arguments.
 * Called if --help is provided as argument or in case of invalid syntax
 */
static void print_help()
{
    printf("Usage: \r\n");
    printf("Syntax : \r\n");
    printf("Read from eFuse: \r\n ./efuse_access --read "
        "<Offset in hex>\r\n");
    printf("Write into eFuse: \r\n ./efuse_access --write "
        "<Offset in hex> <Value in hex>\r\n");
    printf("\r\n");
    printf("Arguments : \r\n");
    printf("-h --help \t Prints help\r\n");
    printf("-r --read \t Read from eFuse\r\n");
    printf("-w --write \t Write into eFuse\r\n");
    printf("For more details please refer -"
        "https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/"
        "18841682/Solution+ZynqMP+SoC+revision+read+mechanism\r\n");
}
 
/*
 * Returns the supported length of the efuse in bytes as per the provided offset
 * In case of invalid offset, returns 0xFF.
 */
static u_int32_t get_length(u_int32_t offset)
{
    const EfuseLookupTable EfuseSize[EFUSE_MAX_ROWS] = {
        /*+-----+-----+
        *|Offset| Size|
        *+------+-----+
        */
        {0x4, 0x0}, /* Version */
        {0xC, 0xC}, /* DNA */
        {0x20, 0x4}, /* User0 */
        {0x24, 0x4}, /* User1 */
        {0x28, 0x4}, /* User2 */
        {0x2C, 0x4}, /* User3 */
        {0x30, 0x4}, /* User4 */
        {0x34, 0x4}, /* User5 */
        {0x38, 0x4}, /* User6 */
        {0x3C, 0x4}, /* User7 */
        {0x40, 0x4}, /* Misc User */
        {0x58, 0x4}, /* Secure Control */
        {0x5C, 0x4}, /* SPK ID */
        {0x60, 0x20}, /* AES Key */
        {0xA0, 0x30}, /* PPK0 Hash */
        {0xD0, 0x30}, /* PPK1 Hash */
    };
    u_int32_t size = 0xFF;
    int32_t index;
 
    for(index = 0; index < EFUSE_MAX_ROWS; index++) {
        if (EfuseSize[index].offset == offset) {
            size = EfuseSize[index].size;
            break;
        }
    }
 
    return size;
}
 
/*
 * Removes 0x or 0X from starting of the string
 * eg : 0x1234 -> 1234
 * Returns length of the updated string
 */
static u_int32_t remove_initial_0x(char *str)
{
    int32_t index;
    int32_t n = strnlen(str, 48);
 
    if ((*str == '0') && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
        strcpy(str, &str[2]);
    }
 
    return strnlen(str, 48);
}
 
/*
 * Validates offset
 */
static u_int32_t validate_offset(char *str)
{
    u_int32_t index = 0;
    u_int32_t modified_len = remove_initial_0x(str);
    if (modified_len > 2) {
        return EINVAL;
    }
    for (index = 0; str[index] != '\0'; index++) {
        if ((str[index] < '0' || str[index] > '9') &&
            (str[index] < 'A' || str[index] > 'F') &&
            (str[index] < 'a' || str[index] > 'f')) {
            return EINVAL;
        }
    }
    return 0;
}
 
/*
 * Reads eFUSE values from the offset
 */
static int32_t read_efuse(int fd, u_int32_t offset)
{
    u_int32_t length = get_length(offset);
    ssize_t size;
    u_int32_t read_data[50] = {0};
    int32_t index;
 
    if (length == 0xFF) {
        printf("Invalid offset\n\r");
        return EINVAL;
    }
 
    if (offset == 0x60) {
        printf("Read is not allowed for AES key\n\r");
        return EINVAL;
    }
 
    size = pread(fd, (void *)&read_data, length, offset);
    if (size == length) {
        for (index = (size/4)-1; index >= 0; index--) {
            printf("%x ", read_data[index]);
        }
        printf("\n\r");
    }
    else {
        printf("size != length\n\r");
        return EFUSE_RD_FAILED;
    }
 
    return 0;
}
 
/*
 * Writes user provided value in the eFUSE at the given offset
 */
static int32_t write_efuse(int fd, u_int32_t offset, char* value, u_int32_t val_len)
{
    u_int32_t length = get_length(offset);
    ssize_t size;
    unsigned char write_data[48] = {0};
    int32_t status;
    int32_t index;
 
    if (length == 0xFF) {
        printf("Invalid offset\n\r");
        return EINVAL;
    }
 
    if (offset == 0xC) {
        printf("Write is not allowed for DNA\n\r");
        return EINVAL;
    }
 
    if (offset == 0x4) {
        printf("Write is not allowed for Version\n\r");
        return EINVAL;
    }
 
    if (val_len > (length*2)) {
        printf("Length of provided value is longer than expected\n\r");
        return EINVAL;
    }
 
    /* Convert to big endian format in case of PPK0/PPK1 */
    if ((offset == 0xA0) || (offset == 0xD0)) {
        status = convert_string_to_hex_be(value, write_data,
            length*8);
        if (status != 0) {
            return status;
        }
    }
    /* Convert to little endian format in case of other fuses */
    else {
        status = convert_string_to_hex_le(value, write_data,
            length*8);
        if (status != 0) {
            return status;
        }
    }
 
    size = pwrite(fd, (void *)&write_data, length, offset);
    if (size == length) {
        printf("Data written at offset = %x of size = %d bytes\n\r",
            offset, size);
    }
    else {
        return EFUSE_WR_FAILED;
    }
 
    return 0;
}
 
 
/*
 * Converts character to nibble
 */
static u_int32_t convert_char_to_nibble(char in_char, unsigned char *num)
{
    if ((in_char >= '0') && (in_char <= '9')) {
        *num = in_char - '0';
    }
    else if ((in_char >= 'a') && (in_char <= 'f')) {
        *num = in_char - 'a' + 10;
    }
    else if ((in_char >= 'A') && (in_char <= 'F')) {
        *num = in_char - 'A' + 10;
    }
    else {
        return EINVAL;
    }
 
    return 0;
}
 
/*
 * Converts string to hex in big endian format
 */
static u_int32_t convert_string_to_hex_be(const char *str, unsigned char *buf,
    u_int32_t len)
{
    u_int32_t converted_len;
    unsigned char lower_nibble = 0U;
    unsigned char upper_nibble = 0U;
 
    if ((str == NULL) || (buf == NULL)) {
        return EINVAL;
    }
 
    if ((len == 0U) || ((len % 8) != 0U)) {
        return EINVAL;
    }
 
    if((strlen(str) * 4) > len) {
        return EINVAL;
    }
 
    converted_len = 0U;
    while (converted_len < strlen(str)) {
        if (convert_char_to_nibble(str[converted_len],&upper_nibble) ==
            0) {
            if (convert_char_to_nibble(str[converted_len+1],
                    &lower_nibble) == 0) {
                buf[converted_len/2] = (upper_nibble << 4) |
                    lower_nibble;
            }
            else {
                return EINVAL;
            }
        }
        else {
            return EINVAL;
        }
        converted_len += 2U;
    }
 
    return 0;
}
 
/*
 * Converts string to hex in little endian format
 */
static u_int32_t convert_string_to_hex_le(const char *str, unsigned char *buf,
    u_int32_t len)
{
    u_int32_t converted_len;
    unsigned char lower_nibble = 0U;
    unsigned char upper_nibble = 0U;
    u_int32_t str_index;
 
    if ((NULL == str) || (NULL == buf)) {
        return EINVAL;
    }
 
    if ((len == 0U) || ((len % 8) != 0U)) {
        return EINVAL;
    }
 
    if((strlen(str) * 4) > len) {
        return EINVAL;
    }
 
    str_index = (len / 8) - 1U;
    converted_len = 0U;
 
    while (converted_len < strlen(str)) {
        if (convert_char_to_nibble(str[converted_len],
                &upper_nibble) == 0) {
            if (convert_char_to_nibble(str[converted_len + 1],
                    &lower_nibble) == 0) {
                buf[str_index] = (upper_nibble << 4) | lower_nibble;
                str_index = str_index - 1U;
            }
            else {
                return EINVAL;
            }
        }
        else {
            return EINVAL;
        }
        converted_len += 2U;
    }
 
    return 0;
}

