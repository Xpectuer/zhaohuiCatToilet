## 项目功能
本项目为一套使用ESP32作为智能猫砂盆的主控元件的硬件方案，综合处理压力传感器，红外传感器，按钮等传感器信息，驱动电机进行自动清理工作，并通过Wifi实现无线控制功能。

目前仅实现了Wifi连接与电机运转的测试功能

## 安装开发环境

根据 ESP-IDF(Espressif IoT Development Framework) 安装开发环境
[ESP-IDF 快速入门](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.2.1/esp32/get-started/index.html)

## 硬件需求

* 任意ESP32开发板，带usb线
* 12V直流有刷电机
* 12V直流电源
* DRV8871模块H桥驱动器
* 连接线若干

## 硬件连接

* ESP32开发板通过usb线连接到电脑
* 12V直流电源接DRV8871的Power+-两端
* 直流有刷电机接DRV8871的MOTOR两端
* ESP32 GND引脚接DRV8871的GND引脚
* ESP32 两个输出GPIO引脚（如 26,27, 需要与下面配置项目中的GPIO编号一致）接 DRV8871 的 In1,In2 引脚

## 设置运行时环境变量
安装完成 ESP-IDF 后, 

[Eclipse](https://github.com/espressif/idf-eclipse-plugin/blob/master/README_CN.md) 或[VSCode](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md) IDE用户：
按对应插件说明配置运行环境

Linux/MacOS 用户:

```sh
. $HOME/esp/esp-idf/export.sh
```

Windows 用户: 打开命令行cmd.exe，cd进入esp-idf安装目录，运行

```cmd
export.bat
```

## 配置项目
(至少在第一次编译前配置一次)


运行

```
idf.py set-target esp32
idf.py menuconfig
```

在弹出的文字图形界面中，
在 `Example Connection Configuration` 菜单下配置以下参数:

* 勾选 `connect using WiFi interface`
* 勾选 `Provide wiri connect commands`'
* 输入 WiFi SSID 为要连接的WiFi名称
* 输入 WiFi Password 为要连接的Wifi密码

在 `Example Configuration Options` 菜单下配置以下参数:

* 勾选 `IPV4` 或 `IPV6` 或两者都选
* 设置 `Port` 为 ESP32 硬件端 TCP 服务器的端口号
* 设置 `brushed motor driver PWM pin A`与`brushed motor driver PWM pin B` 为连接到 DRV8871 驱动模块的引脚GPIO编号(如26，27)

## 编译与烧写

编译固件:

```
idf.py build
```

将编译好的固件烧写到ESP32，请确保ESP32已通过usb连接到电脑，下面的/dev/ttyUSB0可换成对应的串口名称(Linux: `/dev/tty*`, macOS:`/dev/cu.*`, WINDOWS:`COM*`):

```
idf.py -p /dev/ttyUSB0 flash
```

打开串口监视器:

```
idf.py -p /dev/ttyUSB0 monitor
```

合并以上三条命令，编译，烧写并打开串口监视器:

```
idf.py -p /dev/ttyUSB0 flash monitor
```

(按下 ``Ctrl-]`` 可退出串口监视器)

可参考[ESP-IDF 快速入门](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.2.1/esp32/get-started/index.html)学习使用ESP-IDF建立项目的完整步骤

## 常见问题

