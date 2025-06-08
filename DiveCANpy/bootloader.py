"""Class to communicate with the STM32 bootloader per AN3154"""

import can
import time
import math
from enum import Enum

from statemachine import StateMachine
from statemachine.states import States

class STMBootloaderNoMessageException(Exception):
    """ Raised when we do not get the expected message within the timeout period """
    pass

class STMBootloaderWriteNACK(Exception):
    """ Raised when we do not get the expected message within the timeout period """
    pass

WRITE_BYTE = 0x04

class STMBootloader(object):
    def __init__(self, device: str) -> None:
        self._bus = can.Bus(interface='slcan', channel=device, bitrate=125000, timeout=0.1, rtscts=True)
        # Half a second for stuff we request
        self._timeout = 0.1

        # Make a listener, consume the socket
        self.reader = can.BufferedReader()
        self.notifier = can.Notifier(self._bus, [self.reader])
        while self.reader.get_message(0) is not None:
            self.reader.get_message(0)
       

    def stop(self)-> None:
        try:
            if hasattr(self, 'notifier'):
                self.notifier.stop()

            if hasattr(self, '_bus'):
                self._bus.shutdown()
        except can.exceptions.CanOperationError as e:
            print(f"Can bus faulted out")

    def __del__(self)-> None:
        self.stop()

    def flush_rx(self) -> None:
        msg = self.reader.get_message(0)
        while msg is not None:
            print(msg)
            msg = self.reader.get_message(0)

    def _rx_msg_timed(self, id: int, timeout: int) -> can.Message:
        start_time = time.time()
        while time.time() - start_time < timeout:
            msg = self.reader.get_message(self._timeout)
            if msg is not None and msg.arbitration_id == id:
                return msg
            if msg is not None and msg.arbitration_id == WRITE_BYTE:
                raise STMBootloaderWriteNACK("Received NACK for write memory command")
            elif msg is not None and msg.arbitration_id == 0x79:
                self.send_bootloader()
            elif msg is not None:
                print(f"Received unexpected message with ID {msg.arbitration_id:02X}, data: {msg.data.hex(',')}")
        raise STMBootloaderNoMessageException(f"No message received for ID {id:02X} within {timeout} seconds")

    def _rx_msg(self, id: int) -> can.Message:
        return self._rx_msg_timed(id, self._timeout)
    
    def send_bootloader(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x79, data=[], is_extended_id = False)
        self._bus.send(tx_msg)

    def wait_for_ack(self) -> None:
        self._rx_msg_timed(0x79, 3)

    def get_bootloader_info(self) -> int:
        tx_msg = can.Message(arbitration_id = 0x00, data=[], is_extended_id = False, is_fd=True)
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)
        try:
            version = 0
            msg = self._rx_msg(0x00)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack

            msg = self._rx_msg(0x00)
            data_length = msg.data[0] + 1 # Sent data is the number of bytes minus one, so we add one to get the total number of bytes
            for i in range(data_length):
                msg = self._rx_msg(0x00)
                assert(len(msg.data) == 1) # Each message should have one byte of data
                if i == 0:
                    version = msg.data[0]
                    print(f"Bootloader Version: {version}")
                else:
                    print(f"Available command {i-1}: {msg.data[0]}")
            
            msg = self._rx_msg(0x00)
            assert(msg.data[0] == 0x79) # Last message should be an ack
            return version

        except STMBootloaderNoMessageException:
            print("Bootloader info request timed out")
            self.get_bootloader_info()


    def send_get_version(self) -> int:
        tx_msg = can.Message(arbitration_id = 0x01, data=[], is_extended_id = False)
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)
        try:
            msg = self._rx_msg(0x01)
            assert(msg.data[0] == 0x79) # First message should be an ack

            msg = self._rx_msg(0x01)
            assert(len(msg.data) == 1)
            version = msg.data[0]
            print(f"Bootloader Version: {version}")

            msg = self._rx_msg(0x01)
            assert(len(msg.data) == 2) 

            #Message should be 2 dummy bytes
            assert(msg.data[0] == 0x00)
            assert(msg.data[1] == 0x00)
                       
            
            msg = self._rx_msg(0x01)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # Last message should be an ack
            return version

        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Bootloader version request timed out")
    def send_get_id(self) -> int:
        tx_msg = can.Message(arbitration_id = 0x02, data=[], is_extended_id = False)
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)
        try:
            msg = self._rx_msg(0x02)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack

            msg = self._rx_msg(0x02)
            assert(len(msg.data) == 1)
            chip_id = msg.data[0]
                    
            msg = self._rx_msg(0x01)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # Last message should be an ack
            return chip_id
        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Bootloader ID request timed out")

    def send_read_memory(self, address: int, length: int) -> bytes:
        assert(0 <= address < 0xFFFFFFFF), "Address must be a 32-bit unsigned integer"
        assert(0 < length <= 256), "Length must be a positive integer between 1 and 256"
        assert(address + length <= 0xFFFFFFFF), "Address and length must not exceed 32-bit unsigned integer limit"
        tx_msg = can.Message(arbitration_id = 0x11, data=[(address >> 24) & 0xFF, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF, length & 0xFF], is_extended_id = False)
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)
        data = []
        try:
            msg = self._rx_msg(0x11)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack

            for _ in range((length+1)//8):
                msg = self._rx_msg(0x11)
                for byte in msg.data:
                    print(f"Data Byte: {byte:02X}", end=' ')
                    data.append(byte)

            msg = self._rx_msg(0x11)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79)

            return bytes(data)
        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Read memory request timed out")

    def send_go(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x21, data=[0x08, 0x00, 0x00, 0x00], is_extended_id = False) # 0x08000000 is the address of the start of the application
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)

        try:
            msg = self._rx_msg(0x21)
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack
        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Go command timed out")
    
    def send_write_init(self, address: int, length: int) -> None:
        tx_msg = can.Message(arbitration_id = 0x31, data=[(address >> 24) & 0xFF, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF, length-1 & 0xFF], is_extended_id = False)
        self._bus.send(tx_msg)

    def send_write_data_block(self, data: list[int]) -> None:
        assert(len(data) <= 8), "Data must be a list of 8 bytes"
        tx_msg = can.Message(arbitration_id = WRITE_BYTE, data=data, is_extended_id = False)
        self._bus.send(tx_msg)

    def send_write_memory(self, address: int, length: int, data: bytes) -> None:
        assert(0 <= address < 0xFFFFFFFF), "Address must be a 32-bit unsigned integer"
        assert(0 < length <= 256), "Length must be a positive integer between 1 and 256"
        assert(address + length <= 0xFFFFFFFF), "Address and length must not exceed 32-bit unsigned integer limit"
        assert(len(data) == length), "Data length must match the specified length"
        assert(all(0 <= byte < 256 for byte in data)), "Data must be a sequence of bytes (0-255)"

        

        try:
            msg = self._rx_msg_timed(0x31, 3)
            if msg is None:
                print("No message received for write memory begin")
                raise STMBootloaderNoMessageException("No message received for write memory request")
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack

            for i in range(math.ceil(length / 8)):
                tx_msg = can.Message(arbitration_id = WRITE_BYTE, data=data[i*8:(i+1)*8], is_extended_id = False)
                tx_success = False
                attempts = 0
                
                while (not tx_success) and attempts < 3:
                    try:
                        self._bus.send(tx_msg)
                        msg = self._rx_msg_timed(0x31, 3)
                        tx_success = True  # If we reach here, the message was sent successfully
                    except Exception as e:
                        print(f"Error sending write memory chunk: {e}, retrying...")
                        attempts += 1
                        continue
                
                if not tx_success:
                    self.send_write_memory(address, length, data)  # Retry the entire write memory operation
                    return



                if msg is None:
                    raise STMBootloaderNoMessageException("No message received for write memory request")
                assert(msg.data[0] == 0x79) # Ensure we get an ack for each data chunk
                assert(len(msg.data) == 1)

            msg = self._rx_msg_timed(0x31, 3)
            if msg is None:
                raise STMBootloaderNoMessageException("No message received for write memory request")
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79)
        except STMBootloaderWriteNACK as e:
            time.sleep(0.1)  # Wait a bit before retrying
            print(f"Got NACK on msg, retrying...")
            self.flush_rx()  # Clear any previous messages
            self.send_bootloader()  # Reset the bootloader state
            self.flush_rx()  # Clear any previous messages
            self.send_write_memory(address, length, data)  # Retry the entire write memory operation
            return
        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Write memory request timed out")

    # For simplicity, just do a global erase of the memory
    def send_erase_memory(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x43, data=[0xFF], is_extended_id = False)
        self.flush_rx()  # Clear any previous messages
        self._bus.send(tx_msg)

        try:
            msg = self._rx_msg_timed(0x43, 30) # Wait up to 30 seconds for the response
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # First message should be an ack

            msg = self._rx_msg_timed(0x43, 30) # Wait up to 30 seconds for the response
            assert(len(msg.data) == 1)
            assert(msg.data[0] == 0x79) # We should get a second ack after the erase command
        except STMBootloaderNoMessageException:
            raise STMBootloaderNoMessageException("Erase memory request timed out")


