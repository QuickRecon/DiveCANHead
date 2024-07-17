""" Class to send and receive DiveCAN messages to the DUT """
import can
import time
import pytest

DUT_ID = 4

class DiveCANNoMessageException(Exception):
    """ Raised when we do not get the expected message within the timeout period """
    pass


class DiveCAN(object):
    """ Class to send and receive DiveCAN messages to the DUT """
    def __init__(self) -> None:
        try:
            self._bus = can.interface.Bus(interface='slcan', channel='/dev/ttyACM1', bitrate=125000)

            # Half a second for stuff we request
            self._timeout = 0.1

            # 2 Seconds for listening for periodic receives
            self._poll_timeout = 2

            # Make a listener, consume the socket
            self.reader = can.BufferedReader()
            self.notifier = can.Notifier(self._bus, [self.reader])
            while self.reader.get_message(0) is not None:
                self.reader.get_message(0)
        except Exception: 
            pytest.skip("Cannot open CANBus")

    def __del__(self)-> None:
        if hasattr(self, 'notifier'):
            self.notifier.stop()

#        if hasattr(self, '_bus'):
#            self._bus.shutdown()

    def flush_rx(self) -> None:
        while self.reader.get_message(0) is not None:
            self.reader.get_message(0)

    def _rx_msg_timed(self, id: int, timeout: int) -> can.Message:
        start_time = time.time()
        while time.time() - start_time < timeout:
            msg = self.reader.get_message(self._timeout)
            if msg is not None and msg.arbitration_id == id:
                return msg
        raise DiveCANNoMessageException("Expected message never arrived")

    def _rx_msg(self, id: int) -> can.Message:
        return self._rx_msg_timed(id, self._poll_timeout)

    def send_bootloader(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x79, data=[], is_extended_id = False)
        self._bus.send(tx_msg)

    def send_bootloader_go(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x21, data=[0x08, 0x00, 0x00, 0x00], is_extended_id = False)
        self._bus.send(tx_msg)

    def send_menu_req(self, target_id: int, src_id: int) -> None:
        id = 0xd0a0000 | src_id | (target_id << 8)
        tx_msg = can.Message(arbitration_id = id, data=[0x4, 0x0, 0x22, 0x91, 0x0, 0x0, 0x0, 0x0])
        self._bus.send(tx_msg)

    def send_menu_item(self, target_id: int, src_id: int, item_idx: int) -> None:
        id = 0xd0a0000 | src_id | (target_id << 8)
        idx = 0x10 | item_idx
        tx_msg = can.Message(arbitration_id = id, data=[0x4, 0x0, 0x22, 0x91, idx, 0x0, 0x0, 0x0])
        self._bus.send(tx_msg)

    def send_menu_flag(self, target_id: int, src_id: int, item_idx: int) -> None:
        id = 0xd0a0000 | src_id | (target_id << 8)
        idx = 0x30 | item_idx
        tx_msg = can.Message(arbitration_id = id, data=[0x4, 0x0, 0x22, 0x91, idx, 0x0, 0x0, 0x0])
        self._bus.send(tx_msg)

    def send_menu_value(self, target_id: int, src_id: int, item_idx: int, value: int) -> None:
        id = 0xd0a0000 | src_id | (target_id << 8)
        idx = 0x50 | item_idx
        tx_msg = can.Message(arbitration_id = id, data=[0x10, 0x8, 0x0, 0x2e, 0x93, idx, 0x0, 0x0])
        self._bus.send(tx_msg)

        tx_msg = can.Message(arbitration_id = id, data=[0x21, 0x0, value, 0x0, 0x0, 0x0, 0x0, 0x0])
        self._bus.send(tx_msg)

    def send_menu_ack(self, target_id: int, src_id: int):
        id = 0xd0a0000 | src_id | (target_id << 8)
        tx_msg = can.Message(arbitration_id = id, data=[0x30, 0x23, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0])
        self._bus.send(tx_msg)

    def send_id(self, id: int) -> None:
        tx_msg = can.Message(arbitration_id = 0xD000000+id, data=[0x1,0x0,0x0])
        self._bus.send(tx_msg)

    def send_calibrate(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD130201, data=[0x64,0x03,0xf6])
        self._bus.send(tx_msg)

    def send_shutdown(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD030004, data=[0x64,0x03,0xf6])
        self._bus.send(tx_msg)

    def listen_for_id(self) -> can.Message:
        return self._rx_msg(0xD000004)

    def listen_for_name(self) -> can.Message:
        return self._rx_msg(0xD010004)

    def listen_for_status(self) -> can.Message:
        return self._rx_msg(0xDCB0004)

    def listen_for_ppo2(self) -> can.Message:
        return self._rx_msg(0xD040004)

    def listen_for_millis(self) -> can.Message:
        return self._rx_msg(0xD110004)
    
    def listen_for_cell_state(self) -> can.Message:
        return self._rx_msg(0xDCA0004)

    def listen_for_cal(self) -> can.Message:
        return self._rx_msg_timed(0xD120004, 5)
    
    def listen_for_menu(self, target_id: int, src_id: int) -> can.Message:
        id = 0xD0A0000 | src_id | (target_id << 8)
        return self._rx_msg(id)
