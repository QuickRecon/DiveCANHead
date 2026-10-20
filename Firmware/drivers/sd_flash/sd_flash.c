/**
 * @file sd_flash.c
 * @brief Raw SD block device exposed through Zephyr's flash API.
 *
 * The DiveCAN MainPCB uses a removable SD card where DiveCAN Jr uses SPI NOR.
 * This driver preserves the existing flash_area/MCUboot/NVS/FCB interfaces by
 * translating flash operations into 512-byte Disk Access transactions.  The
 * exposed range is raw storage: no partition table or filesystem is parsed.
 */

#define DT_DRV_COMPAT quickrecon_sd_flash

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(sd_flash, CONFIG_QUICKRECON_SD_FLASH_LOG_LEVEL);

#define SD_FLASH_SECTOR_SIZE 512U
#define SD_FLASH_ERASE_VALUE 0xffU

struct sd_flash_config {
    const char *disk_name;
    uint32_t size;
    uint32_t erase_block_size;
    uint32_t write_block_size;
    struct flash_pages_layout layout;
    struct flash_parameters parameters;
};

struct sd_flash_data {
    struct k_mutex lock;
    uint8_t sector[SD_FLASH_SECTOR_SIZE] __aligned(4);
    bool disk_ready;
};

static bool sd_flash_range_valid(const struct sd_flash_config *cfg,
                                 off_t offset, size_t len)
{
    bool valid = offset >= 0;

    if (valid) {
        uint64_t start = (uint64_t)offset;
        valid = (start <= cfg->size) && (len <= ((uint64_t)cfg->size - start));
    }

    return valid;
}

static int sd_flash_disk_result(int rc)
{
    int result = rc;

    if (rc > 0) {
        result = -EIO;
    }

    return result;
}

static int sd_flash_prepare_locked(const struct sd_flash_config *cfg,
                                   struct sd_flash_data *data)
{
    int result = 0;
    int status = disk_access_status(cfg->disk_name);

    if ((!data->disk_ready) || (DISK_STATUS_OK != status)) {
        data->disk_ready = false;
        result = sd_flash_disk_result(disk_access_ioctl(
            cfg->disk_name, DISK_IOCTL_CTRL_INIT, NULL));

        if (0 == result) {
            uint32_t sector_size = 0U;
            uint32_t sector_count = 0U;

            result = sd_flash_disk_result(disk_access_ioctl(
                cfg->disk_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size));
            if (0 == result) {
                result = sd_flash_disk_result(disk_access_ioctl(
                    cfg->disk_name, DISK_IOCTL_GET_SECTOR_COUNT,
                    &sector_count));
            }

            if ((0 == result) && (SD_FLASH_SECTOR_SIZE != sector_size)) {
                LOG_ERR("%s sector size %u is not supported",
                        cfg->disk_name, sector_size);
                result = -ENOTSUP;
            }

            if ((0 == result) &&
                ((uint64_t)sector_count * SD_FLASH_SECTOR_SIZE < cfg->size)) {
                LOG_ERR("%s capacity %u sectors is smaller than %u bytes",
                        cfg->disk_name, sector_count, cfg->size);
                result = -ENOSPC;
            }

            data->disk_ready = 0 == result;
        }
    }

    return result;
}

static int sd_flash_read_locked(const struct sd_flash_config *cfg,
                                struct sd_flash_data *data, off_t offset,
                                uint8_t *destination, size_t len)
{
    int result = 0;
    uint32_t sector = (uint32_t)offset / SD_FLASH_SECTOR_SIZE;
    size_t sector_offset = (size_t)offset % SD_FLASH_SECTOR_SIZE;
    size_t remaining = len;
    uint8_t *cursor = destination;

    while ((0 == result) && (remaining > 0U)) {
        if ((0U == sector_offset) && (remaining >= SD_FLASH_SECTOR_SIZE)) {
            size_t full_sectors = remaining / SD_FLASH_SECTOR_SIZE;
            uint32_t count = (uint32_t)MIN(full_sectors, (size_t)UINT32_MAX);

            result = sd_flash_disk_result(disk_access_read(
                cfg->disk_name, cursor, sector, count));
            if (0 == result) {
                size_t bytes = (size_t)count * SD_FLASH_SECTOR_SIZE;
                cursor += bytes;
                remaining -= bytes;
                sector += count;
            }
        } else {
            result = sd_flash_disk_result(disk_access_read(
                cfg->disk_name, data->sector, sector, 1U));
            if (0 == result) {
                size_t bytes = MIN(remaining,
                                   SD_FLASH_SECTOR_SIZE - sector_offset);
                memcpy(cursor, &data->sector[sector_offset], bytes);
                cursor += bytes;
                remaining -= bytes;
                sector_offset = 0U;
                ++sector;
            }
        }
    }

    if (0 != result) {
        data->disk_ready = false;
    }

    return result;
}

