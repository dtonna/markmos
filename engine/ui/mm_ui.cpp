// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_ui.hpp"
#include "../core/mm_vfs.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <simdjson.h>

// simdjson header-only implementation
#include "../../thirdparty/simdjson/singleheader/simdjson.cpp"

namespace ui {

// Text draws record with SortKey{1,0,0,1.0f} (see draw_text): scissor
// commands guarding text must carry the same key or submit()'s
// stable-sort leaves them behind in the key-0 block (text unclipped).

// Tooltip show/hide: the widget is created lazily on first reveal and its
// content comes from the caller's string, so nothing is copied or freed.
void Manager::tooltip(u16 host, const char *text, f32 delay) noexcept {
    if (host >= MAX || text == nullptr) {
        return;
    }
    tooltip_str[host]   = text;
    tooltip_delay[host] = delay;
}

void Manager::tooltip_hide(u16 host) noexcept {
    if (host >= MAX) {
        return;
    }
    tooltip_str[host] = nullptr;
    if (tooltip_for == host) {
        tooltip_for = UINT16_MAX;
        tooltip_t   = 0.0f;
    }
}

void Manager::handle(const InputState &input) noexcept {
    rebuild_abs_cache();
    clicked = UINT16_MAX;

    // ── Disabled / hidden widgets are inert: ONE sanity block ──
    // The invariant, established here and relied on by everything below:
    // focus_id, editing_id and every open ComboBox popup point at widgets that
    // are VISIBLE and ENABLED. Each consumer then needs no check of its own.
    //
    // Why here and not in set_enabled/set_visible: disabling a PANEL disables its
    // children (is_enabled is the EFFECTIVE state, walking ancestors), so the
    // setter would have to search the subtree, and it would still miss every
    // other route into an ineligible focus - begin_modal's root, remove(), a host
    // poking flags[] directly. handle() runs before render() in all three apps, so
    // a ring that goes away here is gone from the same frame's pixels.
    //
    // What this closed, all of it found by reading rather than by a report:
    //  - focus stayed parked on a disabled widget (nothing ever cleared it; only
    //    find_next_focus/prev, which SKIP such a widget, had been correct);
    //  - editing_id was never cleared, so a disabled TextField went on eating
    //    every keystroke, the caret keys and Home/End;
    //  - a disabled ComboBox still opened its popup on an arrow key;
    //  - Confirm had no enabled check at all, so a disabled Button fired
    //    on_click and a disabled Toggle/Checkbox flipped on Enter/Space.
    //
    // set_focusable() already did the small version of this ("never leave focus
    // parked on a widget that just left the ring"); this is the same rule applied
    // where it actually mattered.
    for (u16 i = 0; i < count; ++i) {
        // DISABLED only - not hidden. The two are not the same question, and the
        // difference is deliberate (combobox_tests: "Hiding a panel leaves an OPEN
        // popup's state alone"): a hidden widget draws nothing, popup included, so
        // leaving the flag set costs the user nothing and lets an app that
        // toggles a row re-show it with the list still open and still scrolled
        // where they left it.
        //
        // A DISABLED widget is a different story: it is still painted, greyed out,
        // with the popup drawn over it by the Pass-4 overlay (which only tests
        // is_visible). A live-looking dropdown hanging off a control that cannot
        // be operated is a lie, and it is unreachable - taps are refused. Closing
        // it says what the player can see: this is off. Re-enabling does not
        // reopen it, because the intent to dismiss was the app's, not a glitch.
        if (pool[i].type == (u8)widget_type::COMBOBOX && combobox_open[i] && !is_enabled(i)) {
            combobox_close_cancel(i);
        }
    }
    if (editing_id < MAX && (!is_visible(editing_id) || !is_enabled(editing_id))) {
        editing_id = UINT16_MAX;
    }
    if (focus_id < MAX && (!is_visible(focus_id) || !is_enabled(focus_id))) {
        // Remember where we were so Tab resumes from here instead of restarting
        // at the top of the page. Only set when nothing is pending, or a widget
        // that legitimately has focus could overwrite the anchor mid-flight.
        if (focus_anchor == UINT16_MAX) {
            focus_anchor = focus_id;
        }
        focus_id = UINT16_MAX;
    }

    // ── Modal owns this frame's input ──────────────────────────
    // Back/Escape closes it; the key does nothing else. A modal is a dialog,
    // so it is allowed to eat the frame - that is the whole point (the app no
    // longer hand-rolls "if (overlay_open) return;").
    if (modal_id != UINT16_MAX) {
        for (u8 i = 0; i < input.action_count; ++i) {
            if (input.actions[i] == InputAction::Back || input.actions[i] == InputAction::Pause) {
                const u16 root = modal_id;
                ClickCallback  cb   = modal_dismiss;
                void          *user = modal_user;
                end_modal();
                if (cb) {
                    cb(root, user);
                }
                return;
            }
        }
    }

    // Set by the TextField editor below when it commits on Tab/Shift-Tab and
    // moves focus itself. The focus-nav loop further down sees the SAME action in
    // the SAME list (nothing consumes it), so without this flag one Tab would
    // move focus twice.
    bool focus_moved = false;

    // ── TextField editing input ────────────────────────────────
    if (editing_id != UINT16_MAX) {
        auto &w = pool[editing_id];

        // Character input
        for (u8 i = 0; i < input.text_count; ++i) {
            char    c   = input.text_input[i];
            u8 len = static_cast<u8>(std::strlen(w.text));
            if (len >= sizeof(w.text) - 1) {
                continue;
            }
            // Shift right from cursor
            for (u8 j = len + 1; j > cursor_pos[editing_id]; --j) {
                w.text[j] = w.text[j - 1];
            }
            w.text[cursor_pos[editing_id]] = c;
            cursor_pos[editing_id]++;
        }

        // Backspace
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]) {
            if (cursor_pos[editing_id] > 0) {
                cursor_pos[editing_id]--;
                u8 len = static_cast<u8>(std::strlen(w.text));
                for (u8 j = cursor_pos[editing_id]; j < len; ++j) {
                    w.text[j] = w.text[j + 1];
                }
            }
        }

