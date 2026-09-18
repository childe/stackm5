# Cardputer 双 app 固件的常用指令。
# 只依赖 PlatformIO Core：brew install platformio
#
# 日常循环：make test && make flash

ENV := cardputer-adv

.PHONY: help build flash test monitor clean

help:
	@echo 'make build    编译'
	@echo 'make flash    编译 + 烧写到设备'
	@echo 'make test     在电脑上跑单元测试（不需要设备，约 3 秒）'
	@echo 'make monitor  看串口输出（要在真实终端里跑）'
	@echo 'make clean    清掉构建产物'

build:
	pio run -e $(ENV)

flash:
	pio run -e $(ENV) -t upload

test:
	pio test -e native

# 串口需要交互式终端，管道或重定向会报错。
# 脚本里读串口就直接用 pyserial，别走这个。
monitor:
	pio device monitor

clean:
	pio run -e $(ENV) -t clean
