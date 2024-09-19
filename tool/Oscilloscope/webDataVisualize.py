#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import threading
import socket
import re
import os, signal, platform, select

class Event_with_fd:
    def __init__(self, init_value = 0):
        self.is_windows = (platform.system() == 'Windows')
        if self.is_windows:
            self.fd, self.fd_w = os.pipe()
            os.set_inheritable(self.fd_w, True)
            self.read = lambda : int.from_bytes(read(self.fd, 8))
            self.write = lambda x: os.write(self.fd_w, x.to_bytes(8))
        else:
            self.fd = os.eventfd(0, os.EFD_CLOEXEC)
            self.read = lambda : os.eventfd_read(self.fd)
            self.write = lambda x: os.eventfd_write(self.fd, x)
        self.lock = threading.Lock()
        os.set_inheritable(self.fd, True)
        if init_value:
            self.set(init_value)

    def clear(self):
        with self.lock:
            rlist, wlist, xlist = select.select((self.fd, ), (), (self.fd, ), 0)
            if rlist:
                self.read()

    def set(self):
        with self.lock:
            self.write(1)

    def is_set(self):
        rlist, wlist, xlist = select.select((self.fd, ), (), (self.fd, ), 0)
        return bool(rlist)

    def wait(self, timeout=None):
        rlist, wlist, xlist = select.select((self.fd, ), (), (self.fd, ), timeout)
        return bool(rlist)

    def close(self):
        os.close(self.fd)

    def fileno(self):
        return self.fd

