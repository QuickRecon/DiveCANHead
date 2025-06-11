"""Class to communicate with the STM32 bootloader per AN3154"""

import can
import can.interfaces.slcan
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
        self._bus = can.interfaces.slcan.slcanBus(channel=device, bitrate=125000, timeout=0.1, rtscts=True)
        # Half a second for stuff we request
        self._timeout = 0.05

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

    def reset(self) -> None:
        self._bus.close()
        self._bus.open()

    def flush_rx(self) -> None:
        msg = self.reader.get_message(0)
        while msg is not None:
            #print(msg)
            msg = self.reader.get_message(0)
        self.reset()

    def _rx_msg_timed(self, id: int, timeout: float) -> can.Message:
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
        #print(tx_msg)
        self._bus.send(tx_msg)

    def send_nack_msg(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x1F, data=[], is_extended_id = False)
        #print(tx_msg)
        self._bus.send(tx_msg)
        tx_msg = can.Message(arbitration_id = 0x31, data=[0x1F], is_extended_id = False)
        #print(tx_msg)
        self._bus.send(tx_msg)

    def send_version(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x02, data=[], is_extended_id = False)
        #print(tx_msg)
        self._bus.send(tx_msg)

    def send_version_ack(self) -> None:
        tx_msg = can.Message(arbitration_id = 0x02, data=[0x79], is_extended_id = False)
        #(tx_msg)
        self._bus.send(tx_msg)

    def wait_for_ack(self) -> None:
        self._rx_msg_timed(0x79, 3)

    def get_bootloader_info(self) -> int|None:
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
        data = []
        success = False
        while not success:
            try:
                tx_msg = can.Message(arbitration_id = 0x11, data=[(address >> 24) & 0xFF, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF, length-1 & 0xFF], is_extended_id = False)
                self.flush_rx()  # Clear any previous messages
                self._bus.send(tx_msg)
                msg = self.reader.get_message(0.1)
                assert(msg is not None), "No message received"
                assert(msg.arbitration_id == 0x11), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x11"
                assert(len(msg.data) == 1), "Wrong length"
                assert(msg.data[0] == 0x79), "Not an ack"
                success = True
            except AssertionError as e:
                print(f"Got unexpected response to read memory request {repr(e)}, retrying...")
                time.sleep(1)
                continue
        try:
            for i in range((length)//8):
                msg = self.reader.get_message(0.1)
                assert(msg is not None), "No message received"
                assert(msg.arbitration_id == 0x11), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x11"
                if i != (length // 8) - 1:
                    assert(len(msg.data) == 8), "Wrong length"
                else:
                    assert(len(msg.data) <= 8), "Wrong length"
                for byte in msg.data:
                    #print(f"Data Byte: {byte:02X}", end=' ')
                    data.append(byte)

            msg = self.reader.get_message(0.1)
            assert(msg is not None), "No message received for read memory ack"
            assert(msg.arbitration_id == 0x11), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x11"
            assert(len(msg.data) == 1), "Wrong length"
            assert(msg.data[0] == 0x79), "Not an ack"

            return bytes(data)
        except AssertionError as e:
            print(f"Got unexpected response to read memory data {repr(e)}, retrying...")
            return self.send_read_memory(address, length)

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
        #print(tx_msg)
        self._bus.send(tx_msg)

    def send_write_data_block(self, data: list[int]) -> None:
        assert(len(data) <= 8), "Data must be a list of 8 bytes"
        tx_msg = can.Message(arbitration_id = WRITE_BYTE, data=data, is_extended_id = False)
        #print(tx_msg)
        self._bus.send(tx_msg)
    def send_write_memory(self, address: int, length: int, data: bytes, warm=True) -> None:
        assert(0 <= address < 0xFFFFFFFF), "Address must be a 32-bit unsigned integer"
        assert(0 < length <= 256), "Length must be a positive integer between 1 and 256"
        assert(address + length <= 0xFFFFFFFF), "Address and length must not exceed 32-bit unsigned integer limit"
        assert(len(data) == length), "Data length must match the specified length"
        assert(all(0 <= byte < 256 for byte in data)), "Data must be a sequence of bytes (0-255)"

        complete = False
        startTime = time.time()
        sm = BootloaderStateMachine(self, address, length, data, warm)
        while not complete:
            msg = self.reader.get_message(0.5)
            #print(msg)
            if msg is None:
                pass
            elif msg.arbitration_id == 0x02:
                if msg.data[0] == 0x79:
                    sm.send("version_ack_message")
                elif msg.data[0] == 0x1F:
                    sm.send("nack_message")
            elif msg.arbitration_id == 0x79:
                if msg.data[0] == 0x79:
                    sm.send("ack_message")
                elif msg.data[0] == 0x1F:
                    sm.send("nack_message") #Not really a nack, its our 0x79 being bounced as an invalid command
            elif msg.arbitration_id == 0x31:
                if msg.data[0] == 0x79:
                    sm.send("write_ack_message")
                elif msg.data[0] == 0x1F:
                    sm.send("nack_message")
            elif msg.arbitration_id is WRITE_BYTE:
                sm.send("nack_message")
            elif msg.arbitration_id > 0xFF:
                sm.send("app_message")
            
            if sm.FINAL.is_active:
                complete = True
            elif time.time() - startTime > 0.05 and sm.IDLE.is_active and not complete:
                self.flush_rx()
                sm.send("bytes_ready")
            elif msg is None:
                sm.send("timeout")

    def send_write_memory_fast(self, address: int, length: int, data: bytes, warm=True) -> None:
        assert(0 <= address < 0xFFFFFFFF), "Address must be a 32-bit unsigned integer"
        assert(0 < length <= 256), "Length must be a positive integer between 1 and 256"
        assert(address + length <= 0xFFFFFFFF), "Address and length must not exceed 32-bit unsigned integer limit"
        assert(len(data) == length), "Data length must match the specified length"
        assert(all(0 <= byte < 256 for byte in data)), "Data must be a sequence of bytes (0-255)"

        complete = False
        while not complete:
            self.flush_rx()  # Clear any previous messages
            try:
                self.send_write_init(address, length)
                msg = self.reader.get_message(0.2)
                assert(msg is not None), "No message received"
                assert(msg.arbitration_id == 0x31), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x31"
                assert(msg.data[0] == 0x79), "Not an ack"
                assert(len(msg.data) == 1), "Wrong length"
                complete = True
            except AssertionError as e: 
                print(f"Got unexpected response to write memory request {repr(e)}, retrying...")
                continue
        
        for i in range(0, len(data), 8):
            complete = False
            attempts = 0
            while not complete and attempts < 5:
                attempts += 1
                self.flush_rx()
                try:
                    self.send_write_data_block(list(data[i:i+8]))
                    msg = self.reader.get_message(0.1)
                    assert(msg is not None), "No message received"
                    assert(msg.arbitration_id == 0x31), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x31"
                    assert(msg.data[0] == 0x79), "Not an ack "
                    assert(len(msg.data) == 1), "Wrong length "
                    complete = True
                except AssertionError as e:
                    if attempts > 2:
                        print(f"Got unexpected response to write memory data {i} {repr(e)}, retrying...")
                    continue

            if not complete:
                print("retrying from the top")
                self.send_write_memory_fast(address, length, data, warm)
                return

        try:
            msg = self.reader.get_message(0.5)
            assert(msg is not None), "No message received"
            assert(msg.arbitration_id == 0x31), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x31"
            assert(msg.data[0] == 0x79), "Not an ack "
            assert(len(msg.data) == 1), "Wrong length "
        except AssertionError as e:
            print(f"Got unexpected response in final ack {repr(e)}, retry from the top")
            self.send_write_memory_fast(address, length, data, warm)
            return



    # For simplicity, just do a global erase of the memory
    def send_erase_memory(self) -> None:
        success = False
        while not success:
            try:
                tx_msg = can.Message(arbitration_id = 0x43, data=[0xFF], is_extended_id = False)
                self.flush_rx()  # Clear any previous messages
                self._bus.send(tx_msg)

                msg = self._rx_msg_timed(0x43, 1) # Wait up to 1 sec for the response
                assert(len(msg.data) == 1)
                assert(msg.data[0] == 0x79) # First message should be an ack

                msg = self._rx_msg_timed(0x43, 30) # Wait up to 30 seconds for the response
                assert(len(msg.data) == 1)
                assert(msg.data[0] == 0x79) # We should get a second ack after the erase command
                success = True
            except STMBootloaderNoMessageException:
                print("Erase memory request timed out")
                continue

    def send_erase_memory_page(self, pageNumber: int) -> None:
        assert(0 <= pageNumber < 127), "Page number must be between 0 and 127"
        success = False
        while not success:
            self.flush_rx()
            try:
                tx_msg = can.Message(arbitration_id = 0x43, data=[0], is_extended_id = False)
                self._bus.send(tx_msg)

                msg = self.reader.get_message(5) # Wait up to 1 sec for the response
                assert(msg is not None), "No message received for erase memory request"
                assert(msg.arbitration_id == 0x43), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x43"
                assert(len(msg.data) == 1), "Wrong length"
                assert(msg.data[0] == 0x79), "Not ack"

                tx_msg = can.Message(arbitration_id = 0x43, data=[pageNumber], is_extended_id = False)
                self._bus.send(tx_msg)

                msg = self.reader.get_message(5) 
                assert(msg is not None), "No message received for erase memory data"
                assert(msg.arbitration_id == 0x43), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x43"
                assert(len(msg.data) == 1), "Wrong length"
                assert(msg.data[0] == 0x79), "Not ack"

                msg = self.reader.get_message(30) 
                assert(msg is not None), "No message received for erase memory write"
                assert(msg.arbitration_id == 0x43), f"Wrong arbitration ID {msg.arbitration_id:02X}, expected 0x43"
                assert(len(msg.data) == 1), "Wrong length"
                assert(msg.data[0] == 0x79), "Not ack"
                success = True
            except AssertionError as e:
                print(f"Erase memory error: {repr(e)}, retrying...")
                tx_msg = can.Message(arbitration_id = 0x43, data=[0xF0], is_extended_id = False) # An invalid number of sectors on our 256k device
                self._bus.send(tx_msg)
                continue

        self.flush_rx()
        # Verify that reading back the page returns all 0xFF
        pageStartAddress = 0x08000000+(pageNumber*2048)
        readback = self.send_read_memory(pageStartAddress, 256)
        if not all(byte == 0xFF for byte in readback):
            print(f"Error erasing page {pageNumber}, readback did not return all 0xFF, retrying...")
            time.sleep(0.5)
            self.send_erase_memory_page(pageNumber)
            return




class BootloaderStates(Enum):
    IDLE = 1
    APP = 2
    FLASH_WRITE = 3
    FLASH_WRITE_BYTE = 4
    FLASH_WRITTEN = 5
    FINAL = 6
    #VERSION_SENT = 7
    #VERSION_DATA = 8

class BootloaderStateMachine(StateMachine):
    # Configure the state machine
    states = States.from_enum(
        BootloaderStates,
        initial=BootloaderStates.IDLE,
        final = BootloaderStates.FINAL,
        use_enum_instance=True
    )

    bytes_ready = states.IDLE.to(states.FLASH_WRITE)
    bytes_empty = states.FLASH_WRITE_BYTE.to(states.FLASH_WRITTEN)
    app_message = states.IDLE.to(states.APP)| states.APP.to.itself(internal=True)

    ack_message = states.IDLE.to.itself() | states.APP.to(states.IDLE)
    
    #version_ack_message = states.VERSION_SENT.to(states.VERSION_DATA) | states.VERSION_DATA.to(states.FLASH_WRITE)

    write_ack_message = states.FLASH_WRITE.to(states.FLASH_WRITE_BYTE) | states.FLASH_WRITE_BYTE.to.itself(on="FLASH_WRITE_BYTE_success") | states.FLASH_WRITTEN.to(states.FINAL) | \
                        states.IDLE.to.itself(on='send_nack') | states.APP.to(states.IDLE, on='send_nack') \
                        #| states.VERSION_SENT.to(states.IDLE, on='send_nack') | states.VERSION_DATA.to(states.IDLE, on='send_nack')
    nack_message = states.APP.to(states.IDLE) | states.FLASH_WRITE_BYTE.to(states.IDLE) | states.FLASH_WRITTEN.to(states.IDLE) | states.IDLE.to.itself(internal=True, on="on_nack_message") | states.FLASH_WRITE.to(states.IDLE) \
        #| states.VERSION_SENT.to(states.IDLE)
    timeout = states.APP.to.itself() | states.IDLE.to.itself() | states.FLASH_WRITE.to.itself() | states.FLASH_WRITE_BYTE.to.itself() | states.FLASH_WRITTEN.to(states.IDLE) \
        #| states.VERSION_DATA.to.itself() | states.VERSION_SENT.to.itself()

    def __init__(self, bootloader: STMBootloader, address: int, length: int, data: bytes, warm: bool) -> None:
        self._address = address
        self._length = length
        self._bootloader = bootloader
        self._original_data = data
        StateMachine.__init__(self)
        if not warm:
            self.send("app_message")
    
    def on_timeout(self) -> None:
        #print("timeout")
        self._bootloader.reset()

    def on_nack_message(self)-> None:
        self._bootloader.reset()
        print("got nack")
        time.sleep(0.1)

    def send_nack(self) -> None:
        self._bootloader.reset()
        self._bootloader.send_nack_msg()
        time.sleep(1)

    def on_enter_IDLE(self) -> None:
        #print("Entering IDLE state")
        self._data_queue = list(self._original_data).copy()
        self._bootloader.flush_rx()
    
    def on_enter_APP(self) -> None:
        self._bootloader.flush_rx()  # Clear any previous messages
        self._bootloader.send_bootloader()  # Send the bootloader command to enter bootloader mode
        time.sleep(0.01)
        self._bootloader.send_bootloader()  # Send the bootloader command to enter bootloader mode
        #print("Entering application state")

    def on_enter_VERSION_SENT(self) -> None:
        #print("Entering VERSION_SENT state")
        self._bootloader.send_version()

    def on_exit_VERSION_DATA(self) -> None:
        #print("exit VERSION_DATA")
        time.sleep(0.1)
        self._bootloader.flush_rx()

    def on_enter_FLASH_WRITE(self) -> None:
        #print("Entering FLASH_WRITE state")
        self._bootloader.flush_rx()  # Clear any previous messages
        self._bootloader.send_write_init(self._address, self._length)
    
    def on_enter_FLASH_WRITE_BYTE(self) -> None:
        #print("Entering FLASH_WRITE_BYTE state")
        if len(self._data_queue) == 0:
            self.send("bytes_empty")
        else:
            #print(f"Writing {list(map(hex, self._data_queue[:8]))}")
            self._bootloader.send_write_data_block(self._data_queue[:8])
    
    #def on_enter_FLASH_WRITTEN(self) -> None:
        #print("Entering FLASH_WRITTEN state")

    def on_enter_FINAL(self) -> None:
        #print("Entering FINAL state")
        time.sleep(0.01)
        self._bootloader.flush_rx()

    def FLASH_WRITE_BYTE_success(self) -> None:
        #print("had successfully written a byte")
        del self._data_queue[:8]
        
def writePage(k, page_length, file_bytes, bootloader, base_address, firmware_chunksize, start_time):
    readback_ok = False
    while not readback_ok:
        page_number = k // page_length
        print(f"Begining page write {page_number}")
        for i in range(k, k+page_length, firmware_chunksize):
            chunk = file_bytes[i:i+firmware_chunksize]
            if len(chunk) > 0:
                elapsed = time.time() - start_time
                progress = (i + len(chunk)) / len(file_bytes)
                if progress > 0:
                    remaining = elapsed * (1 - progress) / progress
                else:
                    remaining = 0
                state_str = f"State: Writing firmware {i + len(chunk)}/{len(file_bytes)} ({progress * 100:.2f}%) - Est. {remaining:.1f}s remaining"
                print(state_str)
                bootloader.send_write_memory_fast(base_address + i, len(chunk), chunk)

        readback_ok = True
        readback_chunksize = 256
        print("Read back the page to verify")
        for i in range(k, k+page_length, readback_chunksize):
            actual_chunk = file_bytes[i:i+readback_chunksize]
            success = False
            if len(actual_chunk) > 0:
                while not success:
                    try:
                        read_chunk = bootloader.send_read_memory(base_address + i, len(actual_chunk))
                        state_str = f"State: Verifying firmware {i + len(read_chunk)}/{len(file_bytes)} ({(i + len(read_chunk))/len(file_bytes) * 100:.2f}%)"
                        print(state_str)
                        for j in range(len(actual_chunk)):
                            if read_chunk[j] != actual_chunk[j]:
                                print(f"Error verifying firmware at address {base_address + i + j}, got {read_chunk[j]:02X}, expected {actual_chunk[j]:02X}")
                                readback_ok = False
                        success = True
                    except STMBootloaderNoMessageException:
                        print(f"Error reading memory at address {base_address + i}, retrying...")
                        continue
        if not readback_ok:
            print(f"Error reading back page {page_number}, erasing and retrying...")
            bootloader.send_erase_memory_page(page_number)

def main():
    bootloader = STMBootloader("/dev/ttyCAN0")
    # sm = BootloaderStateMachine(bootloader, 0x00, 0x00, [], False)
    # img_path = "test.png"
    # sm._graph().write_png(img_path)

    firmware_chunksize = 128  # Bytes per chunk for firmware update
    base_address = 0x08000000  # Base address for the firmware
    page_length = 2048 # 2K bytes per page
    with open("/home/aren/DiveCANHeadRev2/STM32.bin", 'rb') as firmware_file:
        bootloader.send_write_memory(0x08000000, 1, bytes([0xFF]), False) # Write a byte that ensures we're in the proper mode
        file_bytes = firmware_file.read()
        bootloader.flush_rx()
        bootloader.send_erase_memory()
        # Write the firmware to the device
        start_time = time.time()
        for k in range(page_length, len(file_bytes), page_length):
            writePage(k, page_length, file_bytes, bootloader, base_address, firmware_chunksize, start_time)

        writePage(0, page_length, file_bytes, bootloader, base_address, firmware_chunksize, start_time)

        # Read the whole firmware back to verify, infill busted pages
        validated = False
        while not validated: # Beatings continue until morale improves
            validated = True
            for k in range(0, len(file_bytes), page_length):
                for i in range(k, k+page_length, 256):
                    actual_chunk = file_bytes[i:i+256]
                    if( len(actual_chunk) == 0):
                        continue
                    read_chunk = bootloader.send_read_memory(base_address + i, len(actual_chunk))
                    state_str = f"State: Verifying firmware {i + len(read_chunk)}/{len(file_bytes)} ({(i + len(read_chunk))/len(file_bytes) * 100:.2f}%)"
                    print(state_str)
                    
                    readback_ok = True
                    for j in range(len(actual_chunk)):
                        if read_chunk[j] != actual_chunk[j]:
                            print(f"Error verifying firmware at address {base_address + i + j}, got {read_chunk[j]:02X}, expected {actual_chunk[j]:02X}")
                            readback_ok = False
                            validated = False
                    
                    if not readback_ok:
                        print(f"Error reading back page {k // page_length}, erasing and retrying...")
                        bootloader.send_erase_memory_page(k // page_length)
                        writePage(k, page_length, file_bytes, bootloader, base_address, firmware_chunksize, start_time)
    # HURRAY WE MADE IT
    bootloader.send_go()  # Send the go command to start the application
# Test code for flashing the app
if __name__ == "__main__":
    main()
    