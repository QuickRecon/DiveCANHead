#include "menu.h"
#include "Transciever.h"
#include "../Hardware/printer.h"

#define MENU_COUNT 5
#define MENU_ITEMS 1

#define BYTE_1_OFFSET 8
#define BYTE_2_OFFSET 16
#define BYTE_3_OFFSET 24
typedef enum
{
    MENU_REQ = 0x04,
    MENU_ACK_INIT = 0x05,
    MENU_RESP_HEADER = 0x10,
    MENU_RESP_BODY_BASE = 0x20,
    MENU_RESP_ACK_HEADER = 0x30,
    MENU_RESP_FLAGS = 0x00 /*  Unsure if this is accurate */
} DiveCANMenuOp_t;

typedef enum
{
    REQ_ITEM = 0x10,
    REQ_TEXT_FIELD = 0x50,
    REQ_FLAGS = 0x30
} DiveCANMenuReq_t;

typedef enum
{
    CONFIG_VALUE_1 = 0,
    CONFIG_VALUE_2 = 1,
    CONFIG_VALUE_3 = 2,
    CONFIG_VALUE_4 = 3,
    DATASOURCE_STATIC = 4
} DiveCANMenuDatasource_t;

/*  Struct to hold the details of static menu items
 *  Doing dynamic menu items in a clean way (sensor values, errors, etc)
 *  is going to have to be via a different route, because
 *  no function pointers */
typedef struct
{
    const char *const title;
    const uint8_t fieldCount;
    const char *const fieldItems[MENU_ITEMS];
    const DiveCANMenuItemType_t itemType;
    const bool editable;
    const DiveCANMenuDatasource_t dataVal;
} DiveCANMenuItem_t;

/*  A maximum of 5 items can be shown in the bus menu */
const DiveCANMenuItem_t menu[MENU_COUNT] = {
    {.title = "FW Commit",
     .fieldCount = 1,
     .fieldItems = {COMMIT_HASH},
     .itemType = STATIC_TEXT,
     .editable = false,
     .dataVal = DATASOURCE_STATIC},
    {.title = "Config 1",
     .fieldCount = 0,
     .fieldItems = {0},
     .itemType = DYNAMIC_NUM,
     .editable = true,
     .dataVal = CONFIG_VALUE_1},
    {.title = "Config 2",
     .fieldCount = 0,
     .fieldItems = {0},
     .itemType = DYNAMIC_NUM,
     .editable = true,
     .dataVal = CONFIG_VALUE_2},
    {.title = "Config 3",
     .fieldCount = 8,
     .fieldItems = {0},
     .itemType = DYNAMIC_NUM,
     .editable = true,
     .dataVal = CONFIG_VALUE_3},
    {.title = "Config 4",
     .fieldCount = 0,
     .fieldItems = {0},
     .itemType = DYNAMIC_NUM,
     .editable = true,
     .dataVal = CONFIG_VALUE_4}};

static const uint8_t NUMBER_MASK = 0x0F;
static const uint8_t REQ_MASK = 0xF0;
static const uint8_t REQ_OP_FIELD_IDX = 4;
static const uint8_t SAVE_REQ_FIELD_IDX = 5;
static const uint8_t MAX_1_MSG_SAVE_LEN = 6;
static const uint8_t MIN_VALUES_VAL = 4; /*  If the second hex digit is above this value its a value request */

void HandleMenuReq(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration)
{
    DiveCANType_t target = (DiveCANType_t)(0xF & (message->id));
    DiveCANType_t source = deviceSpec->type;
    uint8_t reqByte = message->data[REQ_OP_FIELD_IDX];

    uint8_t itemNumber = reqByte & NUMBER_MASK;
    uint8_t menuItemNumber = ((reqByte & REQ_MASK) >> 5) - 1;

    if (0 == reqByte)
    { /*  If we just need to ack then do that */
        serial_printf("ACK %d\r\n", itemNumber);
        txMenuAck(target, source, MENU_COUNT);
    }
    else if (itemNumber < MENU_COUNT)
    { /*  Otherwise we're decoding a bit more */
        if ((reqByte & REQ_MASK) == REQ_ITEM)
        {
            serial_printf("Item %d\r\n", itemNumber);
            txMenuItem(target, source, reqByte, menu[itemNumber].title, menu[itemNumber].itemType == STATIC_TEXT, menu[itemNumber].editable);
        }
        else if ((reqByte & REQ_MASK) == REQ_FLAGS)
        {
            serial_printf("Flags %d\r\n", itemNumber);
            if (menu[itemNumber].itemType == STATIC_TEXT)
            {
                txMenuFlags(target, source, reqByte, 1, menu[itemNumber].fieldCount);
            }
            else if (menu[itemNumber].itemType == DYNAMIC_NUM)
            {
                uint32_t maxVal = 0xFF;
                uint32_t currVal = 0;

                serial_printf("Cnf_total: 0x%lx\r\n", configuration->bits);
                switch (menu[itemNumber].dataVal)
                {
                case CONFIG_VALUE_1:
                    currVal = (uint8_t)(configuration->bits);
                    serial_printf("Cnf1: 0x%x\r\n", (uint8_t)currVal);
                    break;
                case CONFIG_VALUE_2:
                    currVal = (uint8_t)(configuration->bits >> BYTE_1_OFFSET);
                    serial_printf("Cnf2: 0x%x\r\n", (uint8_t)currVal);
                    break;
                case CONFIG_VALUE_3:
                    currVal = (uint8_t)(configuration->bits >> BYTE_2_OFFSET);
                    serial_printf("Cnf3: 0x%x\r\n", (uint8_t)currVal);
                    break;
                case CONFIG_VALUE_4:
                    currVal = (uint8_t)(configuration->bits >> BYTE_3_OFFSET);
                    serial_printf("Cnf4: 0x%x\r\n", (uint8_t)currVal);
                    break;
                default:
                    NON_FATAL_ERROR(MENU_ERR);
                }
                txMenuFlags(target, source, reqByte, maxVal, currVal);
            }
            else
            {
                NON_FATAL_ERROR(MENU_ERR);
            }
        }
        else if (((reqByte & REQ_MASK) >> HALF_BYTE_WIDTH) > MIN_VALUES_VAL)
        {
            serial_printf("Field %d, %d\r\n", menuItemNumber, itemNumber);

            txMenuField(target, source, reqByte, menu[menuItemNumber - 1].fieldItems[itemNumber - 1]);
        }
        else
        {
            NON_FATAL_ERROR(MENU_ERR);
        }
    }
    else
    {
        NON_FATAL_ERROR(MENU_ERR);
    }
}

