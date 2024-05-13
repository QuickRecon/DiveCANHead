""" Class to send and receive DiveCAN messages to the DUT """
import can
import time
import pytest

class DiveCANNoMessageException(Exception):
    """ Raised when we do not get the expected message within the timeout period """
    pass


class DiveCAN(object):
    """ Class to send and receive DiveCAN messages to the DUT """
    def __init__(self) -> None:
        self._bus = can.interface.Bus(interface='socketcan', channel='can0', bitrate=500000)

        # Half a second for stuff we request
        self._timeout = 0.1

        # 2 Seconds for listening for periodic receives
        self._poll_timeout = 2

        # Make a listener, consume the socket
        reader = can.BufferedReader()
        notifier = can.Notifier(self._bus, [reader])
        while reader.get_message(0.1) is not None:
            reader.get_message(0.1)
        reader.stop()
        notifier.stop()

    def __del__(self)-> None:
        self._bus.shutdown()

    def flush_rx(self) -> None:
        # Make a listener, consume the socket
        reader = can.BufferedReader()
        notifier = can.Notifier(self._bus, [reader])
        while reader.get_message(0.1) is not None:
            reader.get_message(0.1)
        reader.stop()
        notifier.stop()

    def _rx_msg_timed(self, id: int, timeout: int) -> can.Message:
        start_time = time.time()
        reader = can.BufferedReader()
        notifier = can.Notifier(self._bus, [reader])

        while time.time() - start_time < timeout:
            msg = reader.get_message(self._timeout)
            if msg is not None and msg.arbitration_id == id:
                notifier.stop()
                reader.stop()
                return msg
        reader.stop()
        notifier.stop()
        raise DiveCANNoMessageException("Expected message never arrived")

    def _rx_msg(self, id: int) -> can.Message:
        return self._rx_msg_timed(id, self._poll_timeout)

    def send_id(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD000001, data=[0x1,0x0,0x0])
        self._bus.send(tx_msg)

    def send_bootloader(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x79, data=[], is_extended_id = False)
        self._bus.send(tx_msg)

    def send_bootloader_go(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x21, data=[0x08, 0x00, 0x00, 0x00], is_extended_id = False)
        self._bus.send(tx_msg)

    def send_id(self, id: int) -> None:
        tx_msg = can.Message(arbitration_id = 0xD000000+id, data=[0x1,0x0,0x0])
        self._bus.send(tx_msg)

    def send_calibrate(self) -> None:
        tx_msg = can.Message(arbitration_id = 0xD130201, data=[0x64,0x04,0x00])
        self._bus.send(tx_msg)

    def listen_for_id(self) -> None:
        return self._rx_msg(0xD000004)

    def listen_for_name(self) -> None:
        return self._rx_msg(0xD010004)

    def listen_for_status(self) -> None:
        return self._rx_msg(0xDCB0004)

    def listen_for_ppo2(self) -> None:
        return self._rx_msg(0xD040004)

    def listen_for_millis(self) -> None:
        return self._rx_msg(0xD110004)

    def listen_for_cal(self) -> None:
        return self._rx_msg_timed(0xD120004, 5)
