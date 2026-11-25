#include "run_store.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    json toJson(const RunRecord& r)
    {
        json j;
        j["id"] = r.id;
        j["status"] = r.status;
        j["createdAt"] = r.createdAt;
        j["spec"] = r.spec;
        if (!r.result.is_null())
            j["result"] = r.result;
        if (!r.error.empty())
            j["error"] = r.error;
        j["kind"] = r.kind;
        j["ideas"] = r.ideas;
        if (!r.progress.is_null())
            j["progress"] = r.progress;
        return j;
    }

    // Throws on a file that is not a run record.
    RunRecord fromJson(const json& j)
    {
        RunRecord r;
        r.id = j.at("id").get<std::string>();
        r.status = j.at("status").get<std::string>();
        r.createdAt = j.at("createdAt").get<std::string>();
        r.spec = j.at("spec");
        if (j.contains("result"))
            r.result = j.at("result");
        if (j.contains("error"))
            r.error = j.at("error").get<std::string>();
        if (j.contains("kind"))                     // files from before sweeps existed have none: a plain run of one idea
            r.kind = j.at("kind").get<std::string>();
        if (j.contains("ideas"))
            r.ideas = j.at("ideas").get<int>();
        if (j.contains("progress"))
            r.progress = j.at("progress");
        return r;
    }

    std::optional<RunRecord> readFile(const fs::path& path)
    {
        std::ifstream in(path);
        if (!in)
            return std::nullopt;
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded() || !doc.is_object())
            throw std::runtime_error("run file is damaged: " + path.string());
        try
        {
            return fromJson(doc);
        }
        catch (const json::exception&)
        {
            throw std::runtime_error("run file is damaged: " + path.string());
        }
    }
}

bool RunStore::isValidId(const std::string& id)
{
    return !id.empty() && id.size() <= 12 && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

RunStore::RunStore(std::string dir) : dir_(std::move(dir))
{
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec || !fs::is_directory(dir_))
        throw std::runtime_error("cannot create the runs folder: " + dir_);

    for (const auto& entry : fs::directory_iterator(dir_))
    {
        const std::string stem = entry.path().stem().string();
        if (entry.path().extension() == ".json" && isValidId(stem))
            next_ = std::max(next_, std::stoll(stem) + 1);
    }
}

std::string RunStore::pathOf(const std::string& id) const
{
    if (!isValidId(id))
        throw std::invalid_argument("bad run id: " + id);
    return (fs::path(dir_) / (id + ".json")).string();
}

std::string RunStore::nextId()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << std::setw(6) << std::setfill('0') << next_++;
    return out.str();
}

void RunStore::save(const RunRecord& record)
{
    const std::string path = pathOf(record.id);
    const std::string temp = path + ".tmp";

    std::lock_guard<std::mutex> lock(mutex_);
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + temp);
        out << toJson(record).dump();
        if (!out)
            throw std::runtime_error("cannot write " + temp);
    }
    fs::rename(temp, path);
}

std::optional<RunRecord> RunStore::load(const std::string& id) const
{
    const std::string path = pathOf(id);
    std::lock_guard<std::mutex> lock(mutex_);
    return readFile(path);
}

std::vector<RunRecord> RunStore::list() const
{
    std::vector<RunRecord> all;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : fs::directory_iterator(dir_))
    {
        if (entry.path().extension() != ".json" || !isValidId(entry.path().stem().string()))
            continue;
        try
        {
            if (auto r = readFile(entry.path()))
                all.push_back(std::move(*r));
        }
        catch (const std::runtime_error&)
        {
            // a damaged file is skipped
        }
    }
    std::sort(all.begin(), all.end(), [](const RunRecord& a, const RunRecord& b) { return std::stoll(a.id) > std::stoll(b.id); });
    return all;
}
