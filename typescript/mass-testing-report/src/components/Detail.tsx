/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Database } from "@sqlite.org/sqlite-wasm";
import { filesize } from "filesize";
import { useState } from "preact/hooks";

import { distinctSignatures, stageEntries, stageFilters, statusCount, statusStats } from "../db";
import { commas } from "../format";
import { PageConfig } from "../pages";
import { Metadata } from "../types";
import { DataTableView } from "./DataTableView";
import { Flamegraphs } from "./Flamegraphs";
import { StageBars, StageSelect } from "./StageBars";
import { SummaryCard } from "./SummaryCard";

// ---- Detail pages (crashes / timeouts / ooms) ---------------------------

function summaryNote(cfg: PageConfig, meta: Metadata, nSig: number): string {
    const conf = meta.configurations?.[0];
    if (cfg.status === "CRASHED") {
        return `across ${commas(nSig)} crash signatures`;
    } else if (cfg.status === "TIMED_OUT" && conf?.timeout) {
        return `${conf.timeout} s wall-clock limit - ${commas(nSig)} stacktraces`;
    } else if (cfg.status === "OOM" && conf?.memory_limit) {
        return `${filesize(conf.memory_limit, { base: 2 })} memory limit - ${commas(nSig)} stacktraces`;
    }
    return `across ${commas(nSig)} stacktraces`;
}

export function Detail({ db, meta, cfg }: { db: Database; meta: Metadata; cfg: PageConfig }) {
    const s = statusStats(db);
    const count = statusCount(s, cfg.status!);
    const nSig = distinctSignatures(db, cfg.status!);
    const note = summaryNote(cfg, meta, nSig);

    const filters = stageFilters(db, cfg.category!);
    const initialFilter = filters.includes("all") ? "all" : filters[0];
    // Shared filter state drives both the stage bars and the flamegraph.
    const [filter, setFilter] = useState(initialFilter);
    const entries = stageEntries(db, cfg.category!, filter);

    return (
        <>
            <div class="summary-grid">
                <SummaryCard cfg={cfg} count={count} total={s.total} note={note} />
                <div class={`card ${cfg.accent}`}>
                    <div class="comp-head" style={{ marginBottom: "14px" }}>
                        <span class="card-label">By pipeline stage</span>
                        {filters.length > 1 ? (
                            <StageSelect filters={filters} value={filter} onChange={setFilter} />
                        ) : null}
                    </div>
                    <StageBars entries={entries} limit={8} twoCol={true} />
                </div>
            </div>

            <div class="fg-card">
                <div class="fg-title">Flamegraphs</div>
                <div class="fg-hint">
                    {`Aggregated stack traces - ${cfg.fgEnd} location at the top`}
                </div>
                <Flamegraphs prefix={cfg.fgPrefix!} filter={filter} end={cfg.fgEnd!} />
            </div>

            <DataTableView db={db} meta={meta} cfg={cfg} />
        </>
    );
}
