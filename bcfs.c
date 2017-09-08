#include "bcfs.h"

#include <stdlib.h>
#include <string.h>

#ifndef _GNU_SOURCE
    #error "_GNU_SOURCE required"
#endif

struct cp_xx_entry {
    union {
        char c[4];
        u_int32_t i;
    } magic1;

    int32_t unknown;
    u_int16_t size;
    u_int16_t unknown2;

    union {
        char c[4];
        u_int32_t i;
    } magic2;
};

struct cp_bcwz_entry {
    struct cp_xx_entry hdr;
    u_int64_t partition_offset;
    char unknown[0x1000 - 0x18];
};

struct cp_rhdp_entry {
    struct cp_xx_entry hdr;
    u_int32_t num_sectors;
    u_int32_t unknown;
    u_int32_t num_elements;
    char unknown2[0x200 - 0x10 - 12];
};

struct cp_yedp_entry {
    struct cp_xx_entry hdr;
    // u_int32_t num_sectors;
    u_int32_t unknown1;
    u_int32_t partition_type;
    // u_int32_t num_elements;
    u_int32_t unknown3;
    char unknown[0x200 - 0x10 - 12];
};

struct cp_eedp_entry {
    struct cp_xx_entry hdr;
    u_int32_t offset;
    char unknown[0x40-0x14];
};

struct cp_hp_entry {
    struct cp_xx_entry hdr;
    u_int32_t checksum;
    char unknown[3052];
};

struct cp_ce_entry {
    struct cp_xx_entry hdr;
    int32_t num_elements;
    int32_t unknown2;
    u_int32_t offset;
    int32_t unknown3[9];
};

struct cp_czk_entry {
    struct cp_xx_entry hdr;
    char unknown[0xc0];
    struct cp_ce_entry entries[16];
    char unknown2[0x34];
    /* signature */
    char pkcs7[0x706+0x27f5];
};

struct cp_ie_entry {
    struct cp_xx_entry hdr;
    u_int64_t offset;
    u_int64_t size;
    u_int32_t path_idx;
    u_int32_t filename_idx;
};

struct string_entry {
    u_int64_t size;
    u_int64_t offset;
};


static void file_item_add_child(struct file_item *item, struct file_item *child) {
    struct file_item **newchildren = malloc(sizeof(struct file_item) * (item->num_children + 1));
    if(item->num_children > 0) {
        memcpy(newchildren, item->children, sizeof(struct file_item) * item->num_children);
        free(item->children);
    }
    item->children = newchildren;
    item->children[item->num_children] = child;
    item->num_children++;
}

static void file_item_remove_items(struct file_item *item) {
    while(item->num_children > 0) {
        item->num_children--;
        file_item_remove_items(item->children[item->num_children]);
        free(item->children[item->num_children]);
    }
    free(item->children);
}

struct bcfs* bcfs_new() {
    struct bcfs* bcfs = (struct bcfs*)malloc(sizeof(struct bcfs));
    memset(bcfs, 0, sizeof(struct bcfs));
    bcfs->root.is_dir = 1;
    return bcfs;
}

void bcfs_delete(struct bcfs** bcfs) {
    if(bcfs && *bcfs) {
        if((*bcfs)->imagefile) {
            fclose((*bcfs)->imagefile);
            if((*bcfs)->strings) {
                for(u_int64_t i = 0; i < (*bcfs)->num_strings; i++) {
                    free((void*)(*bcfs)->strings[i].buf);
                }
                free((*bcfs)->strings);
            }
            file_item_remove_items(&(*bcfs)->root);
        }
        free(*bcfs);
    }
    *bcfs = NULL;
}

struct file_item *bcfs_find_file_item(struct file_item *root, const char *path) {
    // printf("Searching for %s\n", path);
    char basename[256];
    if(!path || !path[0])
        return root;

    if(path[0] == '/')
        path++;

    if(strcmp(path, root->filename) == 0)
        return root;

    const char *next = strchrnul(path, '/');
    if(!next)
        return NULL;

    size_t sz = next - path;
    if(*next) next++;
    sz = sz > 255 ? 255 : sz;
    strncpy(basename, path, sz);
    basename[sz] = '\0';
    // printf("Path does not match. Searching for %s\n", basename);
    for(u_int32_t i = 0; i < root->num_children; i++) {
        struct file_item *child = root->children[i];
        if(strcmp(child->filename, basename) == 0) {
            child = bcfs_find_file_item(child, next);
            if(child) return child;
        }
    }

