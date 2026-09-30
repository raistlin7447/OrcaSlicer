#include "core/Reporters.hpp"

#include "core/Document.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Slic3r { namespace Bench {

namespace {

constexpr std::size_t line_width  = 80;
constexpr std::size_t stage_width = 29;
constexpr std::size_t time_width  = 8;
constexpr std::size_t cv_width    = 7;
constexpr std::size_t share_width = 7;
constexpr std::size_t cpu_width   = 8;
constexpr std::size_t peak_width  = 10;

double in_seconds(Clock::duration duration) { return std::chrono::duration<double>(duration).count(); }

std::string fixed(double value, int decimals)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

// A nonzero time too short to print as 0.1 prints as <0.1, never as a zero.
std::string milliseconds(Millis time) { return time.count() > 0 && time.count() < 0.05 ? "<0.1" : fixed(time.count(), 1); }

std::string percent(double ratio) { return fixed(100 * ratio, 1) + "%"; }

std::string mebibytes(std::uint64_t bytes) { return std::to_string(std::llround(double(bytes) / (1024.0 * 1024.0))); }

// 606972 as 606,972.
std::string grouped(std::uint64_t value)
{
    std::string digits = std::to_string(value);
    for (std::size_t at = digits.size(); at > 3; at -= 3)
        digits.insert(at - 3, ",");
    return digits;
}

// The first eight of the sixteen hex digits the document writes.
std::string short_hash(std::uint64_t hash)
{
    char text[9];
    std::snprintf(text, sizeof(text), "%08llx", static_cast<unsigned long long>(hash >> 32));
    return text;
}

std::string padded(const std::string& text, std::size_t width)
{
    return text.size() < width ? text + std::string(width - text.size(), ' ') : text;
}

std::string right(const std::string& text, std::size_t width)
{
    return text.size() < width ? std::string(width - text.size(), ' ') + text : text;
}

// A name with a text flush with the console's right edge, or two spaces after a name too long for that.
std::string name_line(const std::string& name, const std::string& text)
{
    return padded(name, std::max(line_width - text.size(), name.size() + 2)) + text + "\n";
}

// The column naming a row, with one space after a name too long for it.
std::string name_cell(const std::string& name) { return "  " + (name.size() < stage_width ? padded(name, stage_width) : name + " "); }

std::string label(const Properties& properties, const char* key)
{
    const auto found = properties.find(key);
    return found == properties.end() ? "-" : found->second;
}

// Whether the flags turn optimization off, as OrcaSlicer's RelWithDebInfo does.
bool unoptimized(const std::string& flags)
{
    std::istringstream words(flags);
    std::string        word;
    while (words >> word)
        if (word == "/Od" || word == "-O0")
            return true;
    return false;
}

class ConsoleReporter : public Reporter
{
public:
    ConsoleReporter(std::ostream& out, const ReportOptions& options) : m_out(out), m_options(options) {}

    void started(const Result& header) override
    {
        m_header                       = header;
        const BuildIdentity&   build   = header.build;
        const MachineIdentity& machine = header.machine;
        m_out << "orca_bench  " << label(header.measurement, MeasurementKey::policy)
              << "  threads=" << label(header.measurement, MeasurementKey::threads)
              << "  " << build.compiler << " " << build.compiler_version
              << "  " << build.config << (unoptimized(build.flags) ? " (unoptimized)" : "")
              << "  " << build.revision << (build.dirty ? " dirty" : "") << "\n"
              << machine.host << "  " << machine.os << "  " << machine.cpu << "  " << std::to_string(machine.logical_cores) << " cores\n"
              << std::flush;
    }

    void workload(const WorkloadResult& workload) override
    {
        m_out << "\n";
        if (workload.outcome == Outcome::Ran)
            write_ran(workload);
        else
            m_out << name_line(workload.name, workload.outcome == Outcome::Skipped ? "SKIPPED" : "FAILED") << "  " << workload.reason
                  << "\n";
        m_out << std::flush;
    }

