import tkinter as tk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import threading
import serial 

class OscilloscopeApp:
    def __init__(self, root, serial_port):
        self.root = root
        self.root.title("Oscilloscope")
        
        self.figure, self.ax = plt.subplots()
        self.ax.set_xlim(0, 100)  # 设置X轴范围
        self.ax.set_ylim(-0xFFFFFF, 0xFFFFFF)   # 设置Y轴范围
        self.line, = self.ax.plot([], [], lw=2)
        
        self.canvas = FigureCanvasTkAgg(self.figure, master=self.root)
        self.canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)
        
        self.data = np.zeros(100)
        self.xdata = np.arange(100)
        
        # 打开串口
        try:
            self.serial = serial.Serial(serial_port, baudrate=115200, timeout=0.1)
        except Exception as e:
            print(f"error port:{e}")
            self.serial = None
        
        if self.serial:
            # 启动读取串口数据的线程
            self.serial_thread = threading.Thread(target=self.read_serial_data)
            self.serial_thread.daemon = True
            self.serial_thread.start()
        
        self.update_plot()
    
    def update_plot(self):
        self.line.set_data(self.xdata, self.data)
        self.canvas.draw()
        self.canvas.flush_events()
        self.root.after(10, self.update_plot)  # 每隔10ms更新一次

        
    def read_serial_data(self):
        while True:
            if self.serial.in_waiting > 0:
                try:
                    line = self.serial.readline().decode().strip()
                    if line:
                        # 解析串口数据格式：XXXXX: FFFFFF
                        parts = line.split(':')
                        if len(parts) == 2:
                            data_hex = parts[1].strip()
                            new_data = int(data_hex, 16)
                            if new_data >= 0x800000 :
                                new_data = new_data - 0x1000000
                            self.data = np.append(self.data[1:], new_data)
                            # 根据当前数据更新Y轴范围
                            min_data = np.min(self.data)
                            max_data = np.max(self.data)
                            margin = 0.1 * (max_data - min_data)
                            self.ax.set_ylim(min_data - margin, max_data + margin)
                            self.canvas.draw()
                except Exception as e:
                    print(f"Error reading serial data: {str(e)}")
                    continue

if __name__ == "__main__":
    root = tk.Tk()
    serial_port = 'COM3'  # 替换为你的串口号，例如 COM1, COM2 等
    app = OscilloscopeApp(root, serial_port)
    root.mainloop()
