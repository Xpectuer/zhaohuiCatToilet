import tkinter as tk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import threading
import sys

class OscilloscopeApp:
    def __init__(self, root):
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
        
        # 开始监视标准输入的线程
        self.stdin_thread = threading.Thread(target=self.read_stdin)
        self.stdin_thread.daemon = True
        self.stdin_thread.start()
        
        self.update_plot()
    
    def update_plot(self):
        self.line.set_data(self.xdata, self.data)
        self.canvas.draw()
        self.canvas.flush_events()
        self.root.after(10, self.update_plot)  # 每隔10ms更新一次
    
    def read_stdin(self):
        while True:
            try:
                line = sys.stdin.readline().strip()
                if line:
                    new_data = float(line)
                    self.data = np.append(self.data[1:], new_data)
                    # 根据当前数据更新Y轴范围
                    min_data = np.min(self.data)
                    max_data = np.max(self.data)
                    margin = 0.1 * (max_data - min_data)
                    self.ax.set_ylim(min_data - margin, max_data + margin)
                    self.canvas.draw()
            except Exception as e:
                print(f"Error reading from stdin: {str(e)}")

if __name__ == "__main__":
    root = tk.Tk()
    app = OscilloscopeApp(root)
    root.mainloop()
