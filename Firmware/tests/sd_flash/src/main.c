#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/ztest.h>

#define TEST_FLASH_NODE DT_NODELABEL(test_flash)
#define TEST_FLASH_SIZE 16384U
#define TEST_ERASE_SIZE 4096U
#define TEST_WRITE_SIZE 8U

static const struct device *const test_flash = DEVICE_DT_GET(TEST_FLASH_NODE);

static void reset_flash(void)
{
    zassert_ok(flash_erase(test_flash, 0, TEST_FLASH_SIZE));
}

ZTEST(sd_flash, test_geometry)
{
    const struct flash_parameters *parameters;
    struct flash_pages_info page;
    uint64_t size = 0U;

    zassert_true(device_is_ready(test_flash));
    zassert_ok(flash_get_size(test_flash, &size));
    zassert_equal(size, TEST_FLASH_SIZE);

    parameters = flash_get_parameters(test_flash);
    zassert_not_null(parameters);
    zassert_equal(parameters->write_block_size, TEST_WRITE_SIZE);
    zassert_equal(parameters->erase_value, 0xffU);

    zassert_ok(flash_get_page_info_by_offs(test_flash, TEST_ERASE_SIZE,
                                           &page));
    zassert_equal(page.start_offset, TEST_ERASE_SIZE);
    zassert_equal(page.size, TEST_ERASE_SIZE);
    zassert_equal(page.index, 1U);
}

ZTEST(sd_flash, test_cross_sector_read_write)
{
    uint8_t source[32];
    uint8_t destination[48];

    reset_flash();
    for (size_t i = 0U; i < sizeof(source); ++i) {
        source[i] = (uint8_t)(0xf0U ^ i);
    }

    /* Offset 504 is write-aligned but the payload crosses a 512-byte sector. */
    zassert_ok(flash_write(test_flash, 504, source, sizeof(source)));
    memset(destination, 0, sizeof(destination));
    zassert_ok(flash_read(test_flash, 496, destination, sizeof(destination)));

    for (size_t i = 0U; i < 8U; ++i) {
        zassert_equal(destination[i], 0xffU);
    }
    zassert_mem_equal(&destination[8], source, sizeof(source));
    for (size_t i = 40U; i < sizeof(destination); ++i) {
        zassert_equal(destination[i], 0xffU);
    }
}

ZTEST(sd_flash, test_rejects_zero_to_one_write)
{
    uint8_t zeros[TEST_WRITE_SIZE] = {0};
    uint8_t ones[TEST_WRITE_SIZE];
    uint8_t result[TEST_WRITE_SIZE];

    reset_flash();
    memset(ones, 0xff, sizeof(ones));

    zassert_ok(flash_write(test_flash, 0, zeros, sizeof(zeros)));
    zassert_equal(flash_write(test_flash, 0, ones, sizeof(ones)), -EIO);
    zassert_ok(flash_read(test_flash, 0, result, sizeof(result)));
    zassert_mem_equal(result, zeros, sizeof(result));
}

ZTEST(sd_flash, test_erase_alignment_and_contents)
{
    uint8_t source[TEST_WRITE_SIZE] = {0x55, 0xaa, 0x00, 0xf0,
                                      0x0f, 0x33, 0xcc, 0x5a};
    uint8_t result[TEST_ERASE_SIZE];

    reset_flash();
    zassert_equal(flash_erase(test_flash, 8, TEST_ERASE_SIZE), -EINVAL);
    zassert_equal(flash_erase(test_flash, 0, TEST_ERASE_SIZE - 8U), -EINVAL);

    zassert_ok(flash_write(test_flash, TEST_ERASE_SIZE, source,
                           sizeof(source)));
    zassert_ok(flash_erase(test_flash, TEST_ERASE_SIZE, TEST_ERASE_SIZE));
    zassert_ok(flash_read(test_flash, TEST_ERASE_SIZE, result,
                          sizeof(result)));
    for (size_t i = 0U; i < sizeof(result); ++i) {
        zassert_equal(result[i], 0xffU);
    }
}

ZTEST(sd_flash, test_bounds_alignment_and_zero_length)
{
    uint8_t value[TEST_WRITE_SIZE] = {0};

    reset_flash();
    zassert_ok(flash_read(test_flash, TEST_FLASH_SIZE, NULL, 0));
    zassert_ok(flash_write(test_flash, TEST_FLASH_SIZE, NULL, 0));
    zassert_ok(flash_erase(test_flash, TEST_FLASH_SIZE, 0));

    zassert_equal(flash_read(test_flash, TEST_FLASH_SIZE, value, 1U),
                  -EINVAL);
    zassert_equal(flash_write(test_flash, 1, value, sizeof(value)), -EINVAL);
    zassert_equal(flash_write(test_flash, 0, value, sizeof(value) - 1U),
                  -EINVAL);
    zassert_equal(flash_write(test_flash, TEST_FLASH_SIZE, value,
                              sizeof(value)), -EINVAL);
}

ZTEST_SUITE(sd_flash, NULL, NULL, NULL, NULL, NULL);