        // Left arrow
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Left)]) {
            if (cursor_pos[editing_id] > 0) {
                cursor_pos[editing_id]--;
            }
        }
        // Right arrow
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Right)]) {
            u8 len = static_cast<u8>(std::strlen(w.text));
            if (cursor_pos[editing_id] < len) {
                cursor_pos[editing_id]++;
            }
        }
        // Home / End. Home was unreachable before: macOS sends Fn+Left, so the
        // host could only see Left and could not tell the two apart.
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Home)]) {
            cursor_pos[editing_id] = 0;
        }
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::End)]) {
            cursor_pos[editing_id] = static_cast<u8>(std::strlen(w.text));
        }
        // Forward delete. Distinct virtual keycode from Backspace everywhere;
        // there was no KeyCode for it, so Delete did nothing at all.
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Delete)]) {
            u8 len = static_cast<u8>(std::strlen(w.text));
            if (cursor_pos[editing_id] < len) {
                for (u8 j = cursor_pos[editing_id]; j < len; ++j) {
                    w.text[j] = w.text[j + 1];
                }
            }
        }
        // Typing or moving the caret resets the blink, so the caret is visible
        // as you work instead of vanishing mid-word.
        if (input.text_count > 0 || input.keys_just_pressed[static_cast<size_t>(KeyCode::Left)] ||
            input.keys_just_pressed[static_cast<size_t>(KeyCode::Right)] ||
            input.keys_just_pressed[static_cast<size_t>(KeyCode::Home)] ||
            input.keys_just_pressed[static_cast<size_t>(KeyCode::End)]) {
            cursor_timer = 0.0f;
        }

        // Enter → finalize editing, skip confirm action.
        // Escape → cancel, restoring the text the field had when editing began.
        // Neither existed before: Escape mapped to InputAction::Pause and this
        // block ignored it, so Escape (or a long-press) ended the edit with
        // every keystroke kept and no way to back out of a mistake.
        bool finalized = false;
        // Tab / Shift-Tab while editing: COMMIT and move, like every other
        // toolkit and like the mm_07 D5 page's own caption claimed. It used to
        // fall through to the `if (!finalized) return` below, which meant the
        // editor swallowed the key - Tab did nothing at all, and Shift-Tab did
        // nothing at all, and both looked identical to "focus is stuck".
        //
        // The move is done HERE rather than left to the focus-nav loop below,
        // because the action list is not consumed and that loop would move focus
        // a second time (two fields per Tab). `focus_moved` tells it not to.
        // No FOCUS_GAINED here: the focus-edge diff at the end of handle() owns
        // that event, and emitting it twice would f64 every edge.
        for (u8 i = 0; i < input.action_count; ++i) {
            auto a = input.actions[i];
            if (a == InputAction::Confirm || a == InputAction::MenuConfirm) {
                editing_id = UINT16_MAX;
                finalized  = true;
                break;
            }
            if (a == InputAction::Pause) {
                std::memcpy(w.text, edit_snapshot[editing_id], sizeof(w.text));
                cursor_pos[editing_id] = static_cast<u8>(std::strlen(w.text));
                editing_id             = UINT16_MAX;
                finalized              = true;
                break;
            }
            if (a == InputAction::FocusNext || a == InputAction::FocusPrev) {
                // No `text_count == 0` guard needed any more. It used to be the
                // whole test, because Tab arrived as MenuDown - the same action W
                // and S produce alongside their letter - so the only way to tell
                // "pressed Tab" from "typed w" was to see whether any text came
                // with it. Tab now has its own actions (FocusNext/FocusPrev) and
                // never carries a character, so the guard was pure ceremony.
                //
                // Anchor on the field being edited, not on focus_id: a tap starts
                // editing WITHOUT moving focus, so focus_id can still be nothing.
                const u16 was = editing_id;
                editing_id          = UINT16_MAX;
                finalized           = true;
                focus_moved          = true;
                const bool forward   = (a == InputAction::FocusNext);
                focus_id             = forward ? find_next_focus(was) : find_prev_focus(was);
                break;
            }
        }

        if (finalized) {
            // Don't process remaining input; editing just ended
        } else {
            // Check for Select action: if it's not on this TextField,
            // dismiss editing and let the click-through be processed.
            for (u8 i = 0; i < input.action_count; ++i) {
                if (input.actions[i] == InputAction::Select) {
                    // Compute hot under action position to see if we tapped elsewhere
                    f32 sx = input.action_x;
                    f32 sy = input.action_y;
                    bool  hit_any = false;
                    for (u16 j = count; j > 0; --j) {
                        auto &tw = pool[j - 1];
                        // Effective state: a widget inside a disabled panel is
                        // disabled too (own flag AND every ancestor's).
                        if (!is_visible(j - 1)) {
                            continue;
                        }
                        if (!is_enabled(j - 1)) {
                            continue;
                        }
                        // Backgrounds and labels are not hit targets. PANEL was
                        // already skipped before WF_ENABLED existed on it; now
                        // that it carries the flag, skip it by type so picking
                        // behaves exactly as before.
                        if (tw.type == (u8)widget_type::LABEL || tw.type == (u8)widget_type::PANEL) {
                            continue;
                        }
                        f32 tax = abs_x(j - 1);
                        f32 tay = abs_y(j - 1);
                        if (tw.frame.w <= 0.0f || tw.frame.h <= 0.0f) {
                            continue;
                        }
                        if (sx >= tax && sx <= tax + tw.frame.w && sy >= tay && sy <= tay + tw.frame.h) {
                            hit_any = true;
                            if (j - 1 != editing_id) {
                                editing_id = UINT16_MAX;
                                finalized  = true;
                            }
                            break;
                        }
                    }
                    if (!hit_any) {
                        // Tap on empty space: dismiss too (standard
                        // tap-outside-to-close); the Select then falls
                        // through to normal processing and hits nothing.
                        editing_id = UINT16_MAX;
                        finalized  = true;
                    }
                    break;
                }
            }
            if (!finalized) {
                return;
            }
        }
    }

    // Get pointer position — prefer touch, then action, then mouse.
    //
    // The mouse term is GATED on has_pointer. mouse_x/mouse_y are 0,0 until a
    // MouseMove arrives and stay 0,0 forever on a platform that never sends one
    // (only mm_app_mac.mm emits them), so an ungated read meant pick(0,0) EVERY
    // FRAME: whatever widget covers the top-left corner sat permanently in the
    // hover state - hover glow, the hot thumb colour, and a tooltip that popped
    // up with no pointer anywhere near it.
    f32 px = 0.0f;
    f32 py = 0.0f;
    bool  have_pos = false;
    if (input.has_pointer) {
        px        = input.mouse_x;
        py        = input.mouse_y;
        have_pos  = true;
    }
    for (u8 i = 0; i < input.touch.active_count; ++i) {
        auto &f = input.touch.fingers[i];
        if (f.phase == TouchPhase::Pressing || f.phase == TouchPhase::Moved) {
            px        = f.curr_x;
            py        = f.curr_y;
            have_pos  = true;
            break;
        }
    }

    bool has_select = false;
    for (u8 i = 0; i < input.action_count; ++i) {
        if (input.actions[i] == InputAction::Select) {
            px         = input.action_x;
            py         = input.action_y;
            has_select = true;
            // A Select action CARRIES a position, so it counts as having a
            // pointer even where no MouseMove is ever emitted. Missed this at
            // first and every tap-driven test failed at once - a tap is the one
            // pointer position a touch device definitely has.
            have_pos   = true;
            break;
        }
    }

    // No pointer and no finger: nothing is under the cursor, so nothing is hot.
    // (pick() would happily return whatever sits at 0,0.)
    hot                 = have_pos ? pick(px, py) : UINT16_MAX;
    bool finger_down    = is_finger_down(input);

    // Gesture-lifetime clean-up: when no finger is down and there is no release
    // action in flight, the gesture is over - whether it ended in a Select, a
    // cancel, or a host that consumed it. Two things used to outlive it:
    //
    //   scroll_dragging was cleared ONLY by the Select below, so a cancelled
    //     touch left it latched true forever. Survivable while it only swallowed
    //     one tap; now the press visual reads it, so a latched flag would kill
    //     the squeeze on every button in the app.
    //   `active` was never cleared at all outside the click path. A press then
    //     re-anchoring is guarded on `active == UINT16_MAX`, so the NEXT press on
    //     a child of the same ScrollView skipped the anchor and compared against
    //     the PREVIOUS gesture's grab point - a fresh tap scrolled instead of
    //     firing, by however far the last drag had moved.
    //
    // `has_select` keeps both alive through the release frame, which is what the
    // tap-suppression and the click path below need.
    if (!finger_down && !has_select) {
        scroll_dragging = false;
        active          = UINT16_MAX;
    }

    // ── ComboBox popup pre-pass (overlay beats pick()) ─────────
    // An open popup is not a widget: test it before generic pick().
    // Item tap = select+close+click; field tap = toggle shut (silent);
    // outside tap = cancel and pass through to the generic path.
    // Reset once per frame; set where a FOCUSED widget owns an Up/Down nav key.
    nav_consumed = false;
    bool combo_consumed = false;
    if (has_select) {
        for (u16 k = count; k > 0 && !combo_consumed; --k) {
            u16 id = k - 1;
            if (pool[id].type != (u8)widget_type::COMBOBOX) {
                continue;
            }
            if (!is_visible(id)) {
                continue;
            }
            if (!combobox_open[id]) {
                continue;
            }
            // Shrinking-out popup: taps pass through instead of hitting a row
            // that is on its way out (it was already dismissed).
            if (combobox_closing[id]) {
                combobox_shut_now(id);
                combo_consumed = true;
                continue;
            }
            f32 qx, qy, qw, qh;
            if (!combobox_popup_rect(id, qx, qy, qw, qh)) {
                combobox_open[id] = false; // metrics not ready: fail closed
                continue;
            }
            if (px >= qx && px <= qx + qw && py >= qy && py <= qy + qh) {
                const ListboxMetrics &lm      = combobox_metrics[id];
                int                   vis_row = static_cast<int>(combobox_scroll[id] + (py - qy) / lm.row_h);
                int                   item    = combobox_visible_to_item(id, vis_row);
                if (item >= 0 && item < combobox_count[id]) {
                    combobox_commit(id, item); // selection + label, shut
                    clicked = id;
                    emit_event(ui_event_type::CLICK, id);
                    emit_event(ui_event_type::CHANGE, id, static_cast<f32>(item));
                    if (pool[id].on_click) {
                        pool[id].on_click(id, click_user[id]);
                    }
                } else {
                    combobox_close_cancel(id); // "No match" tap: shut, no change
                }
                active         = UINT16_MAX;
                combo_consumed = true;
            } else {
                f32 ax = abs_x(id), ay = abs_y(id);
                if (px >= ax && px <= ax + pool[id].frame.w && py >= ay && py <= ay + pool[id].frame.h) {
                    combobox_close_cancel(id); // tap field = toggle shut, no change
                    active         = UINT16_MAX;
                    combo_consumed = true;
                } else {
                    combobox_close_cancel(id); // outside = cancel, pass through
                }
            }
        }
    }

    // Modal: a tap that did NOT land inside the subtree dismisses it and stops
    // there. Doing it before the normal pick means `pick()` never has to care
    // (it is already scoped, this is the "tap outside" rule).
    if (has_select && modal_id != UINT16_MAX) {
        u16 hit = pick(px, py);
        if (hit == UINT16_MAX || !in_modal(hit)) {
            const u16 root = modal_id;
            ClickCallback  cb   = modal_dismiss;
            void          *user = modal_user;
            end_modal();
            active = UINT16_MAX;
            hot    = UINT16_MAX;
            if (cb) {
                cb(root, user);
            }
            return;
        }
    }

    // A release that ENDS a scroll drag is not also a tap. Without this, every
    // flick that started on a button fired that button on release.
    if (has_select && scroll_dragging) {
        scroll_dragging = false;
        has_select      = false;
    }

    if (has_select && !combo_consumed) {
        // Click detection: prefer active (held) widget, but fall back to hot
        // under the release position when active was already reset (fast clicks).
        u16 target = (active != UINT16_MAX && active == hot) ? active : hot;
        // An accordion HEADER toggles its own section. Checked before the normal
        // click path so a header does not need a callback at all - the app asked
        // for a section, not for a button.
        if (target != UINT16_MAX && is_accordion(target)) {
            accordion_toggle(target);
            emit_event(ui_event_type::CHANGE, target, accordion_is_open(target) ? 1.0f : 0.0f);
            if (pool[target].on_click) {
                pool[target].on_click(target, click_user[target]);
            }
            clicked = UINT16_MAX; // consumed
            target  = UINT16_MAX;
        }
        if (target != UINT16_MAX) {
            clicked = target;
            auto &w = pool[clicked];
            if (w.type == (u8)widget_type::TABBAR) {
                // Tap = switch tab. A tap on the ALREADY active cell is a
                // silent no-op (same rule as a radiobox): the app's swap is
                // idempotent and firing it again would be noise.
                const int cell = tabbar_cell_at(w.frame.w, tabbar_count_[clicked], px - abs_x(clicked));
                if (cell >= 0 && cell != tabbar_active[clicked]) {
                    tabbar_active[clicked] = static_cast<i8>(cell);
                    emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(cell));
                    if (on_change[clicked]) {
                        on_change[clicked](clicked, static_cast<f32>(cell));
                    }
                }
            } else if (w.type == (u8)widget_type::LISTBOX) {
                // Tap = select row (a tap never starts a drag). Rows live in
                // the content rect: taps on the border bands are dead.
                const ListboxMetrics &lm = listbox_metrics[clicked];
                if (lm.row_h > 0.0f) {
                    f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
                    ui::content_insets(styles[w.style_id], w.frame.w, w.frame.h, il, it, ir, ib);
                    f32 cy0 = abs_y(clicked) + it;
                    f32 cy1 = abs_y(clicked) + w.frame.h - ib;
                    // DataGrid header: a tap on a column title SORTS, and it
                    // CONSUMES the tap - otherwise it would also select the row
                    // that happens to sit under the finger, and one press would
                    // both reorder and move the selection.
                    const f32 rows_y = row_area_top(clicked, cy0);
                    if (is_grid(clicked) && py >= cy0 && py < rows_y) {
                        u8 col = 0;
                        if (grid_header_at(clicked, px - abs_x(clicked) - il, col)) {
                            grid_toggle_sort(clicked, col);
                            emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(col));
                            // The app re-sorts in its callback (grid_sort_order +
                            // set_grid_order), so it must run even though no row
                            // was selected. Reuse the row-select slot: it is the
                            // same widget and the same "the model changed" event.
                            emit_event(ui_event_type::CLICK, clicked);
                            if (w.on_click) {
                                w.on_click(clicked, click_user[clicked]);
                            }
                        }
                        active = UINT16_MAX;
                        return;
                    }
                    if (cy1 > rows_y && py >= rows_y && py <= cy1) {
                        f32 rel = py - rows_y;
                        int   row = static_cast<int>(listbox_scroll[clicked] + rel / lm.row_h);
                        if (row >= 0 && row < listbox_count[clicked]) {
                            listbox_selected[clicked] = static_cast<i8>(row);
                            // TreeView: a tap on a row's INDENT ZONE toggles it
                            // instead of selecting. Checked before the events,
                            // and it CONSUMES the tap - a click that opened a
                            // branch and also fired the row's action would be
                            // two answers to one press.
                            if (tree_nodes[clicked] != nullptr && tree_tap_is_expander(clicked, px, py)) {
                                u32 nodes[K_TREE_ROW_SCRATCH];
                                const u32 node = tree_row_node(clicked, static_cast<u32>(row), nodes);
                                if (node != UINT32_MAX && tree_has_children(clicked, node)) {
                                    tree_toggle(clicked, node);
                                    emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(row));
                                    active = UINT16_MAX;
                                    return;
                                }
                            }
                            emit_event(ui_event_type::CLICK, clicked);
                            emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(row));
                        }
                    }
                }
                w.state = 0;
            }
            if (w.type == (u8)widget_type::TEXT_FIELD) {
                editing_id = clicked;
                std::memcpy(edit_snapshot[clicked], pool[clicked].text, sizeof(edit_snapshot[0]));
                active = UINT16_MAX;
                return;
            }
            bool combo_silent = false;
            if (w.type == (u8)widget_type::COMBOBOX) {
                // Tap field = toggle popup (both directions silent;
                // selecting commits in the pre-pass above).
                if (combobox_open[clicked]) {
                    combobox_close_cancel(clicked);
                } else {
                    combobox_open_now(clicked);
                }
                active       = UINT16_MAX;
                combo_silent = true;
            }
            if (w.type == (u8)widget_type::TOGGLE || w.type == (u8)widget_type::CHECKBOX) {
                w.state = w.state ? 0 : 1;
            } else if (w.type == (u8)widget_type::RADIOBOX) {
                if (w.state) {
                    combo_silent = true; // re-tap selected radio: no change, no callback
                } else {
                    radiobox_select(clicked);
                }
            }
            if (!combo_silent && w.type != (u8)widget_type::LISTBOX && w.type != (u8)widget_type::COMBOBOX) {
                // Listbox/Combobox emit in their own branches above (with Change);
                // the toggle here covers Button/Slider taps (Click only).
                emit_event(ui_event_type::CLICK, clicked);
                if (w.type == (u8)widget_type::TOGGLE || w.type == (u8)widget_type::CHECKBOX) {
                    emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(w.state));
                } else if (w.type == (u8)widget_type::RADIOBOX) {
                    emit_event(ui_event_type::CHANGE, clicked, 1.0f);
                }
            }
            if (w.on_click && !combo_silent) {
                w.on_click(clicked, click_user[clicked]);
            }
        }
        active = UINT16_MAX;
    } else if (finger_down) {
        // Overlay-first grab: an open popup wins over pick() — pick() only
        // sees widget rects, so a popup overlapping another widget would
        // otherwise hand active to the widget underneath and the popup
        // could never scroll (mirror of the Select pre-pass above).
        if (active == UINT16_MAX) {
            for (u16 k = count; k > 0; --k) {
                u16 id = k - 1;
                if (pool[id].type != (u8)widget_type::COMBOBOX) {
                    continue;
                }
                if (!combobox_open[id]) {
                    continue;
                }
                if (!is_visible(id) || !is_enabled(id)) {
                    continue;
                }
                f32 qx, qy, qw, qh;
                if (!combobox_popup_rect(id, qx, qy, qw, qh)) {
                    continue;
                }
                if (px < qx || px > qx + qw || py < qy || py > qy + qh) {
                    continue;
                }
                active                      = id;
                auto                 &cw    = pool[id];
                const ListboxMetrics &lm    = combobox_metrics[id];
                u8               vis   = combobox_visible_count(id);
                f32                 maxsc = combobox_maxscroll(vis);
                f32                 th    = ui::combobox::thumb_h(vis, qh, lm);
                f32                 ty    = qy + (maxsc > 0.0f ? combobox_scroll[id] / maxsc * (qh - th) : 0.0f);
                bool                  on_thumb =
                    (lm.row_h > 0.0f && vis > Manager::K_MAX_POPUP_ROWS) && px >= qx + qw - lm.scrollbar_w && px <= qx + qw && py >= ty && py <= ty + th;
                cw.state            = on_thumb ? 1 : 2;
                cw.thumb_pos        = py;
                combobox_anchor[id] = combobox_scroll[id];
                break;
            }
        }
        if (active == UINT16_MAX && hot != UINT16_MAX) {
            active = hot;
            // ScrollView: anchor the press so a later vertical drag can scroll
            // it. This must happen even when `hot` is a CHILD of the scroll
            // view: children are pickable, so the scroll view itself is rarely
            // what a press lands on. State lives on the scroll view (state
            // doubles as the drag mode, as it does for the ListBox: 0 idle,
            // 1 thumb, 2 body) plus one Manager-wide flag for "this gesture is
            // a scroll", which is what suppresses the child's tap on release.
            {
                const u16 sv = scroll_ancestor_of(active);
                if (sv != UINT16_MAX) {
                    pool[sv].thumb_pos = py;
                    scroll_anchor[sv]  = scroll_scroll[sv];
                    pool[sv].state     = 0;
                }
            }
            // ListBox: decide thumb vs body drag at grab time (state doubles
            // as the drag-mode flag; thumb_pos holds the body anchor py).
            if (pool[active].type == (u8)widget_type::LISTBOX) {
                auto                 &lw    = pool[active];
                const ListboxMetrics &lm    = listbox_metrics[active];
                f32                 lax   = abs_x(active);
                f32                 lay   = abs_y(active);
                u8               n     = listbox_count[active];
                f32                 maxsc = ui::listbox::maxscroll(n, lm);
                f32                 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
                ui::content_insets(styles[lw.style_id], lw.frame.w, lw.frame.h, il, it, ir, ib);
                f32 sb_right = lax + lw.frame.w - ir;
                f32 track_y0 = lay + it;
                f32 track_y1 = lay + lw.frame.h - ib;
                f32 track_h  = track_y1 - track_y0;
                if (track_h <= 0.0f) {
                    track_h  = lw.frame.h; // degenerate style: frame behavior
                    track_y0 = lay;
                }
                f32 th = ui::listbox::thumb_h(n, track_h, lm);
                f32 ty = track_y0 + (maxsc > 0.0f ? listbox_scroll[active] / maxsc * (track_h - th) : 0.0f);
                bool  on_thumb =
                    (lm.row_h > 0.0f && n > lm.visible) && px >= sb_right - lm.scrollbar_w && px <= sb_right && py >= ty && py <= ty + th;
                if (on_thumb) {
                    lw.state = 1;
                } else {
                    lw.state               = 2;
                    lw.thumb_pos           = py;
                    listbox_anchor[active] = listbox_scroll[active];
                }
            }
        }
        // ScrollView body drag. The press was anchored on the scroll view (see
        // the press block), so this runs whenever the held widget lives inside
        // one - which is the common case, because children are pickable.
        if (active != UINT16_MAX) {
            const u16 sv = scroll_ancestor_of(active);
            if (sv != UINT16_MAX && scroll_max(sv) > 0.0f) {
                const f32 dy = py - pool[sv].thumb_pos;
                if (pool[sv].state == 1) {
                    // Thumb drag: the finger position maps onto the offset.
                    const f32 track_h = pool[sv].frame.h;
                    const f32 th       = scroll_thumb_h(pool[sv].frame.h, scroll_max(sv));
                    if (track_h > th) {
                        const f32 t = (py - abs_y(sv) - th * 0.5f) / (track_h - th);
                        set_scroll(sv, t * scroll_max(sv));
                        scroll_dragging = true;
                    }
                } else if (pool[sv].state == 2) {
                    // Already engaged: keep tracking, EVERY frame.
                    //
                    // This block used to be gated on `!scroll_dragging`, which
                    // looks like a re-entry guard but is not: scroll_dragging is
                    // only cleared on release (it exists to suppress the child's
                    // tap), so a body drag moved the content on the one frame
                    // that crossed the slop and then froze for the rest of the
                    // gesture. Dragging 500px scrolled 2px.
                    //
                    // Re-evaluating `anchor - dy` is exactly 1:1 tracking: the
                    // anchor is the grab point and dy is total travel, so nothing
                    // accumulates and the content cannot drift.
                    set_scroll(sv, scroll_anchor[sv] - dy);
                    scroll_dragging = true;
                } else if (dy > K_SCROLL_DRAG_SLOP || dy < -K_SCROLL_DRAG_SLOP) {
                    // Past the slop: the gesture becomes a drag. state 2 is the
                    // latch, so the slop decides ONCE and never again.
                    pool[sv].state = 2;
                    set_scroll(sv, scroll_anchor[sv] - dy);
                    scroll_dragging = true;
                }
            }
        }
        if (active != UINT16_MAX) ui::slider::drag_update(*this, active, px);
        // ListBox drag update: thumb (center-on-finger) or body scroll
        if (active != UINT16_MAX && pool[active].type == (u8)widget_type::LISTBOX) {
            auto                 &lw = pool[active];
            const ListboxMetrics &lm = listbox_metrics[active];
            if (lm.row_h <= 0.0f) {
                hot = active;
            } else if (lw.state == 1) {
                f32   ay    = abs_y(active);
                u8 n     = listbox_count[active];
                f32   maxsc = ui::listbox::maxscroll(n, lm);
                f32   il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
                ui::content_insets(styles[lw.style_id], lw.frame.w, lw.frame.h, il, it, ir, ib);
                f32 track_y0 = ay + it;
                f32 track_h  = lw.frame.h - it - ib;
                if (track_h <= 0.0f) {
                    track_y0 = ay; // degenerate style: frame behavior
                    track_h  = lw.frame.h;
                }
                f32 th = ui::listbox::thumb_h(n, track_h, lm);
                if (maxsc > 0.0f && track_h > th) {
                    listbox_scroll[active] = (py - track_y0 - th * 0.5f) / (track_h - th) * maxsc;
                    ui::listbox::clamp_scroll(listbox_scroll[active], n, lm);
                }
            } else if (lw.state == 2) {
                listbox_scroll[active] = listbox_anchor[active] - (py - lw.thumb_pos) / lm.row_h;
                ui::listbox::clamp_scroll(listbox_scroll[active], listbox_count[active], lm);
            }
            hot = active;
        }
        // ComboBox popup drag update: thumb (center-on-finger) or body scroll
        if (active != UINT16_MAX && pool[active].type == (u8)widget_type::COMBOBOX && combobox_open[active]) {
            auto                 &cw = pool[active];
            const ListboxMetrics &lm = combobox_metrics[active];
            f32                 qx, qy, qw, qh;
            if (lm.row_h <= 0.0f || !combobox_popup_rect(active, qx, qy, qw, qh)) {
                hot = active;
            } else if (cw.state == 1) {
                u8 vis   = combobox_visible_count(active);
                f32   maxsc = combobox_maxscroll(vis);
                f32   th    = ui::combobox::thumb_h(vis, qh, lm);
                if (maxsc > 0.0f && qh > th) {
                    combobox_scroll_target[active] = (py - qy - th * 0.5f) / (qh - th) * maxsc;
                    if (combobox_scroll_target[active] < 0.0f) {
                        combobox_scroll_target[active] = 0.0f;
                    }
                    if (combobox_scroll_target[active] > maxsc) {
                        combobox_scroll_target[active] = maxsc;
                    }
                }
            } else if (cw.state == 2) {
                f32 maxsc             = combobox_maxscroll(combobox_visible_count(active));
                combobox_scroll_target[active] = combobox_anchor[active] - (py - cw.thumb_pos) / lm.row_h;
                if (combobox_scroll_target[active] < 0.0f) {
                    combobox_scroll_target[active] = 0.0f;
                }
                if (combobox_scroll_target[active] > maxsc) {
                    combobox_scroll_target[active] = maxsc;
                }
            }
            hot = active;
        }
    } else {
        // Click elsewhere while editing → stop editing
        if (editing_id != UINT16_MAX) {
            editing_id = UINT16_MAX;
        }
        active = UINT16_MAX;
    }

    // ListBox wheel scroll (mouse; touch uses thumb/body drag instead)
    // ComboBox open popups scroll too (tested before plain boxes so an
    // overlapping popup wins over the widget underneath).
    if (input.mouse_scroll_dy != 0.0f) {
        bool wheel_done = false;
        for (u16 i = 0; i < count && !wheel_done; ++i) {
            auto &w = pool[i];
            if (w.type != (u8)widget_type::COMBOBOX) {
                continue;
            }
            if (!combobox_open[i]) {
                continue;
            }
            // EFFECTIVE: a hidden popup must not eat a wheel tick, because a
            // listbox UNDERNEATH it is a legitimate target for the same wheel.
            if (!is_visible(i) || !is_enabled(i)) {
                continue;
            }
            f32 qx, qy, qw, qh;
            if (!combobox_popup_rect(i, qx, qy, qw, qh)) {
                continue;
            }
            if (px >= qx && px <= qx + qw && py >= qy && py <= qy + qh) {
                f32 maxsc               = combobox_maxscroll(combobox_visible_count(i));
                combobox_scroll_target[i] -= input.mouse_scroll_dy * 3.0f;
                if (combobox_scroll_target[i] < 0.0f) {
                    combobox_scroll_target[i] = 0.0f;
                }
                if (combobox_scroll_target[i] > maxsc) {
                    combobox_scroll_target[i] = maxsc;
                }
                combobox_scroll_vel[i] = 0.0f; // wheel is a discrete step, not a flick
                wheel_done = true; // popup ate the wheel: a listbox under it must not scroll too
            }
        }
        for (u16 i = 0; i < count && !wheel_done; ++i) {
            auto &w = pool[i];
            if (w.type != (u8)widget_type::LISTBOX) {
                continue;
            }
            if (!is_visible(i) || !is_enabled(i)) {
                continue;
            }
            f32 ax = abs_x(i);
            f32 ay = abs_y(i);
            if (px >= ax && px <= ax + w.frame.w && py >= ay && py <= ay + w.frame.h) {
                listbox_scroll[i] -= input.mouse_scroll_dy * 3.0f;
                ui::listbox::clamp_scroll(listbox_scroll[i], listbox_count[i], listbox_metrics[i]);
                break;
            }
        }
        // ScrollView wheel. Tested LAST and innermost-first: a scroll view is a
        // container, so an inner one under the cursor must win over an outer one
        // (the reverse iteration is what makes that true - an outer scroll view
        // is created first and would otherwise swallow every wheel tick).
        for (u16 i = count; i > 0 && !wheel_done; --i) {
            const u16 id = i - 1;
            auto         &w  = pool[id];
            if (w.type != (u8)widget_type::SCROLLVIEW) {
                continue;
            }
            if (!is_visible(id) || !is_enabled(id)) {
                continue;
            }
            // The viewport rect is abs_x/abs_y of the scroll view itself: it is
            // not offset by its own scroll, which is exactly the window shown.
            const f32 ax = abs_x(id);
            const f32 ay = abs_y(id);
            if (px >= ax && px <= ax + w.frame.w && py >= ay && py <= ay + w.frame.h) {
                if (scroll_max(id) > 0.0f) {
                    set_scroll(id, scroll_scroll[id] - input.mouse_scroll_dy * K_SCROLL_WHEEL_STEP);
                    wheel_done = true;
                }
                break;
            }
        }
    }

    // Escape closes any open ComboBox popup (fail shut, no callback).
    if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Escape)]) {
        for (u16 i = 0; i < count; ++i) {
            if (pool[i].type == (u8)widget_type::COMBOBOX && combobox_open[i]) {
                combobox_close_cancel(i);
            }
        }
    }

    // ComboBox editable filter input: the label and the filter are
    // separate states. Showing a selection ("apple") is NOT a filter —
    // reopening shows the full list. The first keystroke after open
    // starts a fresh filter (chars replace the label; Backspace alone
    // starts from label-minus-one). Append-only, like a search field.
    {
        u16 target = UINT16_MAX;
        for (u16 k = count; k > 0; --k) { // topmost open wins
            u16 id = k - 1;
            if (pool[id].type != (u8)widget_type::COMBOBOX) {
                continue;
            }
            if (!combobox_open[id] || !combobox_editable[id]) {
                continue;
            }
            // EFFECTIVE, so a combo inside a hidden panel stops swallowing every
            // keystroke. This is the worst site of the set: it runs BEFORE the
            // host's own keys (which consult is_text_capture, now fixed the same
            // way), so the letter went into a filter nobody can see and the app
            // never saw the key at all.
            if (!is_visible(id) || !is_enabled(id)) {
                continue;
            }
            target = id;
            break;
        }
        // A FOCUSED combo that is then hidden keeps focus - nothing clears it -
        // so this second path is the one an app actually reaches.
        if (target == UINT16_MAX && focus_id < MAX && pool[focus_id].type == (u8)widget_type::COMBOBOX && combobox_editable[focus_id] &&
            is_visible(focus_id) && is_enabled(focus_id)) {
            target = focus_id;
        }
        if (target != UINT16_MAX) {
            auto &w      = pool[target];
            bool  typing = input.text_count > 0;
            bool  erase  = input.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)];
            if (!typing && !erase) {
                // No input this frame: leave label/filter alone (a focused
                // closed combo must not auto-open or drop its label).
            } else {
                bool changed = false;
                if (!combobox_filtering[target]) {
                    if (typing) {
                        w.text[0] = '\0'; // label shown → start a fresh filter
                    }
                    combobox_filtering[target] = true;
                    changed                    = true;
                }
                for (u8 ci = 0; ci < input.text_count; ++ci) {
                    u8 len = static_cast<u8>(std::strlen(w.text));
                    if (len >= sizeof(w.text) - 1) {
                        break;
                    }
                    w.text[len]     = input.text_input[ci];
                    w.text[len + 1] = '\0';
                    changed         = true;
                }
                if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]) {
                    u8 len = static_cast<u8>(std::strlen(w.text));
                    if (len > 0) {
                        w.text[len - 1] = '\0';
                        changed         = true;
                    }
                }
                if (changed) {
                    if (!combobox_open[target]) {
                        combobox_open[target]   = true;
                    combobox_closing[target] = false;
                    combobox_anim[target]    = 0.0f; // reopened by typing
                    }
                    combobox_scroll[target]      = 0.0f;
                    combobox_scroll_target[target] = 0.0f;
                    u8 vis             = combobox_visible_count(target);
                    combobox_hl[target]     = (vis > 0) ? 0 : -1; // first match ready for Enter
                    if (combobox_item_to_visible(target, combobox_selected[target]) < 0) {
                        combobox_selected[target] = -1;
                    }
                    measure_dirty = true;
                }
            }
        }
    }

    // Keyboard/gamepad focus navigation.
    for (u8 i = 0; i < input.action_count; ++i) {
        auto a          = input.actions[i];
        // Focused ComboBox: arrows and Home/End move the HIGHLIGHT only (never
        // commit, never touch the label); Enter commits the highlight and shuts.
        // Home/End jump to the first / last visible row - and like the arrows they
        // OPEN the popup if it is shut, because on a combo the first and last item
        // are not reachable without being seen.
        //
        // `< MAX`, not `!= UINT16_MAX`: these read pool[focus_id], and a focus id
        // that is merely not the sentinel is still out of the pool. Same guard as
        // every other id read in this file.
        //
        // MenuUp/MenuDown/MenuFirst/MenuLast ONLY - Tab arrives as FocusNext /
        // FocusPrev, so walking focus is never swallowed here. Do not "helpfully"
        // add SwapUp/SwapDown back: those used to reach this branch, and that is
        // exactly how ComboBox became a one-way door (see FocusNext in InputAction).
        bool cb_focused = focus_id < MAX && pool[focus_id].type == (u8)widget_type::COMBOBOX;
        // F4 / Alt+Down: open if shut, cancel-shut if open. A separate action
        // from MenuDown on purpose (see InputAction::MenuToggle) - a widget that
        // claimed "down is mine" would otherwise also claim the chord, and the
        // popup would open and shut in one keystroke. Placed BEFORE the arrow
        // branch so a plain Down never reaches the toggle path.
        if (a == InputAction::MenuToggle && cb_focused) {
            nav_consumed = true; // the focused widget owns this key
            if (combobox_open[focus_id]) {
                combobox_close_cancel(focus_id);
            } else {
                combobox_open_now(focus_id); // silent open, hl = selection
            }
            continue;
        }
        if ((a == InputAction::MenuDown || a == InputAction::MenuUp || a == InputAction::MenuFirst || a == InputAction::MenuLast || a == InputAction::MenuPageUp ||
             a == InputAction::MenuPageDown) &&
            cb_focused) {
            nav_consumed = true; // the focused widget owns this key
            if (!combobox_open[focus_id]) {
                combobox_open_now(focus_id); // shows full list, hl = selection
            }
            int vis_n = static_cast<int>(combobox_visible_count(focus_id));
            if (vis_n > 0) {
                // A page is the popup's own viewport, so it shrinks with the
                // filter: what the player can actually see is what a page moves.
                const int view  = (vis_n < static_cast<int>(Manager::K_MAX_POPUP_ROWS)) ? vis_n : static_cast<int>(Manager::K_MAX_POPUP_ROWS);
                const int page = (view > 1) ? view - 1 : 1;
                int       nv   = 0;
                if (a == InputAction::MenuFirst) {
                    nv = 0;
                } else if (a == InputAction::MenuLast) {
                    nv = vis_n - 1;
                } else if (a == InputAction::MenuPageUp || a == InputAction::MenuPageDown) {
                    const int d   = (a == InputAction::MenuPageDown) ? page : -page;
                    const int cur = combobox_hl[focus_id];
                    // No highlight yet: anchor at the end the page travels FROM and
                    // then jump a whole viewport. The arrow branch below lands ON row
                    // 0 instead, because a single step from "before the list" is row
                    // 0 - a page is not a step, and PageDown as a no-op on a fresh
                    // popup would read as a dead key.
                    const int base = (cur < 0 || cur >= vis_n) ? (d > 0 ? 0 : vis_n - 1) : cur;
                    nv            = base + d;
                } else {
                    const int d   = (a == InputAction::MenuDown) ? 1 : -1;
                    const int cur = combobox_hl[focus_id];
                    nv            = (cur < 0 || cur >= vis_n) ? (d > 0 ? 0 : vis_n - 1) : cur + d;
                }
                if (nv < 0) {
                    nv = 0;
                }
                if (nv > vis_n - 1) {
                    nv = vis_n - 1;
                }
                combobox_hl[focus_id] = nv;
                // Arrow autoscroll goes through the TARGET like every other
                // input, so holding Down glides instead of stepping 8 times.
                f32 &sc             = combobox_scroll_target[focus_id];
                int    maxr           = static_cast<int>(Manager::K_MAX_POPUP_ROWS);
                if (nv < static_cast<int>(sc)) {
                    sc = static_cast<f32>(nv);
                } else if (nv >= static_cast<int>(sc) + maxr) {
                    sc = static_cast<f32>(nv - maxr + 1);
                }
                f32 maxsc = combobox_maxscroll(static_cast<u8>(vis_n));
                if (sc > maxsc) {
                    sc = maxsc;
                }
                if (sc < 0.0f) {
                    sc = 0.0f;
                }
                combobox_scroll_vel[focus_id] = 0.0f; // keys are discrete, no flick
            }
            continue;
        }
        if ((a == InputAction::Confirm || a == InputAction::MenuConfirm) && cb_focused) {
            if (combobox_open[focus_id]) {
                // Commit the highlight (fall back to the selection when the
                // highlight is stale); an empty filter just shuts.
                int hl    = combobox_hl[focus_id];
                int vis_n = static_cast<int>(combobox_visible_count(focus_id));
                int item  = (hl >= 0 && hl < vis_n) ? combobox_visible_to_item(focus_id, hl) : combobox_selected[focus_id];
                if (item >= 0 && item < combobox_count[focus_id] && item != combobox_selected[focus_id]) {
                    combobox_commit(focus_id, item);
                    clicked = focus_id;
                    emit_event(ui_event_type::CLICK, focus_id);
                    emit_event(ui_event_type::CHANGE, focus_id, static_cast<f32>(item));
                    if (pool[focus_id].on_click) {
                        pool[focus_id].on_click(focus_id, click_user[focus_id]);
                    }
                } else {
                    combobox_close_cancel(focus_id);
                }
            } else {
                combobox_open_now(focus_id); // silent open
            }
            continue;
        }
        if ((a == InputAction::Back || a == InputAction::Pause) && cb_focused && combobox_open[focus_id]) {
            combobox_close_cancel(focus_id);
            continue;
        }
        // Tab / Shift-Tab on a focused ComboBox: CANCEL the open popup, then let
        // focus walk on. Deliberately NOT `continue` and NOT nav_consumed - this
        // key belongs to the focus ring, and the popup has to be shut first or it
        // would stay open on a widget that no longer has focus (drawn by the
        // Pass-4 overlay, which only tests is_visible).
        //
        // Cancel rather than commit: a player who opens the list, moves the
        // highlight and then Tabs away meant to leave, not to accept. Commit
        // stays on Enter, which is the key that means "yes, this one".
        if ((a == InputAction::FocusNext || a == InputAction::FocusPrev) && cb_focused && combobox_open[focus_id]) {
            combobox_close_cancel(focus_id);
        }
        // Focused ListBox owns up/down AND Home/End: move the selection + autoscroll
        // instead of moving focus away. MenuUp/MenuDown/MenuFirst/MenuLast ONLY -
        // Tab is FocusNext/FocusPrev, so Tab still walks the focus ring (the
        // ComboBox branch above documents why SwapUp/SwapDown must not come back).
        //
        // Home/End fold into the SAME body rather than getting their own, because
        // "pick a row, then fire and autoscroll" is the whole contract - a second
        // copy would be free to drift on the event half, which is the half an app
        // actually depends on.
        bool lb_focused = focus_id < MAX && pool[focus_id].type == (u8)widget_type::LISTBOX;
        if ((a == InputAction::MenuDown || a == InputAction::MenuUp || a == InputAction::MenuFirst || a == InputAction::MenuLast || a == InputAction::MenuPageUp ||
             a == InputAction::MenuPageDown) &&
            lb_focused) {
            nav_consumed = true; // the focused widget owns this key
            int n = listbox_count[focus_id];
            if (n > 0) {
                i8 sel      = listbox_selected[focus_id];
                const ListboxMetrics &lm_pre = listbox_metrics[focus_id];
                const int   vis_pre = lm_pre.visible > 0 ? lm_pre.visible : 1;
                // A page is one viewport, and -1 because the row you land on has to
                // still be VISIBLE: moving a full `visible` rows would put the
                // selection exactly one row past the bottom edge, and the
                // autoscroll below would then have to fix it up anyway.
                const int page = (vis_pre > 1) ? vis_pre - 1 : 1;
                int       ns   = 0;
                if (a == InputAction::MenuFirst) {
                    ns = 0;
                } else if (a == InputAction::MenuLast) {
                    ns = n - 1;
                } else if (a == InputAction::MenuPageUp || a == InputAction::MenuPageDown) {
                    const int d = (a == InputAction::MenuPageDown) ? page : -page;
                    // Anchor at the far end when nothing is selected, then jump a
                    // viewport - the same reasoning as the ComboBox page branch
                    // above, and deliberately NOT the arrow branch's "land on the
                    // first row": one step from before the list is row 0, but a
                    // whole page is not a step.
                    ns = (sel < 0) ? ((d > 0 ? 0 : n - 1) + d) : static_cast<int>(sel) + d;
                } else {
                    const int d = (a == InputAction::MenuDown) ? 1 : -1;
                    ns          = (sel < 0) ? (d > 0 ? 0 : n - 1) : static_cast<int>(sel) + d;
                }
                if (ns < 0) {
                    ns = 0;
                }
                if (ns > n - 1) {
                    ns = n - 1;
                }
                if (ns != sel) {
                    listbox_selected[focus_id] = static_cast<i8>(ns);
                    emit_event(ui_event_type::CLICK, focus_id);
                    emit_event(ui_event_type::CHANGE, focus_id, static_cast<f32>(ns));
                    if (pool[focus_id].on_click) {
                        pool[focus_id].on_click(focus_id, click_user[focus_id]);
                    }
                }
                f32                &sc  = listbox_scroll[focus_id];
                const ListboxMetrics &lm  = listbox_metrics[focus_id];
                int                   vis = lm.visible > 0 ? lm.visible : 1;
                if (ns < static_cast<int>(sc)) {
                    sc = static_cast<f32>(ns);
                } else if (ns >= static_cast<int>(sc) + vis) {
                    sc = static_cast<f32>(ns - vis + 1);
                }
                ui::listbox::clamp_scroll(sc, listbox_count[focus_id], lm);
            }
            continue;
        }
        // Focused TabBar owns up/down AND Home/End: switch tab instead of moving
        // focus away (same contract as the focused ListBox above). Clamped, NOT
        // wrapping - a strip that wraps hides the ends of the range.
        // MenuUp/MenuDown/MenuFirst/MenuLast ONLY: clamped + wrapping-free would
        // make Tab a one-way door here too if Tab reached this branch.
        //
        // Left/Right join this branch because a tab strip is a HORIZONTAL row of
        // cells - that is its natural axis, and every desktop toolkit drives it
        // that way. PageUp/PageDown deliberately do NOT: a strip is one line with
        // no viewport, so "a page" of it is undefined. Leaving them unconsumed
        // means a host can still bind them, which is the same contract as Esc on
        // a ListBox (a deliberate no-op, not a missing branch).
        bool tb_focused = focus_id < MAX && pool[focus_id].type == (u8)widget_type::TABBAR;
        if ((a == InputAction::MenuDown || a == InputAction::MenuUp || a == InputAction::MenuFirst || a == InputAction::MenuLast || a == InputAction::MenuLeft ||
             a == InputAction::MenuRight) &&
            tb_focused) {
            nav_consumed = true; // the focused widget owns this key
            const int n = tabbar_count_[focus_id];
            if (n > 0) {
                int t = 0;
                if (a == InputAction::MenuFirst) {
                    t = 0;
                } else if (a == InputAction::MenuLast) {
                    t = n - 1;
                } else {
                    const int d  = (a == InputAction::MenuDown || a == InputAction::MenuRight) ? 1 : -1;
                    t            = tabbar_active[focus_id] + d;
                    if (t < 0) {
                        t = 0;
                    }
                    if (t > n - 1) {
                        t = n - 1;
                    }
                }
                if (t != tabbar_active[focus_id]) {
                    tabbar_active[focus_id] = static_cast<i8>(t);
                    emit_event(ui_event_type::CHANGE, focus_id, static_cast<f32>(t));
                    if (on_change[focus_id]) {
                        on_change[focus_id](focus_id, static_cast<f32>(t));
                    }
                }
            }
            continue;
        }
        // Focused Slider owns up/down AND Home/End. It had NO keyboard branch at
        // all until now, which made it the one focusable widget that ignored the
        // arrow keys - Tab walked straight through it as if it were a button, and
        // a value nobody could adjust without a pointer.
        //
        // The step is a NAMED constant, not a literal in the branch: 1% is small
        // enough to reach any value by repeated presses and large enough to cross
        // the track in 100, and it is deliberately independent of the widget's
        // width (a click is positional, a key is not - tying the two together
        // would make the keyboard step depend on how wide the app drew it).
        bool sl_focused = focus_id < MAX && pool[focus_id].type == (u8)widget_type::SLIDER;
        if ((a == InputAction::MenuDown || a == InputAction::MenuUp || a == InputAction::MenuFirst || a == InputAction::MenuLast) && sl_focused) {
            nav_consumed = true; // the focused widget owns this key
            f32 v = slider_value[focus_id];
            if (a == InputAction::MenuFirst) {
                v = 0.0f;
            } else if (a == InputAction::MenuLast) {
                v = 1.0f;
            } else {
                v += (a == InputAction::MenuDown) ? K_SLIDER_KEY_STEP : -K_SLIDER_KEY_STEP;
                if (v < 0.0f) {
                    v = 0.0f;
                }
                if (v > 1.0f) {
                    v = 1.0f;
                }
            }
            // Same event contract as the drag path, or a keyboard adjustment would
            // be invisible to the app: emit_event(CHANGE) + on_change, both skipped
            // when the value did not actually move (a Home at 0 is a no-op, not a
            // spurious "changed to 0"). `hot` is pointer state and stays untouched.
            if (v != slider_value[focus_id]) {
                slider_value[focus_id] = v;
                emit_event(ui_event_type::CHANGE, focus_id, v);
                if (on_change[focus_id]) {
                    on_change[focus_id](focus_id, v);
                }
            }
            continue;
        }
        // The ONE place focus walks. Tab arrives as FocusNext/FocusPrev - never as
        // MenuUp/MenuDown - so no widget branch above can swallow it, and no
        // text-capture guard is needed: W and S are MenuUp/MenuDown AND letters,
        // but those actions simply no longer mean "move focus" here. A focused
        // TextField takes the editing branch above (which moves focus itself and
        // sets focus_moved, because the action list is not consumed).
        //
        // focus_anchor: when focus was dropped because the focused widget became
        // disabled or hidden (see the sanity block at the top of handle()), Tab
        // resumes from WHERE IT WAS instead of jumping to the first focusable on
        // the page. Matches browsers, where removing the focused element leaves
        // Tab continuing from that position.
        if (a == InputAction::FocusNext) {
            if (!focus_moved) {
                focus_id = find_next_focus(focus_id < MAX ? focus_id : focus_anchor);
                focus_anchor = UINT16_MAX;
            }
        } else if (a == InputAction::FocusPrev) {
            if (!focus_moved) {
                focus_id = find_prev_focus(focus_id < MAX ? focus_id : focus_anchor);
                focus_anchor = UINT16_MAX;
            }
        } else if (a == InputAction::Confirm || a == InputAction::MenuConfirm) {
            // EXPLICIT per type, not a default that fires on_click for anything
            // focusable. The permissive version ran `emit_event(CLICK)` whenever
            // `changed` was true, and `changed` defaulted to true - so every
            // focusable type WITHOUT an on_click emitted a CLICK that went
            // nowhere. Two of them: TabBar and Slider both take only a
            // ChangeCallback. Enter on a focused TabBar therefore told the app it
            // had been clicked, while switching no cell and calling no callback -
            // a false signal is worse than a no-op, because the app acts on it.
            if (focus_id < MAX) {
                auto &w = pool[focus_id];

                // TextField: start editing on confirm
                if (w.type == (u8)widget_type::TEXT_FIELD) {
                    nav_consumed = true;
                    clicked      = focus_id;
                    editing_id   = clicked;
                    std::memcpy(edit_snapshot[clicked], pool[clicked].text, sizeof(edit_snapshot[0]));
                    active = UINT16_MAX;
                    return;
                }

                if (w.type == (u8)widget_type::TOGGLE || w.type == (u8)widget_type::CHECKBOX) {
                    nav_consumed      = true;
                    clicked           = focus_id;
                    w.state           = w.state ? 0 : 1;
                    emit_event(ui_event_type::CLICK, clicked);
                    emit_event(ui_event_type::CHANGE, clicked, static_cast<f32>(w.state));
                    if (w.on_click) {
                        w.on_click(focus_id, click_user[focus_id]);
                    }
                    continue;
                }

                if (w.type == (u8)widget_type::RADIOBOX) {
                    // Re-confirming the selected radio is a silent no-op, same as
                    // a second tap.
                    if (w.state) {
                        continue;
                    }
                    nav_consumed = true;
                    clicked      = focus_id;
                    radiobox_select(clicked);
                    emit_event(ui_event_type::CLICK, clicked);
                    emit_event(ui_event_type::CHANGE, clicked, 1.0f);
                    if (w.on_click) {
                        w.on_click(focus_id, click_user[focus_id]);
                    }
                    continue;
                }

                if (w.type == (u8)widget_type::BUTTON || w.type == (u8)widget_type::LISTBOX) {
                    // A Button presses; a ListBox activates its current selection
                    // (the arrows move the selection, this acts on it).
                    nav_consumed = true;
                    clicked      = focus_id;
                    emit_event(ui_event_type::CLICK, clicked);
                    if (w.on_click) {
                        w.on_click(focus_id, click_user[focus_id]);
                    }
                    continue;
                }

                // TABBAR, SLIDER, and anything else focusable: Confirm means
                // nothing here, so it means NOTHING - no event, no callback, and
                // no nav_consumed, because nothing consumed the key. A TabBar's
                // arrows already move the active cell, so Enter had no state left
                // to change; a Slider has no discrete activation at all.
            }
        }
    }

    // ── Make the focused widget VISIBLE, not merely focused ──
    // At the END of handle(), so every path that can move focus is covered by one
    // call: the nav loop, the TextField Tab commit, begin_modal, and the release
    // block at the top. Putting it at each site instead would be four places to
    // forget; putting it at the top would lag a frame, which reads as the ring
    // appearing off-screen and then jumping.
    //
    // A ScrollView's children are all in the tab order - it is a VIEWPORT, not a
    // selection control - so Tab WILL walk into content that is currently below
    // the fold. Before this, nothing reconciled the ring with the offset: the ring
    // was drawn outside the viewport and clipped away, so focus looked stuck with
    // no visible cause. Browsers, macOS and Qt all scroll the focused control into
    // view. Instant, not animated, to match the ListBox's own autoscroll.
    reveal_focused();

    // Update button/toggle/checkbox visual states
    for (u16 i = 0; i < count; ++i) {
        // EFFECTIVE, so a widget inside a hidden panel does not sit at HOVER or
        // PRESSED forever: nothing resets those on hide, so the stale state would
        // be there the moment the panel came back.
        if (!is_visible(i)) {
            continue;
        }
        u8 t = pool[i].type;
        if (t != (u8)widget_type::BUTTON && t != (u8)widget_type::TOGGLE && t != (u8)widget_type::CHECKBOX &&
            t != (u8)widget_type::RADIOBOX) {
            continue;
        }
        if (t == (u8)widget_type::TOGGLE || t == (u8)widget_type::CHECKBOX || t == (u8)widget_type::RADIOBOX) {
            if (!is_enabled(i)) {
                pool[i].bg_color = ui_darken(off_color[i], 80);
            } else if (i == active && hot == active) {
                pool[i].bg_color = pool[i].state ? on_color[i] : off_color[i];
            } else if (i == hot || i == focus_id) {
                pool[i].bg_color = pool[i].state ? ui_lighten(on_color[i], 20) : ui_lighten(off_color[i], 20);
            } else {
                pool[i].bg_color = pool[i].state ? on_color[i] : off_color[i];
            }
        } else if (!is_enabled(i)) {
            pool[i].state = (u8)btn_state::NORMAL;
        } else if (i == active && hot == active && !scroll_dragging) {
            // PRESSED, but not once the gesture has become a scroll: a pressed
            // row inside a ScrollView stayed squeezed for the whole drag, which
            // reads as "the item shrank while the content moved", and it is not
            // what a native scroll view does either (the touch is cancelled on
            // the cell the moment the scroll starts). scroll_dragging is cleared
            // at the top of handle() when the gesture ends, so this cannot latch.
            pool[i].state = (u8)btn_state::PRESSED;
        } else if (i == hot || i == focus_id) {
            pool[i].state = (u8)btn_state::HOVER;
        } else {
            pool[i].state = (u8)btn_state::NORMAL;
        }
    }

    // Focus / hover edge detection (net change per frame, all paths above
    // funnel here; UINT16_MAX = nothing).
    if (focus_id != prev_focus) {
        if (prev_focus != UINT16_MAX) {
            emit_event(ui_event_type::FOCUS_LOST, prev_focus);
        }
        if (focus_id != UINT16_MAX) {
            emit_event(ui_event_type::FOCUS_GAINED, focus_id);
        }
        prev_focus = focus_id;
    }
    if (hot != prev_hot) {
        if (prev_hot != UINT16_MAX) {
            emit_event(ui_event_type::HOVER_EXIT, prev_hot);
        }
        if (hot != UINT16_MAX) {
            emit_event(ui_event_type::HOVER_ENTER, hot);
        }
        prev_hot = hot;
    }
}

