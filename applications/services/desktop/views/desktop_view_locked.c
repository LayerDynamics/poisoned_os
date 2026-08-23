#include <furi.h>

#include <gui/elements.h>
#include <gui/icon.h>
#include <gui/view.h>

#include <assets_icons.h>

#include "../desktop_i.h"
#include "desktop_view_locked.h"

#define DOOR_MOVING_INTERVAL_MS  (1000 / 16)
#define LOCKED_HINT_TIMEOUT_MS   (1000)
#define UNLOCKED_HINT_TIMEOUT_MS (2000)

#define DOOR_OFFSET_START (-55)
#define DOOR_OFFSET_END   (0)

#define DOOR_L_FINAL_POS (0)
#define DOOR_R_FINAL_POS (60)

#define UNLOCK_CNT         (3)
#define UNLOCK_RST_TIMEOUT (600)

struct DesktopViewLocked {
    View* view;
    DesktopViewLockedCallback callback;
    void* context;

    FuriTimer* timer;
    uint8_t lock_count;
    uint32_t lock_lastpress;
};

typedef enum {
    DesktopViewLockedStateUnlocked,
    DesktopViewLockedStateLocked,
    DesktopViewLockedStateDoorsClosing,
    DesktopViewLockedStateLockedHintShown,
    DesktopViewLockedStateUnlockedHintShown
} DesktopViewLockedState;

typedef struct {
    bool pin_locked;
    int8_t door_offset;
    DesktopViewLockedState view_state;
} DesktopViewLockedModel;

void desktop_view_locked_set_callback(
    DesktopViewLocked* locked_view,
    DesktopViewLockedCallback callback,
    void* context) {
    furi_assert(locked_view);
    furi_assert(callback);
    locked_view->callback = callback;
    locked_view->context = context;
}

static void locked_view_timer_callback(void* context) {
    DesktopViewLocked* locked_view = context;
    locked_view->callback(DesktopLockedEventUpdate, locked_view->context);
}

static void desktop_view_locked_doors_draw(Canvas* canvas, DesktopViewLockedModel* model) {
    int32_t offset = model->door_offset;
    int32_t door_left_x = DOOR_L_FINAL_POS + offset;
    int32_t door_right_x = DOOR_R_FINAL_POS - offset;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, door_left_x, STATUS_BAR_Y_SHIFT, 68, 64 - STATUS_BAR_Y_SHIFT);
    canvas_draw_box(canvas, door_right_x, STATUS_BAR_Y_SHIFT, 68, 64 - STATUS_BAR_Y_SHIFT);
    canvas_set_color(canvas, ColorWhite);
    for(uint8_t y = STATUS_BAR_Y_SHIFT + 5; y < 62; y += 9) {
        canvas_draw_line(canvas, door_left_x + 4, y, door_left_x + 62, y);
        canvas_draw_line(canvas, door_right_x + 4, y, door_right_x + 62, y);
    }
}

static void desktop_view_locked_draw_secure(
    Canvas* canvas,
    const DesktopViewLockedModel* model,
    bool show_release_hint) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, STATUS_BAR_Y_SHIFT, 15, 64 - STATUS_BAR_Y_SHIFT);
    canvas_draw_line(canvas, 19, 34, 123, 34);
    canvas_draw_icon(canvas, 4, STATUS_BAR_Y_SHIFT + 5, &I_Lock_7x8);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 20, 23, "POISONED / SECURE");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 20, 32, "SECURE");
    canvas_set_font(canvas, FontSecondary);
    if(show_release_hint) {
        canvas_draw_str(canvas, 20, 47, "3x BACK TO RELEASE");
    } else {
        canvas_draw_str(canvas, 20, 47, model->pin_locked ? "PIN REQUIRED" : "INPUT LOCKED");
    }
    canvas_draw_line(canvas, 20, 54, 123, 54);
    canvas_draw_str(canvas, 20, 63, "LOCAL CONTROL HELD");
}

static bool desktop_view_locked_doors_move(DesktopViewLockedModel* model) {
    bool stop = false;
    if(model->door_offset < DOOR_OFFSET_END) {
        model->door_offset = CLAMP(model->door_offset + 5, DOOR_OFFSET_END, DOOR_OFFSET_START);
        stop = true;
    }

    return stop;
}

static void desktop_view_locked_update_hint_icon_timeout(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    const bool change_state = (model->view_state == DesktopViewLockedStateLocked) &&
                              !model->pin_locked;
    if(change_state) {
        model->view_state = DesktopViewLockedStateLockedHintShown;
    }
    view_commit_model(locked_view->view, change_state);
    furi_timer_start(locked_view->timer, LOCKED_HINT_TIMEOUT_MS);
}

void desktop_view_locked_update(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    DesktopViewLockedState view_state = model->view_state;

    if(view_state == DesktopViewLockedStateDoorsClosing &&
       !desktop_view_locked_doors_move(model)) {
        locked_view->callback(DesktopLockedEventDoorsClosed, locked_view->context);
        model->view_state = DesktopViewLockedStateLocked;
    } else if(view_state == DesktopViewLockedStateLockedHintShown) {
        model->view_state = DesktopViewLockedStateLocked;
    } else if(view_state == DesktopViewLockedStateUnlockedHintShown) {
        model->view_state = DesktopViewLockedStateUnlocked;
    }

    view_commit_model(locked_view->view, true);

    if(view_state != DesktopViewLockedStateDoorsClosing) {
        furi_timer_stop(locked_view->timer);
    }
}

