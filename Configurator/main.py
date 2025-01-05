import dearpygui.dearpygui as dpg
import dearpygui.demo as demo
from  DiveCANpy import configuration
from math import sin

dpg.create_context()
dpg.create_viewport(title='DiveCAN Configurator', resizable=True)

def update_plot_data(sender, app_data, plot_data):
    mouse_y = app_data[1]
    if len(plot_data) > 100:
        plot_data.pop(0)
    plot_data.append(sin(mouse_y / 30))
    dpg.set_value("plot", plot_data)

data = []
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
            dpg.add_slider_float(min_value=0, max_value=2.5, label="Setpoint")
            dpg.add_simple_plot(label="Simple Plot", min_scale=-1.0, max_scale=1.0, height=300, tag="plot")

with dpg.handler_registry():
    dpg.add_mouse_move_handler(callback=update_plot_data, user_data=data)

dpg.set_primary_window(primary_window, True)
dpg.setup_dearpygui()
dpg.show_viewport()
dpg.start_dearpygui()
dpg.destroy_context()