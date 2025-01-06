import dearpygui.dearpygui as dpg
import dearpygui.demo as demo
from DiveCANpy import configuration, DiveCAN
from math import sin
import threading

dpg.create_context()
dpg.create_viewport(title='DiveCAN Configurator', resizable=True)


diveCANRun = True
loadConfig = False
configToWrite = None
setpointToSend = None
sendreset = False

index = [0]
consensus_PPO2 = [0]
c1_PPO2 = [0]
c2_PPO2 = [0]
c3_PPO2 = [0]
setpoint = [0]


def DiveCANTick(divecan_client):
    global loadConfig
    global configToWrite
    global setpointToSend
    global sendreset

    divecan_client.send_id(1)
    stat_msg = divecan_client.listen_for_status()
    dpg.set_value("connection_status", "Status: Connected")
    dpg.set_value("setpoint_slider", stat_msg.data[5]/100)

    minimum_battery_voltage = 100
    if(dpg.get_value("realtime_sample_checkbox")):
        consensus_msg = divecan_client.listen_for_cell_state()
        ppo2_msg = divecan_client.listen_for_ppo2()
        divecan_client.send_id(1)
        status_msg = divecan_client.listen_for_status()
        print(consensus_msg)
        print(ppo2_msg)
        if len(index) > 200:
            consensus_PPO2.pop(0)
            c1_PPO2.pop(0)
            c2_PPO2.pop(0)
            c3_PPO2.pop(0)
            setpoint.pop(0)
            index.pop(0)

        index.append(max(index)+1)
        consensus_PPO2.append(consensus_msg.data[1]/100)
        c1_PPO2.append(ppo2_msg.data[1]/100)
        c2_PPO2.append(ppo2_msg.data[2]/100)
        c3_PPO2.append(ppo2_msg.data[3]/100)
        setpoint.append(status_msg.data[5]/100)
        dpg.set_value('consensus_series', [index, consensus_PPO2])
        dpg.set_value('c1_series', [index, c1_PPO2])
        dpg.set_value('c2_series', [index, c2_PPO2])
        dpg.set_value('c3_series', [index, c3_PPO2])
        dpg.set_value('setpoint_series', [index, setpoint])

        battery_voltage = status_msg.data[0]/10
        minimum_battery_voltage = min(battery_voltage, minimum_battery_voltage)
        dpg.set_value('battery_indicator', "Battery Voltage: " + str(battery_voltage) + "(" + str(minimum_battery_voltage) + ")")
        dpg.set_axis_limits("x_axis", max(index)-200, max(index))
        dpg.set_axis_limits("y_axis", 0, max(consensus_PPO2)+0.1)

    if loadConfig :
        config = configuration.read_board_config(divecan_client)
        dpg.set_value("c1_config", config.cell1.name)
        dpg.set_value("c2_config", config.cell2.name)
        dpg.set_value("c3_config", config.cell3.name)
        dpg.set_value("power_mode_config", config.powerMode.name)
        dpg.set_value("calibration_mode_config", config.calMethod.name)
        dpg.set_value("printing_config", config.enableUartPrinting)
        dpg.set_value("battery_alarm_config", config.alarmVoltageThreshold.name)
        dpg.set_value("ppo2_control_config", config.PPO2ControlMode.name)
        loadConfig = False

    if setpointToSend is not None:
        divecan_client.send_setpoint(1, (int)(setpointToSend*100))
        setpointToSend = None

    if sendreset:
        DiveCAN.resetBoard(divecan_client)
        sendreset = False

    if configToWrite is not None:
        try:
            configuration.configureBoard(divecan_client, configToWrite)
            configToWrite = None
        except:
            print("Configuration rejected by board")

def DiveCANListen():
    global diveCANRun
    diveCANRun = False
    try:
        divecan_client = DiveCAN.DiveCAN(dpg.get_value("divecan_adaptor_path"))
        diveCANRun = True
        dpg.set_value("connection_status", "Status: Connecting")
    except:
        dpg.set_value("connection_status", "Status: Error, cannot open adaptor")

    while diveCANRun:
        DiveCANTick(divecan_client)

def send_setpoint(sender, appdata):
    global setpointToSend
    setpointToSend = dpg.get_value("setpoint_slider")

def reset_board(sender, appdata):
    global sendreset
    sendreset = True

def load_config(sender, appdata):
    global loadConfig
    loadConfig = True