void Manager::layout_children(u16 parent_id) noexcept {
    if (parent_id >= MAX) {
        return;
    }
    if (layout_type[parent_id] == 1) {
        layout_children_axis<0>(parent_id);
    } else if (layout_type[parent_id] == 2) {
        layout_children_axis<1>(parent_id);
    }
}

// Flow-layout kernel, Axis 0 = horizontal (HBox), 1 = vertical (VBox).
// Template so the axis choice folds at compile time (zero-cost vs branches).
// Two passes: measure total extent, then place with alignment.
// Legacy equivalence: with align=Start/Start, pct=0, margin=0 the output is
// bit-identical to the old sequential pile-from-padding pass.
template <u8 Axis> void Manager::layout_children_axis(u16 parent_id) noexcept {
    auto   &parent      = pool[parent_id];
    f32   lpad        = static_cast<f32>(layout_pad[parent_id]);
    f32   gap         = static_cast<f32>(layout_spacing[parent_id]);
    u8 pack        = layout_align[parent_id];
    u8 main_align  = pack & 0x3;
    u8 cross_align = (pack >> 2) & 0x3;
    if (main_align > 2) {
        main_align = 0;
    }
    if (cross_align > 2) {
        cross_align = 0;
    }

    f32 parent_main  = (Axis == 0) ? parent.frame.w : parent.frame.h;
    f32 parent_cross = (Axis == 0) ? parent.frame.h : parent.frame.w;
    f32 inner_main   = parent_main - 2.0f * lpad;
    f32 inner_cross  = parent_cross - 2.0f * lpad;
    if (inner_main < 0.0f) {
        inner_main = 0.0f;
    }
    if (inner_cross < 0.0f) {
        inner_cross = 0.0f;
    }

    auto main_size = [&](u16 id) noexcept -> f32 {
        u8 pct = (Axis == 0) ? size_pct_w[id] : size_pct_h[id];
        f32   abs = (Axis == 0) ? pool[id].frame.w : pool[id].frame.h;
        return (pct == 0) ? abs : inner_main * (static_cast<f32>(pct) / 100.0f);
    };
    auto cross_size = [&](u16 id) noexcept -> f32 {
        u8 pct = (Axis == 0) ? size_pct_h[id] : size_pct_w[id];
        f32   abs = (Axis == 0) ? pool[id].frame.h : pool[id].frame.w;
        return (pct == 0) ? abs : inner_cross * (static_cast<f32>(pct) / 100.0f);
    };
    // Margin indices: HBox main = left[3]/right[1], cross = top[0]/bottom[2];
    // VBox swaps the roles.
    auto     m_before_main  = [&](u16 id) noexcept -> f32 { return static_cast<f32>((Axis == 0) ? margin[id][3] : margin[id][0]); };
    auto     m_after_main   = [&](u16 id) noexcept -> f32 { return static_cast<f32>((Axis == 0) ? margin[id][1] : margin[id][2]); };
    auto     m_before_cross = [&](u16 id) noexcept -> f32 { return static_cast<f32>((Axis == 0) ? margin[id][0] : margin[id][3]); };
    auto     m_after_cross  = [&](u16 id) noexcept -> f32 { return static_cast<f32>((Axis == 0) ? margin[id][2] : margin[id][1]); };

    // Pass 1: total main-axis extent.
    f32    total          = 0.0f;
    u16 n              = 0;
    for (u16 i = 0; i < count; ++i) {
        if (pool[i].parent != parent_id) {
            continue;
        }
        // Own flag ON PURPOSE, unlike every handle() site above. This asks
        // "should this widget take up space / animate", not "can the user
        // reach it": a hidden subtree has no visual effect, and skipping it is
        // what stops an invisible widget from reserving layout space or easing
        // something nobody can see. Effective visibility is right for a RENDER
        // pass - a pass that draws its own primitive outlives the rect it was
        // clipped to - and wrong here.
        if (!(pool[i].flags & WF_VISIBLE)) {
            continue;
        }
        // Out-of-flow: ABSOLUTE/ANCHORED children keep their own placement
        // (resolved in rebuild_abs_cache), so the flow ignores them.
        if (pool[i].pos_mode != mm_math::position_mode::RELATIVE) {
            continue;
        }
        // A SCROLLVIEW's children are CONTENT, not a flow: their coordinates are
        // their position in the scrollable area (which may be far taller than the
        // window), so laying them out would stack them from the padding and
        // destroy the arrangement the app asked for. Same rule as out-of-flow,
        // different reason - and it has to be BOTH passes or the group total and
        // the placement disagree.
        if (pool[parent_id].type == (u8)widget_type::SCROLLVIEW) {
            continue;
        }
        total += m_before_main(i) + main_size(i) + m_after_main(i);
        ++n;
    }
    if (n > 1) {
        total += gap * static_cast<f32>(n - 1);
    }

    // Pass 2: place with main-axis alignment (overflow falls back to Start).
    f32 start = lpad;
    if (main_align == 1) {
        start += (inner_main - total) * 0.5f;
    } else if (main_align == 2) {
        start += inner_main - total;
    }
    if (start < lpad) {
        start = lpad;
    }
    f32 cursor = start;
    bool  first  = true;
    for (u16 i = 0; i < count; ++i) {
        if (pool[i].parent != parent_id) {
            continue;
        }
        // Own flag ON PURPOSE, unlike every handle() site above. This asks
        // "should this widget take up space / animate", not "can the user
        // reach it": a hidden subtree has no visual effect, and skipping it is
        // what stops an invisible widget from reserving layout space or easing
        // something nobody can see. Effective visibility is right for a RENDER
        // pass - a pass that draws its own primitive outlives the rect it was
        // clipped to - and wrong here.
        if (!(pool[i].flags & WF_VISIBLE)) {
            continue;
        }
        if (pool[i].pos_mode != mm_math::position_mode::RELATIVE) {
            continue; // out-of-flow (see Pass 1)
        }
        if (pool[parent_id].type == (u8)widget_type::SCROLLVIEW) {
            continue; // scroll content (see Pass 1)
        }
        auto &child = pool[i];
        if (!first) {
            cursor += gap;
        }
        first                = false;
        cursor              += m_before_main(i);
        f32 c_main         = main_size(i);
        f32 c_cross        = cross_size(i);
        f32 c_total_cross  = m_before_cross(i) + c_cross + m_after_cross(i);
        f32 cross_off      = 0.0f;
        if (cross_align == 1) {
            cross_off = (inner_cross - c_total_cross) * 0.5f;
        } else if (cross_align == 2) {
            cross_off = inner_cross - c_total_cross;
        }
        if (cross_off < 0.0f) {
            cross_off = 0.0f;
        }
        if constexpr (Axis == 0) {
            child.frame.x = cursor;
            child.frame.y = lpad + m_before_cross(i) + cross_off;
            if (size_pct_w[i] != 0) {
                child.frame.w = c_main;
            }
            if (size_pct_h[i] != 0) {
                child.frame.h = c_cross;
            }
        } else {
            child.frame.y = cursor;
            child.frame.x = lpad + m_before_cross(i) + cross_off;
            if (size_pct_h[i] != 0) {
                child.frame.h = c_main;
            }
            if (size_pct_w[i] != 0) {
                child.frame.w = c_cross;
            }
        }
        cursor += c_main + m_after_main(i);

        if (layout_type[i] != 0) {
            layout_children(i);
        }
    }
}

