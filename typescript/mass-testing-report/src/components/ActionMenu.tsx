/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

import { useRef } from "preact/hooks";

import { rowActions } from "../actions";
import { ActionItem, Metadata } from "../types";
import { Icon } from "./Icons";

// ---- Row action menu (...) ----------------------------------------------
//
// The menu is a floating popup appended to `document.body` and driven
// imperatively: the trigger buttons live inside DataTables-managed DOM, so a
// plain document-level popup keeps behaviour identical regardless of where the
// anchor is rendered.

let currentMenu: HTMLElement | null = null;

function closeActionMenu() {
    if (currentMenu !== null) {
        currentMenu.remove();
        currentMenu = null;
        document.removeEventListener("click", onDocClick, true);
        document.removeEventListener("scroll", closeActionMenu, true);
        window.removeEventListener("resize", closeActionMenu);
    }
}

function onDocClick(ev: MouseEvent) {
    if (currentMenu !== null && !currentMenu.contains(ev.target as Node)) {
        closeActionMenu();
    }
}

function openActionMenu(anchor: HTMLElement, items: ActionItem[]) {
    const wasOpen = currentMenu;
    closeActionMenu();
    if (wasOpen !== null && anchor.dataset.menuId === wasOpen.dataset.menuId) {
        return; // toggling the same button off
    }

    const menu = document.createElement("div");
    menu.className = "action-menu";
    const menuId = String(Math.floor(performance.now() * 1000));
    menu.dataset.menuId = menuId;
    anchor.dataset.menuId = menuId;

    for (const item of items) {
        if (item.label === "-") {
            const sep = document.createElement("div");
            sep.className = "sep";
            menu.append(sep);
            continue;
        }
        const button = document.createElement("button");
        button.className = `action-item${item.strong ? " strong" : ""}`;
        button.textContent = item.label;
        button.addEventListener("click", () => {
            closeActionMenu();
            item.run();
        });
        menu.append(button);
    }

    const r = anchor.getBoundingClientRect();
    menu.style.top = `${r.bottom + 5}px`;
    menu.style.left = "auto";
    menu.style.right = `${window.innerWidth - r.right}px`;

    document.body.append(menu);
    currentMenu = menu;
    // Defer listener registration so the opening click does not close it
    setTimeout(() => {
        document.addEventListener("click", onDocClick, true);
        document.addEventListener("scroll", closeActionMenu, true);
        window.addEventListener("resize", closeActionMenu);
    }, 0);
}

export function ActionsButton({
    row,
    meta,
    inDetail,
}: {
    row: any;
    meta: Metadata;
    inDetail: boolean;
}) {
    const btn = useRef<HTMLButtonElement>(null);
    return (
        <div style={{ display: "flex", justifyContent: "flex-end" }}>
            <button
                ref={btn}
                class="actions-btn"
                title="Actions"
                onClick={(ev) => {
                    ev.stopPropagation();
                    openActionMenu(btn.current!, rowActions(row, meta, inDetail));
                }}
            >
                <Icon name="dots" />
            </button>
        </div>
    );
}
