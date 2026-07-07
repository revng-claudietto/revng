/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Tarball } from "@obsidize/tar-browserify";
import { saveAs } from "file-saver";
import { basename } from "path";

import { sleep } from "./db";
import { ActionItem, Metadata } from "./types";

// ---- Perfetto / reproducer (unchanged behaviour) -------------------------

async function openPerfetto(name: string, trace: string): Promise<void> {
    const req_promise = fetch(trace);
    const handle = window.open("https://ui.perfetto.dev", "_blank");
    if (handle === null) {
        return;
    }
    let ponged = false;
    window.addEventListener("message", function listener(ev) {
        if (ev.data === "PONG") {
            ponged = true;
            window.removeEventListener("message", listener);
        }
    });
    for (;;) {
        handle.postMessage("PING", "*");
        await sleep(100);
        if (ponged) {
            break;
        }
    }
    const req = await req_promise;
    if (!req.ok) {
        return;
    }
    handle.postMessage(
        {
            perfetto: {
                buffer: await req.arrayBuffer(),
                title: `Trace of ${name}`,
                fileName: basename(trace),
            },
        },
        "*"
    );
}

async function createReproducer(name: string, meta: Metadata): Promise<Uint8Array | undefined> {
    const binReq = await fetch(`${name}/input`);
    if (!binReq.ok) {
        return undefined;
    }
    const tarball = new Tarball();
    tarball.addBinaryFile("input", new Uint8Array(await binReq.arrayBuffer()), { fileMode: 0o444 });

    const commandReq = await fetch(`${name}/test-harness.json`);
    const command: string[] = (await commandReq.json()).command;

    for (let i = 0; i < command.length; i++) {
        if (command[i] == "%INPUT%") {
            command[i] = "input";
        }
    }

    const script = `#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(realpath "$(dirname "\${BASH_SOURCE[0]}")")

${meta.reproducer_prelude || ""}

${command.join(" ")} "$@"
`;
    tarball.addTextFile("go.sh", script, { fileMode: 0o555 });

    return tarball.toUint8Array();
}

export function downloadUrl(url: string, filename?: string) {
    const a = document.createElement("a");
    a.href = url;
    if (filename) a.download = filename;
    document.body.append(a);
    a.click();
    a.remove();
}

// The set of actions available for a single table/detail row.
export function rowActions(row: any, meta: Metadata, inDetail: boolean): ActionItem[] {
    const name: string = row.name;
    const items: ActionItem[] = [
        { label: "Download binary", run: () => downloadUrl(`${name}/input`, row.input_name) },
        { label: "View log", run: () => window.open(`${name}/output.log`, "_blank") },
    ];
    for (const dl of meta.downloads || []) {
        items.push({ label: dl.label, run: () => window.open(`${name}/${dl.name}`, "_blank") });
    }
    if (row.has_trace) {
        const trace = `${name}/trace.json.gz`;
        items.push({ label: "Download trace", run: () => downloadUrl(trace) });
        items.push({ label: "Open in Perfetto", run: () => openPerfetto(name, trace) });
    }
    items.push({
        label: "Reproduce",
        run: async () => {
            const tar = await createReproducer(name, meta);
            if (tar !== undefined) {
                saveAs(
                    new Blob([tar as any], { type: "application/tar" }),
                    `${basename(name)}-reproducer.tar`
                );
            }
        },
    });
    items.push({ label: "All files", run: () => window.open(`${name}/`, "_blank") });
    if (!inDetail) {
        items.push({ label: "-", run: () => {} });
        items.push({
            label: "Open details",
            strong: true,
            run: () => {
                window.location.href = `binary.html#${name}`;
            },
        });
    }
    return items;
}
