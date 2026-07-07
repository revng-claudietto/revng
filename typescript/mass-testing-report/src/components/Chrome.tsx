/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { ComponentChildren } from "preact";

import { formatDatePill, revngVersion } from "../format";
import { NAV_ORDER, PAGES, PageConfig, pageHref } from "../pages";
import { Metadata } from "../types";

// ---- Page chrome (top bar + tabs) ---------------------------------------

export function Chrome({
    meta,
    cfg,
    children,
}: {
    meta: Metadata;
    cfg: PageConfig;
    children: ComponentChildren;
}) {
    const configName = meta.configurations?.[0]?.name ?? "revng mass testing";
    const shortHash = revngVersion(meta)?.slice(0, 7);

    return (
        <div class="panel">
            <div class="topbar">
                <div class="brand">
                    <span class="brand-title">Mass Testing</span>
                    <span class="brand-sub">{configName}</span>
                </div>
                <div class="badges">
                    {meta.start_time ? (
                        <span class="pill">Report - {formatDatePill(meta.start_time)}</span>
                    ) : null}
                    {shortHash ? <span class="pill mono">{shortHash}</span> : null}
                </div>
            </div>
            <div class={`tabs ${cfg.accent}`}>
                {NAV_ORDER.map((key) => (
                    <a href={pageHref(key)} class={key === cfg.page ? "active" : ""}>
                        {PAGES[key].title}
                    </a>
                ))}
            </div>
            <div class="content">{children}</div>
        </div>
    );
}
