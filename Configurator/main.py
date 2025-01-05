import dearpygui.dearpygui as dpg
import dearpygui.demo as demo
from DiveCANpy import configuration, DiveCAN
from math import sin
import threading

dpg.create_context()
dpg.create_viewport(title='DiveCAN Configurator', resizable=True)

divecan_client = DiveCAN.DiveCAN("/dev/ttyACM0")

diveCANRun = True

index = [0]
consensus_PPO2 = [0]
c1_PPO2 = [0]
c2_PPO2 = [0]
c3_PPO2 = [0]
setpoint = [0]


def DiveCANListen():
    while diveCANRun:
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
            dpg.set_axis_limits("x_axis", max(index)-200, max(index))
            dpg.set_axis_limits("y_axis", 0, max(consensus_PPO2)+0.1)


def send_setpoint(sender, appdata):
    setpoint = dpg.get_value("setpoint_slider")
    divecan_client.send_setpoint(1, (int)(setpoint*100))

with dpg.window(label="main", autosize=True) as primary_window:
    with dpg.tab_bar():
        with dpg.tab(label="Configuration"):
            dpg.add_text("Firmware Version: 7")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 1")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 2")
            dpg.add_combo(items=([name for name, member in configuration.CellType.__members__.items()]), label="Cell 3")
            dpg.add_combo(items=([name for name, member in configuration.PowerSelectMode.__members__.items()]), label="Power Mode")
            dpg.add_combo(items=([name for name, member in configuration.OxygenCalMethod.__members__.items()]), label="Calibration Method")
            dpg.add_checkbox(label="Enable Debug Printing (UART2)")
            dpg.add_combo(items=([name for name, member in configuration.VoltageThreshold.__members__.items()]), label="Battery Alarm Threshold")
            dpg.add_combo(items=([name for name, member in configuration.PPO2ControlScheme.__members__.items()]), label="PPO2 Control Mode")
        
            dpg.add_button(label="Load")
            dpg.add_button(label="Save")
            dpg.add_button(label="Reset Board")

        with dpg.tab(label="PID"):
            dpg.add_slider_float(min_value=-10, max_value=10, label="Proportional")
            dpg.add_slider_float(min_value=-10, max_value=10, label="Integral")
            dpg.add_slider_float(min_value=-10, max_value=10, label="Derivative")
            dpg.add_button(label="Write")
            dpg.add_button(label="Read")


        with dpg.tab(label="PPO2"):
            dpg.add_slider_float(min_value=0, max_value=1, label="Setpoint", default_value=0.7, tag="setpoint_slider", clamped=True)
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

diveCANthread = threading.Thread(target=DiveCANListen)
diveCANthread.start()
dpg.set_primary_window(primary_window, True)
dpg.setup_dearpygui()
dpg.show_viewport()
dpg.start_dearpygui()
dpg.destroy_context()
diveCANRun = False
diveCANthread.join()