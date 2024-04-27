#include "menu.h"
#include "Transciever.h"
#include "../Hardware/printer.h"

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
    STATIC_TEXT,
    DYNAMIC_TEXT,
    DYNAMIC_NUM
} DiveCANMenuItemType_t;

/*  Struct to hold the details of static menu items
 *  Doing dynamic menu items in a clean way (sensor values, errors, etc)
 *  is going to have to be via a different route, because
 *  no function pointers */
typedef struct
{
    const char *const title;
    const uint8_t fieldCount;
    const char *const fieldItems[10];
    const DiveCANMenuItemType_t itemType;
    const bool editable;
} DiveCANMenuItem_t;

/*  A maximum of 5 items can be shown in the bus menu */
const uint8_t menuCount = 5;
const DiveCANMenuItem_t menu[5] = {
    {.title = "CELL1 MODE",
     .fieldCount = 2,
     .fieldItems = {"DIGITAL   ", "ANALOG    "},
     .itemType = STATIC_TEXT,
     .editable = true},
    {.title = "CELL2 MODE",
     .fieldCount = 2,
     .fieldItems = {"DIGITAL   ", "ANALOG    "},
     .itemType = STATIC_TEXT,
     .editable = true},
    {.title = "CELL3 MODE",
     .fieldCount = 2,
     .fieldItems = {"DIGITAL   ", "ANALOG    "},
     .itemType = STATIC_TEXT,
     .editable = true},
    {.title = "COMMIT",
     .fieldCount = 1,
     .fieldItems = {COMMIT_HASH},
     .itemType = STATIC_TEXT,
     .editable = false},
    {.title = "COMMIT",
     .fieldCount = 1,
     .fieldItems = {COMMIT_HASH},
     .itemType = STATIC_TEXT,
     .editable = false}};

static const uint8_t numberMask = 0x0F;
static const uint8_t reqMask = 0xF0;
static const uint8_t ReqOpFieldIdx = 4;
static const uint8_t minValuesval = 4; /*  If the second hex digit is above this value its a value request */

void HandleMenuReq(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec)
{
    DiveCANType_t target = (DiveCANType_t)(0xF & (message->id));
    DiveCANType_t source = deviceSpec->type;
    uint8_t reqByte = message->data[ReqOpFieldIdx];

    if (0 == reqByte)
    { /*  If we just need to ack then do that */
        serial_printf("ACK\r\n");
        txMenuAck(target, source, menuCount);
    }
    else
    { /*  Otherwise we're decoding a bit more */

        uint8_t itemNumber = reqByte & numberMask;
        if ((reqByte & reqMask) == REQ_ITEM)
        {
            serial_printf("Item\r\n");
            txMenuItem(target, source, reqByte, menu[itemNumber].title, true, menu[itemNumber].editable);
        }
        else if ((reqByte & reqMask) == REQ_FLAGS)
        {
            serial_printf("Flags\r\n");
            txMenuFlags(target, source, reqByte, menu[itemNumber].fieldCount);
        }
        else if (((reqByte & reqMask) >> HALF_BYTE_WIDTH) > minValuesval)
        {
            serial_printf("Field\r\n");
            uint8_t menuItemNumber = ((reqByte & reqMask) >> 5) - 1;
            txMenuField(target, source, reqByte, menu[menuItemNumber].fieldItems[itemNumber]);
        }
        else
        {
            /*  Something we weren't expecting */
        }
    }
}

void HandleMenuSave(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec)
{
    DiveCANType_t target = (DiveCANType_t)(0xF & (message->id));
    DiveCANType_t source = deviceSpec->type;
    uint8_t reqByte = message->data[5];
    txMenuSaveAck(target, source, reqByte);
}

void ProcessMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec)
{
    uint8_t op = message->data[0];
    switch (op)
    {
    case MENU_REQ:
        HandleMenuReq(message, deviceSpec);
        break;
    case MENU_RESP_ACK_HEADER:
        /*  Do nothing for now */
        break;
    case MENU_RESP_HEADER: /*  Handset sends that to us during save */
        HandleMenuSave(message, deviceSpec);
        break;
    default:
        serial_printf("Unexpected message\r\n");
        /*  Something we weren't expecting */
    }
}