static int sd_flash_read(const struct device *dev, off_t offset,
                         void *destination, size_t len)
{
    const struct sd_flash_config *cfg = dev->config;
    struct sd_flash_data *data = dev->data;
    int result = 0;

    if (0U == len) {
        result = 0;
    } else if ((NULL == destination) ||
               (!sd_flash_range_valid(cfg, offset, len))) {
        result = -EINVAL;
    } else {
        result = k_mutex_lock(&data->lock, K_FOREVER);
        if (0 == result) {
            result = sd_flash_prepare_locked(cfg, data);
            if (0 == result) {
                result = sd_flash_read_locked(cfg, data, offset,
                                              destination, len);
            }
            (void)k_mutex_unlock(&data->lock);
        }
    }

    return result;
}

static int sd_flash_update_sector_locked(const struct sd_flash_config *cfg,
                                         struct sd_flash_data *data,
                                         uint32_t sector,
                                         size_t sector_offset,
                                         const uint8_t *source, size_t len)
{
    int result = sd_flash_disk_result(disk_access_read(
        cfg->disk_name, data->sector, sector, 1U));
    bool changed = false;

    for (size_t i = 0U; (0 == result) && (i < len); ++i) {
        uint8_t old_value = data->sector[sector_offset + i];
        uint8_t new_value = source[i];

        if ((old_value & new_value) != new_value) {
            result = -EIO;
        } else if (old_value != new_value) {
            data->sector[sector_offset + i] = new_value;
            changed = true;
        } else {
            /* Byte already has the requested value. */
        }
    }

    if ((0 == result) && changed) {
        result = sd_flash_disk_result(disk_access_write(
            cfg->disk_name, data->sector, sector, 1U));
    }

    return result;
}

static int sd_flash_write(const struct device *dev, off_t offset,
                          const void *source, size_t len)
{
    const struct sd_flash_config *cfg = dev->config;
    struct sd_flash_data *data = dev->data;
    const uint8_t *input = source;
    int result = 0;

    if (0U == len) {
        result = 0;
    } else if ((NULL == source) ||
               (!sd_flash_range_valid(cfg, offset, len)) ||
               (0U != ((size_t)offset % cfg->write_block_size)) ||
               (0U != (len % cfg->write_block_size))) {
        result = -EINVAL;
    } else {
        result = k_mutex_lock(&data->lock, K_FOREVER);
        if (0 == result) {
            result = sd_flash_prepare_locked(cfg, data);

            uint32_t sector = (uint32_t)offset / SD_FLASH_SECTOR_SIZE;
            size_t sector_offset = (size_t)offset % SD_FLASH_SECTOR_SIZE;
            size_t remaining = len;

            while ((0 == result) && (remaining > 0U)) {
                size_t bytes = MIN(remaining,
                                   SD_FLASH_SECTOR_SIZE - sector_offset);
                result = sd_flash_update_sector_locked(
                    cfg, data, sector, sector_offset, input, bytes);
                input += bytes;
                remaining -= bytes;
                sector_offset = 0U;
                ++sector;
            }

            if (0 != result) {
                data->disk_ready = false;
            }
            (void)k_mutex_unlock(&data->lock);
        }
    }

    return result;
}

