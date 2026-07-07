/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Database } from "@sqlite.org/sqlite-wasm";
import { useEffect } from "preact/hooks";

import { sqlOptions } from "../db";
import { Metadata } from "../types";
import { ActionsButton } from "./ActionMenu";
import { getColumns } from "./DataTableView";
import { Icon } from "./Icons";

// ---- Binary detail page --------------------------------------------------

export function BinaryDetail({ db, meta }: { db: Database; meta: Metadata }) {
    const hash = window.location.hash;
    const name = hash.startsWith("#") ? hash.slice(1) : "";

    useEffect(() => {
        if (name) {
            document.title += ` ${name}`;
        }
    }, [name]);

    if (!name) {
        return null;
    }

    const rows = db.exec({
        sql: "SELECT * FROM main WHERE name = ?",
        bind: [name],
        ...sqlOptions,
    });
    const data: Record<string, any> | undefined = rows[0];

    return (
        <div class="app">
            <div class="panel">
                <div class="content">
                    <div
                        class="topbar"
                        style={{ padding: "0 0 18px", background: "transparent", border: "0" }}
                    >
                        <div class="brand">
                            <a href="index.html" class="brand-sub back-link">
                                <Icon name="back" />
                                Mass Testing
                            </a>
                        </div>
                    </div>
                    {data === undefined ? (
                        <h2 class="detail-title">{`No data for ${name}`}</h2>
                    ) : (
                        <>
                            <h2 class="detail-title">{name}</h2>
                            <table class="detail-table">
                                <tbody>
                                    {getColumns(meta, { setSearch: () => {} }, true)
                                        .filter((column) => column.title !== "Actions")
                                        .map((column) => {
                                            const value =
                                                column.data !== undefined
                                                    ? data[column.data]
                                                    : undefined;
                                            const useCell =
                                                column.cell !== undefined &&
                                                (column.useCellForDetail ?? true);
                                            return (
                                                <tr>
                                                    <th>{column.title || column.name}</th>
                                                    <td>
                                                        {useCell
                                                            ? column.cell!(value, data)
                                                            : value === undefined || value === null
                                                              ? ""
                                                              : String(value)}
                                                    </td>
                                                </tr>
                                            );
                                        })}
                                    <tr>
                                        <th>Actions</th>
                                        <td>
                                            <ActionsButton row={data} meta={meta} inDetail={true} />
                                        </td>
                                    </tr>
                                </tbody>
                            </table>
                        </>
                    )}
                </div>
            </div>
        </div>
    );
}