    void finished(const Result& result) override
    {
        std::string legends;
        if (m_noisy)
            legends += "~ CV above the " + fixed(100 * significance_bar, 0) + "% significance bar, so a change that size there is noise\n";
        if (m_shared)
            legends += "* readings shared with stages that ran at the same time\n";
        if (m_floored)
            legends += "CPU left out where Windows' " + fixed(Millis(windows_cpu_tick).count(), 1) + " ms steps could skew it past " +
                       fixed(100 * cpu_error_limit, 0) + "%\n";
        if (!legends.empty())
            m_out << "\n" << legends;
        m_out << "\n"
              << count_of(result, Outcome::Ran) << " ran, " << count_of(result, Outcome::Skipped) << " skipped, "
              << count_of(result, Outcome::Failed) << " failed   " << fixed(in_seconds(result.suite.duration), 1) << "s\n"
              << std::flush;
    }

private:
    void write_ran(const WorkloadResult& workload)
    {
        std::vector<std::string> footer;
        if (workload.iterations.empty()) {
            m_out << workload.name << "\n";
        } else {
            const std::size_t count   = workload.iterations.size();
            const std::string warmup  = label(m_header.measurement, MeasurementKey::warmup);
            std::string       passes  = std::to_string(count) + (count == 1 ? " iteration" : " iterations");
            if (warmup != "-" && warmup != "0")
                passes = warmup + " warmup + " + passes;
            m_out << name_line(workload.name, passes);
            WorkloadSummary summary = summarize(workload, m_header, m_options.verbose);
            write_table(summary);
            m_out << "\n";
            if (summary.peak_rss_bytes)
                footer.push_back("peak RSS " + mebibytes(*summary.peak_rss_bytes) + " MiB");
            if (summary.cpu)
                footer.push_back("CPU " + fixed(*summary.cpu, 1) + "x");
            else if (summary.cpu_below_floor)
                footer.push_back("CPU -");
            m_floored = m_floored || summary.cpu_below_floor;
        }
        if (workload.work) {
            footer.push_back("moves " + grouped(workload.work->moves));
            footer.push_back("layers " + grouped(workload.work->layers));
        }
        if (workload.output_hash)
            footer.push_back("hash " + short_hash(*workload.output_hash));
        std::string line;
        for (const std::string& part : footer)
            line += (line.empty() ? "  " : "   ") + part;
        if (!line.empty())
            m_out << line << "\n";
    }

    void write_table(WorkloadSummary& summary)
    {
        std::vector<StageRow>& rows = summary.rows;
        if (m_options.sort_by == SortBy::Start)
            std::sort(rows.begin(), rows.end(), [](const StageRow& a, const StageRow& b) {
                return std::tie(a.first_start, a.stage, a.scope) < std::tie(b.first_start, b.stage, b.scope);
            });
        else
            std::sort(rows.begin(), rows.end(), [](const StageRow& a, const StageRow& b) {
                return a.mean != b.mean ? a.mean > b.mean : std::tie(a.stage, a.scope) < std::tie(b.stage, b.scope);
            });
        const std::optional<OtherRow> other = m_options.verbose ? std::nullopt : collapse(rows, m_options.collapse_below);

        m_out << "  " << std::string(stage_width, ' ') << right("mean", time_width) << right("min", time_width) << right("CV", cv_width)
              << " " << right("share", share_width) << right("CPU", cpu_width) << right("peak MiB", peak_width) << "\n";
        for (const StageRow& row : rows)
            m_out << row_line(row);
        if (other) {
            std::string name = "other (" + std::to_string(other->stages) + (other->stages == 1 ? " stage" : " stages");
            if (other->not_run > 0)
                name += ", " + std::to_string(other->not_run) + " not run";
            m_out << name_cell(name + ")") << right(milliseconds(other->mean), time_width) << right("-", time_width) << right("-", cv_width)
                  << " " << right(percent(other->share), share_width) << "\n";
        }
        m_out << "  " << std::string(line_width - 2, '-') << "\n"
              << name_cell("summed work") << right(milliseconds(summary.summed_work), time_width) << "\n"
              << name_cell("wall envelope") << right(milliseconds(summary.wall), time_width) << "\n"
              << name_cell("unaccounted") << right(milliseconds(summary.unaccounted), time_width);
        if (summary.wall.count() > 0)
            m_out << right(percent(summary.unaccounted / summary.wall), time_width + cv_width + 1 + share_width);
        m_out << "\n";
    }

    std::string row_line(const StageRow& row)
    {
        std::string name = row.scope.empty() ? row.stage : row.stage + " " + row.scope;
        if (row.shared)
            name += "*";
        m_shared = m_shared || row.shared;
        switch (row.state) {
        case StageState::Unfinished: return name_cell(name) + right("never finished", time_width) + "\n";
        case StageState::NotRun: return name_cell(name) + right("not run", time_width) + "\n";
        case StageState::Instant: return name_cell(name) + right("instant", time_width) + "\n";
        case StageState::Ran: break;
        }
        const bool noisy = row.cv && *row.cv > significance_bar;
        m_noisy          = m_noisy || noisy;
        m_floored        = m_floored || row.cpu_below_floor;
        return name_cell(name) + right(milliseconds(row.mean), time_width) + right(milliseconds(row.min), time_width) +
               right(row.cv ? percent(*row.cv) : "-", cv_width) + (noisy ? "~" : " ") + right(percent(row.share), share_width) +
               right(row.cpu ? fixed(*row.cpu, 1) + "x" : "-", cpu_width) +
               right(row.peak_rss_bytes ? mebibytes(*row.peak_rss_bytes) : "-", peak_width) + "\n";
    }