static int sd_flash_erase(const struct device *dev, off_t offset, size_t size)
{
    const struct sd_flash_config *cfg = dev->config;
    struct sd_flash_data *data = dev->data;
    int result = 0;

    if (0U == size) {
        result = 0;
    } else if ((!sd_flash_range_valid(cfg, offset, size)) ||
               (0U != ((size_t)offset % cfg->erase_block_size)) ||
               (0U != (size % cfg->erase_block_size))) {
        result = -EINVAL;
    } else {
        result = k_mutex_lock(&data->lock, K_FOREVER);
        if (0 == result) {
            result = sd_flash_prepare_locked(cfg, data);
            if (0 == result) {
                memset(data->sector, SD_FLASH_ERASE_VALUE,
                       sizeof(data->sector));

                uint32_t sector = (uint32_t)offset / SD_FLASH_SECTOR_SIZE;
                size_t remaining = size;

                while ((0 == result) && (remaining > 0U)) {
                    result = sd_flash_disk_result(disk_access_write(
                        cfg->disk_name, data->sector, sector, 1U));
                    remaining -= SD_FLASH_SECTOR_SIZE;
                    ++sector;
                }
            }

            if (0 != result) {
                data->disk_ready = false;
            }
            (void)k_mutex_unlock(&data->lock);
        }
    }

    return result;
}

static const struct flash_parameters *sd_flash_get_parameters(
    const struct device *dev)
{
    const struct sd_flash_config *cfg = dev->config;

    return &cfg->parameters;
}

static int sd_flash_get_size(const struct device *dev, uint64_t *size)
{
    const struct sd_flash_config *cfg = dev->config;
    int result = 0;

    if (NULL == size) {
        result = -EINVAL;
    } else {
        *size = cfg->size;
    }

    return result;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void sd_flash_page_layout(const struct device *dev,
                                 const struct flash_pages_layout **layout,
                                 size_t *layout_size)
{
    const struct sd_flash_config *cfg = dev->config;

    *layout = &cfg->layout;
    *layout_size = 1U;
}
#endif

static int sd_flash_init(const struct device *dev)
{
    const struct sd_flash_config *cfg = dev->config;
    struct sd_flash_data *data = dev->data;
    int result = 0;

    if ((0U == cfg->size) ||
        (0U != (cfg->size % SD_FLASH_SECTOR_SIZE)) ||
        (0U == cfg->erase_block_size) ||
        (0U != (cfg->erase_block_size % SD_FLASH_SECTOR_SIZE)) ||
        (0U == cfg->write_block_size) ||
        (0U != (SD_FLASH_SECTOR_SIZE % cfg->write_block_size))) {
        result = -EINVAL;
    } else {
        k_mutex_init(&data->lock);
        data->disk_ready = false;
    }

    return result;
}

static DEVICE_API(flash, sd_flash_api) = {
    .read = sd_flash_read,
    .write = sd_flash_write,
    .erase = sd_flash_erase,
    .get_parameters = sd_flash_get_parameters,
    .get_size = sd_flash_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
    .page_layout = sd_flash_page_layout,
#endif
};

#define SD_FLASH_DEFINE(inst)                                                \
    BUILD_ASSERT((DT_INST_PROP(inst, size) % SD_FLASH_SECTOR_SIZE) == 0U,   \
                 "raw SD flash size must be sector aligned");              \
    BUILD_ASSERT((DT_INST_PROP(inst, erase_block_size) %                     \
                  SD_FLASH_SECTOR_SIZE) == 0U,                              \
                 "raw SD erase size must be sector aligned");              \
    static struct sd_flash_data sd_flash_data_##inst;                        \
    static const struct sd_flash_config sd_flash_config_##inst = {           \
        .disk_name = DT_INST_PROP(inst, disk_name),                          \
        .size = DT_INST_PROP(inst, size),                                    \
        .erase_block_size = DT_INST_PROP(inst, erase_block_size),            \
        .write_block_size = DT_INST_PROP(inst, write_block_size),            \
        .layout = {                                                          \
            .pages_count = DT_INST_PROP(inst, size) /                        \
                           DT_INST_PROP(inst, erase_block_size),             \
            .pages_size = DT_INST_PROP(inst, erase_block_size),              \
        },                                                                   \
        .parameters = {                                                      \
            .write_block_size = DT_INST_PROP(inst, write_block_size),        \
            .erase_value = SD_FLASH_ERASE_VALUE,                             \
        },                                                                   \
    };                                                                       \
    DEVICE_DT_INST_DEFINE(inst, sd_flash_init, NULL,                          \
                          &sd_flash_data_##inst, &sd_flash_config_##inst,     \
                          POST_KERNEL,                                       \
                          CONFIG_QUICKRECON_SD_FLASH_INIT_PRIORITY,          \
                          &sd_flash_api);

DT_INST_FOREACH_STATUS_OKAY(SD_FLASH_DEFINE)
