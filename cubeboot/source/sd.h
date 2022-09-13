typedef enum {
	SD_OK         =  0, // Success
    SD_FAIL       = -1, // Failed
	SD_DATA_ERROR = -3, // Input error
	SD_BUF_ERROR  = -5  // Not enough room for output
} sd_error_code;

// // Swiss: Device number.
// typedef enum {
// 	DEV_SDA = 0,
// 	DEV_SDB,
// 	DEV_SDC,
// 	DEV_MAX
// } DeviceNumber;

int mount_available_device();
int unmount_current_device();
const char *get_current_dev_name();

int get_file_size(char *path);
int load_file_dynamic(char *path, void **buf_ptr);
int load_file_buffer(char *path, void* buf);
