#include "modules/search.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>

namespace cstatic {
namespace modules {

void generate_search_index(const Config& cfg, const nlohmann::json& pages,
                           const std::string& output_dir) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& page : pages) {
        std::string url = page.value("url", "");
        std::string title = page.value("title", "");
        if (url.empty() || title.empty()) continue;

        nlohmann::json entry;
        entry["title"]   = title;
        entry["url"]     = url;
        entry["excerpt"] = page.value("excerpt", "");
        entry["date"]    = page.value("date", "");
        entry["tags"]    = page.value("tags", nlohmann::json::array());
        entries.push_back(entry);
    }

    nlohmann::json index;
    index["pages"] = entries;

    std::string path = output_dir + "/" + cfg.search_output;
    utils::write_file(path, index.dump(2));
}

} // namespace modules
} // namespace cstatic
