import tkinter as tk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import threading
import socket
import signal

class OscilloscopeApp:
    def __init__(self, root, ip_addr, ip_port):
        self.max_samples = 250
        self.root = root
        self.root.title("Oscilloscope")
        
        self.figure, self.ax = plt.subplots()
        self.ax.set_xlim(0, self.max_samples)  # 设置X轴范围
        self.ax.set_ylim(-0xFFFFFF, 0xFFFFFF)   # 设置Y轴范围
        self.line, = self.ax.plot([], [], lw=2)
        
        self.canvas = FigureCanvasTkAgg(self.figure, master=self.root)
        self.canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)
        
        self.data = np.zeros(self.max_samples)
        self.xdata = np.arange(self.max_samples)

        self.root.protocol("WM_DELETE_WINDOW", self.close)
        signal.signal(signal.SIGINT, lambda sig, frame:self.close())
        
        # 打开网络端口
        try:
            self.socket = socket.create_connection((ip_addr, ip_port), timeout=5)
            #self.socket = serial.Serial(serial_port, baudrate=115200, timeout=1)
        except Exception as e:
            print(f"error connect:{e}")
            self.socket = None

        if self.socket:
            self.socket.set_inheritable(True)
            self.close_event = threading.Event()
            self.close_event.clear()
            # 启动读取数据的线程
            self.socket_thread = threading.Thread(target=self.read_socket_data)
            #self.socket_thread.daemon = True
            self.socket_thread.start()
        
        self.update_plot()

    def close(self):
        print("shutting down...")
        self.close_event.set()
        self.socket_thread.join()
        if self.socket:
            self.socket.shutdown(socket.SHUT_WR)
            data_remain = self.socket.recv(4096)
            while data_remain:
                print(data_remain.decode())
                data_remain = self.socket.recv(4096)
            self.socket.close()
        self.root.destroy()
    
    def update_plot(self):
        self.line.set_data(self.xdata, self.data)
        self.canvas.draw()
        self.canvas.flush_events()
        self.root.after(100, self.update_plot)  # 每隔100ms更新一次
        
    def read_socket_data(self):
        self.input = self.socket.makefile('r')
        self.socket.send('d\n'.encode())
        print(self.socket.recv(4096).decode())
        while not self.close_event.is_set():
            #if self.socket.in_waiting > 0:
            try:
                line = self.input.readline().strip()
                print(line)
                if line == "":
                    continue

                # 解析数据格式：(TTTTTT) weight: FFFFFF
                parts = line.split(':')
                if len(parts) != 2:
                    continue

                data_hex = parts[1].strip()
                new_data = int(data_hex)
                #if new_data >= 0x800000 :
                #    new_data = new_data - 0x1000000
                self.data = np.append(self.data[1:], new_data)
                # 根据当前数据更新Y轴范围
                min_data = np.min(self.data)
                max_data = np.max(self.data)
                margin = 0.1 * (max_data - min_data)
                self.ax.set_ylim(min_data - margin, max_data + margin)
                #self.canvas.draw()

                # 更新画布
                #self.update_plot()

            except Exception as e:
                print(f"Error reading socket data: {str(e)}")
                break
        self.input.close()

if __name__ == "__main__":
    root = tk.Tk()
    ip_addr = '192.168.1.66'  # 替换为实际ip及端口号，例如 192.168.1.1:3333
    ip_port = 3333
    app = OscilloscopeApp(root, ip_addr, ip_port)
    root.mainloop()
