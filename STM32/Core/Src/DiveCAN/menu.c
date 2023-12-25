#include "menu.h"
#include "Transciever.h"
#include <stdio.h>

extern void serial_printf(const char *fmt, ...);

typedef enum DiveCANMenuOp_e
{
    MENU_REQ = 0x04,
    MENU_ACK_INIT = 0x05,
    MENU_RESP_HEADER = 0x10,
    MENU_RESP_BODY_BASE = 0x20,
    MENU_RESP_ACK_HEADER = 0x30,
    MENU_RESP_FLAGS = 0x00 // Unsure if this is accurate
} DiveCANMenuOp_t;

typedef enum DiveCANMenuReq_e
{
    REQ_ITEM = 0x10,
    REQ_TEXT_FIELD = 0x50,
    REQ_FLAGS = 0x30
} DiveCANMenuReq_t;

static const uint8_t numberMask = 0x0F;
static const uint8_t reqMask = 0xF0;
static const uint8_t ReqOpFieldIdx = 4;

const uint8_t menuCount = 2;
const char *const menuItems[2] = {"TESTITEM 1", "TESTITEM 2"};
const char *const fieldItems[3] = {"FIELD 1", "FIELD 2", "FIELD 3"};

void HandleMenuReq(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec)
{
    DiveCANType_t target = (DiveCANType_t)(0xF & (message->id));
    DiveCANType_t source = deviceSpec->type;
    uint8_t reqByte = message->data[ReqOpFieldIdx];
    static uint8_t value = 0;

    if (0 == reqByte)
    { // If we just need to ack then do that
        serial_printf("ACK\r\n");
        txMenuAck(target, source, menuCount);
    }
    else
    { // Otherwise we're decoding a bit more

        uint8_t itemNumber = reqByte & numberMask;
        if ((reqByte & reqMask) == REQ_ITEM)
        {
            serial_printf("Item\r\n");
            txMenuItem(target, source, reqByte, menuItems[itemNumber], true, true);
        }
        else if ((reqByte & reqMask) == REQ_FLAGS)
        {
            serial_printf("Flags\r\n");
            txMenuFlags(target, source, reqByte, 1);
        }
        else if (((reqByte & reqMask) >> 4) >= 5)
        {
            serial_printf("Field\r\n");
            char teststr[9] = "";
            value++;
            sprintf(teststr, "F %d", value);
            txMenuField(target, source, reqByte, teststr);
        }
        else
        {
            // Something we weren't expecting
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
        // Do nothing for now
        break;
    case MENU_RESP_HEADER: // Handset sends that to us during save
        HandleMenuSave(message, deviceSpec);
        break;
    default:
        serial_printf("Unexpected message\r\n");
        // Something we weren't expecting
    }
}