""" Class to send and receive DiveCAN messages to the DUT """
import can

class DiveCAN(object):
    """ Class to send and receive DiveCAN messages to the DUT """
    def __init__(self) -> None:
        self._bus = can.interface.Bus(interface='socketcan', channel='can0', bitrate=500000)
        self._bus.set_filters([{"can_id": 0xD000004, "can_mask": 0x1FFFFFFF, "extended": True}])

        # Half a second for stuff we request
        self._timeout = 0.1

        # 2 Seconds for listening for periodic receives
        self._poll_timeout = 2

    def __del__(self)-> None:
        self._bus.shutdown()

    def send_id(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD000001, data=[0x1,0x0,0x0])
        self._bus.send(tx_msg)

    def send_id(self, id: int) -> None:
        tx_msg = can.Message(arbitration_id = 0xD000000+id, data=[0x1,0x0,0x0])
        self._bus.send(tx_msg)

    def send_calibrate(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD130201, data=[0x64,0x04,0x00])
        self._bus.send(tx_msg)

    def listen_for_id(self) -> None:
        self._bus.set_filters([{"can_id": 0xD000004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(self._timeout)

    def listen_for_name(self) -> None:
        self._bus.set_filters([{"can_id": 0xD010004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(self._timeout)

    def listen_for_status(self) -> None:
        self._bus.set_filters([{"can_id": 0xDCB0004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(self._timeout)

    def listen_for_ppo2(self) -> None:
        self._bus.set_filters([{"can_id": 0xD040004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(self._poll_timeout)

    def listen_for_millis(self) -> None:
        self._bus.set_filters([{"can_id": 0xD110004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(self._poll_timeout)

    def listen_for_cal(self) -> None:
        self._bus.set_filters([{"can_id": 0xD120004, "can_mask": 0x1FFFFFFF, "extended": True}])
        return self._bus.recv(5)
