import can

class DiveCAN:
    def __init__(self):
        self.bus = can.interface.Bus(interface='socketcan', channel='can0', bitrate=500000)
        self.bus.set_filters([{"can_id": 0xD000004, "can_mask": 0x1FFFFFFF, "extended": True}])
        self._timeout = 2

    def __del__(self):
        self.bus.shutdown()
    
    def SendId(self):
        txMsg = can.Message(arbitration_id = 0xD000001, data=[0x1,0x0,0x0])
        self.bus.send(txMsg)

    def ListenForID(self):
        self.bus.set_filters([{"can_id": 0xD000004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self.bus.recv(self._timeout)

    def ListenForName(self):
        self.bus.set_filters([{"can_id": 0xD010004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self.bus.recv(self._timeout)

    def ListenForStatus(self):
        self.bus.set_filters([{"can_id": 0xDCB0004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self.bus.recv(self._timeout)