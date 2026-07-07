/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Database } from "@sqlite.org/sqlite-wasm";
import { useMemo, useState } from "preact/hooks";

import { filterLabel, stageEntries, stageFilters } from "../db";
import { commas, percent } from "../format";
import { StageEntry } from "../types";

// ---- Bar lists -----------------------------------------------------------

export function StageBars({
    entries,
    limit,
    twoCol,
}: {
    entries: StageEntry[];
    limit: number;
    twoCol: boolean;
}) {
    const shown = entries.slice(0, limit);
    const total = entries.reduce((a, e) => a + e.count, 0) || 1;
    const max = Math.max(1, ...shown.map((e) => e.count));

    return (
        <div class={twoCol ? "bars two-col" : "bars"}>
            {shown.map((e) => (
                <div class="bar">
                    <div class="bar-top">
                        <span class="bar-name" title={e.name}>
                            {e.name}
                        </span>
                        <span class="bar-val">
                            {`${commas(e.count)} - ${percent(e.count, total)}`}
                        </span>
                    </div>
                    <div class="bar-track">
                        <div class="bar-fill" style={{ width: `${(e.count / max) * 100}%` }} />
                    </div>
                </div>
            ))}
        </div>
    );
}

// The "Binary size" label + percentile dropdown, shown when more than one
// crash_components filter exists for a category.
export function StageSelect({
    filters,
    value,
    onChange,
}: {
    filters: string[];
    value: string;
    onChange: (filter: string) => void;
}) {
    return (
        <div class="pctl-row">
            <span class="lbl">Binary size</span>
            <select
                class="pctl"
                value={value}
                onChange={(ev) => onChange((ev.target as HTMLSelectElement).value)}
            >
                {filters.map((f) => (
                    <option value={f} selected={f === value}>
                        {filterLabel(f)}
                    </option>
                ))}
            </select>
        </div>
    );
}

// Overview stage card: header + optional percentile dropdown + bars, with its
// own filter state.
export function StageCard({
    db,
    label,
    category,
    accent,
    headCount,
    limit,
}: {
    db: Database;
    label: string;
    category: string;
    accent: string;
    headCount: number;
    limit: number;
}) {
    const filters = useMemo(() => stageFilters(db, category), [category]);
    const initialFilter = filters.includes("all") ? "all" : filters[0];
    const [filter, setFilter] = useState(initialFilter);
    const entries = stageEntries(db, category, filter);
    // Matches the original: the "+ N more stages" line is fixed to the initial
    // filter's stage count and does not track dropdown changes.
    const initialCount = useMemo(
        () => stageEntries(db, category, initialFilter).length,
        [category, initialFilter]
    );

    return (
        <div class={`card stage-card ${accent}`}>
            <div class="stage-card-head">
                <span class="dot" />
                <span class="stage-card-title">{label}</span>
                <span class="stage-card-count">{commas(headCount)}</span>
            </div>
            {filters.length > 1 ? (
                <StageSelect filters={filters} value={filter} onChange={setFilter} />
            ) : null}
            <StageBars entries={entries} limit={limit} twoCol={false} />
            {initialCount > limit ? (
                <div class="stage-more">{`+ ${initialCount - limit} more stages`}</div>
            ) : null}
        </div>
    );
}
