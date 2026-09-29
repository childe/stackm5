# Cardputer-Adv 的常用指令。
# 只依赖 PlatformIO Core：brew install platformio
#
# 日常循环：make test && make flash adv

# 设备名直接就是 platformio.ini 里的环境名，所以这里不需要任何映射：
#   adv = Cardputer-Adv（简谱 / 背单词 / 遥控器 / 骰子 / 噪音计）
#
# 目前只有这一台，设备名可以省略；写成目标（make flash adv）或变量（make flash DEV=adv）也行。
# 目标形式下 make 会把 adv 当成一个要构建的目标，所以下面留一个空规则把它消化掉。
DEV ?= adv
ENV := $(DEV)

.PHONY: help build flash adv test monitor clean

help:
	@echo 'make build [adv]     编译'
	@echo 'make flash [adv]     编译 + 烧写'
	@echo 'make monitor [adv]   看串口输出（要在真实终端里跑）'
	@echo 'make test            在电脑上跑单元测试（不需要设备，约 5 秒）'
	@echo 'make clean [adv]     清掉构建产物'
	@echo
	@echo 'adv = Cardputer-Adv，可省略'

# 让 `make flash adv` 里的 adv 不被当成真正要构建的目标。
adv:
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