static void desktop_view_locked_draw(Canvas* canvas, void* model) {
    DesktopViewLockedModel* m = model;
    DesktopViewLockedState view_state = m->view_state;
    canvas_set_color(canvas, ColorBlack);

    if(view_state == DesktopViewLockedStateDoorsClosing) {
        canvas_clear(canvas);
        desktop_view_locked_doors_draw(canvas, m);
    } else if(view_state == DesktopViewLockedStateLocked) {
        desktop_view_locked_draw_secure(canvas, m, false);
    } else if(view_state == DesktopViewLockedStateLockedHintShown) {
        desktop_view_locked_draw_secure(canvas, m, true);
    } else if(view_state == DesktopViewLockedStateUnlockedHintShown) {
        canvas_clear(canvas);
        canvas_draw_box(canvas, 0, STATUS_BAR_Y_SHIFT, 128, 64 - STATUS_BAR_Y_SHIFT);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignBottom, "RELEASED");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 49, AlignCenter, AlignBottom, "LOCAL CONTROL RESTORED");
    }
}

View* desktop_view_locked_get_view(DesktopViewLocked* locked_view) {
    furi_assert(locked_view);
    return locked_view->view;
}

static bool desktop_view_locked_input(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    bool is_changed = false;
    const uint32_t press_time = furi_get_tick();
    DesktopViewLocked* locked_view = context;
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    if(model->view_state == DesktopViewLockedStateUnlockedHintShown &&
       event->type == InputTypePress) {
        model->view_state = DesktopViewLockedStateUnlocked;
        is_changed = true;
    }
    const DesktopViewLockedState view_state = model->view_state;
    const bool pin_locked = model->pin_locked;
    view_commit_model(locked_view->view, is_changed);

    if(view_state == DesktopViewLockedStateUnlocked) {
        return false;
    } else if(view_state == DesktopViewLockedStateLocked && pin_locked) {
        locked_view->callback(DesktopLockedEventShowPinInput, locked_view->context);
    } else if(
        view_state == DesktopViewLockedStateLocked ||
        view_state == DesktopViewLockedStateLockedHintShown) {
        if(press_time - locked_view->lock_lastpress > UNLOCK_RST_TIMEOUT) {
            locked_view->lock_lastpress = press_time;
            locked_view->lock_count = 0;
        }

        desktop_view_locked_update_hint_icon_timeout(locked_view);

        if(event->key == InputKeyBack) {
            if(event->type == InputTypeShort) {
                locked_view->lock_lastpress = press_time;
                locked_view->lock_count++;
                if(locked_view->lock_count == UNLOCK_CNT) {
                    locked_view->callback(DesktopLockedEventUnlocked, locked_view->context);
                }
            }
        } else {
            locked_view->lock_count = 0;
        }

        locked_view->lock_lastpress = press_time;
    }

    return true;
}

DesktopViewLocked* desktop_view_locked_alloc(void) {
    DesktopViewLocked* locked_view = malloc(sizeof(DesktopViewLocked));
    locked_view->view = view_alloc();
    locked_view->timer =
        furi_timer_alloc(locked_view_timer_callback, FuriTimerTypePeriodic, locked_view);

    view_allocate_model(locked_view->view, ViewModelTypeLocking, sizeof(DesktopViewLockedModel));
    view_set_context(locked_view->view, locked_view);
    view_set_draw_callback(locked_view->view, desktop_view_locked_draw);
    view_set_input_callback(locked_view->view, desktop_view_locked_input);

    return locked_view;
}

void desktop_view_locked_free(DesktopViewLocked* locked_view) {
    furi_assert(locked_view);
    furi_timer_free(locked_view->timer);
    view_free(locked_view->view);
    free(locked_view);
}

void desktop_view_locked_close_doors(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    furi_assert(model->view_state == DesktopViewLockedStateLocked);
    model->view_state = DesktopViewLockedStateDoorsClosing;
    model->door_offset = DOOR_OFFSET_START;
    view_commit_model(locked_view->view, true);
    furi_timer_start(locked_view->timer, DOOR_MOVING_INTERVAL_MS);
}

void desktop_view_locked_lock(DesktopViewLocked* locked_view, bool pin_locked) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    furi_assert(model->view_state == DesktopViewLockedStateUnlocked);
    model->view_state = DesktopViewLockedStateLocked;
    model->pin_locked = pin_locked;
    view_commit_model(locked_view->view, true);
}

void desktop_view_locked_unlock(DesktopViewLocked* locked_view) {
    locked_view->lock_count = 0;
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    model->view_state = DesktopViewLockedStateUnlockedHintShown;
    model->pin_locked = false;
    view_commit_model(locked_view->view, true);
    furi_timer_start(locked_view->timer, UNLOCKED_HINT_TIMEOUT_MS);
}

bool desktop_view_locked_is_locked_hint_visible(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    const DesktopViewLockedState view_state = model->view_state;
    view_commit_model(locked_view->view, false);
    return view_state == DesktopViewLockedStateLockedHintShown;
}
