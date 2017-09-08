#include <sys/types.h>
#include <stdio.h>

struct string {
    u_int64_t size;
    char* buf;
};

struct file_item {
    int is_dir;
    char filename[256];
    u_int64_t size;
    u_int64_t offset;
    u_int32_t num_children;
    struct file_item **children;
};

struct bcfs {
    FILE *imagefile;
    u_int64_t offset;
    u_int64_t num_strings;
    struct string *strings;
    struct file_item root;
};

struct bcfs* bcfs_new();
void bcfs_delete(struct bcfs** bcfs);
struct file_item *bcfs_find_file_item(struct file_item *root, const char *path);
struct file_item *bcfs_get_file_item(struct bcfs *bcfs, const char * path);
struct file_item *bcfs_add_file(struct file_item *root, const char *path);
int bcfs_load_image(struct bcfs *bcfs, const char *imagefile, u_int64_t offset);
