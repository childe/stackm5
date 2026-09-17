#include "library.h"

#include <LittleFS.h>
#include <jianpu.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char *kDir = "/songs";
constexpr const char *kLastFile = "/last";
constexpr const char *kSeedFile = "/seeded";
constexpr uint8_t kMaxId = 99;
constexpr size_t kPreviewBytes = 96;

void pathFor(uint8_t id, char *out, size_t n)
{
    std::snprintf(out, n, "%s/%02u.jp", kDir, static_cast<unsigned>(id));
}

// 文件名形如 "01.jp"。不同版本的 esp32 core 里 File::name() 有时给全路径、
// 有时只给文件名，所以统一取最后一段再解析。
int idFromName(const char *name)
{
    const char *base = std::strrchr(name, '/');
    base = base ? base + 1 : name;

    if (std::strlen(base) != 5) return -1;
    if (base[0] < '0' || base[0] > '9') return -1;
    if (base[1] < '0' || base[1] > '9') return -1;
    if (std::strcmp(base + 2, ".jp") != 0) return -1;

    return (base[0] - '0') * 10 + (base[1] - '0');
}

// 读开头一小段，去掉头部行，截到第一个换行 —— 拿来当列表里的预览
std::string previewOf(uint8_t id)
{
    char path[32];
    pathFor(id, path, sizeof(path));

    File f = LittleFS.open(path, "r");
    if (!f) return std::string();

    char buf[kPreviewBytes];
    const size_t n = f.readBytes(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    f.close();

    const size_t skip = jianpu::headerPrefixLen(buf, n);
    std::string s(buf + skip, n - skip);

    const size_t nl = s.find('\n');
    if (nl != std::string::npos) s.resize(nl);

    return s;
}

}  // namespace

bool library::begin()
{
    // 第一个参数 true = 挂载失败就格式化。第一次开机时 flash 里还没有
    // 文件系统，必须靠它。分区标签默认就是 "spiffs"，和分区表对得上。
    if (!LittleFS.begin(true)) return false;
    if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
    return true;
}

std::vector<library::Entry> library::list()
{
    std::vector<Entry> out;

    File dir = LittleFS.open(kDir);
    if (!dir || !dir.isDirectory()) return out;

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        const int id = idFromName(f.name());
        if (id < 0) continue;

        Entry e;
        e.id = static_cast<uint8_t>(id);
        out.push_back(e);
    }
    dir.close();

    // 目录遍历的顺序没有保证，按 id 排一下（简单插入排序，最多 99 项）
    for (size_t i = 1; i < out.size(); ++i) {
        Entry key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].id > key.id) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }

    // 预览要在目录句柄关掉之后再读，避免同时开太多文件
    for (Entry &e : out) e.preview = previewOf(e.id);

    return out;
}

bool library::load(uint8_t id, std::string &out)
{
    char path[32];
    pathFor(id, path, sizeof(path));

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    out.clear();
    out.reserve(f.size());
    while (f.available()) {
        char chunk[128];
        const size_t n = f.readBytes(chunk, sizeof(chunk));
        if (n == 0) break;
        out.append(chunk, n);
    }
    f.close();

    return true;
}

bool library::save(uint8_t id, const std::string &text)
{
    char path[32];
    pathFor(id, path, sizeof(path));

    File f = LittleFS.open(path, "w");
    if (!f) return false;

    const size_t written = f.write(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    f.close();

    return written == text.size();
}

bool library::remove(uint8_t id)
{
    char path[32];
    pathFor(id, path, sizeof(path));
    return LittleFS.remove(path);
}

int library::createNew(const std::string &text)
{
    for (uint8_t id = 1; id <= kMaxId; ++id) {
        char path[32];
        pathFor(id, path, sizeof(path));
        if (!LittleFS.exists(path)) {
            return save(id, text) ? static_cast<int>(id) : -1;
        }
    }
    return -1;  // 99 首满了
}

int library::lastOpened()
{
    File f = LittleFS.open(kLastFile, "r");
    if (!f) return -1;

    char buf[8] = {0};
    const size_t n = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    if (n == 0) return -1;

    const int id = std::atoi(buf);
    return (id >= 1 && id <= kMaxId) ? id : -1;
}

int library::seedVersion()
{
    File f = LittleFS.open(kSeedFile, "r");
    if (!f) return 0;

    char buf[8] = {0};
    const size_t n = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    if (n == 0) return 0;

    // 老固件在这个文件里写的是 "1"，正好等于第 1 版，不用特殊处理
    const int v = std::atoi(buf);
    return (v > 0) ? v : 0;
}

void library::setSeedVersion(int v)
{
    File f = LittleFS.open(kSeedFile, "w");
    if (!f) return;

    char buf[8];
    const int n = std::snprintf(buf, sizeof(buf), "%d", v);
    f.write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(n));
    f.close();
}

void library::setLastOpened(uint8_t id)
{
    File f = LittleFS.open(kLastFile, "w");
    if (!f) return;

    char buf[8];
    const int n = std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(id));
    f.write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(n));
    f.close();
}
