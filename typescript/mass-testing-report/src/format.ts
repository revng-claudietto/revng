/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { Metadata } from "./types";

export function truncate(num: number, digits: number): number {
    return Math.trunc(num * 10 ** digits) / 10 ** digits;
}

export function percent(num: number, total: number): string {
    return `${truncate((num * 100) / total, 2)}%`;
}

export function commas(num: number): string {
    return num.toLocaleString("en-US");
}

function padNumber(num: number, amount: number): string {
    return `${num}`.padStart(amount, "0");
}

function splitHMS(seconds: number): { h: number; m: number; s: number } {
    return {
        h: Math.floor(seconds / 3600),
        m: Math.floor((seconds / 60) % 60),
        s: Math.floor(seconds % 60),
    };
}

// Per-run elapsed time as MM:ss.mmm
export function formatTime(seconds: number): string {
    const milliseconds = seconds.toFixed(3).split(".", 2)[1];
    return `${padNumber(Math.floor(seconds / 60), 2)}:${padNumber(
        Math.floor(seconds % 60),
        2
    )}.${milliseconds}`;
}

// hh:mm:ss without milliseconds
export function formatClock(seconds: number): string {
    const { h, m, s } = splitHMS(seconds);
    return `${padNumber(h, 2)}:${padNumber(m, 2)}:${padNumber(s, 2)}`;
}

// "41h 57m"
export function formatHM(seconds: number): string {
    const { h, m } = splitHMS(seconds);
    return `${h}h ${padNumber(m, 2)}m`;
}

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

// From a UNIX timestamp (seconds), "2026-07-03 15:48:39 UTC"
export function formatTimestamp(unixSeconds: number): string {
    return new Date(unixSeconds * 1000).toISOString().replace("T", " ").slice(0, 19) + " UTC";
}

// From a UNIX timestamp (seconds), "Jul 3, 2026"
export function formatDatePill(unixSeconds: number): string {
    const d = new Date(unixSeconds * 1000);
    return `${MONTHS[d.getUTCMonth()]} ${d.getUTCDate()}, ${d.getUTCFullYear()}`;
}

// Extract the (short) commit hash from the free-form `notes` string
export function revngVersion(meta: Metadata): string | undefined {
    const match = (meta.notes || "").match(/[0-9a-f]{7,40}/);
    return match ? match[0] : undefined;
}