template void Manager::layout_children_axis<0>(u16) noexcept;
template void Manager::layout_children_axis<1>(u16) noexcept;

// Layout a subtree: containers get laid out (recursing into nested ones),
// plain widgets only forward the search to their children. This fixes
// containers nested under non-layout parents (previously never laid out);
// root-level containers behave exactly as before.
void          Manager::layout_subtree(u16 id) noexcept {
    if (id >= MAX) {
        return;
    }
    // Own flag on purpose (see layout_children_axis): a hidden subtree takes no
    // space and animates nothing.
    if (!(pool[id].flags & WF_VISIBLE)) {
        return;
    }
    if (layout_type[id] != 0) {
        layout_children(id);
        return;
    }
    for_each_child(id, [this](u16 i) noexcept { layout_subtree(i); });
}

void Manager::layout(Renderer &r) noexcept {
    if (measure_dirty) {
        measure(r);
        measure_dirty = false;
    }
    abs_cache_dirty = true;
    for (u16 i = 0; i < count; ++i) {
        if (pool[i].parent != UINT16_MAX) {
            continue;
        }
        // Own flag on purpose (see layout_children_axis): a hidden root lays out
        // nothing, which is what keeps an invisible subtree from reserving space.
        if (!(pool[i].flags & WF_VISIBLE)) {
            continue;
        }
        layout_subtree(i);
    }
}

