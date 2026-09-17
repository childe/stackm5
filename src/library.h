// Flash 曲库：一首歌一个纯文本文件，存在 LittleFS 上。
//
// 分区表 default_8MB.csv 里的 spiffs 分区（0x670000 起、1.5MB）挂成 LittleFS。
// 文件内容就是谱面原文（第一行头部 + 后面音符），所见即所存 —— 以后想加
// SD 卡导入导出、或者用电脑写谱，格式天然兼容。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace library {

struct Entry {
    uint8_t id = 0;
    std::string preview;  // 谱面开头，用来在列表里靠旋律认歌
};

bool begin();  // 挂载（第一次开机时 flash 里还没文件系统，会自动格式化）

std::vector<Entry> list();  // 按 id 升序
bool load(uint8_t id, std::string &out);
bool save(uint8_t id, const std::string &text);
bool remove(uint8_t id);

// 新建一首，返回新的 id；-1 表示满了（最多 99 首）
int createNew(const std::string &text);

// 上次打开的曲子，开机恢复用。没有记录时返回 -1
int lastOpened();
void setLastOpened(uint8_t id);

// 内置示例的写入版本号。开机时如果存的版本比固件里的低，就补写新增的那些；
// 相等就一首都不动 —— 否则用户删掉的示例每次开机都会自己长回来。
// 没有记录时返回 0。
int seedVersion();
void setSeedVersion(int v);

}  // namespace library
