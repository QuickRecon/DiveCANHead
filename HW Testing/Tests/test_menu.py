""" Assert that the menu interface works as expected and we can rx and tx configs as needed """
import DiveCAN
import HWShim
import pytest

expectedMenuCount = 5

expectedFieldNames = ["FW Commit\x00", "Config 1\x00\x00", "Config 2\x00\x00", "Config 3\x00\x00", "Config 4\x00\x00"]
expectedEditableBit = [False, True, True, True, True]
expectedTextFieldBit = [True, False, False, False, False]

@pytest.mark.parametrize("our_id", range(0,9))
def test_menu_req_receives_ack(divecan_client: DiveCAN.DiveCAN, our_id: int):
    """ Test that we get an ack when we send the opening message, and that it contains our expected number of menu items """
    divecan_client.send_menu_req(DiveCAN.DUT_ID, our_id)
    message = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    assert message.data[4] == 0 # Ack for init is 0
    assert message.data[5] == expectedMenuCount


@pytest.mark.parametrize("our_id", range(0,9))
@pytest.mark.parametrize("itemIndex", range(0,5))
def test_menu_item_gets_name(divecan_client: DiveCAN.DiveCAN, our_id: int, itemIndex: int):
    """ Test we get our name and basic properties when we ask for them """
    divecan_client.send_menu_item(DiveCAN.DUT_ID, our_id, itemIndex)
    message1 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, our_id)
    message2 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    message3 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)

    itemName = [message1.data[6], message1.data[7], message2.data[1], message2.data[2], message2.data[3], message2.data[4], message2.data[5], message2.data[6], message2.data[7], message3.data[1]]

    assert message1.data[5] == 0x10 | itemIndex
    assert bytes(itemName).decode("utf-8") == expectedFieldNames[itemIndex]
    assert message3.data[2] == expectedTextFieldBit[itemIndex]
    assert message3.data[3] == expectedEditableBit[itemIndex]


@pytest.mark.parametrize("our_id", range(0,9))
def test_menu_commit_flags(divecan_client: DiveCAN.DiveCAN, our_id: int):
    """ Test the commit item has just its row"""
    divecan_client.send_menu_flag(DiveCAN.DUT_ID, our_id, 0)
    message1 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, our_id)
    message2 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    message3 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)

    maxVal = (message1.data[6] << 56) | (message1.data[7] << 48) | (message2.data[1] << 40) | (message2.data[2] << 32) | (message2.data[3] << 24) | (message2.data[4] << 16) | (message2.data[5] << 8) | message2.data[6]
    actualVal = (message2.data[7] << 56) | (message3.data[1] << 48) | (message3.data[2] << 40) | (message3.data[3] << 32) | (message3.data[4] << 24) | (message3.data[5] << 16) | (message3.data[6] << 8) | message3.data[7]

    assert maxVal == 1
    assert actualVal == 1

@pytest.mark.parametrize("our_id", range(0,9))
@pytest.mark.parametrize("configByte", range(0,4))
def test_menu_config_flags(divecan_client: DiveCAN.DiveCAN, our_id: int, configByte: int):
    divecan_client.send_menu_flag(DiveCAN.DUT_ID, our_id, configByte+1)
    message1 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, our_id)
    message2 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)
    message3 = divecan_client.listen_for_menu(our_id, DiveCAN.DUT_ID)

    maxVal = (message1.data[6] << 56) | (message1.data[7] << 48) | (message2.data[1] << 40) | (message2.data[2] << 32) | (message2.data[3] << 24) | (message2.data[4] << 16) | (message2.data[5] << 8) | message2.data[6]
    actualVal = (message2.data[7] << 56) | (message3.data[1] << 48) | (message3.data[2] << 40) | (message3.data[3] << 32) | (message3.data[4] << 24) | (message3.data[5] << 16) | (message3.data[6] << 8) | message3.data[7]

    assert maxVal == 0xFF
    # We can't define the actual val until we're testing config land
    #assert actualVal == 1