// ─── Line drawing helper ───────────────────────────────────────────

// ── Plan B: time-driven state advance (moved verbatim out of render) ──
void Manager::update(f32 dt) noexcept {
    const f32 anim_speed = 10.0f;
    f32       t_lerp     = mm_math::exp_damp(anim_speed, dt);
    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        // Own flag on purpose (see layout_children_axis): driving the animation of
        // something nobody can see is wasted work, and freezing mid-ease is
        // invisible - the widget is not on screen to be "stuck".
        if (!(w.flags & WF_VISIBLE)) {
            continue;
        }
        if (w.type == (u8)widget_type::BUTTON || w.type == (u8)widget_type::TOGGLE || w.type == (u8)widget_type::CHECKBOX ||
            w.type == (u8)widget_type::RADIOBOX) {
            if (!is_enabled(i)) {
                w.press_scale_target = 1.0f;
            } else if (w.state == (u8)btn_state::PRESSED) {
                w.press_scale_target = 0.85f;
            } else {
                w.press_scale_target = 1.0f;
            }
            w.press_scale += (w.press_scale_target - w.press_scale) * t_lerp;
            if (w.type == (u8)widget_type::TOGGLE) {
                f32 target  = w.state ? 1.0f : 0.0f;
                w.thumb_pos  += (target - w.thumb_pos) * t_lerp;
            }
            if (w.type == (u8)widget_type::CHECKBOX || w.type == (u8)widget_type::RADIOBOX) {
                // State cross-fade (sibling of the toggle thumb slide): the box
                // colour lerps off->on and the dot/checkmark fades in with it.
                f32 target    = w.state ? 1.0f : 0.0f;
                state_fade[i]  += (target - state_fade[i]) * t_lerp;
            }
            if (w.type == (u8)widget_type::BUTTON) {
                f32 target    = (is_enabled(i) && w.state == (u8)btn_state::HOVER) ? 1.0f : 0.0f;
                w.hover_factor += (target - w.hover_factor) * t_lerp;
            }
        } else {
            w.press_scale = 1.0f;
        }
        if (w.type == (u8)widget_type::PANEL && w.anim_t < 1.0f) {
            w.anim_t += dt * anim_speed;
            if (w.anim_t > 1.0f) {
                w.anim_t = 1.0f;
            }
        }
        // Accordion height. Advances in update() like every other driven value
        // (render stays pure draw), and the content's visibility flips exactly
        // when there is nothing left of it to see - never on the frame the
        // close starts, which would make the animation pointless.
        if (acc_content[i] != UINT16_MAX) {
            const f32 next = accordion_step(acc_drawn_h[i], acc_target_h[i], dt);
            if (next != acc_drawn_h[i]) {
                acc_drawn_h[i] = next;
                if (next <= AccordionStyle::MIN_H && is_visible(acc_content[i])) {
                    set_visible(acc_content[i], false);
                }
                abs_cache_dirty = true;
            }
        }
        // Tooltip timing ONLY: update() has no Renderer and creating a widget
        // from here would churn the pool mid-frame. The panel is drawn as a
        // plain overlay in render() (a hint is not a widget: no focus, no
        // input, nothing to lay out).
        if (w.type != (u8)widget_type::COMBOBOX) {
            const u16 host = (i == hot) ? i : UINT16_MAX;
            const bool     owned = (host != UINT16_MAX) && (tooltip_str[host] != nullptr);
            if (owned && dt > 0.0f) {
                if (tooltip_for != host) {
                    tooltip_for = host;
                    tooltip_t   = 0.0f;
                }
                tooltip_t += dt;
            } else if (tooltip_for != UINT16_MAX) {
                tooltip_for = UINT16_MAX;
                tooltip_t   = 0.0f;
            }
        }
        if (w.type == (u8)widget_type::COMBOBOX) {
            // Popup open/close animation: uniform scale about the popup's
            // field-side edge + alpha, advanced here so render() stays pure
            // draw (Plan B). open_now() starts it at 0.
            if (combobox_open[i] && dt > 0.0f) {
                const f32 target = combobox_closing[i] ? 0.0f : 1.0f;
                const f32 rest   = target - combobox_anim[i];
                combobox_anim[i] += rest * t_lerp;
                // Snap on the REMAINING DISTANCE, never on the current value:
                // a fresh popup sits at anim == 0 with target == 1, so a
                // value-based test snapped it open on frame 1 (and dt == 0
                // snapped it too, which broke the no-op guarantee).
                if (__builtin_fabsf(rest) < 0.01f) {
                    combobox_anim[i] = target;
                    if (combobox_closing[i]) {
                        // animation landed: actually take it down
                        combobox_closing[i] = false;
                        combobox_open[i]    = false;
                        combobox_scroll[i] = 0.0f;
                        combobox_scroll_target[i] = 0.0f;
                    }
                }
            }
            // Scroll: the drawn value eases toward the target instead of
            // tracking the finger 1:1 (stutter), and a flick keeps going after
            // release. dt == 0 advances nothing (noop guard, like the rest).
            if (combobox_open[i] && dt > 0.0f) {
                const ListboxMetrics &cm = combobox_metrics[i];
                if (cm.row_h > 0.0f) {
                    const f32 maxsc = combobox_maxscroll(combobox_visible_count(i));
                    f32       &tgt  = combobox_scroll_target[i];
                    f32       &val  = combobox_scroll[i];
                    f32       &vel  = combobox_scroll_vel[i];
                    // state 1 = thumb drag, 2 = body drag
                    const bool dragging = (active == i) && (w.state == 1 || w.state == 2);
                    if (dragging) {
                        // Rows the finger asked for this frame -> rows/second,
                        // smoothed (one frame is far too noisy).
                        const f32 d = tgt - val;
                        vel           = vel * 0.55f + (d / dt) * 0.45f;
                    } else if (vel != 0.0f) {
                        // Flick inertia: keep pushing the target, decay fast.
                        tgt += vel * dt;
                        vel *= std::exp(-4.0f * dt);
                        if (__builtin_fabsf(vel) < 0.6f) {
                            vel = 0.0f;
                        }
                    }
                    if (tgt < 0.0f) {
                        tgt = 0.0f;
                        vel = 0.0f;
                    }
                    if (tgt > maxsc) {
                        tgt = maxsc;
                        vel = 0.0f;
                    }
                    // Big jump (reopen, filter change): don't glide across
                    // half the list, land there.
                    if (__builtin_fabsf(tgt - val) > 4.0f) {
                        val = tgt;
                    } else {
                        val += (tgt - val) * t_lerp;
                    }
                    if (__builtin_fabsf(tgt - val) < 0.0005f) {
                        val = tgt;
                    }
                }
            }
        }
        if (w.type == (u8)widget_type::PROGRESSBAR) {
            // Displayed value chases the set_progress() target (same
            // exponential smoothing as toggle thumbs); snap on arrival so
            // targets always land. dt == 0 advances nothing (noop guard).
            f32 diff = progressbar_target[i] - progressbar_value[i];
            if (diff != 0.0f && dt > 0.0f) {
                f32 step = diff * t_lerp;
                if (__builtin_fabsf(step) < 0.0005f || __builtin_fabsf(diff) < 0.0005f) {
                    progressbar_value[i] = progressbar_target[i];
                } else {
                    progressbar_value[i] += step;
                }
            }
            // Striped fill: the band phase walks at a constant rate and wraps
            // at the pitch, so the pattern drifts forever without growing the
            // f32. dt == 0 advances nothing (same noop guard as the value).
            const f32 pitch = styles[w.style_id].stripe_pitch;
            if (pitch > 0.0f && dt > 0.0f) {
                // fmodf, not a single `-= pitch`: the increment is
                // dt * speed and a large dt (a hitch) or a small pitch makes it
                // exceed one period, so one subtraction can leave the phase
                // OUT of [0, pitch). The test asserts the invariant.
                progressbar_stripe_t[i] =
                    fmodf(progressbar_stripe_t[i] + dt * progressbar::K_STRIPE_SPEED, pitch);
            }
            // Completion burst (circle only): arrival at full pops one
            // sparkle per fill; dropping the target re-arms it. Advance
            // runs before trigger so a newborn burst never expires in the
            // same (large-dt) frame.
            if (w.shape == (u8)shape_type::CIRCLE) {
                if (progressbar_burst[i] >= 0.0f && dt > 0.0f) {
                    progressbar_burst[i] += dt;
                    if (progressbar_burst[i] > progressbar::K_BURST_DUR) {
                        progressbar_burst[i] = -1.0f;
                    }
                }
                if (progressbar_target[i] < 0.999f) {
                    progressbar_celebrated[i] = false;
                } else if (progressbar_value[i] >= 0.999f && !progressbar_celebrated[i] && progressbar_burst[i] < 0.0f) {
                    progressbar_burst[i]       = 0.0f;
                    progressbar_celebrated[i] = true;
                }
            }
        }
    }
    cursor_timer += dt; // drives cursor blink + focus-ring pulse (read in render)
}

// Text box for alignment: the content rect when the widget pins its own width
// (set_text_align), else the ink box so LEFT is bit-identical to before.
static f32 text_box_w(const Manager &m, u16 i, f32 ink_w) noexcept {
    const auto &w = m.pool[i];
    // No pinned box: the frame hugs the ink, so nothing is ever clipped. That
    // keeps every unpinned label bit-identical to before.
    if (m.text_box[i] == 0) {
        return ink_w;
    }
    // A PINNED box is the text's limit whatever the alignment. The old test was
    // `a != LEFT`, which conflated "the app pinned a box" with "the app asked
    // for left": a left-aligned heading in a fixed-width cell overflowed with
    // nothing to stop it, because LEFT was the one alignment truncation never
    // reached. text_box[i] is the real discriminator - see set_text_align.
    return static_cast<f32>(m.text_box[i]) - static_cast<f32>(w.pad[1] + w.pad[3]);
}

// The LIMIT, and the ONLY trigger for ellipsis. This has to be separate from
// text_box_w(): for an unpinned label those two are the same number - the ink
// width - and budgeting the "..." against the ink width truncates EVERY label
// in the toolkit, which is exactly what happened the first time.
static f32 text_box_limit(const Manager &m, u16 i) noexcept {
    if (m.text_box[i] == 0) {
        return 0.0f;
    }
    const auto &w = m.pool[i];
    return static_cast<f32>(m.text_box[i]) - static_cast<f32>(w.pad[1] + w.pad[3]);
}

static text_align text_align_of(const Manager &m, u16 i) noexcept {
    return m.align[i];
}

// Draw a widget's text as up to N lines (N = whatever the string contains -
// '\\n' was previously skipped, so a two-line label lost the character).
// `x` + `box_w` are the text BOX (not the ink box): every line is placed inside
// it per `a`, using its OWN width, so a multi-line string aligns as a block and
// each line's offset stays exact. Returns the number of lines drawn.
static u32 draw_text_lines(Renderer &r, const char *txt, f32 x, f32 top, f32 scale, u32 color, text_align a, f32 box_w, f32 limit_w) noexcept {
    if (txt == nullptr || txt[0] == '\0') {
        return 0;
    }
    const f32 lh = r.default_font.line_height * scale;
    u32     n  = 0;
    const char  *p  = txt;
    // A PINNED box limits the text, so an overlong line is cut with "...".
    // Measured with the SAME walk that draws it (scan_line), and the dots are
    // ASCII because Karla has no U+2026. `scan_line` cuts on a codepoint
    // boundary, so there is no UTF-8 back-off here any more.
    //
    // `limit_w` is 0 for an unpinned label - which must never truncate, since
    // its box IS the text. `box_w` is still passed for alignment.
    const bool  ellipsize = limit_w > 0.0f;
    f32       dots      = 0.0f;
    if (ellipsize) {
        if (const auto *dg = r.default_font.get_glyph('.')) {
            dots = 3.0f * static_cast<f32>(dg->advance) * scale;
        }
    }
    for (;;) {
        const char  *nl   = std::strchr(p, '\n');
        const size_t len  = nl ? static_cast<size_t>(nl - p) : std::strlen(p);
        // budget excludes the dots, so a line is only cut when the kept text
        // plus "..." still fits the box.
        const LineScan ls  = scan_line(r.default_font, p, len, scale, ellipsize ? limit_w - dots : -1.0f);
        // Ascent of THIS line, so each line's baseline is exact.
        const f32 asc = (ls.ascent > 0.001f) ? ls.ascent : 26.0f * scale;
        f32       lx  = x;
        if (a != text_align::LEFT) {
            // This line's own advance (not the block's) is what it is aligned
            // BY - centring every line on the widest line would left-align the
            // short ones inside the block.
            lx = align_text_x(a, x, box_w, ls.advance);
        }
        char   line[256];
        size_t cl = len;
        if (ellipsize && ls.keep < len && ls.keep + 3 < sizeof(line)) {
            std::memcpy(line, p, ls.keep);
            std::memcpy(line + ls.keep, "...", 3);
            cl = ls.keep + 3;
            // Terminate at the CUT length: the source tail is still in the
            // buffer, and without this the truncated string runs on into it
            // ("a heading far t" + "..." + "long for its box") - which reads
            // exactly like a second, untruncated copy of the label.
            line[cl] = '\0';
        } else {
            cl = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            std::memcpy(line, p, cl);
            line[cl] = '\0';
        }
        r.draw_text(r.default_font, line, lx, top + static_cast<f32>(n) * lh + asc, color, scale);
        ++n;
        if (nl == nullptr) {
            break;
        }
        p = nl + 1;
    }
    return n;
}