def save_config(sender, appdata):
    global configToWrite
    configToWrite = configuration.Configuration(
        7,
        configuration.CellType[dpg.get_value("c1_config")],
        configuration.CellType[dpg.get_value("c2_config")],
        configuration.CellType[dpg.get_value("c3_config")],
        configuration.PowerSelectMode[dpg.get_value("power_mode_config")],
        configuration.OxygenCalMethod[dpg.get_value("calibration_mode_config")],
        bool(dpg.get_value("printing_config")),
        configuration.VoltageThreshold[dpg.get_value("battery_alarm_config")],
        configuration.PPO2ControlScheme[dpg.get_value("ppo2_control_config")]
    )

diveCANthread = threading.Thread(target=DiveCANListen)

def connect_to_board(sender, appdata):
    diveCANthread.start()


def disconnect_from_board(sender, appdata):
    global diveCANRun
    diveCANRun = False
    diveCANthread.join()
    dpg.set_value("connection_status", "Status: Disconnected")

with dpg.window(label="main", autosize=True) as primary_window:
        dpg.add_text(default_value="Status: Disconnected", tag="connection_status")
        with dpg.collapsing_header(label="Connection"):
            dpg.add_input_text(label="Adaptor Path", default_value="/dev/ttyACM0", tag="divecan_adaptor_path")
            dpg.add_button(label="Connect", callback=connect_to_board)
            dpg.add_button(label="Disconnect", callback=disconnect_from_board)
            


        with dpg.collapsing_header(label="Configuration"):
            dpg.add_text("Firmware Version: 7")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 1", tag="c1_config")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 2", tag="c2_config")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 3", tag="c3_config")
            dpg.add_combo(items=([name for name, member in configuration.PowerSelectMode.__members__.items()]), label="Power Mode", tag="power_mode_config")
            dpg.add_combo(items=([name for name, member in configuration.OxygenCalMethod.__members__.items()]), label="Calibration Method", tag="calibration_mode_config")
            dpg.add_checkbox(label="Enable Debug Printing (UART2)", tag="printing_config")
            dpg.add_combo(items=([name for name, member in configuration.VoltageThreshold.__members__.items()]), label="Battery Alarm Threshold", tag="battery_alarm_config")
            dpg.add_combo(items=([name for name, member in configuration.PPO2ControlScheme.__members__.items()]), label="PPO2 Control Mode", tag="ppo2_control_config")
        
            dpg.add_button(label="Load", callback=load_config)
            dpg.add_button(label="Save", callback=save_config)
            dpg.add_button(label="Reset Board", callback=reset_board)

        with dpg.collapsing_header(label="PID"):
            dpg.add_slider_float(min_value=-10, max_value=10, label="Proportional")
            dpg.add_slider_float(min_value=-10, max_value=10, label="Integral")
            dpg.add_slider_float(min_value=-10, max_value=10, label="Derivative")
            dpg.add_button(label="Write")
            dpg.add_button(label="Read")

        with dpg.group(height=-1):
            dpg.add_table
            dpg.add_slider_float(min_value=0, max_value=1, label="Setpoint", default_value=0.7, tag="setpoint_slider", clamped=True)
            dpg.add_text(default_value="Battery Voltage: ?", tag="battery_indicator")
            dpg.add_checkbox(label="Realtime Sample", tag="realtime_sample_checkbox")
            # create plot
            with dpg.plot(label="PPO2", height=-1, width=-1):
                # optionally create legend
                dpg.add_plot_legend()

                # REQUIRED: create x and y axes
                dpg.add_plot_axis(dpg.mvXAxis, label="Samples", tag="x_axis")
                dpg.add_plot_axis(dpg.mvYAxis, label="PPO2", tag="y_axis")

                # series belong to a y axis
                dpg.add_line_series(index, c1_PPO2, label="C1", parent="y_axis", tag="c1_series")
                dpg.add_line_series(index, c2_PPO2, label="C2", parent="y_axis", tag="c2_series")
                dpg.add_line_series(index, c3_PPO2, label="C3", parent="y_axis", tag="c3_series")
                dpg.add_line_series(index, c3_PPO2, label="Setpoint", parent="y_axis", tag="setpoint_series")
                dpg.add_line_series(index, consensus_PPO2, label="Consensus", parent="y_axis", tag="consensus_series")



with dpg.item_handler_registry(tag="setpoint_slider_handler") as handler:
    dpg.add_item_deactivated_after_edit_handler(callback=send_setpoint)
dpg.bind_item_handler_registry("setpoint_slider","setpoint_slider_handler")

load_config(None, None)

dpg.set_primary_window(primary_window, True)
dpg.setup_dearpygui()
dpg.show_viewport()
dpg.start_dearpygui()
dpg.destroy_context()