    return NULL;
}

struct file_item *bcfs_get_file_item(struct bcfs *bcfs, const char * path) {
    return bcfs_find_file_item(&bcfs->root, path);
}

struct file_item *bcfs_add_file(struct file_item *root, const char *path) {
    // printf("Adding %s\n", path);
    char basename[256];
    if(!path || !path[0])
        return NULL;

    if(path[0] == '/')
        path++;

    const char *next = strchrnul(path, '/');
    if(!next)
        return NULL;

    size_t sz = next - path;
    if(*next) next++;
    sz = sz > 255 ? 255 : sz;
    strncpy(basename, path, sz);
    basename[sz] = '\0';
    // printf("Path does not match. Searching for %s\n", basename);
    for(u_int32_t i = 0; *next && i < root->num_children; i++) {
        struct file_item *child = root->children[i];
        if(strcmp(child->filename, basename) == 0 && child->is_dir) {
            return bcfs_add_file(child, next);
        }
    }

    struct file_item *res = malloc(sizeof(struct file_item));
    memset(res, 0, sizeof(struct file_item));
    strncpy(res->filename, basename, 256);
    // printf("Added new child %s\n", res->filename);
    res->is_dir = *next != 0;
    file_item_add_child(root, res);
    if(res->is_dir)
        return bcfs_add_file(res, next);
    else
        return res;
}


#define CHAR2DWORD(c) (c[0] | c[1] << 8 | c[2] << 16 | c[3] << 24)
#define BCFS_CHECK_HEADER(hdr, magic1, magic2, size) BCFS_CHECK_HEADER2(hdr, CHAR2DWORD(#magic1), CHAR2DWORD(#magic2), size)
#define BCFS_CHECK_HEADER2(hdr, m1, m2, s) do { \
    if(hdr.magic1.i != m1 || hdr.magic2.i != m2) \
        return 2; \
    if(hdr.size != s) \
        return 3; \
    } while(0)


static void bcfs_load_strings(struct bcfs *bcfs, struct cp_ce_entry *string_entry) {
    printf("Stringtable located at %lx with %d entries\n", string_entry->offset + bcfs->offset, string_entry->num_elements);

    fseek(bcfs->imagefile, string_entry->offset + bcfs->offset, SEEK_SET);
    bcfs->num_strings = string_entry->num_elements;
    bcfs->strings = malloc(sizeof(struct string) * bcfs->num_strings);

    struct string *strings = bcfs->strings;
    for(u_int64_t i = 0; i < bcfs->num_strings; i++) {
        struct string_entry se;
        fread(&se, sizeof(struct string_entry), 1, bcfs->imagefile);
        size_t off = bcfs->offset + string_entry->offset + se.offset;
        printf("Loading entry %lu from %lu with %lu bytes\n", i, off, se.size);
        size_t pos = ftell(bcfs->imagefile);
        fseek(bcfs->imagefile, off, SEEK_SET);
        strings->size = se.size;
        strings->buf = malloc(se.size + 1);
        fread((char*)strings->buf, se.size, 1, bcfs->imagefile);
        strings->buf[se.size] = '\0';
        fseek(bcfs->imagefile, pos, SEEK_SET);
        strings++;
    }
}

static void bcfs_load_files(struct bcfs *bcfs, struct cp_ce_entry *files_entry) {
    struct string *strings = bcfs->strings;
    printf("Files located at %x with %d entries\n", files_entry->offset, files_entry->num_elements);
    for(u_int64_t i = 0; i < files_entry->num_elements; i++) {
        struct cp_ie_entry ie;
        u_int64_t off = bcfs->offset + files_entry->offset + i * 0x100;
        printf("Reading file entry @ %lx\n", off);
        fseek(bcfs->imagefile, off, SEEK_SET);
        fread(&ie, sizeof(struct cp_ie_entry), 1, bcfs->imagefile);
        size_t idx = ie.path_idx >> 4;
        const char *path = strings[idx].buf;
        // printf("File %lu (%lu <> %u): %s\n", i, idx, ie.path_idx, path);
        struct file_item* file = bcfs_add_file(&bcfs->root, path);
        file->offset = bcfs->offset + files_entry->offset + ie.offset;
        file->size = ie.size;
        printf("File #%lu: %s @ %lx (%lu)\n", i, path, file->offset, file->size);
    }
}