class OscilloscopeApp:
    def __init__(self, root, ip_addr, ip_port):
        self.max_samples = 250
        self.root = root
        self.command_eventfd = Event_with_fd(0)
        self.end_event = threading.Event()
        self.after_id = None
        self.socket = None
        self.socket_thread = None

        #interrupts and events
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        #self.root.bind("<<ThreadClose>>", self.thread_close_callback)
        #signal.signal(signal.SIGINT, lambda sig,frame:self.close())

        #these two will be made into tk.StringVar in self.layout()
        self.ip_addr = ip_addr
        self.ip_port = ip_port

        self.figure, self.ax = plt.subplots()
        self.ax.set_xlim(0, self.max_samples)  # 设置X轴范围
        self.ax.set_ylim(-0xFFFFFF, 0xFFFFFF)   # 设置Y轴范围
        self.line, = self.ax.plot([], [], lw=2)
        
        self.layout()
        
        self.datalock = threading.Lock()
        with self.datalock:
            self.data = np.zeros(self.max_samples)
            self.xdata = np.arange(self.max_samples)

        #start updating plot
        self.update_plot()

    def quit(self):
        print("shutting down...")
        #stop updating plot
        if self.after_id:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        #disconnect socket
        self.disconnect()
        #destroy window
        self.root.destroy()
        self.root.quit()
    
    def update_plot(self):
        # 根据当前数据更新Y轴范围
        with self.datalock:
            min_data = np.min(self.data)
            max_data = np.max(self.data)
            margin = max(0.1 * (max_data - min_data),100)
            self.ax.set_ylim(min_data - margin, max_data + margin)
            self.line.set_data(self.xdata, self.data)
        self.canvas.draw()
        self.canvas.flush_events()
        if self.end_event.is_set():
            self.end_event.clear()
            self.disconnect()
        self.after_id = self.root.after(100, self.update_plot)  # 每隔100ms更新一次
        
    def read_socket_data_thread(self):
        self.input = self.socket.makefile('r')
        # 解析数据格式：(TTTTTT) weight: FFFFFF
        print("reading thread started")
        self.start_stop()
        pattern = re.compile(r"\((\d+)\) * weight: *(-?\d+)")
        print(self.socket.recv(4096).decode())
        while not self.command_eventfd.is_set():
            #if self.socket.in_waiting > 0:
            try:
                line = self.input.readline().strip()
                #print(line)
                if line == "":
                    continue

                parts = pattern.match(line)
                if not parts:
                    print(line) #other output
                    continue
                else:
                    parts = parts.groups()
                    print(f"{int(parts[0])/1000}: {parts[1]}")
                #if len(parts) != 2:
                #    continue

                data_hex = parts[1].strip()
                new_data = int(data_hex)
                #if new_data >= 0x800000 :
                #    new_data = new_data - 0x1000000
                with self.datalock:
                    self.data = np.append(self.data[1:], new_data)
                #self.canvas.draw()

                # 更新画布
                #self.update_plot()

            except Exception as e:
                print(f"Error reading socket data: {str(e)}")
                #self.root.event_generate("<<ThreadClose>>", when="tail")
                #print("ThreadClose event generated")
                self.end_event.set()
                break
        print("reading loop ended")
        self.input.close()
        print("reading thread ended")

    def connect_disconnect(self):
        if not self.socket:
            self.connect()
        else:
            self.disconnect()

    def connect(self):
        if self.status.get() == "connecting":
            return
        self.status.set("connecting")
        # 打开网络端口
        try:
            addr_tuple = (self.ip_addr.get(), self.ip_port.get())
            self.socket = socket.create_connection(addr_tuple, timeout=5)
            #self.socket = serial.Serial(serial_port, baudrate=115200, timeout=1)
        except Exception as e:
            print(f"error connect:{e}")
            self.socket = None
            return

        # socket connect successful
        self.status.set("connected")
        self.button_connect.config(text="disconnect")
        self.button_start.state(['!disabled'])
        self.socket.set_inheritable(True)
        self.command_eventfd.clear()
        self.end_event.clear()
        # 启动读取数据的线程
        self.socket_thread = threading.Thread(target=self.read_socket_data_thread)
        #self.socket_thread.daemon = True
        self.socket_thread.start()

    def disconnect(self):
        print("stopping reading thread...")
        self.command_eventfd.set()
        print("send stopping signal...")
        if self.socket_thread:
            self.socket_thread.join()
            self.socket_thread=None
        print("closing socket...")
        if self.socket:
            self.socket.shutdown(socket.SHUT_WR)
            try:
                data_remain = self.socket.recv(4096)
                while data_remain:
                    print(data_remain.decode())
                    data_remain = self.socket.recv(4096)
                self.socket.close()
            except TimeoutError as e:
                print(e)
        self.socket = None
        self.button_connect.config(text="connect")
        self.status.set("unconnected")
        self.button_start.state(['disabled'])
    
    def thread_close_callback(self, event):
        print("thread closed with Error")
        self.root.after(100, self.disconnect)

    def start_stop(self):
        if self.socket:
            self.socket.send('d\n'.encode())

    def layout(self):
        #root window
        self.root.title("Oscilloscope")
        #self.root.geometry("500x300")

        #variables
        self.ip_addr = tk.StringVar(self.root, self.ip_addr)
        self.ip_port = tk.StringVar(self.root, self.ip_port)
        self.status = tk.StringVar(self.root, "unconnected")
        
        #right frame
        self.frame_r = ttk.Frame(self.root)
        self.frame_r.grid(column=1, row=0)
        #bottom frame
        self.frame_b = ttk.Frame(self.root)
        self.frame_b.grid(column=0, row=1, columnspan=2, pady=5, sticky="EW")
        #matplotlib canvas
        self.canvas = FigureCanvasTkAgg(self.figure, master=self.root)
        self.canvas.get_tk_widget().grid(column=0, row=0)

        #buttons
        self.button_start = ttk.Button(self.frame_r, text="start/stop",
                                       command=self.start_stop)
        self.button_start.pack(padx=5, pady=10)
        self.button_start.state(['disabled'])

        self.button_connect = ttk.Button(self.frame_r, text="connect",
                                         command=self.connect_disconnect)
        self.button_connect.pack(padx=5, pady=10)
        
        self.button_quit = ttk.Button(self.frame_r, text="quit", command=self.quit)
        self.button_quit.pack(padx=5, pady=10)
        #ip address bar
        ttk.Label(self.frame_b, text="ip :").pack(side="left", padx=5)
        self.address_entry = ttk.Entry(self.frame_b, textvariable=self.ip_addr)
        self.address_entry.pack(side="left", padx=5)
        
        ttk.Label(self.frame_b, text="port :").pack(side="left", padx=5)
        self.port_entry = ttk.Entry(self.frame_b, textvariable=self.ip_port, width=6)
        self.port_entry.pack(side="left", padx=5)
        #status bar
        self.status_bar = ttk.Label(self.frame_b, textvariable=self.status)
        self.status_bar.pack(side="right", padx=5)
        ttk.Label(self.frame_b, text="status :").pack(side="right", padx=5)

if __name__ == "__main__":
    root = tk.Tk()
    ip_addr = '192.168.1.66'  # 替换为实际ip及端口号，例如 192.168.1.1:3333
    #ip_addr = '127.0.0.1'
    ip_port = 3333
    app = OscilloscopeApp(root, ip_addr, ip_port)
    root.mainloop()
