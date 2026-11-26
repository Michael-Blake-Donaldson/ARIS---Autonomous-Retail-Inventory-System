#pragma once

#include "argus/audit/mission.hpp"
#include "argus/mission/store.hpp"
#include "live_engine.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace argus::desktop {

enum class JobKind {
    catalog,
    lookup,
    find,
    audit,
    shelf_audit,
    scan_mission,
    dock_mission,
    inspect_mission,
    confirm_inspection,
    cancel_mission,
    save_run,
    reopen_run
};

struct ShelfAssignment {
    inventory::BinId location;
    RobotId robot;
    RunId run;
    std::optional<std::filesystem::path> journal;
};

struct Job {
    JobKind kind;
    std::string text{};
    std::optional<inventory::Epc> after{};
    std::optional<MissionId> subject{};
    std::unique_ptr<LiveEngine> live{};
    std::optional<mission::SavedRun> archive{};
    std::optional<ShelfAssignment> shelf{};
};

struct WorkResult {
    JobKind kind{JobKind::catalog};
    std::string status{};
    std::vector<inventory::Product> products{};
    std::optional<inventory::ProductPage> page{};
    std::optional<audit::Report> audit{};
    std::optional<mission::Record> mission{};
    std::vector<mission::Event> events{};
    std::unique_ptr<LiveEngine> live{};
    std::optional<mission::SavedRun> archive{};
    std::string review_text{};
    std::optional<mission::ShelfAudit> shelf{};
};

class Workbench {
  public:
    explicit Workbench(std::optional<std::filesystem::path> directory = {}, bool paced = true,
                       bool stockroom = false);
    ~Workbench();
    Workbench(const Workbench &) = delete;
    Workbench &operator=(const Workbench &) = delete;
    [[nodiscard]] bool submit(Job &&job);
    [[nodiscard]] std::optional<WorkResult> take();
    [[nodiscard]] bool busy() const;
    [[nodiscard]] std::shared_ptr<const ObservedFrame> preview() const;
    void interrupt(bool emergency = false) noexcept;

  private:
    void run(std::stop_token stop, const std::optional<std::filesystem::path> &directory,
             bool paced, bool stockroom);
    void publish(WorkResult result);

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::optional<Job> pending_{Job{JobKind::catalog}};
    std::optional<WorkResult> result_;
    std::shared_ptr<const ObservedFrame> preview_;
    std::atomic_bool interrupted_{};
    std::atomic_bool emergency_{};
    bool busy_{true};
    bool available_{true};
    std::jthread worker_;
};

} // namespace argus::desktop