class BootloaderStates(Enum):
    IDLE = 1
    APP = 2
    FLASH_WRITE = 3
    FLASH_WRITE_BYTE = 4
    FLASH_WRITTEN = 5

class BootloaderStateMachine(StateMachine):
    # Configure the state machine
    states = States.from_enum(
        BootloaderStates,
        initial=BootloaderStates.IDLE,
        use_enum_instance=True
    )

    bytes_ready = states.IDLE.to(states.FLASH_WRITE)
    bytes_empty = states.FLASH_WRITE_BYTE.to(states.FLASH_WRITTEN)
    app_message = states.IDLE.to(states.APP) | states.FLASH_WRITE.to(states.APP) | states.FLASH_WRITE_BYTE.to(states.APP) | states.FLASH_WRITTEN.to(states.APP)
    ack_message = states.APP.to(states.IDLE) | states.FLASH_WRITE.to(states.FLASH_WRITE_BYTE) | states.FLASH_WRITE_BYTE.to.itself(on="FLASH_WRITE_BYTE_success") | states.FLASH_WRITTEN.to(states.IDLE)
    nack_message = states.FLASH_WRITE_BYTE.to(states.FLASH_WRITE) | states.FLASH_WRITTEN.to(states.FLASH_WRITE)

    def __init__(self, bootloader: STMBootloader, address: int, length: int, data: list[int]) -> None:
        self._address = address
        self._length = length
        self._bootloader = bootloader
        self._data_queue = data

    def on_enter_IDLE(self) -> None:
        print("Entering IDLE state")
        self._bootloader.flush_rx()
    
    def on_enter_APP(self) -> None:
        self._bootloader.flush_rx()  # Clear any previous messages
        self._bootloader.send_bootloader()  # Send the bootloader command to enter bootloader mode
        print("Entering application state")

    def on_enter_FLASH_WRITE(self) -> None:
        print("Entering FLASH_WRITE state")
        self._bootloader.send_write_init(self._address, self._length)
    
    def on_enter_FLASH_WRITE_BYTE(self) -> None:
        print("Entering FLASH_WRITE_BYTE state")
        self._bootloader.flush_rx()
        if len(self._data_queue) is 0:
            self.send("bytes_empty")
        else:
            self._bootloader.send_write_data_block(self._data_queue[:8])
    
    def FLASH_WRITE_BYTE_success(self) -> None:
        print("had successfully written a byte")
        del self._data_queue[:8]
        


# Test code for flashing the app
if __name__ == "__main__":
    
    bootloader = STMBootloader("/dev/ttyCAN0")
    
    sm = BootloaderStateMachine(bootloader, 0x08000000, 256)
    img_path = "test.png"
    sm._graph().write_png(img_path)
    sm.send("app_message")