static int bcfs_load_filesystem(struct bcfs *bcfs) {
    printf("Loading filesystem from %lx\n", bcfs->offset);
    fseek(bcfs->imagefile, bcfs->offset, SEEK_SET);
    {
        struct cp_hp_entry entry;
        fread(&entry, sizeof(entry), 1, bcfs->imagefile);
        BCFS_CHECK_HEADER(entry.hdr, _CP_, _HP_, sizeof(entry));
    }

    {
        printf("Loading czk from %lx\n", ftell(bcfs->imagefile));
        struct cp_czk_entry entry;
        fread(&entry, sizeof(entry), 1, bcfs->imagefile);
        BCFS_CHECK_HEADER(entry.hdr, _CP_, _CZK, sizeof(entry));

        struct cp_ce_entry *string_entry = &entry.entries[0];
        bcfs_load_strings(bcfs, string_entry);
        /*
        struct cp_ce_entry *options_entry = &entry.entries[1];
        */

        struct cp_ce_entry *files_entry = &entry.entries[2];
        bcfs_load_files(bcfs, files_entry);

    }
    return 0;
}

static u_int64_t bcfs_find_partition_type(struct bcfs *bcfs, struct cp_rhdp_entry *partition_header_entry, u_int64_t off, int type) {
    struct cp_yedp_entry partition_entry;
    off += partition_header_entry->hdr.size;
    for(size_t i = 0; i < partition_header_entry->num_elements; i++) {
        fseek(bcfs->imagefile, off, SEEK_SET);
        fread(&partition_entry, sizeof(partition_entry), 1, bcfs->imagefile);
        //BCFS_CHECK_HEADER(partition_entry.hdr, _CP_, YEDP, sizeof(partition_entry));

        if(partition_entry.partition_type == type) {
            return off;
        }
        off += partition_header_entry->num_sectors;
    }
    return 0;
}

static int bcfs_load_partition(struct bcfs *bcfs) {
    printf("Loading boot entry from %lx\n", bcfs->offset);
    struct cp_bcwz_entry boot_entry;
    fread(&boot_entry, sizeof(boot_entry), 1, bcfs->imagefile);
    BCFS_CHECK_HEADER(boot_entry.hdr, _CP_, BCWZ, sizeof(boot_entry));
    printf("Loading partition header from %lx\n", boot_entry.partition_offset);

    fseek(bcfs->imagefile, boot_entry.partition_offset, SEEK_SET);
    struct cp_rhdp_entry partition_header_entry;
    fread(&partition_header_entry, sizeof(partition_header_entry), 1, bcfs->imagefile);
    BCFS_CHECK_HEADER(partition_header_entry.hdr, _CP_, RHDP, sizeof(partition_header_entry));

    u_int64_t off = bcfs_find_partition_type(bcfs, &partition_header_entry, boot_entry.partition_offset, 4);
    if(!off) return 5;
    fseek(bcfs->imagefile, off + sizeof(struct cp_yedp_entry), SEEK_SET);
    struct cp_eedp_entry eedp_entry;
    fread(&eedp_entry, sizeof(eedp_entry), 1, bcfs->imagefile);
    BCFS_CHECK_HEADER(eedp_entry.hdr, _CP_, EEDP, sizeof(eedp_entry));

    off = bcfs_find_partition_type(bcfs, &partition_header_entry, boot_entry.partition_offset + eedp_entry.offset, 6);
    if(!off) return 6;
    fseek(bcfs->imagefile, off + sizeof(struct cp_yedp_entry), SEEK_SET);
    struct cp_eedp_entry eedp_entry2;
    fread(&eedp_entry2, sizeof(eedp_entry2), 1, bcfs->imagefile);
    BCFS_CHECK_HEADER(eedp_entry2.hdr, _CP_, EEDP, sizeof(eedp_entry2));

    bcfs->offset = boot_entry.partition_offset + eedp_entry.offset + eedp_entry2.offset;

    return 0;
}

int bcfs_load_image(struct bcfs *bcfs, const char *imagefile, u_int64_t offset) {
    bcfs->imagefile = fopen(imagefile, "rb");
    if(!bcfs->imagefile)
        return 1;
    bcfs->offset = offset;
    if(bcfs_load_filesystem(bcfs)) {
        fseek(bcfs->imagefile, offset, SEEK_SET);
        int res = bcfs_load_partition(bcfs);
        if(res)
            return res;
        return bcfs_load_filesystem(bcfs);
    }

    return 0;
}
