#ifndef _DIVECAN_H
#define _DIVECAN_H

#define CAN_NAME_LENGTH 8

#include "CAN_lib/mcp_can.h"
#include <util/delay.h>
#include "mcc_generated_files/system/system.h" // Printf
#include "string.h"

class DiveCAN
{
    public:
        DiveCAN(byte in_canID, char* inName);
        void HandleInboundMessages(); // Event driven/interupt is hard, so keep things sequential (we have time)
    protected:

    // Raw messages
        void sendID();
        void sendName();
        void sendMillis();
        void sendPPO2();
        void sendCellsStat();
        void sendStatus();
        void sendCalAck();
        void sendCalComplete();
        void sendMenuAck();
        void sendMenuText(byte a);
        void sendMenuFields(byte a);
    
    private:
        byte canID; // The bus ID we ident as
        char name [CAN_NAME_LENGTH+1];
        MCP_CAN CAN0; // Our CAN interface
};

#endif