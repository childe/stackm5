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

.PHONY: help build flash adv test monitor clean agentbridge agentbridge-install agentbridge-uninstall

help:
	@echo 'make build [adv]     编译'
	@echo 'make flash [adv]     编译 + 烧写'
	@echo 'make monitor [adv]   看串口输出（要在真实终端里跑）'
	@echo 'make test            在电脑上跑单元测试（不需要设备，约 5 秒）'
	@echo 'make clean [adv]     清掉构建产物'
	@echo 'make agentbridge            编译 Mac 端蓝牙桥（AGENTS app 用）'
	@echo 'make agentbridge-install    安装到 ~/.local/bin 并由 launchd 常驻'
	@echo 'make agentbridge-uninstall  停掉并删除'
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

# ── AGENTS app 的 Mac 端蓝牙桥 ──────────────────────────────
# Info.plist 嵌进二进制（__TEXT,__info_plist），launchd 启动时 TCC 才认它自己的蓝牙用途说明。
# ad-hoc 签名：TCC 按 cdhash 记授权，所以每次重新编译后要重新点一次蓝牙授权。
AB_DIR := tools/agentbridge
AB_BIN := $(AB_DIR)/build/agentbridge
AB_LABEL := com.childe.agentbridge
AB_PLIST := $(HOME)/Library/LaunchAgents/$(AB_LABEL).plist

# Command Line Tools 自带的 usr/include/swift 里 module.modulemap 和 bridging.modulemap 重复定义了
# SwiftBridging，swiftc 直接报 "redefinition of module"。不改系统文件，用 VFS overlay 把
# bridging.modulemap 换成空文件。没有这个问题的工具链上 overlay 也无害。
AB_CLT_SWIFT := /Library/Developer/CommandLineTools/usr/include/swift
AB_OVERLAY := $(AB_DIR)/build/overlay.yaml

agentbridge:
	mkdir -p $(AB_DIR)/build
	: > $(AB_DIR)/build/empty.modulemap
	printf '{"version":0,"roots":[{"name":"%s","type":"directory","contents":[{"name":"bridging.modulemap","type":"file","external-contents":"%s"}]}]}\n' \
	  $(AB_CLT_SWIFT) $(CURDIR)/$(AB_DIR)/build/empty.modulemap > $(AB_OVERLAY)
	swiftc -O -swift-version 5 -vfsoverlay $(AB_OVERLAY) -Xcc -ivfsoverlay -Xcc $(AB_OVERLAY) \
	  $(AB_DIR)/main.swift -o $(AB_BIN) \
	  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker $(AB_DIR)/Info.plist
	codesign -s - -f -i $(AB_LABEL) $(AB_BIN)

agentbridge-install: agentbridge
	mkdir -p $(HOME)/.local/bin $(HOME)/.cache $(HOME)/Library/LaunchAgents
	cp $(AB_BIN) $(HOME)/.local/bin/agentbridge
	sed 's|__HOME__|$(HOME)|g' $(AB_DIR)/$(AB_LABEL).plist > $(AB_PLIST)
	launchctl bootout gui/$$(id -u)/$(AB_LABEL) 2>/dev/null || true
	launchctl bootstrap gui/$$(id -u) $(AB_PLIST)

agentbridge-uninstall:
	launchctl bootout gui/$$(id -u)/$(AB_LABEL) 2>/dev/null || true
	rm -f $(AB_PLIST) $(HOME)/.local/bin/agentbridge
