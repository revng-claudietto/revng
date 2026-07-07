/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import "./main.css";

import { Database } from "@sqlite.org/sqlite-wasm";
import { render } from "preact";
import * as yaml from "yaml";

import { loadDBFromURL } from "./db";
import { PAGES, PageConfig } from "./pages";
import { Metadata } from "./types";
import { BinaryDetail } from "./components/BinaryDetail";
import { Chrome } from "./components/Chrome";
import { Detail } from "./components/Detail";
import { ListPage } from "./components/ListPage";
import { Overview } from "./components/Overview";

// Picks the page body based on the page configuration and wraps it in the
// shared chrome (top bar + tabs).
function Page({ db, meta, cfg }: { db: Database; meta: Metadata; cfg: PageConfig }) {
    let body;
    if (cfg.page === "overview") {
        body = <Overview db={db} meta={meta} />;
    } else if (cfg.category) {
        body = <Detail db={db} meta={meta} cfg={cfg} />;
    } else {
        body = <ListPage db={db} meta={meta} cfg={cfg} />;
    }
    return (
        <Chrome meta={meta} cfg={cfg}>
            {body}
        </Chrome>
    );
}

// ---- Entrypoint ----------------------------------------------------------

async function main() {
    const db = await loadDBFromURL("main.db");
    window.db = db;

    const metaReq = await fetch("meta.yml");
    const meta: Metadata = yaml.parse(await metaReq.text());

    // Binary detail is a standalone page
    const detailRoot = document.getElementById("binary-detail");
    if (detailRoot !== null) {
        render(<BinaryDetail db={db} meta={meta} />, detailRoot);
        return;
    }

    const pageRoot = document.getElementById("page");
    if (pageRoot === null) {
        return;
    }
    const pageKey = pageRoot.getAttribute("data-page") || "overview";
    const cfg = PAGES[pageKey] || PAGES["overview"];
    render(<Page db={db} meta={meta} cfg={cfg} />, pageRoot);
}

main().then(() => {});
