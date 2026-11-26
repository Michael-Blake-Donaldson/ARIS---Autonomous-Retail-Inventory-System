#include "workbench.hpp"

#include "argus/mission/simulation.hpp"
#include "fleet_report.hpp"
#include "operations_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
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

std::optional<nlohmann::json> archive_report(const mission::SavedRun &saved,
                                             inventory::Repository &catalog,
                                             mission::Store &store) {
    if (saved.report.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    std::size_t events = 0;
    auto report = nlohmann::json::parse(
        saved.report,
        [&](int depth, nlohmann::json::parse_event_t, nlohmann::json &) {
            if (depth > 16 || ++events > 32768) {
                throw std::string{"Saved audit report exceeds its review limits"};
            }
            return true;
        },
        false);
    if (!report.is_object() || !report.contains("session") ||
        !report["session"].is_number_unsigned() || report["session"] != saved.audit_session ||
        !report.contains("request") || !report["request"].is_object() ||
        !report["request"].contains("mode") || report["request"]["mode"] != "audit" ||
        !report.contains("items") || !report["items"].is_array()) {
        return std::nullopt;
    }
    const auto session = catalog.session(inventory::SessionId{saved.audit_session});
    if (!std::holds_alternative<inventory::ScanSession>(session) ||
        (saved.search_session != 0 &&
         !std::holds_alternative<inventory::ScanSession>(
             catalog.session(inventory::SessionId{saved.search_session}))) ||
        (saved.mission != 0 &&
         !std::holds_alternative<mission::Record>(store.get(MissionId{saved.mission})))) {
        return std::nullopt;
    }
    return report;
}

WorkResult process(Job &job, inventory::Repository &catalog, mission::Store &missions,
                   std::uint64_t &sequence, simulation::ObservedRun *live) {
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
        if (!live) {
            throw std::string{"Choose the RFID audit warehouse for routed work"};
        }
        result.audit =
            checked(audit::run_observed(catalog, request, inventory::SessionId{next_id()}, *live));
        result.status = "Investigation complete";
        break;
    }
    case JobKind::shelf_audit: {
        if (!live || !job.shelf) {
            throw std::string{"Shelf audit requires a live robot"};
        }
        const audit::AuditAisle scope{inventory::AisleId{job.text}};
        auto report = checked(audit::run_shelf(catalog, scope, inventory::SessionId{next_id()},
                                               job.shelf->robot, *live));
        if (report.visits.size() != 2 || std::ranges::any_of(report.visits, [](const auto &visit) {
                return visit.frames < audit::min_coverage_frames;
            })) {
            throw std::string{"Shelf coverage incomplete. Retry the audit."};
        }
        std::vector<inventory::Product> products;
        for (const auto &quantity : report.quantities) {
            products.push_back(checked(catalog.find(quantity.product)).product);
        }
        const auto detail = make_shelf_report(report, products);
        if (!detail) {
            throw std::string{"Shelf audit product records are incomplete"};
        }
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        auto encoded = encode_shelf_report(*detail);
        if (millis <= 0 || !decode_shelf_report(encoded)) {
            throw std::string{"Shelf audit exceeds its review limits"};
        }
        mission::ShelfAudit saved{job.shelf->location,
                                  job.shelf->robot,
                                  job.shelf->run,
                                  report.session.value(),
                                  static_cast<std::uint64_t>(millis),
                                  std::move(encoded)};
        if (job.shelf->journal) {
            auto journal = checked(mission::Store::open(*job.shelf->journal));
            checked(journal.save_shelf_audit(saved));
        }
        result.shelf = std::move(saved);
        result.status = "Shelf audit complete";
        break;
    }
    case JobKind::scan_mission:
    case JobKind::dock_mission:
    case JobKind::inspect_mission: {
        if (!live) {
            throw std::string{"Choose the RFID audit warehouse for routed work"};
        }
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
        job.subject = intent.id;
        auto executed =
            checked(mission::run_observed(missions, &catalog, *live, {.expected = intent.id}));
        if (executed.record) {
            result.mission = std::move(executed.record);
            result.status = "Mission recorded";
        } else {
            result.mission = checked(missions.cancel(intent.id));
            result.status = "Another mission is ahead of this run";
        }
        break;
    }
    case JobKind::save_run:
        if (!job.archive || !archive_report(*job.archive, catalog, missions)) {
            throw std::string{"Run evidence unavailable"};
        }
        checked(missions.save_run(*job.archive));
        result.status = "Run evidence saved";
        break;
    case JobKind::reopen_run: {
        result.archive = checked(missions.saved_run());
        if (result.archive) {
            const auto parsed = archive_report(*result.archive, catalog, missions);
            if (!parsed) {
                throw std::string{"Saved audit report is damaged or missing its records"};
            }
            auto formatted = parsed->dump(2);
            if (formatted.size() <= mission::archived_report_limit) {
                result.review_text = std::move(formatted);
            }
        }
        result.status = result.archive ? "Reviewing saved run" : "No saved run";
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

Workbench::Workbench(std::optional<std::filesystem::path> directory, bool paced)
    : worker_{[this, directory = std::move(directory), paced](std::stop_token stop) {
          run(stop, directory, paced);
      }} {}

Workbench::~Workbench() {
    interrupt();
    worker_.request_stop();
    condition_.notify_all();
}

bool Workbench::submit(Job &&job) {
    std::scoped_lock lock{mutex_};
    if (!available_ || busy_ || result_ || job.text.size() > 32 ||
        (job.after && !inventory::valid(*job.after))) {
        return false;
    }
    interrupted_ = false;
    emergency_ = false;
    preview_.reset();
    pending_ = std::move(job);
    busy_ = true;
    condition_.notify_all();
    return true;
}

std::shared_ptr<const ObservedFrame> Workbench::preview() const {
    std::scoped_lock lock{mutex_};
    return preview_;
}

void Workbench::interrupt(bool emergency) noexcept {
    if (emergency) {
        emergency_ = true;
    }
    interrupted_ = true;
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

void Workbench::run(std::stop_token stop, const std::optional<std::filesystem::path> &directory,
                    bool paced) {
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
        auto sequence = std::max(checked(catalog.last_session()), checked(store.highest_id()));
        while (!stop.stop_requested()) {
            std::unique_lock lock{mutex_};
            if (!condition_.wait(lock, stop, [this] { return pending_.has_value(); })) {
                break;
            }
            auto job = std::exchange(pending_, std::nullopt).value();
            lock.unlock();
            WorkResult result{.kind = job.kind};
            bool failed = false;
            try {
                std::optional<simulation::ObservedRun> observed;
                if (job.live) {
                    observed.emplace(job.live->borrow_run(
                        [&, ticks = 0U](const auto &navigator, const auto *scan) mutable {
                            job.live->observe_job(navigator, scan);
                            if (ticks++ % 8U == 0) {
                                auto frame = std::make_shared<ObservedFrame>(job.live->frame());
                                std::scoped_lock update{mutex_};
                                preview_ = std::move(frame);
                            }
                            if (paced) {
                                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                            }
                            return !interrupted_.load() && !stop.stop_requested();
                        }));
                }
                result = process(job, catalog, store, sequence, observed ? &*observed : nullptr);
            } catch (const simulation::RunInterrupted &) {
                failed = true;
                result.status = "Run held by operator";
            } catch (const std::string &error) {
                failed = true;
                result.status = error;
            } catch (const std::exception &) {
                failed = true;
                result.status = "Run operation failed";
            }
            if (job.live) {
                job.live->finish_job(emergency_.load());
                if (failed && job.subject) {
                    static_cast<void>(store.recover(job.subject));
                    if (auto current = store.get(*job.subject);
                        auto *record = std::get_if<mission::Record>(&current)) {
                        result.mission = std::move(*record);
                        if (auto journal = store.events(*job.subject);
                            auto *events = std::get_if<std::vector<mission::Event>>(&journal)) {
                            result.events = std::move(*events);
                        }
                    }
                }
                result.live = std::move(job.live);
            }
            publish(std::move(result));
        }
    } catch (...) {
        std::scoped_lock lock{mutex_};
        available_ = false;
        busy_ = false;
        result_ = WorkResult{.status = "Demo data unavailable"};
    }
}

} // namespace argus::desktop
