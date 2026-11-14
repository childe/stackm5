#include "agents.h"

#include <iterator>

namespace agents {

namespace {

// 按 \t 切成字段
std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = s.find('\t', start);
        out.push_back(s.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) return out;
        start = tab + 1;
    }
}

const Session *findById(const std::vector<Session> &list, const std::string &id)
{
    for (const auto &s : list) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

}  // namespace

bool Model::feedLine(const std::string &raw)
{
    std::string line = raw;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

    if (line == "B") {
        pending_.clear();
        receiving_ = true;
        return false;
    }
    if (line == "E") {
        if (!receiving_) return false;
        receiving_ = false;
        commit();
        return true;
    }
    if (!receiving_ || line.size() < 2 || line[0] != 'S' || line[1] != '\t') return false;

    const std::vector<std::string> f = split(line);
    if (f.size() < 6 || f[1].empty() || f[2].size() != 1 || f[3].size() != 1) return false;
    pending_.push_back({f[1], f[2][0], f[3] == "r" ? State::Running : State::Waiting, f[4], f[5]});
    return false;
}

void Model::commit()
{
    const std::string prevId = selected() ? selected()->id : std::string();
    const int prevRow = cursor_;

    alerts_.clear();
    if (haveBaseline_) {
        for (const auto &s : pending_) {
            const Session *old = findById(list_, s.id);
            if (old && old->state == State::Running && s.state == State::Waiting) {
                alerts_.push_back(s.id);
                marks_.insert(s.id);
            }
        }
    }
    haveBaseline_ = true;

    list_.swap(pending_);
    pending_.clear();

    for (auto it = marks_.begin(); it != marks_.end();) {
        it = findById(list_, *it) ? std::next(it) : marks_.erase(it);
    }

    reselect(alerts_.empty() ? prevId : alerts_[0], prevRow);
}

void Model::reselect(const std::string &target, int prevRow)
{
    const std::vector<const Session *> vis = visible();
    const int n = static_cast<int>(vis.size());
    cursor_ = -1;
    for (int i = 0; i < n && !target.empty(); ++i) {
        if (vis[i]->id == target) cursor_ = i;
    }
    if (cursor_ < 0 && n > 0) cursor_ = prevRow < 0 ? 0 : (prevRow < n ? prevRow : n - 1);
}

void Model::setFilter(Filter f)
{
    if (f == filter_) return;
    const std::string prevId = selected() ? selected()->id : std::string();
    const int prevRow = cursor_;
    filter_ = f;
    reselect(prevId, prevRow);
}

std::vector<const Session *> Model::visible() const
{
    std::vector<const Session *> out;
    for (const auto &s : list_) {
        if (filter_ == Filter::All || s.state == State::Waiting) out.push_back(&s);
    }
    return out;
}

void Model::advance()
{
    const int n = static_cast<int>(visible().size());
    if (n == 0) return;
    cursor_ = (cursor_ + 1) % n;
}

void Model::reset()
{
    list_.clear();
    pending_.clear();
    receiving_ = false;
    haveBaseline_ = false;
    alerts_.clear();
    marks_.clear();
    cursor_ = -1;
}

const Session *Model::selected() const
{
    if (cursor_ < 0) return nullptr;
    const std::vector<const Session *> vis = visible();
    return cursor_ < static_cast<int>(vis.size()) ? vis[cursor_] : nullptr;
}

void Model::moveCursor(int delta)
{
    const std::vector<const Session *> vis = visible();
    const int n = static_cast<int>(vis.size());
    if (n == 0) return;
    int c = cursor_ + delta;
    if (c < 0) c = 0;
    if (c > n - 1) c = n - 1;
    cursor_ = c;
    marks_.erase(vis[cursor_]->id);
}

bool Rotation::due(uint32_t now)
{
    if (started_ && static_cast<int32_t>(now - next_) < 0) return false;
    started_ = true;
    next_ = now + kStepMs;
    return true;
}

void Rotation::hold(uint32_t now)
{
    started_ = true;
    next_ = now + kHoldMs;
}

std::string Model::focusSelected()
{
    const Session *s = selected();
    if (!s) return "";
    marks_.erase(s->id);
    return "F\t" + s->id;
}

}  // namespace agents
