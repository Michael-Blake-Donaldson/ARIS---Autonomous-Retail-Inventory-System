#include "workbench.hpp"

#include "argus/audit/stockroom.hpp"
#include "argus/mission/simulation.hpp"
#include "fleet_report.hpp"
#include "operations_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

ShelfReport location_report(const inventory::BinId &bin, const audit::LocationSurvey &evidence) {
    const auto &catalog = audit::stockroom_catalog();
    std::map<std::string_view, ShelfProduct, std::less<>> index;
    const auto row = [&](const inventory::Dpci &id) -> ShelfProduct & {
        const auto product = std::ranges::find(catalog.products, id, &inventory::Product::id);
        if (product == catalog.products.end()) {
            throw std::string{"Missing catalog product"};
        }
        return index.try_emplace(product->id.value, ShelfProduct{*product, 0, 0, {}}).first->second;
    };
    for (const auto &stock : catalog.stock) {
        if (stock.bin == bin) {
            row(stock.product).expected += stock.quantity;
        }
    }
    for (const auto &unit : catalog.units) {
        if (unit.assigned_bin == bin &&
            std::ranges::find(evidence.observed, unit.epc) == evidence.observed.end()) {
            row(unit.product).tags.push_back({unit.epc, false});
        }
    }
    ShelfReport report;
    for (const auto &epc : evidence.observed) {
        const auto unit = std::ranges::find(catalog.units, epc, &inventory::Unit::epc);
        if (unit == catalog.units.end()) {
            report.unidentified.push_back(epc);
        } else {
            auto &product = row(unit->product);
            ++product.observed;
            product.tags.push_back({epc, true});
        }
    }
    for (auto &[id, product] : index) {
        static_cast<void>(id);
        report.products.push_back(std::move(product));
    }
    report.unresolved = evidence.unresolved;
    return report;
}

