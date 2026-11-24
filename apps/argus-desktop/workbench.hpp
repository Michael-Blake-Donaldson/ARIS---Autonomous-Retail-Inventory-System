#pragma once

#include "argus/audit/mission.hpp"
#include "argus/mission/store.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace argus::desktop {

enum class JobKind {
    catalog,
    lookup,
    find,
    audit,
    scan_mission,
    dock_mission,
    inspect_mission,
    confirm_inspection,
    cancel_mission
};

struct Job {
    JobKind kind;
    std::string text{};
    std::optional<inventory::Epc> after{};
    std::optional<MissionId> subject{};
};

struct WorkResult {
    JobKind kind{JobKind::catalog};
    std::string status{};
    std::vector<inventory::Product> products{};
    std::optional<inventory::ProductPage> page{};
    std::optional<audit::Report> audit{};
    std::optional<mission::Record> mission{};
    std::vector<mission::Event> events{};
};

class Workbench {
  public:
    explicit Workbench(std::optional<std::filesystem::path> directory = {});
    ~Workbench();
    Workbench(const Workbench &) = delete;
    Workbench &operator=(const Workbench &) = delete;
    [[nodiscard]] bool submit(Job job);
    [[nodiscard]] std::optional<WorkResult> take();
    [[nodiscard]] bool busy() const;

  private:
    void run(std::stop_token stop, const std::optional<std::filesystem::path> &directory);
    void publish(WorkResult result);

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::optional<Job> pending_{Job{JobKind::catalog}};
    std::optional<WorkResult> result_;
    bool busy_{true};
    bool available_{true};
    std::jthread worker_;
};

} // namespace argus::desktop