// DataGrid header titles (Pass 3). One title per column, laid out with the
// measured column geometry and the SHARED ellipsis/alignment rules rather than a
// second set of them: a title that ignores the ellipsis limit is how a column
// called "Move count" ends up printed over its neighbour.
static void grid_header_text(Manager &m, u16 i, Renderer &r, f32 ax, f32 ay) noexcept {
    const auto &w = m.pool[i];
    if (w.type != (u8)widget_type::LISTBOX || !m.is_grid(i) || m.grid_header_h[i] <= 0.5f) {
        return;
    }
    const ListboxMetrics &lm = m.listbox_metrics[i];
    if (lm.row_h <= 0.0f || m.grid_metrics[i].columns == 0) {
        return;
    }
    f32 gx0 = 0.0f, gy0 = 0.0f, gx1 = 0.0f, gy1 = 0.0f;
    content_rect(m.styles[w.style_id], ax, ay, w.frame.w, w.frame.h, gx0, gy0, gx1, gy1);
    if (!(gx1 > gx0 && gy1 > gy0)) {
        return;
    }
    const GridMetrics &gm      = m.grid_metrics[i];
    const f32        head    = m.grid_header_h[i];
    const f32        top     = gy0 + (head - static_cast<f32>(r.default_font.line_height) * lm.text_scale) * 0.5f;
    const f32        sb_w    = m.listbox_count[i] > lm.visible ? lm.scrollbar_w : 0.0f;
    const f32        avail_w = (gx1 - gx0) - sb_w;
    for (u8 c = 0; c < gm.columns; ++c) {
        const char *t = m.grid_cols[i][c].title;
        if (t == nullptr || t[0] == '\0') {
            continue;
        }
        // The title box stops short of the sort marker.
        const f32  box = gm.w[c] - 14.0f;
        if (box <= 2.0f) {
            continue;
        }
        const grid_sort sd  = m.grid_sort_of(i, c);
        const u32 col = sd == grid_sort::NONE ? m.theme.text_secondary : m.theme.text_primary;
        draw_text_lines(r, t, gx0 + gm.x[c] + 4.0f, top, lm.text_scale, col, m.grid_cols[i][c].align, box,
                        box < avail_w ? box : avail_w - gm.x[c]);
    }
}


void Manager::render(Renderer &r, SpriteBatch &batch) noexcept {
    // Sanitize frames BEFORE anything reads them. Layouts are written in
    // absolute numbers (`view_h - 230`, `frame.w - 24`), so a small window makes
    // a rect NEGATIVE - and the scissor args are uint16, where a negative f32
    // wraps to ~65000. That is not "clip nothing": it is a scissor covering the
    // whole window, which is how a too-small window turned into a full-screen
    // smear instead of a clipped panel.
    //
    // Clamping to 0 degrades to "widget draws nothing", which is the honest
    // outcome for a rect that does not fit. The host also sets a content
    // minimum (mm_app_mac.mm) so this is a net, not the primary fix.
    for (u16 i = 0; i < count; ++i) {
        auto &f = pool[i].frame;
        if (!(f.w > 0.0f)) {
            f.w = 0.0f;
        }
        if (!(f.h > 0.0f)) {
            f.h = 0.0f;
        }
    }
    rebuild_abs_cache();
    view_w = static_cast<f32>(r.width);
    view_h = static_cast<f32>(r.height);

    if (measure_dirty) {
        measure(r);
        measure_dirty = false;
    }
    // Refresh ListBox/ComboBox headers: theme scale → box size → item sizes.
    {
        f32 ui_scale = theme.font_scale;
        f32 line_h   = static_cast<f32>(r.default_font.line_height);
        for (u16 i = 0; i < count; ++i) {
            // EFFECTIVE visibility, not the own flag: a widget inside a hidden
            // container must not draw. The clip alone is not enough — a pass
            // that draws its own primitive (a ScrollView's scrollbar, a
            // TabBar's cells) can outlive the rect it was clipped to. Every
            // render pass below uses is_visible() for the same reason.
            if (!is_visible(i)) {
                continue;
            }
            if (pool[i].type == (u8)widget_type::LISTBOX) {
                // Rows live in the content rect (inside the border bands),
                // so the visible count derives from the inset height.
                // Borderless styles inset 0 → identical to before.
                f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
                ui::content_insets(styles[pool[i].style_id], pool[i].frame.w, pool[i].frame.h, il, it, ir, ib);
                f32 inset_h = pool[i].frame.h - it - ib;
                if (inset_h <= 0.0f) {
                    inset_h = pool[i].frame.h; // degenerate style: frame behavior
                }
                listbox_metrics[i] = Manager::compute_listbox_metrics(inset_h, line_h, ui_scale);
                // DataGrid: the header is exactly one row tall, so the ROW area
                // is the content rect minus that row. row_h derives from line_h
                // alone, so taking it from the full-height metrics first is not
                // circular - only `visible` shrinks.
                grid_header_h[i] = is_grid(i) ? listbox_metrics[i].row_h : 0.0f;
                if (grid_header_h[i] > 0.0f) {
                    const f32 rows_h = inset_h - grid_header_h[i];
                    listbox_metrics[i] = Manager::compute_listbox_metrics(rows_h > 0.0f ? rows_h : 0.0f, line_h, ui_scale);
                    const f32 content_w = pool[i].frame.w - il - ir;
                    grid_metrics[i]     = compute_grid_metrics(grid_cols[i], grid_col_count[i], content_w > 0.0f ? content_w : pool[i].frame.w);
                }
            } else if (pool[i].type == (u8)widget_type::TABBAR) {
                // Strip geometry + label scale from the frame, same place (and
                // same line_h) as the listbox rows.
                tabbar_metrics[i] = tabbar::compute(*this, i, line_h);
            } else if (pool[i].type == (u8)widget_type::COMBOBOX) {
                // Row derivation only (lm.visible is unused: the popup shows
                // up to K_MAX_POPUP_ROWS regardless of field height).
                combobox_metrics[i] = Manager::compute_listbox_metrics(pool[i].frame.h, line_h, ui_scale);
            }
        }
    }
    batch.reset();

    u16 fw = static_cast<u16>(r.width);
    u16 fh = static_cast<u16>(r.height);

    // ── Pass 1: background rectangles ───────────────────────────
    // Flush per material-group for widgets with custom shader overrides
    bool            has_batch   = false;
    const Material *current_mat = nullptr;   // nullptr = no per-widget material override
    // Style-level background texture (WidgetStyle::bg_tex). Grouped like the
    // material so a run of same-texture widgets still batches into one flush.
    TextureHandle   current_tex = TextureHandle{};
    current_tex.handle.id = 0; // "no texture" (see the stex comment)

    // The clip rect is part of the batch identity, exactly like the material and
    // the style texture. It has to be: scissors are recorded into the graph and
    // EXECUTED at submit(), so a run of same-material widgets that all queued a
    // scissor before one flush means only the LAST scissor in the run applies to
    // the whole run - every earlier clipped widget drew unclipped. Invisible
    // until now because labels are text (drawn in Pass 3, where draw_text pairs
    // its own scissor) and panels rarely overlap; ScrollView is the first
    // container whose CHILDREN are ordinary widgets, so it is the first thing
    // that made a Pass 1 clip rect matter.
    i16  cur_cx = 0, cur_cy = 0;
    u16 cur_cw = 0, cur_ch = 0;
    bool     cur_clipped = false;

    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!is_visible(i)) {
            continue;
        }
        if (w.bg_color == 0 && w.type != (u8)widget_type::IMAGE) {
            continue;
        }
        if (w.type == (u8)widget_type::LABEL) {
            continue;
        }
        if (w.type == (u8)widget_type::CHECKBOX || w.type == (u8)widget_type::RADIOBOX) {
            continue; // drawn in pass 1.75 (rounded shader)
        }
        if (w.type == (u8)widget_type::TABBAR) {
            continue; // drawn in pass 1.9 (bar + active cell)
        }

        f32           ax  = abs_x(i);
        f32           ay  = abs_y(i);

        // Resolve per-widget material override, else the style's bg_tex. A
        // per-widget material wins: it can also swap the pipeline, which
        // bg_tex deliberately does not (it keeps the rounded SDF path).
        const Material *mat = widget_material[i].pipeline.is_valid() ? &widget_material[i] : nullptr;
        // is_valid(), NOT handle.id != 0: TextureHandle::invalid() is
        // 0xFFFFFFFF, so an `!= 0` test routes every style that has NO texture
        // (every zero-init one, which is most of them) through the TEXTURED
        // flush with a dead handle - it sampled whatever happened to be bound
        // last, which is how a RECT button rendered as nothing at all. Same
        // trap as WidgetStyle::fill_tex.
        // "No texture" is a ZERO-id handle, not TextureHandle::invalid():
        // flush_sprites_impl resolves `tid != 0` and the group test below is
        // `handle.id != 0`, so invalid() (0xFFFFFFFF) reads as "has a texture"
        // and binds a dead handle. That sampled whatever was bound last - which
        // is how a RECT widget could vanish entirely.
        TextureHandle stex;
        stex.handle.id = 0;
        if (mat == nullptr && styles[w.style_id].bg_tex.is_valid()) {
            stex = styles[w.style_id].bg_tex;
        }

        // Resolve this widget's clip BEFORE the flush decision: a clip change is
        // a batch boundary for the same reason a material change is.
        i16  ncx, ncy;
        u16 ncw, nch;
        const bool nclipped = get_clip(i, ncx, ncy, ncw, nch);
        // Needed below: after the flush decision cur_clipped has been overwritten
        // with THIS widget's value, so the "were we clipped before" test has to
        // be captured first.
        const bool was_clipped = cur_clipped;

        // An empty clip means draw NOTHING (see clip_draws). Skip before the
        // flush decision so the previous widget's scissor stays in force.
        if (!clip_draws(nclipped, ncw, nch)) {
            continue;
        }

        // Flush previous batch if material, style texture, or CLIP changed
        if (mat != current_mat || stex != current_tex || nclipped != cur_clipped ||
            (nclipped && (ncx != cur_cx || ncy != cur_cy || ncw != cur_cw || nch != cur_ch))) {
            if (has_batch) {
                if (current_mat) {
                    r.flush_sprites(batch, *current_mat);
                } else if (current_tex.handle.id != 0) {
                    // Textured, but still through the rounded SDF pipeline so
                    // style corner radius + per-side/ring borders take effect
                    // (the plain sprite shader ignores them).
                    r.flush_rounded_sprites(batch, current_tex, r.default_sampler);
                } else {
                    // Default widgets go through the rounded SDF pipeline so
                    // style corner radius + per-side/ring borders take effect
                    // (the plain sprite shader ignores them).
                    r.flush_rounded_sprites(batch);
                }
                batch.reset();
                has_batch = false;
            }
            current_mat  = mat;
            current_tex  = stex;
            cur_clipped  = nclipped;
            cur_cx       = ncx;
            cur_cy       = ncy;
            cur_cw       = ncw;
            cur_ch       = nch;
        }

        // Applied after the flush decision above, so the scissor command lands
        // immediately before this widget's own draw. Leaving a clipped run has
        // to RESTORE the full frame: the previous widget's scissor is still the
        // active one and would otherwise keep clipping everything after it.
        if (nclipped) {
            r.set_scissor(ncx, ncy, ncw, nch);
        } else if (was_clipped) {
            r.set_scissor(0, 0, fw, fh);
        }

        u32 color   = w.bg_color;
        bool     has_mat = mat != nullptr;
        if (has_mat) {
            // With custom material: draw at full brightness, overlay state later
            color = 0xFFFFFFFF;
        } else if (w.type == (u8)widget_type::BUTTON) {
            // EFFECTIVE enabled, like every other test in this file: a button
            // inside a disabled panel is unclickable, so it must not look
            // clickable either. The own flag answered about THIS widget only.
            if (!is_enabled(i)) {
                color = ui_darken(color, 80);
            } else if (w.state == (u8)btn_state::PRESSED) {
                color = ui_darken(color, 50);
            } else {
                color = ui_lerp_color(color, ui_lighten(color, 30), w.hover_factor);
            }
        }

        // Panel fade-in: modulate alpha by anim_t
        if (w.type == (u8)widget_type::PANEL && w.anim_t < 1.0f) {
            mm_math::color pc = mm_math::color::from_u32_argb(color); // 0xAARRGGBB
            color             = pc.with_alpha(pc.a * w.anim_t).to_u32_argb();
        }

        // Look up style for shape/radius/border
        const WidgetStyle &sty       = styles[w.style_id];
        shape_type         eff_shape = static_cast<shape_type>(w.shape);
        if (eff_shape == shape_type::DEFAULT) {
            eff_shape = sty.shape;
        }
        if (eff_shape == shape_type::CUSTOM) {
            if (has_batch) {
                if (current_mat) {
                    r.flush_sprites(batch, *current_mat);
                } else if (current_tex.handle.id != 0) {
                    r.flush_rounded_sprites(batch, current_tex, r.default_sampler);
                } else {
                    // Default widgets go through the rounded SDF pipeline so
                    // style corner radius + per-side/ring borders take effect
                    // (the plain sprite shader ignores them).
                    r.flush_rounded_sprites(batch);
                }
                batch.reset();
                has_batch   = false;
                current_mat = nullptr;
                current_tex = TextureHandle{};
                current_tex.handle.id = 0;
            }
            continue;
        }

        f32 ps = w.press_scale;
        // Base rect: the frame scaled ABOUT ITS CENTRE, except Toggle which draws
        // a half-height track. The centring is not cosmetic: anchored top-left
        // (bx = ax, by = ay) the press animation walked the top-left corner
        // across the widget, which on the ScrollView page read as "the row
        // shrank and slid", and it disagreed with the checkbox/radio box in
        // Pass 1.75, which has always scaled about its centre.
        f32 bx = ax + w.frame.w * (1.0f - ps) * 0.5f;
        f32 by = ay + w.frame.h * (1.0f - ps) * 0.5f;
        f32 bw2 = w.frame.w * ps, bh2 = w.frame.h * ps;
        if (w.type == (u8)widget_type::TOGGLE) {
            f32 track_h = w.frame.h * 0.5f;
            by            = ay + (w.frame.h - track_h) * 0.5f + w.frame.h * (1.0f - ps) * 0.5f;
            bh2           = track_h * ps;
        }
        f32 ccx = bx + bw2 * 0.5f, ccy = by + bh2 * 0.5f;

        // Hard drop shadow: one quad in the SAME batch (same rounded shader),
        // emitted BEFORE the fill so the fill covers the overlap. Skipped for
        // textured fills - a shadow quad would sample the fill's texture.
        if (sty.shadow_color != 0 && stex.handle.id == 0 && mat == nullptr) {
            const f32 sg = sty.shadow_grow;
            f32       sr = 0.0f; // shadow keeps the widget's own silhouette
            if (eff_shape == shape_type::CIRCLE || eff_shape == shape_type::ELLIPSE) {
                sr = 0.5f;
            } else if (eff_shape != shape_type::RECT) {
                sr = fminf(fmaxf(sty.corner_r, 0.0f), 0.5f);
            }
            batch.add(ccx + sty.shadow_dx, ccy + sty.shadow_dy, bw2 + sg * 2.0f, bh2 + sg * 2.0f, 0.0f, sty.shadow_color, 0, 0, sr,
                     0.0f, 0);
        }

        // Shape switch: Rect draws sharp fill + per-side geometry borders
        // (true per-side width/color, no shader work); RoundedRect keeps
        // corner_r with a uniform SDF border (uniform sides pass through,
        // mixed sides degrade to max-width + first-nonzero color);
        // Circle/Ellipse force radius 0.5 with the ring border only.
        switch (eff_shape) {
        case shape_type::RECT: {
            batch.add(ccx, ccy, bw2, bh2, 0.0f, color, 0, 0, 0.0f, 0.0f, 0);
            f32    sw[4];
            u32 sc[4];
            resolve_border_sides(sty, sw, sc);
            f32 m    = (bw2 < bh2) ? bw2 : bh2; // px per normalized unit
            f32 tT   = (sw[BORDER_T] > 0.0f && sc[BORDER_T]) ? sw[BORDER_T] * m : 0.0f;
            f32 tB   = (sw[BORDER_B] > 0.0f && sc[BORDER_B]) ? sw[BORDER_B] * m : 0.0f;
            f32 tL   = (sw[BORDER_L] > 0.0f && sc[BORDER_L]) ? sw[BORDER_L] * m : 0.0f;
            f32 tR   = (sw[BORDER_R] > 0.0f && sc[BORDER_R]) ? sw[BORDER_R] * m : 0.0f;
            f32 half = m * 0.5f;
            if (tT > half) {
                tT = half;
            }
            if (tB > half) {
                tB = half;
            }
            if (tL > half) {
                tL = half;
            }
            if (tR > half) {
                tR = half;
            }
            // Butt joints: top/bottom span full width, left/right fit between.
            if (tT > 0.0f) {
                batch.add(ccx, by + tT * 0.5f, bw2, tT, 0.0f, sc[BORDER_T], 0);
            }
            if (tB > 0.0f) {
                batch.add(ccx, by + bh2 - tB * 0.5f, bw2, tB, 0.0f, sc[BORDER_B], 0);
            }
            f32 side_h = bh2 - tT - tB;
            if (side_h > 0.0f) {
                f32 side_y = by + tT + side_h * 0.5f;
                if (tL > 0.0f) {
                    batch.add(bx + tL * 0.5f, side_y, tL, side_h, 0.0f, sc[BORDER_L], 0);
                }
                if (tR > 0.0f) {
                    batch.add(bx + bw2 - tR * 0.5f, side_y, tR, side_h, 0.0f, sc[BORDER_R], 0);
                }
            }
            break;
        }
        case shape_type::CIRCLE:
        case shape_type::ELLIPSE: {
            f32    rw;
            u32 rc;
            resolve_ring(sty, rw, rc);
            batch.add(ccx, ccy, bw2, bh2, 0.0f, color, 0, 0, 0.5f, rw, rc);
            break;
        }
        default: {
            // RoundedRect (and Default, already resolved): uniform SDF border.
            f32    sw[4];
            u32 sc[4];
            resolve_border_sides(sty, sw, sc);
            f32    uw = sw[0];
            u32 uc = sc[0];
            for (int k = 1; k < 4; ++k) {
                if (sw[k] > uw) {
                    uw = sw[k];
                }
                if (!uc && sc[k]) {
                    uc = sc[k];
                }
            }
            batch.add(ccx, ccy, bw2, bh2, 0.0f, color, 0, 0, sty.corner_r, uw, uc);
            break;
        }
        }

        has_batch = true;
    }

    if (has_batch) {
        if (current_mat) {
            r.flush_sprites(batch, *current_mat);
        } else {
            r.flush_rounded_sprites(batch); // styled shapes (see above)
        }
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

    // ── Pass 1.5: state overlays for material-backed buttons ─────
    // The clip is part of the batch identity here, exactly as in Pass 1 / 1.8 /
    // 2. This pass used to scissor per widget and flush ONCE at the end, so N
    // material buttons in N different clipped containers all took the LAST
    // rect - and a zero-area clip was handed to the backend, which is invalid
    // (see clip_draws).
    // (current_mat is not reset here: this pass always flushes through the plain
    // sprite pipeline with white_tex, and Pass 1 already nulled it.)
    has_batch = false;
    i16  o5_cx = 0, o5_cy = 0;
    u16 o5_cw = 0, o5_ch = 0;
    bool     o5_clipped = false;
    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!is_visible(i)) {
            continue;
        }
        if (!widget_material[i].pipeline.is_valid()) {
            continue;
        }
        if (w.type != (u8)widget_type::BUTTON) {
            continue;
        }

        u32 overlay = 0;
        if (!is_enabled(i)) {
            overlay = 0x50000000;
        } else if (w.state == (u8)btn_state::PRESSED) {
            overlay = 0x80000000;
        } else if (w.state == (u8)btn_state::HOVER) {
            overlay = 0x30FFFFFF;
        }
        if (overlay == 0) {
            continue;
        }

        i16  ncx, ncy;
        u16 ncw, nch;
        const bool nclipped = get_clip(i, ncx, ncy, ncw, nch);
        if (!clip_draws(nclipped, ncw, nch)) {
            continue;
        }
        if (nclipped != o5_clipped || (nclipped && (ncx != o5_cx || ncy != o5_cy || ncw != o5_cw || nch != o5_ch))) {
            if (has_batch) {
                r.flush_sprites(batch, r.white_tex, r.default_sampler);
                batch.reset();
                has_batch = false;
            }
            if (nclipped) {
                r.set_scissor(ncx, ncy, ncw, nch);
            } else if (o5_clipped) {
                r.set_scissor(0, 0, fw, fh);
            }
            o5_clipped = nclipped;
            o5_cx      = ncx;
            o5_cy      = ncy;
            o5_cw      = ncw;
            o5_ch      = nch;
        }

        f32 ax = abs_x(i);
        f32 ay = abs_y(i);
        batch.add(ax + w.frame.w * 0.5f, ay + w.frame.h * 0.5f, w.frame.w, w.frame.h, 0.0f, overlay, 0);
        has_batch = true;
    }
    if (has_batch) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