WorkResult process(Job &job, inventory::Repository &catalog, mission::Store &missions,
                   std::uint64_t &sequence, simulation::ObservedRun *live,
                   const Workbench::FleetWorker *fleet) {
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
        const auto session = inventory::SessionId{next_id()};
        const auto locations = audit::stockroom_locations();
        const auto location =
            std::ranges::find(locations, job.shelf->location, &audit::StockLocation::id);
        if (location == locations.end()) {
            throw std::string{"Unknown stockroom location"};
        }
        const auto survey =
            audit::survey_location(catalog, *location, session, job.shelf->robot, *live);
        const auto *evidence = std::get_if<audit::LocationSurvey>(&survey);
        if (!evidence) {
            throw std::string{"Location coverage incomplete. Retry the audit."};
        }
        if (fleet && !audit::return_to_berth(*live, fleet->id)) {
            throw std::string{"Robot could not clear the aisle"};
        }
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        auto encoded = encode_shelf_report(location_report(location->id, *evidence));
        if (millis <= 0 || !decode_shelf_report(encoded)) {
            throw std::string{"Shelf audit exceeds its review limits"};
        }
        mission::ShelfAudit saved{job.shelf->location,
                                  job.shelf->robot,
                                  job.shelf->run,
                                  session.value(),
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
    case JobKind::clear_aisle:
        if (!live || !fleet || !audit::return_to_berth(*live, fleet->id)) {
            throw std::string{"Robot could not clear the aisle"};
        }
        result.status = "Aisle released";
        break;
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

Workbench::Workbench(std::optional<std::filesystem::path> directory, bool paced,
                     std::optional<FleetWorker> fleet)
    : fleet_clock_{fleet ? fleet->clock : nullptr},
      worker_{[this, directory = std::move(directory), paced,
               fleet = std::move(fleet)](std::stop_token stop) mutable {
          run(stop, directory, paced, std::move(fleet));
      }} {}

Workbench::~Workbench() {
    if (fleet_clock_) {
        fleet_clock_->close();
    }
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
    return busy_ || result_.has_value();
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

void Workbench::publish_preview(const LiveEngine &engine) {
    auto frame = std::make_shared<ObservedFrame>(engine.frame());
    std::scoped_lock lock{mutex_};
    preview_ = std::move(frame);
}

void Workbench::run(std::stop_token stop, const std::optional<std::filesystem::path> &directory,
                    bool paced, std::optional<FleetWorker> fleet) {
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
            const auto demo =
                fleet ? audit::stockroom_catalog()
                      : checked(inventory::parse_catalog(catalog_fixture, inventory::Format::csv));
            checked(catalog.insert_catalog(demo));
        }
        auto sequence = std::max(checked(catalog.last_session()), checked(store.highest_id()));
        while (!stop.stop_requested()) {
            std::unique_lock lock{mutex_};
            if (fleet && !pending_) {
                lock.unlock();
                if (!fleet->engine->step({}, LiveAction::none, false)) {
                    fleet->access->finish(fleet->id, false);
                    std::scoped_lock lock{mutex_};
                    available_ = false;
                    busy_ = false;
                    pending_.reset();
                    result_ = WorkResult{.status = "Shared simulation stopped"};
                    return;
                }
                const auto live = fleet->engine->frame();
                if (live.telemetry.sample_tick % 4U == 0) {
                    publish_preview(*fleet->engine);
                }
                if (!fleet->access->working()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                }
                continue;
            }
            if (!fleet && !condition_.wait(lock, stop, [this] { return pending_.has_value(); })) {
                break;
            }
            auto job = std::exchange(pending_, std::nullopt).value();
            lock.unlock();
            if (fleet && job.kind != JobKind::catalog) {
                job.live = std::move(fleet->engine);
            }
            WorkResult result{.kind = job.kind};
            bool failed = false;
            try {
                std::optional<simulation::ObservedRun> observed;
                if (job.live) {
                    if (fleet) {
                        unsigned ticks = 0;
                        while (!interrupted_.load() && !stop.stop_requested() &&
                               !fleet->access->admit(fleet->id)) {
                            job.live->traffic_yield(true);
                            if (!job.live->step({}, LiveAction::none, false)) {
                                throw simulation::RunInterrupted{};
                            }
                            if (ticks++ % 4U == 0) {
                                publish_preview(*job.live);
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds{1});
                        }
                        job.live->traffic_yield(false);
                        if (interrupted_.load() || stop.stop_requested()) {
                            throw simulation::RunInterrupted{};
                        }
                    }
                    observed.emplace(job.live->borrow_run(
                        [&, ticks = 0U](const auto &navigator, const auto *scan) mutable {
                            job.live->observe_job(navigator, scan);
                            if (ticks++ % 8U == 0) {
                                publish_preview(*job.live);
                            }
                            if (paced) {
                                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                            }
                            return !interrupted_.load() && !stop.stop_requested();
                        }));
                }
                result = process(job, catalog, store, sequence, observed ? &*observed : nullptr,
                                 fleet ? &*fleet : nullptr);
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
                if (fleet) {
                    const auto live = job.live->frame();
                    const auto berth = audit::stockroom_berth(fleet->id).position;
                    const auto &pose = live.telemetry.estimated_pose;
                    const auto error = live.estimate ? live.estimate->distance_error_m : 1000.0;
                    const bool parked = std::hypot(pose.x.value() - berth.x / 1000.0,
                                                   pose.y.value() - berth.y / 1000.0) +
                                                error <
                                            0.55 &&
                                        std::abs(live.telemetry.measured.linear.value()) < 0.025;
                    fleet->access->finish(fleet->id, parked);
                    fleet->engine = std::move(job.live);
                    publish_preview(*fleet->engine);
                } else {
                    result.live = std::move(job.live);
                }
            }
            publish(std::move(result));
        }
    } catch (...) {
        if (fleet) {
            fleet->clock->close();
        }
        std::scoped_lock lock{mutex_};
        available_ = false;
        busy_ = false;
        result_ = WorkResult{.status = "Demo data unavailable"};
    }
}

} // namespace argus::desktop
