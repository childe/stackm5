# 两块 M5Stack 设备的常用指令。
# 只依赖 PlatformIO Core：brew install platformio
#
# 日常循环：make test && make flash adv

# 设备名直接就是 platformio.ini 里的环境名，所以这里不需要任何映射：
#   adv   = Cardputer-Adv（简谱 / 背单词 / 遥控器）
#   faces = StackChan Core + Faces Bottom3 + Keyboard3（桌面 app 集）
#
# 设备可以写成目标（make flash faces）或变量（make flash DEV=faces）。
# 目标形式更好打，但 make 会把 faces 当成一个要构建的目标，
# 所以下面给 adv / faces 各留一个空规则把它消化掉。
DEV ?= $(if $(filter faces,$(MAKECMDGOALS)),faces,adv)
ENV := $(DEV)

.PHONY: help build flash adv faces test monitor clean

help:
	@echo 'make build [adv|faces]   编译（默认 adv）'
	@echo 'make flash [adv|faces]   编译 + 烧写（默认 adv）'
	@echo 'make monitor [adv|faces] 看串口输出（要在真实终端里跑）'
	@echo 'make test                在电脑上跑单元测试（不需要设备，约 3 秒）'
	@echo 'make clean [adv|faces]   清掉构建产物'
	@echo
	@echo 'adv   = Cardputer-Adv'
	@echo 'faces = StackChan Core + Faces Keyboard3'
	@echo '也可以写 make flash DEV=faces'

# 让 `make flash faces` 里的 faces 不被当成真正要构建的目标。
adv faces:
	@:

build:
	pio run -e $(ENV)

flash:
	pio run -e $(ENV) -t upload

test:
	pio test -e native

# 串口需要交互式终端，管道或重定向会报错。
# 脚本里读串口就直接用 pyserial，别走这个。
monitor:
	pio device monitor -e $(ENV)

clean:
	pio run -e $(ENV) -t clean