void updateConfig(uint8_t itemNumber, uint8_t newVal, Configuration_t *const configuration)
{
    uint8_t configBytes[4] = {(uint8_t)(configuration->bits),
                              (uint8_t)(configuration->bits >> BYTE_1_OFFSET),
                              (uint8_t)(configuration->bits >> BYTE_2_OFFSET),
                              (uint8_t)(configuration->bits >> BYTE_3_OFFSET)};

    switch (menu[itemNumber].dataVal)
    {
    case CONFIG_VALUE_1:
        configBytes[0] = newVal;
        break;
    case CONFIG_VALUE_2:
        configBytes[1] = newVal;
        break;
    case CONFIG_VALUE_3:
        configBytes[2] = newVal;
        break;
    case CONFIG_VALUE_4:
        configBytes[3] = newVal;
        break;
    default:
    }

    configuration->bits = (configBytes[0] | ((uint32_t)configBytes[1] << BYTE_1_OFFSET) | ((uint32_t)configBytes[2] << BYTE_2_OFFSET) | ((uint32_t)configBytes[3] << BYTE_3_OFFSET));
    serial_printf("Saving config %ld", configuration->bits);
    bool valid = saveConfiguration(*configuration);
    if (valid){
        serial_printf("Config accepted");
    } else {
        serial_printf("Config rejected");
    }
}

void HandleMenuSave(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, Configuration_t *const configuration)
{
    static bool waitingForNextRow = false;
    static DiveCANType_t last_target = 0;
    static DiveCANType_t last_source = 0;
    static uint8_t reqByte = 0;
    static uint8_t itemNumber = 0;

    DiveCANType_t target = (DiveCANType_t)(0xF & (message->id));
    DiveCANType_t source = deviceSpec->type;

    uint8_t op = message->data[0] & 0xFC;
    if (op == MENU_RESP_HEADER)
    {
        reqByte = message->data[SAVE_REQ_FIELD_IDX];
        itemNumber = reqByte & NUMBER_MASK;
        uint8_t length = message->data[1];
        if (length > MAX_1_MSG_SAVE_LEN)
        {
            waitingForNextRow = true;
        }
        txMenuSaveAck(target, source, reqByte);
    }
    else if ((op == MENU_RESP_BODY_BASE) &&
             (last_target == target) &&
             (last_source == source) &&
             waitingForNextRow)
    {
        waitingForNextRow = false;
        serial_printf("Got data 0x%x for item %u\r\n", message->data[2], itemNumber);

        switch (menu[itemNumber].dataVal)
        {
        case CONFIG_VALUE_1:
        case CONFIG_VALUE_2:
        case CONFIG_VALUE_3:
        case CONFIG_VALUE_4:
            updateConfig(itemNumber, message->data[2], configuration);
            break;
        default:
        }
    }
    else
    {
        /* No match */
        waitingForNextRow = false;
    }
}

void ProcessMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, Configuration_t *const configuration)
{
    uint8_t op = message->data[0] & 0xFC;
    switch (op)
    {
    case MENU_REQ:
        HandleMenuReq(message, deviceSpec, configuration);
        break;
    case MENU_RESP_ACK_HEADER:
        /*  Do nothing for now */
        break;
    case MENU_RESP_HEADER: /*  Handset sends that to us during save */
    case MENU_RESP_BODY_BASE:
        HandleMenuSave(message, deviceSpec, configuration);
        break;
    default:
        serial_printf("Unexpected menu message [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\r\n", message->data[0], message->data[1], message->data[2], message->data[3], message->data[4], message->data[5], message->data[6], message->data[7]);
        /*  Something we weren't expecting */
    }
}