// ── Pass 1.75: checkbox/radio (see mm_ui_wtoggle.hpp) ──
    // The pass restores a full-frame scissor when it LEAVES a clipped run, but
    // the last widget can end inside one - same reason Pass 1.8 gets its own
    // reset on the next line.
    ui::togglebox::render_pass(*this, batch, r);
    r.set_scissor(0, 0, fw, fh);

    // ── Pass 1.8: listbox selection+scrollbar (see mm_ui_wlist.hpp) ──
    ui::listbox::render_pass(*this, batch, r);
    r.set_scissor(0, 0, fw, fh);

    // ── Pass 1.9: tab strips ────────────────────────────────────
    // Placed after Pass 1.8 on purpose: it owns its own flush, so every
    // earlier pass must already have drained the shared batch (the
    // drain-first rule - a reset() here would eat their pending quads).
    for (u16 i = 0; i < count; ++i) {
        if (pool[i].type == (u8)widget_type::TABBAR && is_visible(i)) {
            tabbar::render_bg(*this, i, r, batch);
        }
    }
    // The strip scissors itself, so the last one can leave a clip in force.
    r.set_scissor(0, 0, fw, fh);

    // ── Pass 2: toggles, sliders, checkmarks, cursor, callbacks ──
    f32 cursor_blink  = std::fmod(cursor_timer, 0.5f) < 0.25f ? 1.0f : 0.0f;

    // Clip state is part of the batch identity here too. Every primitive below
    // (slider track/fill/thumb, toggle thumb, checkmark, progressbar, the
    // ScrollView scrollbar, the textfield caret) queues a per-widget clip and the
    // pass flushes once at the end, so a clipped widget followed by an unclipped
    // one had the FIRST widget's clip applied to both. Same shape as the Pass 1
    // and Pass 1.8 bugs.
    bool     p2_clipped = false;
    i16  p2_cx = 0, p2_cy = 0;
    u16 p2_cw = 0, p2_ch = 0;

    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!is_visible(i)) {
            continue;
        }

        i16  cx, cy;
        u16 cw, ch;
        bool     clipped = get_clip(i, cx, cy, cw, ch);
        // An empty clip means draw NOTHING - not "no clip", and not a zero-size
        // scissor either (that is invalid at the backend; see clip_draws). The
        // checkbox of a collapsed section was the live case: its box vanished in
        // Pass 1.75 while its checkmark survived here, from the same frame.
        if (!clip_draws(clipped, cw, ch)) {
            continue;
        }
        if (clipped != p2_clipped || (clipped && (cx != p2_cx || cy != p2_cy || cw != p2_cw || ch != p2_ch))) {
            if (batch.count > 0) {
                r.flush_rounded_sprites(batch);
                batch.reset();
            }
            if (clipped) {
                r.set_scissor(cx, cy, cw, ch);
            } else if (p2_clipped) {
                r.set_scissor(0, 0, fw, fh); // leaving a clipped run
            }
            p2_clipped = clipped;
            p2_cx      = cx;
            p2_cy      = cy;
            p2_cw      = cw;
            p2_ch      = ch;
        }

        f32 ax = abs_x(i);
        f32 ay = abs_y(i);

        // ── Toggle switch thumb ──────────────────────────────────
        if (w.type == (u8)widget_type::TOGGLE) {
            f32    track_h      = w.frame.h * 0.5f;
            f32    ps           = w.press_scale;
            // Pass 1 draws the track about the frame's CENTRE while pressed (bx
            // there carries the (1-ps)/2 term), so the thumb needs the same
            // offset - otherwise it slides off the shrinking track. The TRAVEL
            // is unchanged: same left/right margins at rest, only the whole span
            // shifts inward by half the shrink.
            f32    press_dx     = w.frame.w * (1.0f - ps) * 0.5f;
            f32    track_y      = ay + (w.frame.h - track_h) * 0.5f + w.frame.h * (1.0f - ps) * 0.5f;
            f32    base_thumb   = track_h * 0.75f;
            f32    thumb_size   = base_thumb * ps;
            f32    track_margin = (track_h - base_thumb) * 0.5f;
            f32    thumb_lx     = ax + press_dx + track_margin + (w.frame.w - thumb_size - 2.0f * track_margin) * w.thumb_pos;
            f32    thumb_ly     = track_y + track_margin;
            u32 thumb_color  = (i == focus_id || i == hot) ? theme.toggle_thumb_hot : theme.toggle_thumb;
            if (!is_enabled(i)) {
                thumb_color = ui_darken(thumb_color, 80);
            }
            // Corner radius from the style, like the frame in Pass 1: a square
            // knob sliding in a pill track is the "radius does not round" look.
            const f32 thumb_r = style_corner_radius(styles[w.style_id], w.shape);
            batch.add(thumb_lx + thumb_size * 0.5f, thumb_ly + thumb_size * 0.5f, thumb_size, thumb_size, 0.0f, thumb_color, 0, 0, thumb_r);
            // Textured thumb: flush it on its own rather than splitting the
            // whole Pass-2 batch by texture (one primitive per widget, so the
            // cost is bounded). Plain sprite shader - an SDF corner radius on
            // a bitmap thumb would clip the art.
            TextureHandle ttex = styles[w.style_id].thumb_tex;
            if (ttex.handle.id != 0) {
                // Rounded pipeline, not the plain sprite shader: the SDF mask is
                // a no-op for a radius-0 quad, so the bitmap thumb is untouched
                // while the tracks already in this batch keep their corners.
                r.flush_rounded_sprites(batch, ttex, r.default_sampler);
                batch.reset();
            }
        }

        ui::slider::render_parts(*this, i, ax, ay, batch, r);

        ui::progressbar::render_parts(*this, i, ax, ay, batch, r);

        ui::togglebox::render_check(*this, i, ax, ay, batch);

        // ── ScrollView scrollbar ─────────────────────────────────
        // Drawn in Pass 2 (rounded pipeline, like every other primitive here -
        // the plain sprite shader ignores the radius). Only when there is
        // something to scroll: a bar that cannot move is noise.
        if (w.type == (u8)widget_type::SCROLLVIEW) {
            const f32 max_sc = scroll_max(i);
            if (max_sc > 0.0f && w.frame.h > 1.0f) {
                f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
                ui::content_insets(styles[w.style_id], w.frame.w, w.frame.h, il, it, ir, ib);
                f32 track_x1 = ax + w.frame.w - ir;
                f32 track_y0 = ay + it;
                f32 track_h  = w.frame.h - it - ib;
                if (track_h <= 0.0f) { // degenerate style: frame behaviour
                    track_y0 = ay;
                    track_h  = w.frame.h;
                }
                const f32 bw = K_SCROLL_BAR_W;
                const f32 th = scroll_thumb_h(track_h, max_sc);
                const f32 ty = track_y0 + (scroll_scroll[i] / max_sc) * (track_h - th);
                const f32 bcx = track_x1 - bw * 0.5f;
                batch.add(bcx, track_y0 + track_h * 0.5f, bw, track_h, 0.0f, theme.slider_track, 0);
                // Hot when the pointer is over the bar OR over the content it
                // scrolls - `hot` is usually a CHILD, so compare the scroll
                // view's ancestor chain, not the id.
                u32    thumb_col = theme.slider_thumb;
                const bool  over      = (i == hot) || (scroll_ancestor_of(hot) == i) ||
                                   (active != UINT16_MAX && scroll_ancestor_of(active) == i);
                if (over) {
                    thumb_col = theme.slider_thumb_hot;
                }
                batch.add(bcx, ty + th * 0.5f, bw, th, 0.0f, thumb_col, 0);
            }
        }

        // ── TextField cursor ─────────────────────────────────────
        if (w.type == (u8)widget_type::TEXT_FIELD && editing_id == i && cursor_blink > 0.0f) {
            ui::textfield::render_cursor(*this, i, ax, ay, r, batch);
        }

        // ── Custom draw callbacks ────────────────────────────────
        if (w.on_draw) {
            if (batch.count > 0) {
                r.flush_sprites(batch, r.white_tex, r.default_sampler);
                batch.reset();
            }
            // Plan B: render is dt-free; callbacks must not rely on dt.
            w.on_draw(i, r, batch, ax, ay, 0.0f, draw_user[i]);
        }

        // NO per-widget scissor restore here. This used to reset to the full
        // frame after every clipped widget, which silently desynchronised the
        // bookkeeping above: p2_clipped still said "clipped, rect X", so the
        // NEXT widget with the same rect was treated as "no change" and its
        // primitives went out under the full frame. A checkbox inside a section
        // drew its box clipped (Pass 1.75 re-applies it) and its checkmark
        // unclipped (Pass 2 did not) - the same widget, the same frame, two
        // different answers. The focus indicator below was the other victim: it
        // is drawn after the restore, so a focused widget in a ScrollView drew
        // its ring outside the viewport.
        //
        // Leaving a clipped run is the top-of-loop's job, where the change is
        // detected; the end of the pass restores the full frame below.

        // ── Focus indicator ─────────────────────────────────────
        if (focus_id == i && (w.flags & WF_FOCUSABLE)) {
            f32    focus_w = w.frame.w > 0 ? w.frame.w : 100.0f;
            f32    focus_h = w.frame.h > 0 ? w.frame.h : 30.0f;
            f32    pulse   = 0.6f + 0.4f * __builtin_sinf(cursor_timer * 6.0f);
            u32 fc      = (theme.focus_color & 0x00FFFFFF) | (static_cast<u32>(pulse * 255.0f) << 24);
            f32    fw4     = focus_w + 4.0f;
            batch.add(ax - 2.0f + fw4 * 0.5f, ay - 1.0f, fw4, 2.0f, 0.0f, fc, 0);
            batch.add(ax - 2.0f + fw4 * 0.5f, ay + focus_h + 1.0f, fw4, 2.0f, 0.0f, fc, 0);
            batch.add(ax - 1.0f, ay + focus_h * 0.5f, 2.0f, focus_h, 0.0f, fc, 0);
            batch.add(ax + focus_w + 1.0f, ay + focus_h * 0.5f, 2.0f, focus_h, 0.0f, fc, 0);
        }
    }

    // Pass 2 flushes through the ROUNDED SDF pipeline, not the plain sprite
    // shader: the plain shader ignores the corner radius entirely, so every
    // Pass-2 primitive (slider track + fill + thumb, progressbar track + fill +
    // stripes, toggle thumb, checkmark strokes) drew square-cornered while its
    // frame was rounded in Pass 1. A radius-0 quad is unaffected by the SDF, so
    // the primitives that DO want sharp corners keep them.
    if (batch.count > 0) {
        r.flush_rounded_sprites(batch);
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

    // ── Pass 3: text ────────────────────────────────────────────
    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!is_visible(i)) {
            continue;
        }
        // A TabBar has no text of its own: its labels come from the app's
        // tab array, so the empty-text guard must not skip it.
        // A widget with no text of its own still draws text in these cases:
        // LISTBOX / COMBOBOX / TABBAR get theirs from an app-supplied array,
        // and SLIDER / PROGRESSBAR draw a VALUE label ("42%") beside the bar.
        // They have to be listed here or the guard skips them and the value
        // never appears at all.
        if (text_of(i)[0] == '\0' && w.type != (u8)widget_type::LISTBOX && w.type != (u8)widget_type::COMBOBOX &&
            w.type != (u8)widget_type::TABBAR && w.type != (u8)widget_type::SLIDER &&
            w.type != (u8)widget_type::PROGRESSBAR) {
            continue;
        }

        f32    ax    = abs_x(i);
        f32    ay    = abs_y(i);

        f32    pad_l = static_cast<f32>(w.pad[3]);
        f32    pad_r = static_cast<f32>(w.pad[1]);
        f32    pad_t = static_cast<f32>(w.pad[0]);
        f32    pad_b = static_cast<f32>(w.pad[2]);

        f32    a     = static_cast<f32>(content_ascent[i]);
        f32    hc    = static_cast<f32>(content_h[i]);

        i16  cx, cy;
        u16 cw, ch;
        bool     clipped = get_clip(i, cx, cy, cw, ch);
        // An empty clip means draw NOTHING - the last pass that was missing this.
        // A collapsed accordion (or anything else a container intersects down to
        // zero) reports "clipped, h == 0", which is NOT "no clip": passing the
        // zero rect through records an invalid scissor in the graph, and every
        // text pass from here on inherits whatever the backend does with it.
        // Every other pass that scissors per widget (1, 1.75, 1.9, 2) already
        // asks clip_draws() first; this one had the comment and not the check.
        if (!clip_draws(clipped, cw, ch)) {
            continue;
        }
        if (clipped) {
            r.set_scissor(cx, cy, cw, ch, ui::text_key(r));
        }

        f32 txt_x = 0, txt_y = 0;
        f32 txt_sc = 1.0f;

        if (w.type == (u8)widget_type::BUTTON) {
            ui::button::render_text(*this, i, ax, ay, pad_l, pad_r, pad_t, pad_b, a, hc, r);
        } else if (w.type == (u8)widget_type::TOGGLE) {
            // Label goes right of the track (same pattern as the slider % label).
            // (Was missing: txt_x stayed 0.0 and every toggle label piled at the window edge.)
            txt_x  = ax + w.frame.w + pad_r + 8.0f;
            txt_y  = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        } else if (w.type == (u8)widget_type::CHECKBOX || w.type == (u8)widget_type::RADIOBOX) {
            txt_x  = ax + pad_l + w.frame.w + 6.0f;
            txt_y  = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            // Multi-line: the ink box (content_h) already includes one
            // line_height per extra line, so centring that box and stacking
            // lines from its top handles 1..N lines with the same maths.
            draw_text_lines(r, text_of(i), txt_x, txt_y - a, txt_sc, w.text_color, text_align_of(*this, i),
                             text_box_w(*this, i, static_cast<f32>(content_w[i])), text_box_limit(*this, i));
        } else if (w.type == (u8)widget_type::TEXT_FIELD) {
            txt_x  = ax + pad_l + 8.0f;
            txt_y  = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            // A text field is a fixed-width box and its value can be longer, so
            // it clips its OWN text - it had no scissor at all and the string
            // drew straight over whatever sat next to it. No ellipsis: the
            // caret tracks the real advance, so cutting the string would put
            // "..." and a caret that disagree about where the text ends.
            //
            // set_scissor REPLACES, and an ancestor clip (WF_CLIP panel) is
            // already active, so intersect the two by hand rather than
            // widening.
            const f32 fl = ax + static_cast<f32>(pad_l);
            const f32 ft = ay + static_cast<f32>(pad_t);
            const f32 fr = ax + w.frame.w - static_cast<f32>(pad_r);
            const f32 fb = ay + w.frame.h - static_cast<f32>(pad_b);
            const f32 cl0 = clipped ? cx : 0.0f;
            const f32 ct0 = clipped ? cy : 0.0f;
            const f32 cr0 = clipped ? (cx + cw) : static_cast<f32>(fw);
            const f32 cb0 = clipped ? (cy + ch) : static_cast<f32>(fh);
            const f32 il  = fmaxf(fl, cl0);
            const f32 it  = fmaxf(ft, ct0);
            const f32 ir  = fminf(fr, cr0);
            const f32 ib  = fminf(fb, cb0);
            const bool  ok  = ir > il && ib > it;
            if (ok) {
                r.set_scissor(il, it, ir - il, ib - it, ui::text_key(r));
            }
            r.draw_text(r.default_font, text_of(i), txt_x, txt_y, w.text_color, txt_sc);
            if (ok) {
                // Back to whatever the ancestor clip (or the full frame) was.
                r.set_scissor(clipped ? cx : 0.0f, clipped ? cy : 0.0f, clipped ? cw : static_cast<f32>(fw),
                              clipped ? ch : static_cast<f32>(fh), ui::text_key(r));
            }
        } else if (w.type == (u8)widget_type::SLIDER) {
            ui::slider::render_text(*this, i, ax, ay, pad_r, pad_t, pad_b, a, hc, r);
        } else if (w.type == (u8)widget_type::PROGRESSBAR) {
            ui::progressbar::render_text(*this, i, ax, ay, pad_r, pad_t, pad_b, a, hc, r);
        } else if (w.type == (u8)widget_type::LISTBOX) {
            // Header titles first, so a title is never overdrawn by a row that
            // scrolled under it (both land at the text key; submission order
            // decides).
            grid_header_text(*this, i, r, ax, ay);
            ui::listbox::render_rows(*this, i, ax, ay, batch, r, clipped, cx, cy, cw, ch, fw, fh);
        } else if (w.type == (u8)widget_type::COMBOBOX) {
            // Closed field: current label (or filter text) + chevron.
            // Row-sized text (same scale as the popup rows), vertically
            // centered with button-identical math at the draw scale.
            const ListboxMetrics &lm  = combobox_metrics[i];
            f32                 csc = (lm.row_h > 0.0f) ? lm.text_scale : 0.45f;
            f32                 sa  = a * csc; // content measured at wg.scale; rescale
            f32                 sh  = hc * csc;
            txt_x                     = ax + pad_l + 8.0f;
            txt_y                     = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * sa - sh) * 0.5f;
            txt_sc                    = csc;
            const char *label         = (w.text[0] != '\0') ? w.text : "--";
            u32    tc            = (w.text[0] != '\0') ? w.text_color : theme.text_secondary;
            // Long labels must not run into the chevron: clip the label to
            // the field minus the chevron zone, then draw the chevron.
            f32       lbl_w         = w.frame.w - 24.0f; // chevron lives in the last ~22px
            if (lbl_w < 8.0f) {
                lbl_w = 8.0f;
            }
            r.set_scissor(static_cast<i16>(ax), static_cast<i16>(ay), static_cast<u16>(lbl_w), static_cast<u16>(w.frame.h), ui::text_key(r));
            r.draw_text(r.default_font, label, txt_x, txt_y, tc, txt_sc);
            if (clipped) {
                r.set_scissor(cx, cy, cw, ch, ui::text_key(r));
            } else {
                r.set_scissor(0, 0, fw, fh, ui::text_key(r));
            }
            const char *chev = combobox_open[i] ? "^" : "v";
            r.draw_text(r.default_font, chev, ax + w.frame.w - pad_r - 16.0f, txt_y, tc, txt_sc);
        } else if (w.type == (u8)widget_type::TABBAR) {
            // Cell labels are positioned from the cell grid, so they cannot go
            // through the shared one-string chain.
            tabbar::render_text(*this, i, r);
        } else {
            // Label (Panel/Image carry no text): top-align the GLYPH top at
            // ay + pad_t. draw_text places glyphs by signed bearings (top
            // lands ~ascent above the origin), so the origin needs the
            // +ascent term — without it labels stick out above their y
            // (clipped under WF_CLIP parents, misaligned in grids).
            // Label / Panel: top-align the ink box, honouring text_of().
            // Multi-line: content_h already spans every line, so stacking from
            // its top handles 1..N with the same maths as one line.
            const char *ltxt = text_of(i);
            txt_x            = ax + pad_l;
            txt_y            = ay + pad_t;
            // Same scale measure() used, so the box and the glyphs agree.
            txt_sc           = widget_text_scale(*this, i);
            draw_text_lines(r, ltxt, txt_x, txt_y, txt_sc, w.text_color, text_align_of(*this, i), text_box_w(*this, i, static_cast<f32>(content_w[i])),
                             text_box_limit(*this, i));
        }
        if (clipped) {
            r.set_scissor(0, 0, fw, fh, ui::text_key(r));
        }
    }

    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }
    batch.reset();

    // ── Pass 3.5: modal backdrop ─────────────────────────────────
    // Four quads AROUND the modal root, flushed at the TEXT key - the whole
    // frame is one pass, so a key-0 quad could never dim key-1 text (scene
    // labels would punch straight through it). Carving the dialog's own rect
    // out of the dim also means the dialog background - drawn back in Pass 1 -
    // is never covered, so no pass reordering is needed.
    if (modal_id != UINT16_MAX && theme.modal_backdrop != 0) {
        const f32 mx = abs_x(modal_id);
        const f32 my = abs_y(modal_id);
        const f32 mw = pool[modal_id].frame.w;
        const f32 mh = pool[modal_id].frame.h;
        const f32 W  = static_cast<f32>(fw);
        const f32 H  = static_cast<f32>(fh);
        const u32 dc = theme.modal_backdrop;
        if (my > 0.0f) { // above
            batch.add(W * 0.5f, my * 0.5f, W, my, 0.0f, dc, 0);
        }
        if (my + mh < H) { // below
            const f32 bh = H - (my + mh);
            batch.add(W * 0.5f, my + mh + bh * 0.5f, W, bh, 0.0f, dc, 0);
        }
        if (mx > 0.0f) { // left
            batch.add(mx * 0.5f, my + mh * 0.5f, mx, mh, 0.0f, dc, 0);
        }
        if (mx + mw < W) { // right
            const f32 rw2 = W - (mx + mw);
            batch.add(mx + mw + rw2 * 0.5f, my + mh * 0.5f, rw2, mh, 0.0f, dc, 0);
        }
        r.flush_sprites(batch, r.white_tex, r.default_sampler, ui::text_key(r));
        batch.reset();
    }

    // ── Pass 3.6: tooltip (overlay, not a widget) ──────────────────
    // Drawn here because it needs the font + real geometry; the TIMING lives in
    // update(). Above the host by 8px, flipped below when it would not fit.
    if (tooltip_for != UINT16_MAX && tooltip_str[tooltip_for] != nullptr && tooltip_t >= tooltip_delay[tooltip_for]) {
        const char *str   = tooltip_str[tooltip_for];
        const f32 sc    = 0.34f;
        const f32 pad   = 6.0f;
        const f32 lh    = r.default_font.line_height * sc;
        f32       tw    = 0.0f;
        u32    nl    = 1;
        for (const char *q = str; *q != '\0'; ++q) {
            if (*q == '\n') {
                ++nl;
                continue;
            }
            const auto *g = r.default_font.get_glyph(static_cast<u32>(static_cast<u8>(*q)));
            if (g) {
                tw += static_cast<f32>(g->advance) * sc;
            }
        }
        const f32 th  = lh * static_cast<f32>(nl);
        const f32 mx  = abs_x(tooltip_for) + pool[tooltip_for].frame.w * 0.5f;
        const f32 my  = abs_y(tooltip_for);
        const f32 bw  = tw + pad * 2.0f;
        const f32 bh  = th + pad * 2.0f;
        f32       px2 = mx - bw * 0.5f;
        f32       py2 = my - bh - 8.0f;
        if (py2 < 0.0f) {
            py2 = my + pool[tooltip_for].frame.h + 8.0f; // flip below
        }
        px2 = fmaxf(0.0f, fminf(px2, static_cast<f32>(fw) - bw));
        py2 = fmaxf(0.0f, fminf(py2, static_cast<f32>(fh) - bh));
        batch.add(px2 + bw * 0.5f, py2 + bh * 0.5f, bw, bh, 0.0f, 0xF01D1D1D, 0, 0, 0.14f, 0.0f, 0);
        r.flush_rounded_sprites(batch);
        batch.reset();
        // A tooltip sizes its own panel to the text, so it never truncates.
        draw_text_lines(r, str, px2 + pad, py2 + pad, sc, 0xFFFFFFFF, text_align::LEFT, tw, 0.0f);
    }

    // ── Pass 4: ComboBox overlay popups (see mm_ui_wlist.hpp) ──
    ui::combobox::render_overlay(*this, batch, r, fw, fh);
}

