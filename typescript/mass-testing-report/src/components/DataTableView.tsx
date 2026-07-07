/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Database, SQLite3Error } from "@sqlite.org/sqlite-wasm";
import DataTable, { Api, ConfigColumns } from "datatables.net-dt";
import { filesize } from "filesize";
import { ComponentChild, render } from "preact";
import { useEffect, useRef } from "preact/hooks";

import { sqlOptions } from "../db";
import { formatTime } from "../format";
import { PageConfig } from "../pages";
import { ColumnDef, Metadata } from "../types";
import { ActionsButton } from "./ActionMenu";
import { ICONS } from "./Icons";

// ---- Cell components -----------------------------------------------------
//
// Cells are authored as Preact nodes and reused by both the DataTables wrapper
// (rendered to a detached DOM node, see `renderToNode`) and the binary detail
// page (rendered directly).

function StacktraceCell({ id, onFilter }: { id: string; onFilter: (query: string) => void }) {
    return (
        <span class="cell-st" title="Filter by this stacktrace" onClick={() => onFilter(id)}>
            {id}
        </span>
    );
}

// Display-only renderers for `meta.extra_columns`. Sorting/searching always
// use the raw value (see `dtColumns`).
const DISPLAY_RENDERERS: Record<string, (data: any) => ComponentChild> = {
    time: (data: number) => formatTime(data),
    filesize: (data: number) => (data < 0 ? "-" : filesize(data, { base: 2 })),
    ellipsis: (data: string) => (data.length < 70 ? data : data.slice(0, 70) + "..."),
};

export interface TableContext {
    setSearch: (query: string) => void;
}

export function getColumns(meta: Metadata, ctx: TableContext, inDetail: boolean): ColumnDef[] {
    const cols: ColumnDef[] = [
        {
            name: "name",
            data: "name",
            title: "Name",
            cell: (data: string) => (
                <span class="cell-name" title={data}>
                    {data}
                </span>
            ),
            useCellForDetail: false,
        },
        {
            name: "elapsed_time",
            data: "elapsed_time",
            title: "Time",
            className: "dt-right mono",
            cell: (data: number) => formatTime(data),
        },
        { name: "exit_code", data: "exit_code", title: "Exit", className: "dt-right mono" },
        {
            name: "status",
            data: "status",
            title: "Status",
            cell: (data: string) => <span class={`badge badge-${data}`}>{data}</span>,
        },
        {
            name: "stacktrace_id",
            data: "stacktrace_id",
            title: "Stacktrace",
            orderable: false,
            cell: (data: string) =>
                data === "" ? "" : <StacktraceCell id={data} onFilter={ctx.setSearch} />,
        },
    ];

    for (const elem of meta.extra_columns || []) {
        const col: ColumnDef = { name: elem.name, data: elem.name, title: elem.label };
        const renderer = elem.renderer !== undefined ? DISPLAY_RENDERERS[elem.renderer] : undefined;
        if (renderer !== undefined) {
            col.cell = (data: any) => renderer(data);
        }
        col.className = `dt-${elem.align ?? "left"} mono`;
        cols.push(col);
    }

    cols.push({
        name: "actions",
        title: "Actions",
        orderable: false,
        searchable: false,
        className: "dt-right",
        cell: (_data: any, row: any) => (
            <ActionsButton row={row} meta={meta} inDetail={inDetail} />
        ),
    });

    return cols;
}

// Render a Preact cell into a detached DOM node for DataTables. Strings are
// passed through directly so DataTables can insert them as-is.
function renderToNode(child: ComponentChild): Node | string {
    if (typeof child === "string") {
        return child;
    }
    const host = document.createElement("div");
    render(child, host);
    return host.firstChild ?? host;
}

function dtColumns(defs: ColumnDef[]): ConfigColumns[] {
    return defs.map((def) => {
        const col: ConfigColumns = { name: def.name, title: def.title };
        if (def.data !== undefined) col.data = def.data;
        if (def.className !== undefined) col.className = def.className;
        if (def.orderable !== undefined) col.orderable = def.orderable;
        if (def.searchable !== undefined) col.searchable = def.searchable;
        if (def.cell !== undefined) {
            const cell = def.cell;
            col.render = (data: any, type: string, row: any) =>
                type !== "display" ? data : renderToNode(cell(data, row));
        }
        return col;
    });
}

// ---- Search state (persisted in the location hash) -----------------------

class SearchState {
    public query: string;
    public sql: boolean;

    constructor(query: string, sql: boolean) {
        this.query = query;
        this.sql = sql;
    }

    toString() {
        return btoa(JSON.stringify(this));
    }

