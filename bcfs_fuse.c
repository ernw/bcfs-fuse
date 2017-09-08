#define FUSE_USE_VERSION 29
#include "bcfs.h"

#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

static struct options {
    const char *filename;
    u_int64_t offset;
} options;

static const struct fuse_opt option_spec[] = {
    { "--imagefile=%s", offsetof(struct options, filename), 1},
    { "--offset=%llu", offsetof(struct options, offset), 1},
    FUSE_OPT_END
};

static void *bcfs_init(struct fuse_conn_info *conn)
{
    struct bcfs *bcfs = bcfs_new();
    int res = bcfs_load_image(bcfs, options.filename, options.offset);
    if(res) {
        fprintf(stderr, "Error loading imagefile: %d\n", res);
    }

    return bcfs;
}


static void bcfs_destroy(void *data) {
    struct bcfs *bcfs = data;
    bcfs_delete(&bcfs);
}

static int bcfs_getattr(const char* path, struct stat *stbuf)
{
    int res = 0;
    struct fuse_context* ctx = fuse_get_context();
    struct bcfs *bcfs = ctx->private_data;
    struct file_item *file = bcfs_get_file_item(bcfs, path);
    memset(stbuf, 0, sizeof(struct stat));

    if(file) {
        if(file->is_dir) {
            stbuf->st_mode = S_IFDIR | 0555;
            stbuf->st_nlink = 2;
        } else {
            stbuf->st_mode = S_IFREG | 0444;
            // stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = file->size;
        }
    } else {
        res = -ENOENT;
    }
    return res;
}

static int bcfs_readdir(const char* path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    struct fuse_context* ctx = fuse_get_context();
    struct bcfs *bcfs = ctx->private_data;
    struct file_item *file = bcfs_get_file_item(bcfs, path);
    if(!file || !file->is_dir)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for(u_int32_t i = 0; i < file->num_children; i++) {
        struct file_item *child = file->children[i];
        if(child) filler(buf, child->filename, NULL, 0);
    }

    return 0;
}

static int bcfs_open(const char* path, struct fuse_file_info *fi)
{
    printf("Request file open: %s\n", path);
    struct fuse_context* ctx = fuse_get_context();
    struct bcfs *bcfs = ctx->private_data;
    struct file_item *file = bcfs_get_file_item(bcfs, path);
    if(!file || file->is_dir) {
        printf("File not found: %s\n", path);
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
    // if (0 && (fi->flags & 3) != O_RDONLY) {
        printf("Only readonly access allowed: %s\n", path);
        return -EACCES;
    }

    return 0;
}

static int bcfs_read(const char* path, char* buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    printf("Reading %lu bytes file %s at %lu\n", size, path, offset);
    struct fuse_context* ctx = fuse_get_context();
    struct bcfs *bcfs = ctx->private_data;
    struct file_item *file = bcfs_get_file_item(bcfs, path);
    if(!file || file->is_dir)
        return -ENOENT;
   
    size_t diff = file->size - offset;
    if(size > diff)
        size = diff;

    fseek(bcfs->imagefile, file->offset + offset, SEEK_SET);
    printf("Reading from %lu\n", file->offset + offset);
    size = fread(buf, 1, size, bcfs->imagefile);
    printf("Read %lu bytes\n", size);

    return size;
}

static struct fuse_operations bcfs_operations = {
    .init    = bcfs_init,
    .destroy = bcfs_destroy,
    .getattr = bcfs_getattr,
    .readdir = bcfs_readdir,
    .open    = bcfs_open,
    .read    = bcfs_read,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if(fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
        return 1;

    if(options.filename)
    {
        printf("Mounting imagefile %s\n", options.filename);
        options.filename = realpath(options.filename, NULL);
        printf("Mounting imagefile (abs) %s\n", options.filename);
        if(!options.filename)
        {
            fprintf(stderr, "File not found\n");
            return 1;
        }

        FILE *fp = fopen(options.filename, "rb");
        if(!fp) {
            perror("Error loading imagefile");
            return 1;
        } else {
            fclose(fp);
        }
    } else {
        fprintf(stderr, "--imagefile= parameter missing\n");
        return 1;
    }

    //return fuse_main(args.argc, args.argv, &bcfs_operations, NULL);
    struct fuse *fuse;
    char *mountpoint;
    int multithreaded;
    int res;

    fuse = fuse_setup(args.argc, args.argv, &bcfs_operations, sizeof(bcfs_operations),
                      &mountpoint, &multithreaded, NULL);
    if (fuse == NULL)
        return 1;

    multithreaded = 0;
    if (multithreaded)
        res = fuse_loop_mt(fuse);
    else
        res = fuse_loop(fuse);

    fuse_teardown(fuse, mountpoint);
    if (res == -1)
        return 1;

    return 0;
}
