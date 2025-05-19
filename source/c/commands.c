#include "system.h"

extern unsigned int code, data, bss, end;

char current_path[PATH_MAX_LEN] = "/";
Fat* mounted_fat = NULL;
Fat primary_fat_volume;

char command_buffer[COMMAND_BUFFER_SIZE];
int command_buffer_pos = 0;

static void append(char *dst, size_t dst_size, const char *src, size_t n)
{
    // find current length
    size_t len = 0;
    while (len + 1 < dst_size && dst[len])
        len++;
    // copy up to n bytes, or until full
    for (size_t i = 0; i < n && len + 1 < dst_size; i++)
    {
        dst[len++] = src[i];
    }
    dst[len] = '\0';
}

static void print_two(unsigned int v)
{
    // печатает две цифры, дополняя слева нулём
    char buf[3];
    buf[0] = '0' + (v / 10) % 10;
    buf[1] = '0' + (v % 10);
    buf[2] = '\0';
    puts(buf);
}

static void print_timestamp(const Timestamp *ts)
{
    // [YYYY-MM-DD hh:mm:ss]
    puts(" [");
    char ybuf[6];
    uitoa(ts->year, ybuf);
    puts(ybuf);
    puts("-");
    print_two(ts->month);
    puts("-");
    print_two(ts->day);
    puts(" ");
    print_two(ts->hour);
    puts(":");
    print_two(ts->min);
    puts(":");
    print_two(ts->sec);
    puts("]");
}

// Helper function to build the full FAT path from logical path
// Output: "/VOLUME_NAME/logical/path"
// Returns 0 on success, -1 on error (e.g., no volume name)
int build_full_fat_path(char *full_fat_path_out, size_t out_size, const char *logical_path_in) {
    memset(full_fat_path_out, 0, out_size);

    if (!mounted_fat || mounted_fat->name_len == 0) {
        return -1;
    }

    // Start with /VOLUME_NAME
    append(full_fat_path_out, out_size, "/", 1);
    append(full_fat_path_out, out_size, mounted_fat->name, mounted_fat->name_len);

    // Append the resolved logical path
    // Handle root path "/" specially to avoid /VOLUME_NAME//
    if (strcmp(logical_path_in, "/") == 0) {
        append(full_fat_path_out, out_size, "/", 1);
    } else {
        if (logical_path_in[0] == '/') {
             append(full_fat_path_out, out_size, logical_path_in, strlen(logical_path_in));
        } else {
             append(full_fat_path_out, out_size, "/", 1);
             append(full_fat_path_out, out_size, logical_path_in, strlen(logical_path_in));
        }
    }
    return 0;
}