    std::ostream& m_out;
    ReportOptions m_options;
    Result        m_header;
    bool          m_noisy   = false;
    bool          m_shared  = false;
    bool          m_floored = false;
};

class JsonReporter : public Reporter
{
public:
    explicit JsonReporter(std::ostream& out) : m_out(out) {}

    void started(const Result&) override {}
    void workload(const WorkloadResult&) override {}

    // A document is valid only whole, so it waits for the end.
    void finished(const Result& result) override { m_out << write_document(result) << std::flush; }

private:
    std::ostream& m_out;
};

class NullReporter : public Reporter
{
public:
    void started(const Result&) override {}
    void workload(const WorkloadResult&) override {}
    void finished(const Result&) override {}
};

struct Named
{
    const char* name;
    std::unique_ptr<Reporter> (*make)(std::ostream& out, const ReportOptions& options);
};

// Each reporter's name and factory, which make_reporter() and reporter_names() both read, so a new
// reporter is a class and a line here.
constexpr Named reporters[] = {
    {"console", [](std::ostream& out, const ReportOptions& options) -> std::unique_ptr<Reporter> {
         return std::make_unique<ConsoleReporter>(out, options);
     }},
    {"json", [](std::ostream& out, const ReportOptions&) -> std::unique_ptr<Reporter> { return std::make_unique<JsonReporter>(out); }},
    {"null", [](std::ostream&, const ReportOptions&) -> std::unique_ptr<Reporter> { return std::make_unique<NullReporter>(); }},
};

} // namespace

std::vector<std::string> reporter_names()
{
    std::vector<std::string> names;
    for (const Named& reporter : reporters)
        names.emplace_back(reporter.name);
    return names;
}

std::unique_ptr<Reporter> make_reporter(std::string_view name, std::ostream& out, const ReportOptions& options)
{
    for (const Named& reporter : reporters)
        if (name == reporter.name)
            return reporter.make(out, options);
    std::string names;
    for (const Named& reporter : reporters)
        names += (names.empty() ? "" : ", ") + std::string(reporter.name);
    throw std::invalid_argument("no reporter is called '" + std::string(name) + "'; the reporters are " + names);
}

void report(Reporter& reporter, const Result& result)
{
    Result header = result;
    header.workloads.clear();
    reporter.started(header);
    for (const WorkloadResult& workload : result.workloads)
        reporter.workload(workload);
    reporter.finished(result);
}

Progress::Progress(std::ostream& out, bool terminal, std::function<Clock::time_point()> now)
    : m_out(out), m_terminal(terminal), m_now(std::move(now))
{}

void Progress::workload_started(std::size_t index, std::size_t count, const std::string& name)
{
    m_workload         = "[" + std::to_string(index) + "/" + std::to_string(count) + "] " + name;
    m_workload_started = m_now();
    if (m_terminal)
        draw(m_workload);
    else
        m_out << m_workload << "\n" << std::flush;
}

void Progress::pass_done(const PassDone& pass)
{
    if (!m_terminal)
        return;
    std::string line = m_workload;
    if (pass.pass <= pass.warmups)
        line += "  warmup " + std::to_string(pass.pass) + "/" + std::to_string(pass.warmups);
    else
        line += "  timed " + std::to_string(pass.pass - pass.warmups) + "/" + std::to_string(pass.passes - pass.warmups);
    line += "  last " + fixed(in_seconds(pass.wall), 2) + " s";
    // The passes left at the pace of those so far, setup included.
    const double left = in_seconds(m_now() - m_workload_started) / double(pass.pass) * double(pass.passes - pass.pass);
    if (left >= 1)
        line += "  about " + std::to_string(std::llround(left)) + " s left";
    draw(line);
}

void Progress::clear()
{
    if (m_drawn == 0)
        return;
    m_out << '\r' << std::string(m_drawn, ' ') << '\r' << std::flush;
    m_drawn = 0;
}

void Progress::draw(const std::string& line)
{
    m_out << '\r' << line;
    if (line.size() < m_drawn)
        m_out << std::string(m_drawn - line.size(), ' ');
    m_out << std::flush;
    m_drawn = line.size();
}

RunEvents report_events(Reporter& reporter, Progress* progress)
{
    RunEvents events;
    events.started = [&reporter](const Result& header) { reporter.started(header); };
    if (progress) {
        events.workload_started = [progress](std::size_t index, std::size_t count, const std::string& name) {
            progress->workload_started(index, count, name);
        };
        events.pass_done = [progress](const PassDone& pass) { progress->pass_done(pass); };
    }
    events.workload_done = [&reporter, progress](const WorkloadResult& workload) {
        if (progress)
            progress->clear();
        reporter.workload(workload);
    };
    return events;
}

}} // namespace Slic3r::Bench
