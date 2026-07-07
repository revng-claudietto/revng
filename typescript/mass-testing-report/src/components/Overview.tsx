/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Database } from "@sqlite.org/sqlite-wasm";

import { sqlOptions, statusCount, statusStats } from "../db";
import { commas, formatClock, formatHM, formatTimestamp, percent, revngVersion } from "../format";
import { Metadata } from "../types";
import { Icon } from "./Icons";
import { StageCard } from "./StageBars";

// ---- Overview page -------------------------------------------------------

const KPI_DEFS: [string, string, string, string | null][] = [
    ["Failures", "FAILED", "a-fail", null],
    ["Crashes", "CRASHED", "a-crash", "crashes.html"],
    ["Timeouts", "TIMED_OUT", "a-timeout", "timeouts.html"],
    ["OOMs", "OOM", "a-oom", "ooms.html"],
];

const COMPOSITION: [string, string, string][] = [
    ["Successes", "OK", "var(--ok)"],
    ["Timeouts", "TIMED_OUT", "var(--timeout)"],
    ["Crashes", "CRASHED", "var(--crash)"],
    ["OOMs", "OOM", "var(--oom)"],
    ["Failures", "FAILED", "var(--fail)"],
];

const STAGE_DEFS: [string, string, string][] = [
    ["Crashes", "CRASHED", "a-crash"],
    ["Timeouts", "TIMED_OUT", "a-timeout"],
    ["OOMs", "OOM", "a-oom"],
];
const STAGE_LIMIT = 6;

export function Overview({ db, meta }: { db: Database; meta: Metadata }) {
    const s = statusStats(db);
    const n = (status: string) => statusCount(s, status);
    const okPct = ((n("OK") * 100) / s.total).toFixed(1);
    const runtime = meta.cpu_count ? s.totalTime / meta.cpu_count : s.totalTime;
    const version = revngVersion(meta);

    const kv: [string, string][] = [];
    if (meta.start_time) kv.push(["Started", formatTimestamp(meta.start_time)]);
    kv.push(["Runtime", `${formatClock(runtime)} - ${meta.cpu_count} CPUs`]);
    if (version) kv.push(["revng version", version]);

    return (
        <>
            {/* KPI cards */}
            <div class="kpi-grid">
                <div class="kpi hero">
                    <div class="kpi-name">Success rate</div>
                    <div class="big">
                        <span class="num">{okPct}</span>
                        <span class="pct">%</span>
                    </div>
                    <div class="kpi-sub">{`${commas(n("OK"))} / ${commas(s.total)} binaries`}</div>
                </div>
                {KPI_DEFS.map(([label, status, accent, href]) => (
                    <div
                        class={`kpi accent ${accent}${href ? " link" : ""}`}
                        onClick={href ? () => (window.location.href = href) : undefined}
                    >
                        <div class="kpi-top">
                            <span class="kpi-name">{label}</span>
                            {href ? <Icon name="chevron" class="chev" /> : null}
                        </div>
                        <div class="kpi-value">{commas(n(status))}</div>
                        <div class="kpi-sub kpi-value-accent">{percent(n(status), s.total)}</div>
                    </div>
                ))}
            </div>

            {/* Run composition bar */}
            <div class="card composition">
                <div class="comp-head">
                    <span class="title">Run composition</span>
                    <span class="meta">
                        {`${commas(s.total)} binaries - ${formatHM(runtime)} on ${meta.cpu_count} CPUs`}
                    </span>
                </div>
                <div class="comp-bar">
                    {COMPOSITION.map(([label, status, color]) => (
                        <span
                            style={{ width: percent(n(status), s.total), background: color }}
                            title={`${label}: ${commas(n(status))}`}
                        />
                    ))}
                </div>
                <div class="comp-legend">
                    {COMPOSITION.map(([label, status, color]) => (
                        <div class="item">
                            <span class="swatch" style={{ background: color }} />
                            {label}
                            <span class="v">{percent(n(status), s.total)}</span>
                        </div>
                    ))}
                </div>
            </div>

            {/* Failure breakdown by pipeline stage */}
            <div class="section-head">
                <span class="title">Failure breakdown by pipeline stage</span>
                <span class="hint">Ranked by count - timeouts & OOMs filterable by binary size</span>
            </div>
            <div class="stage-cols">
                {STAGE_DEFS.map(([label, category, accent]) => (
                    <StageCard
                        db={db}
                        label={label}
                        category={category}
                        accent={accent}
                        headCount={statusCount(s, category)}
                        limit={STAGE_LIMIT}
                    />
                ))}
            </div>

            {/* Notable binaries + run details */}
            <div class="two-grid">
                <div class="card">
                    <div class="card-label">Notable binaries</div>
                    <div class="notable-list">
                        {(meta.highlights || []).map((entry) => {
                            const rows = db.exec({
                                sql: `SELECT name FROM main ${entry.query} LIMIT 1`,
                                ...sqlOptions,
                            });
                            // Colour the badge by the status the highlight query
                            // filters on, rather than matching the free-form text.
                            const statusMatch = entry.query.match(/status\s*=\s*'(\w+)'/);
                            const badgeClass = statusMatch
                                ? `badge-${statusMatch[1]}`
                                : "badge-CRASHED";
                            const shortDesc = entry.description.replace(" binary", "");
                            return (
                                <div class="notable-item">
                                    <span class={`tag badge ${badgeClass}`}>{shortDesc}</span>
                                    {rows.length > 0 ? (
                                        <a
                                            href={`binary.html#${rows[0].name}`}
                                            title={rows[0].name as string}
                                        >
                                            {rows[0].name as string}
                                        </a>
                                    ) : (
                                        <span class="mono" style={{ color: "var(--muted)" }}>
                                            N/A
                                        </span>
                                    )}
                                </div>
                            );
                        })}
                    </div>
                </div>
                <div class="card">
                    <div class="card-label">Run details</div>
                    <div class="kv">
                        {kv.map(([k, v]) => (
                            <>
                                <span class="k">{k}</span>
                                <span class="v">{v}</span>
                            </>
                        ))}
                    </div>
                </div>
            </div>
        </>
    );
}