void handle_ls(char *args)
{
    if (!mounted_fat)
    {
        puts("Error: No FAT volume mounted.\n");
        return;
    }

    char logical_path[PATH_MAX_LEN]; // Path relative to volume root (like /dir/file)
    char full_fat_path[PATH_MAX_LEN]; // Path for FAT library (like /VOLUME_NAME/dir/file)

    // Resolve Logical Path based on current_path and args
    while (*args == ' ') args++; // Trim leading spaces

    if (strlen(args) == 0) {
        // Command without args: use current_path
        strcpy(logical_path, current_path);
    } else if (args[0] == '/') {
        // Absolute path: use args directly as logical path
        strncpy(logical_path, args, PATH_MAX_LEN - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    } else {
        // Relative path: current_path + "/" + args
        strcpy(logical_path, current_path);
        if (strcmp(current_path, "/") != 0) {
             if (logical_path[strlen(logical_path)-1] != '/') {
                strcat(logical_path, "/");
             }
        }
        strncat(logical_path, args, PATH_MAX_LEN - strlen(logical_path) - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    }

    // Basic normalization: remove trailing slash unless it's root "/"
    size_t logical_len = strlen(logical_path);
    if (logical_len > 1 && logical_path[logical_len - 1] == '/') {
        logical_path[logical_len - 1] = '\0';
    }
    // NOTE: This simple logic does NOT handle "." or ".." path segments properly.

    // Construct Full FAT Path: /VOLUME_NAME/logical/path
    if (build_full_fat_path(full_fat_path, sizeof(full_fat_path), logical_path) != 0) {
        puts("Error building FAT path.\n");
        return;
    }

    Dir dir;
    DirInfo info;
    int err = fat_dir_open(&dir, full_fat_path); // <-- Use full_fat_path

    if (err != FAT_ERR_NONE)
    {
        puts("Failed to open directory ");
        puts((unsigned char *)logical_path); // Print the logical path
        puts(". Error: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
        return;
    }

    while ((err = fat_dir_read(&dir, &info)) == FAT_ERR_NONE)
    {
        // NUL‐terminate the 8.3 name
        if (info.name_len < sizeof info.name)
        {
            info.name[info.name_len] = '\0';
        }
        else
        {
            info.name[sizeof info.name - 1] = '\0';
        }

        // skip "." and ".."
        if (strcmp(info.name, ".") == 0 ||
            strcmp(info.name, "..") == 0)
        {
            fat_dir_next(&dir);
            continue;
        }

        puts((unsigned char *)info.name);

        if (info.created.year || info.created.month)
        {
            print_timestamp(&info.created);
        }

        if (info.attr & FAT_ATTR_DIR)
        {
            puts(" <DIR>\n");
        }
        else
        {
            puts(" <FILE> ");
            char buf[12];
            uitoa(info.size, buf);
            puts((unsigned char *)buf);
            puts(" bytes\n");
        }

        fat_dir_next(&dir);
    }

    if (err != FAT_ERR_EOF)
    {
        puts("Error reading directory: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
    }
}

void handle_touch(char *args)
{
    if (!mounted_fat)
    {
        puts("Error: No FAT volume mounted.\n");
        return;
    }

    char logical_path[PATH_MAX_LEN];
    char full_fat_path[PATH_MAX_LEN];

    while (*args == ' ') args++;

    if (strlen(args) == 0)
    {
        puts("Usage: touch <filename>\n");
        return;
    }

    // Resolve Logical Path based on current_path and args
    if (args[0] == '/') {
        strncpy(logical_path, args, PATH_MAX_LEN - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    } else {
        strcpy(logical_path, current_path);
        if (strcmp(current_path, "/") != 0) {
             if (logical_path[strlen(logical_path)-1] != '/') {
                strcat(logical_path, "/");
             }
        }
        strncat(logical_path, args, PATH_MAX_LEN - strlen(logical_path) - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    }
    // Basic normalization (trailing slash)
    size_t logical_len = strlen(logical_path);
    if (logical_len > 1 && logical_path[logical_len - 1] == '/') {
        logical_path[logical_len - 1] = '\0';
    }
    // NOTE: Does NOT handle "." or ".."

    // Construct Full FAT Path
    if (build_full_fat_path(full_fat_path, sizeof(full_fat_path), logical_path) != 0) {
        puts("Error building FAT path.\n");
        return;
    }

    File file;
    int err;

    err = fat_file_open(&file, full_fat_path, FAT_WRITE | FAT_CREATE); // <-- Use full_fat_path

    if (err == FAT_ERR_NONE)
    {
        err = fat_file_close(&file);
        if (err != FAT_ERR_NONE)
        {
            puts("Failed to close file ");
            puts((unsigned char *)logical_path); // Print logical path
            puts(". Error: ");
            puts((unsigned char *)fat_get_error(err));
            puts("\n");
        }
    }
    else
    {
        puts("Failed to touch file ");
        puts((unsigned char *)logical_path); // Print logical path
        puts(". Error: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
    }
}

void handle_mkdir(char *args)
{
    if (!mounted_fat)
    {
        puts("Error: No FAT volume mounted.\n");
        return;
    }

    char logical_path[PATH_MAX_LEN]; // Path relative to volume root (like /dir/file)
    char full_fat_path[PATH_MAX_LEN]; // Path for FAT library (like /VOLUME_NAME/dir/file)

    while (*args == ' ') args++;

    if (strlen(args) == 0)
    {
        puts("Usage: mkdir <directory_path>\n");
        return;
    }

    // Resolve Logical Path based on current_path and args
    if (args[0] == '/') {
        // Absolute path: use args directly as logical path
        strncpy(logical_path, args, PATH_MAX_LEN - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    } else {
        // Relative path: current_path + "/" + args
        strcpy(logical_path, current_path);
         if (strcmp(current_path, "/") != 0) {
            if (logical_path[strlen(logical_path)-1] != '/') {
                strcat(logical_path, "/");
            }
        }
        strncat(logical_path, args, PATH_MAX_LEN - strlen(logical_path) - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    }
     // Basic normalization: remove trailing slash unless it's root "/"
    size_t logical_len = strlen(logical_path);
    if (logical_len > 1 && logical_path[logical_len - 1] == '/') {
        logical_path[logical_len - 1] = '\0';
    }
     // NOTE: Does NOT handle "." or ".."

    // Construct Full FAT Path for fat_dir_create
    if (build_full_fat_path(full_fat_path, sizeof(full_fat_path), logical_path) != 0) {
        puts("Error building FAT path.\n");
        return;
    }

    Dir parent_dir_context; // Variable name seems misleading based on usage

    int err = fat_dir_create(&parent_dir_context, full_fat_path);

    if (err != FAT_ERR_NONE)
    {
        puts("Failed to create directory ");
        puts((unsigned char *)logical_path); // Print logical path
        puts(". Error: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
    }
}

void handle_cat(char *args)
{
    if (mounted_fat == NULL)
    {
        puts("Error: No FAT volume mounted.\n");
        return;
    }

    char logical_path[PATH_MAX_LEN];
    char full_fat_path[PATH_MAX_LEN];

    while (*args == ' ') args++;

    if (strlen(args) == 0)
    {
        puts("Usage: cat <filename>\n");
        return;
    }

     // Resolve Logical Path based on current_path and args
    if (args[0] == '/') {
        strncpy(logical_path, args, PATH_MAX_LEN - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    } else {
        strcpy(logical_path, current_path);
        if (strcmp(current_path, "/") != 0) {
             if (logical_path[strlen(logical_path)-1] != '/') {
                strcat(logical_path, "/");
             }
        }
        strncat(logical_path, args, PATH_MAX_LEN - strlen(logical_path) - 1);
        logical_path[PATH_MAX_LEN - 1] = '\0';
    }
     // Basic normalization (trailing slash)
    size_t logical_len = strlen(logical_path);
    if (logical_len > 1 && logical_path[logical_len - 1] == '/') {
        logical_path[logical_len - 1] = '\0';
    }
     // NOTE: Does NOT handle "." or ".."

    // Construct Full FAT Path
    if (build_full_fat_path(full_fat_path, sizeof(full_fat_path), logical_path) != 0) {
        puts("Error building FAT path.\n");
        return;
    }

    File file;
    int err;

    err = fat_file_open(&file, full_fat_path, FAT_READ); // <-- Use full_fat_path

    if (err == FAT_ERR_NONE)
    {
        char read_buffer[512]; // buffer for reading
        int bytes_read;

        // reading file by partitions
        while (file.offset < file.size)
        {
            err = fat_file_read(&file, read_buffer, sizeof(read_buffer), &bytes_read);

            if (err == FAT_ERR_NONE && bytes_read > 0)
            {
                for (int i = 0; i < bytes_read; i++)
                {
                    putch(read_buffer[i]);
                }
            }
            else if (err != FAT_ERR_NONE)
            {
                puts("\nError reading file: ");
                puts((unsigned char *)fat_get_error(err));
                puts("\n");
                break;
            }
            else if (bytes_read == 0 && file.offset < file.size)
            {
                puts("\nWarning: Read 0 bytes before EOF.\n");
                break;
            }
        }
        putch('\n'); // Add a newline after cat output for cleanliness

        fat_file_close(&file);
    }
    else
    {
        puts("Failed to open file ");
        puts((unsigned char *)logical_path); // Print logical path
        puts(". Error: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
    }
}

// TODO: use args
void handle_mount(char *args)
{
    if (mounted_fat != NULL)
    {
        puts("FAT volume already mounted.\n");
    }
    else
    {
        // Try to mount this volume on RAM-disk (partition 0)
        const char* name_volume = "RAMDISK";
        int status = fat_mount(&ram_disk_ops, 0, &primary_fat_volume, name_volume);

        if (status == FAT_ERR_NONE)
        {
            mounted_fat = &primary_fat_volume;
            puts("FAT volume mounted successfully!\n");
        }
        else
        {
            puts("Failed to mount FAT volume. Error: ");
            puts((unsigned char *)fat_get_error(status));
            puts("\n");
            mounted_fat = NULL;
        }
    }
}

void handle_cd(char *args) {
    while (*args == ' ') args++;

    char target_logical_path[PATH_MAX_LEN]; // The intended new value for current_path
    char full_fat_path_for_check[PATH_MAX_LEN]; // Path used for fat_dir_open verification

    if (strlen(args) == 0 || strcmp(args, "/") == 0) {
        // go to root or specified root
        strcpy(target_logical_path, "/");
    } else if (args[0] == '/') {
        // absolute path
        strncpy(target_logical_path, args, PATH_MAX_LEN - 1);
        target_logical_path[PATH_MAX_LEN - 1] = '\0';
    } else {
        // relative: current_path + "/" + args
        strcpy(target_logical_path, current_path);
        if (strcmp(current_path, "/") != 0) {
             if (target_logical_path[strlen(target_logical_path)-1] != '/') {
                strcat(target_logical_path, "/");
             }
        }
        strncat(target_logical_path, args, PATH_MAX_LEN - strlen(target_logical_path) - 1);
        target_logical_path[PATH_MAX_LEN - 1] = '\0';
    }

    // Basic normalization: remove trailing '/' except root
    size_t len = strlen(target_logical_path);
    if (len > 1 && target_logical_path[len - 1] == '/') {
        target_logical_path[len - 1] = '\0';
    }
    // NOTE: Does NOT handle "." or ".." path segments.

    // verify directory exists using the full FAT path format
    if (!mounted_fat) {
         puts("Error: No FAT volume mounted.\n");
         return;
    }

    // Construct the full FAT path for the target logical path
    if (build_full_fat_path(full_fat_path_for_check, sizeof(full_fat_path_for_check), target_logical_path) != 0) {
        puts("Error building FAT path for CD verification.\n");
        return;
    }

    Dir dir;
    // Check if the target path is a directory using fat_dir_open
    int err = fat_dir_open(&dir, full_fat_path_for_check); // <-- Use full_fat_path_for_check

    if (err == FAT_ERR_NONE) {
        // If valid, update current_path
        strcpy(current_path, target_logical_path);
        // Assuming fat_dir_open resources don't need explicit close here
    } else {
        puts("cd: no such directory or not a directory: ");
        puts((unsigned char *)args); // Print user's input arg
        puts(". Error: ");
        puts((unsigned char *)fat_get_error(err));
        puts("\n");
    }
}

void handle_ram(void)
{
    settextcolor(0x0F, 0x01);

    char buf[12];

    unsigned int size_text = (unsigned int)&data - (unsigned int)&code;
    unsigned int size_data = (unsigned int)&bss - (unsigned int)&data;
    unsigned int size_bss = (unsigned int)&end - (unsigned int)&bss;
    unsigned int total = (unsigned int)&end - (unsigned int)&code;

    puts("=== Segment Sizes ===\n");

    puts("CODE: ");
    uitoa(size_text, buf);
    puts(buf);
    puts(" bytes\n");

    puts("DATA: ");
    uitoa(size_data, buf);
    puts(buf);
    puts(" bytes\n");

    puts("BSS:  ");
    uitoa(size_bss, buf);
    puts(buf);
    puts(" bytes\n");

    puts("-----\nTOTAL: ");
    uitoa(total, buf);
    puts(buf);
    puts(" bytes\n\n");

    settextcolor(0x07, 0x00);
}

void process_command(char *input)
{
    int len = strlen(input);

    if (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r'))
    {
        input[len - 1] = '\0';
        len--;
    }

    while (len > 0 && input[len - 1] == ' ')
    {
        input[--len] = '\0';
    }

    char *args = input;
    while (*args && *args != ' ') ++args;
    char cmd[COMMAND_BUFFER_SIZE];
    size_t cmd_len = args - input;
    if (cmd_len >= COMMAND_BUFFER_SIZE) cmd_len = COMMAND_BUFFER_SIZE - 1;
    strncpy(cmd, input, cmd_len);
    cmd[cmd_len] = '\0';
    while (*args == ' ') ++args;

    if (strcmp(cmd, "cd") == 0)
    {
        handle_cd(args);
    }
    else if (strcmp(cmd, "ram") == 0)
    {
        handle_ram();
    }
    else if (strcmp(cmd, "clear") == 0)
    {
        cls();
    }
    else if (strcmp(cmd, "mount") == 0)
    {
        handle_mount(args);
    }
    else if (strcmp(cmd, "ls") == 0)
    {
        handle_ls(args);
    }
    else if (strcmp(cmd, "cat") == 0)
    {
        handle_cat(args);
    }
    else if (strcmp(cmd, "touch") == 0)
    {
        handle_touch(args);
    }
    else if (strcmp(cmd, "mkdir") == 0)
    {
        handle_mkdir(args);
    }
    else if (strcmp(cmd, "help") == 0)
    {
        puts("Available commands:\n \
clear             - clear screen\n \
ram               - print segment sizes\n \
mount             - mount RAM disk (FAT32)\n \
cd [directory]     - change directory\n \
ls [directory]     - list directory contents\n \
cat <filename>     - display file contents\n \
touch <filename>   - create a file or update its timestamp\n \
mkdir <directory> - create a new directory\n \
help               - help about available commands\n");
    }
    else if (strlen(cmd) > 0)
    {
        puts("Unknown command: ");
        puts((unsigned char *)cmd);
        puts("\n");
    }
}