    static fromString(data: string): SearchState {
        const obj = JSON.parse(atob(data));
        return new SearchState(obj.query, obj.sql);
    }
}

// ---- Data table wrapper --------------------------------------------------
//
// DataTables is inherently imperative, so it is initialized against a ref in an
// effect. The styled title / search box are handed to DataTables' `layout` so
// the existing CSS (which targets `.dt-layout-row`) keeps applying.

export function DataTableView({
    db,
    meta,
    cfg,
}: {
    db: Database;
    meta: Metadata;
    cfg: PageConfig;
}) {
    const cardRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const host = cardRef.current!;
        const table = document.createElement("table");
        table.className = "compact";
        table.style.width = "100%";
        host.append(table);

        let baseQuery = "SELECT * FROM main WHERE 1=1";
        if (cfg.status) {
            baseQuery += ` AND status = '${cfg.status}'`;
        }

        const getData = (sql: string) => db.exec({ sql, ...sqlOptions });
        // The base (unfiltered) rows never change, so fetch them once and reuse
        // for the initial table, the row count and the SQL-off reset.
        const baseData = getData(baseQuery);

        const ctx: TableContext = { setSearch: () => {} };

        // Title (top-left)
        const titleNode = document.createElement("div");
        titleNode.className = "table-title";
        titleNode.textContent = `${baseData.length.toLocaleString("en-US")} ${cfg.rowNoun}`;

        // Search + SQL toggle (top-right)
        const searchInput = document.createElement("input");
        searchInput.type = "search";
        searchInput.placeholder = "Search name, stacktrace id...";
        const sqlCheckbox = document.createElement("input");
        sqlCheckbox.type = "checkbox";
        const sqlToggle = document.createElement("label");
        sqlToggle.className = "sql-toggle";
        sqlToggle.title = "Toggle raw SQL WHERE-clause search";
        sqlToggle.append(sqlCheckbox, document.createTextNode("SQL"));

        const searchIcon = document.createElement("span");
        searchIcon.className = "icon";
        searchIcon.innerHTML = ICONS["search"];
        const searchbox = document.createElement("div");
        searchbox.className = "searchbox";
        searchbox.append(searchIcon, searchInput);
        const searchNode = document.createElement("div");
        searchNode.className = "table-search";
        searchNode.append(searchbox, sqlToggle);

        const dt: Api<any> = new DataTable(table, {
            data: baseData,
            columns: dtColumns(getColumns(meta, ctx, false)),
            order: meta.ordering as any,
            pageLength: 10,
            layout: {
                topStart: (() => titleNode) as unknown as null,
                topEnd: (() => searchNode) as unknown as null,
                bottomStart: "info",
                bottomEnd: "paging",
            },
        });

        function runSearch(state: SearchState) {
            if (state.sql) {
                let data;
                try {
                    data = getData(`${baseQuery} AND (${state.query})`);
                } catch (e) {
                    if (e instanceof SQLite3Error) {
                        return;
                    }
                    throw e;
                }
                dt.clear();
                dt.rows.add(data);
            } else {
                dt.search(state.query);
            }
            dt.draw();
        }

        function updateState(): SearchState {
            const state = new SearchState(searchInput.value, sqlCheckbox.checked);
            window.location.hash = `#${state.toString()}`;
            return state;
        }

        ctx.setSearch = (query: string) => {
            sqlCheckbox.checked = false;
            sqlToggle.classList.remove("on");
            searchInput.value = query;
            runSearch(updateState());
        };

        // Restore a persisted search from the hash
        if (window.location.hash.startsWith("#")) {
            try {
                const state = SearchState.fromString(window.location.hash.slice(1));
                sqlCheckbox.checked = state.sql;
                sqlToggle.classList.toggle("on", state.sql);
                searchInput.value = state.query;
                runSearch(state);
            } catch {
                /* ignore malformed hash */
            }
        }

        sqlCheckbox.addEventListener("input", () => {
            sqlToggle.classList.toggle("on", sqlCheckbox.checked);
            if (sqlCheckbox.checked) {
                dt.search("");
            } else {
                dt.clear();
                dt.rows.add(baseData);
            }
            dt.draw();
            updateState();
        });

        searchInput.addEventListener("input", () => {
            if (!sqlCheckbox.checked) {
                runSearch(updateState());
            }
        });

        searchInput.addEventListener("keyup", (ev: KeyboardEvent) => {
            if (sqlCheckbox.checked && (ev.key === "Enter" || ev.keyCode === 13)) {
                runSearch(updateState());
            }
        });

        return () => {
            dt.destroy();
            host.innerHTML = "";
        };
    }, []);

    return <div class="table-card" ref={cardRef} />;
}
