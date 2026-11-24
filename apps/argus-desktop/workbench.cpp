#include "workbench.hpp"

#include "argus/mission/simulation.hpp"
#include "operations_fixture.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace argus::desktop {
namespace {

class Sandbox {
  public:
    Sandbox() {
        const auto root = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int attempt = 0; attempt < 8; ++attempt) {
            auto candidate = root / ("argus-workbench-" + std::to_string(random()) + "-" +
                                     std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("sandbox");
    }
    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;
    ~Sandbox() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

template <typename T, typename E> T checked(Result<T, E> result) {
    if (auto *value = std::get_if<T>(&result)) {
        return std::move(*value);
    }
    throw std::string{label(std::get<E>(result))};
}

WorkResult process(Job job, inventory::Repository &catalog, mission::Store &missions,
                   std::uint64_t &sequence) {
    WorkResult result{.kind = job.kind};
    const auto next_id = [&] {
        if (sequence >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::string{"id_capacity_reached"};
        }
        return ++sequence;
    };
    switch (job.kind) {
    case JobKind::catalog:
        if (job.text.empty()) {
            result.mission = checked(missions.latest());
        }
        result.products = checked(catalog.products(
            job.text.empty() ? std::nullopt : std::optional{inventory::Dpci{std::move(job.text)}}));
        result.status = "Inventory ready";
        break;
    case JobKind::lookup:
        result.page = checked(catalog.find(inventory::Dpci{std::move(job.text)}, job.after));
        result.status = "Catalog results";
        break;
    case JobKind::find:
    case JobKind::audit: {
        const std::string item = std::move(job.text);
        const auto request = job.kind == JobKind::find
                                 ? audit::Request{audit::FindItem{inventory::Dpci{item}, {}}}
                                 : audit::Request{audit::AuditAisle{inventory::AisleId{item}}};
        result.audit = checked(audit::run_demo(catalog, request, inventory::SessionId{next_id()}));
        result.status = "Investigation complete";
        break;
    }
    case JobKind::scan_mission:
    case JobKind::dock_mission:
    case JobKind::inspect_mission: {
        const auto workflow = job.kind == JobKind::scan_mission   ? mission::Workflow::scan_all
                              : job.kind == JobKind::dock_mission ? mission::Workflow::dock
                                                                  : mission::Workflow::inspect;
        const auto priority = workflow == mission::Workflow::dock ? mission::Priority::safety
                                                                  : mission::Priority::routine;
        mission::Intent intent{MissionId{next_id()}, workflow, priority, 100000};
        if (workflow == mission::Workflow::inspect) {
            intent.aisle = inventory::AisleId{std::move(job.text)};
        }
        result.mission = checked(missions.submit(intent));
        auto executed = checked(mission::run_demo(missions, &catalog));
        if (executed.record) {
            result.mission = std::move(executed.record);
        }
        result.status = "Mission recorded";
        break;
    }
    case JobKind::confirm_inspection:
    case JobKind::cancel_mission:
        if (!job.subject) {
            throw std::string{"invalid_mission"};
        }
        result.mission = job.kind == JobKind::confirm_inspection
                             ? checked(missions.confirm_inspection(*job.subject))
                             : checked(missions.cancel(*job.subject));
        result.status = "Mission updated";
        break;
    }
    if (result.mission) {
        result.events = checked(missions.events(result.mission->intent.id));
    }
    return result;
}

} // namespace

Workbench::Workbench(std::optional<std::filesystem::path> directory)
    : worker_{[this, directory = std::move(directory)](std::stop_token stop) {
          run(stop, directory);
      }} {}

Workbench::~Workbench() {
    worker_.request_stop();
    condition_.notify_all();
}

bool Workbench::submit(Job job) {
    std::scoped_lock lock{mutex_};
    if (!available_ || busy_ || result_ || job.text.size() > 32 ||
        (job.after && !inventory::valid(*job.after))) {
        return false;
    }
    pending_ = std::move(job);
    busy_ = true;
    condition_.notify_all();
    return true;
}

bool Workbench::busy() const {
    std::scoped_lock lock{mutex_};
    return busy_;
}

std::optional<WorkResult> Workbench::take() {
    std::scoped_lock lock{mutex_};
    return std::exchange(result_, std::nullopt);
}

void Workbench::publish(WorkResult result) {
    std::scoped_lock lock{mutex_};
    result_ = std::move(result);
    busy_ = false;
}

void Workbench::run(std::stop_token stop, const std::optional<std::filesystem::path> &directory) {
    try {
        std::optional<Sandbox> sandbox;
        if (!directory) {
            sandbox.emplace();
        } else if (!std::filesystem::is_directory(*directory)) {
            throw std::string{"invalid_data_directory"};
        }
        const auto &root = directory ? *directory : sandbox->path;
        const auto file = root / "catalog.db";
        const bool seed_catalog = !std::filesystem::exists(file);
        auto catalog = checked(inventory::Repository::open(file));
        auto store = checked(mission::Store::open(root / "missions.db"));
        if (seed_catalog) {
            checked(catalog.insert_catalog(
                checked(inventory::parse_catalog(catalog_fixture, inventory::Format::csv))));
        }
        const auto previous = checked(store.latest());
        auto sequence = std::max(checked(catalog.last_session()),
                                 previous ? previous->intent.id.value() : std::uint64_t{0});
        while (!stop.stop_requested()) {
            std::unique_lock lock{mutex_};
            if (!condition_.wait(lock, stop, [this] { return pending_.has_value(); })) {
                break;
            }
            auto job = std::exchange(pending_, std::nullopt).value();
            lock.unlock();
            const auto kind = job.kind;
            try {
                publish(process(std::move(job), catalog, store, sequence));
            } catch (const std::string &error) {
                publish(WorkResult{.kind = kind, .status = error});
            }
        }
    } catch (...) {
        std::scoped_lock lock{mutex_};
        available_ = false;
        busy_ = false;
        result_ = WorkResult{.status = "Demo data unavailable"};
    }
}

} // namespace argus::desktop