// ─── Theme::load — JSON config via simdjson ─────────────────────
Theme Theme::load(const char *path) noexcept {
    Theme   t    = Theme::dark();

    VfsBlob blob = g_vfs.read_bundle(path);
    if (!blob.valid()) {
        return t;
    }

    // simdjson::padded_string expects padding, VFS might not provide it.
    // However, simdjson can parse from a buffer.
    simdjson::dom::parser  parser;
    simdjson::dom::element doc;
    if (parser.parse(static_cast<const u8 *>(blob.data), blob.size).get(doc) != simdjson::SUCCESS) {
        blob.free();
        return t;
    }
    blob.free();

    simdjson::dom::object obj;
    if (doc.get_object().get(obj) != simdjson::SUCCESS) {
        return t;
    }

    auto set = [&](const char *key, u32 &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        if (val.is_string()) {
            std::string_view sv;
            if (val.get_string().get(sv) == simdjson::SUCCESS) {
                field = static_cast<u32>(std::strtoul(sv.data(), nullptr, 16));
            }
        } else if (val.is_uint64()) {
            u64 v;
            if (val.get_uint64().get(v) == simdjson::SUCCESS) {
                field = static_cast<u32>(v);
            }
        } else if (val.is_int64()) {
            i64 v;
            if (val.get_int64().get(v) == simdjson::SUCCESS) {
                field = static_cast<u32>(v);
            }
        }
    };

    // Parse padding array [top, right, bottom, left]
    auto set_pad = [&](const char *key, i8 pad[4]) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        simdjson::dom::array arr;
        if (val.get_array().get(arr) != simdjson::SUCCESS) {
            return;
        }
        size_t i = 0;
        for (auto elem : arr) {
            if (i >= 4) {
                break;
            }
            i64 v;
            if (elem.get_int64().get(v) == simdjson::SUCCESS) {
                pad[i] = static_cast<i8>(v);
            }
            ++i;
        }
    };

    auto set_u8 = [&](const char *key, u8 &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        u64 v;
        if (val.get_uint64().get(v) == simdjson::SUCCESS) {
            field = static_cast<u8>(v);
        }
    };

    auto set_float = [&](const char *key, f32 &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        f64 d;
        if (val.get_double().get(d) == simdjson::SUCCESS) {
            field = static_cast<f32>(d);
        } else if (val.is_int64()) {
            i64 i;
            if (val.get_int64().get(i) == simdjson::SUCCESS) {
                field = static_cast<f32>(i);
            }
        } else if (val.is_uint64()) {
            u64 i;
            if (val.get_uint64().get(i) == simdjson::SUCCESS) {
                field = static_cast<f32>(i);
            }
        }
    };

    auto set_str = [&](const char *key, char *buf, size_t bufsz) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        std::string_view sv;
        if (val.get_string().get(sv) == simdjson::SUCCESS) {
            size_t n = sv.size();
            if (n >= bufsz) {
                n = bufsz - 1;
            }
            std::memcpy(buf, sv.data(), n);
            buf[n] = '\0';
        }
    };

    // ── Colors (format: 0xAARRGGBB) ──
    // JSON example: "FF3A3A3A" = alpha=0xFF, red=0x3A, green=0x3A, blue=0x3A
    set("panel_bg", t.panel_bg);
    set("button_bg", t.button_bg);
    set("button_text", t.button_text);
    set("toggle_track_on", t.toggle_track_on);
    set("toggle_track_off", t.toggle_track_off);
    set("toggle_thumb", t.toggle_thumb);
    set("toggle_thumb_hot", t.toggle_thumb_hot);
    set("toggle_text", t.toggle_text);
    set("slider_bg", t.slider_bg);
    set("slider_track", t.slider_track);
    set("slider_fill", t.slider_fill);
    set("progressbar_bg", t.progressbar_bg);
    set("progressbar_track", t.progressbar_track);
    set("progressbar_fill", t.progressbar_fill);
    set("slider_thumb", t.slider_thumb);
    set("slider_thumb_hot", t.slider_thumb_hot);
    set("slider_text", t.slider_text);
    set("checkbox_on", t.checkbox_on);
    set("checkbox_off", t.checkbox_off);
    set("checkbox_border", t.checkbox_border);
    set("checkbox_check", t.checkbox_check);
    set("checkbox_text", t.checkbox_text);
    set("textfield_bg", t.textfield_bg);
    set("textfield_text", t.textfield_text);
    set("cursor", t.cursor);
    set("focus_color", t.focus_color);
    set("text_primary", t.text_primary);
    set("text_secondary", t.text_secondary);

    // ── Padding ─────────────────────────────────────────────────
    set_pad("panel_pad", t.panel_pad);
    set_pad("label_pad", t.label_pad);
    set_pad("button_pad", t.button_pad);
    set_pad("toggle_pad", t.toggle_pad);
    set_pad("slider_pad", t.slider_pad);
    set_pad("checkbox_pad", t.checkbox_pad);
    set_pad("textfield_pad", t.textfield_pad);

    // ── Layout ──────────────────────────────────────────────────
    set_u8("layout_padding", t.layout_padding);
    set_u8("layout_spacing", t.layout_spacing);

    // ── Font ────────────────────────────────────────────────────
    set_str("font_path", t.font_path, sizeof(t.font_path));
    set_float("font_scale", t.font_scale);

    return t;
}

} // namespace ui
