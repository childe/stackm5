"""AGENTS app 调试探针：不经过 agentbridge 和 Hammerspoon，直接用蓝牙给 Cardputer 发列表。

用法（Cardputer 先进 AGENTS 页）：

    uv run --with bleak python tools/agents_probe.py

连上后发一份基线列表（id1 运行中、id2 该你了），3 秒后发第二份（id1 变成该你了），
Cardputer 应该响一声、闪 3 次、光标跳到 id1。之后每 2 秒重发第二份，并打印
Cardputer 发来的消息（按回车会收到 F<tab>id1）。Ctrl-C 退出。

要在 iTerm 等有蓝牙权限的终端里跑；从 Hammerspoon 起会被系统杀掉。
"""

import asyncio

from bleak import BleakClient, BleakScanner

SERVICE = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0001"
WRITE = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0002"
NOTIFY = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0003"

BASELINE = [
    "S\tid1\tc\tr\t电路图项目增加题目\t~/Projects/saisai.me",
    "S\tid2\tx\tw\t提供项目卡供选择 | stackm5\t~/Projects/stackm5",
]
SECOND = [
    "S\tid1\tc\tw\t电路图项目增加题目\t~/Projects/saisai.me",
    "S\tid2\tx\tw\t提供项目卡供选择 | stackm5\t~/Projects/stackm5",
]


async def send_list(client: BleakClient, rows: list[str]) -> None:
    for line in ["B", *rows, "E"]:
        await client.write_gatt_char(WRITE, line.encode(), response=True)


async def main() -> None:
    print("scanning for AGENTS service...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE in [u.lower() for u in ad.service_uuids], timeout=15
    )
    if device is None:
        raise SystemExit("not found: is the Cardputer on the AGENTS page?")

    async with BleakClient(device) as client:
        print("connected", device.address)
        await client.start_notify(NOTIFY, lambda _, data: print("<-", data.decode(errors="replace")))
        await send_list(client, BASELINE)
        print("-> baseline")
        await asyncio.sleep(3)
        while client.is_connected:
            await send_list(client, SECOND)
            print("-> second")
            await asyncio.sleep(2)
        print("disconnected")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
