#include <gui/gui_i.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <gui/canvas.h>
#include <furi.h>
#include <input/input.h>
#include <dolphin/dolphin.h>

#include "desktop_view_main.h"

struct DesktopMainView {
    View* view;
    DesktopMainViewCallback callback;
    void* context;
    FuriTimer* poweroff_timer;
    bool dummy_mode;
};

typedef struct {
    bool dummy_mode;
} DesktopMainViewModel;

#define DESKTOP_MAIN_VIEW_POWEROFF_TIMEOUT 5000

static void desktop_main_draw_signal(Canvas* canvas) {
    const uint8_t signal[] = {59, 59, 56, 62, 54, 58, 56, 60, 59};

    for(size_t i = 1; i < COUNT_OF(signal); i++) {
        canvas_draw_line(canvas, 19 + ((i - 1) * 13), signal[i - 1], 19 + (i * 13), signal[i]);
    }
}

static void desktop_main_draw_callback(Canvas* canvas, void* _model) {
    furi_assert(canvas);
    furi_assert(_model);
    DesktopMainViewModel* model = _model;

    /* Keep the generated PoisonedOS animation visible beneath this chrome. */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 14, 13, 65, 27);
    canvas_set_color(canvas, ColorBlack);

    /* PoisonedOS assay rail: a stable visual anchor across operational surfaces. */
    canvas_draw_box(canvas, 0, 13, 13, 51);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 3, 17, 7, 3);
    canvas_draw_box(canvas, 3, 24, 7, 1);
    canvas_draw_box(canvas, 3, 29, 7, 7);
    canvas_draw_box(canvas, 3, 40, 7, 1);
    canvas_draw_box(canvas, 3, 45, 7, 3);
    canvas_draw_box(canvas, 3, 52, 7, 8);

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 18, 25, "POISONED");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 19, 36, model->dummy_mode ? "COVER MODE" : "FIELD READY");

    canvas_draw_line(canvas, 18, 40, 123, 40);
    canvas_draw_str(canvas, 19, 51, "< FAV");
    canvas_draw_str_aligned(canvas, 123, 51, AlignRight, AlignBottom, "APPS OK");
    desktop_main_draw_signal(canvas);
}

static void desktop_main_poweroff_timer_callback(void* context) {
    DesktopMainView* main_view = context;
    main_view->callback(DesktopMainEventOpenPowerOff, main_view->context);
}

void desktop_main_set_callback(
    DesktopMainView* main_view,
    DesktopMainViewCallback callback,
    void* context) {
    furi_assert(main_view);
    furi_assert(callback);
    main_view->callback = callback;
    main_view->context = context;
}

View* desktop_main_get_view(DesktopMainView* main_view) {
    furi_assert(main_view);
    return main_view->view;
}

void desktop_main_set_dummy_mode_state(DesktopMainView* main_view, bool dummy_mode) {
    furi_assert(main_view);
    main_view->dummy_mode = dummy_mode;
    with_view_model(
        main_view->view, DesktopMainViewModel * model, { model->dummy_mode = dummy_mode; }, true);
}

bool desktop_main_input_callback(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    DesktopMainView* main_view = context;

    if(main_view->dummy_mode == false) {
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyOk) {
                main_view->callback(DesktopMainEventOpenMenu, main_view->context);
            } else if(event->key == InputKeyUp) {
                main_view->callback(DesktopMainEventOpenLockMenu, main_view->context);
            } else if(event->key == InputKeyDown) {
                main_view->callback(DesktopMainEventOpenArchive, main_view->context);
            } else if(event->key == InputKeyLeft) {
                main_view->callback(DesktopMainEventOpenFavoriteLeftShort, main_view->context);
            } else if(event->key == InputKeyRight) {
                main_view->callback(DesktopMainEventOpenFavoriteRightShort, main_view->context);
            }
        } else if(event->type == InputTypeLong) {
            if(event->key == InputKeyUp) {
                main_view->callback(DesktopMainEventLock, main_view->context);
            } else if(event->key == InputKeyDown) {
                main_view->callback(DesktopMainEventOpenDebug, main_view->context);
            } else if(event->key == InputKeyLeft) {
                main_view->callback(DesktopMainEventOpenFavoriteLeftLong, main_view->context);
            } else if(event->key == InputKeyRight) {
                main_view->callback(DesktopMainEventOpenFavoriteRightLong, main_view->context);
            }
        }
    } else {
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyOk) {
                main_view->callback(DesktopDummyEventOpenOk, main_view->context);
            } else if(event->key == InputKeyUp) {
                main_view->callback(DesktopMainEventOpenLockMenu, main_view->context);
            } else if(event->key == InputKeyDown) {
                main_view->callback(DesktopDummyEventOpenDown, main_view->context);
            } else if(event->key == InputKeyLeft) {
                main_view->callback(DesktopDummyEventOpenLeft, main_view->context);
            } else if(event->key == InputKeyRight) {
                main_view->callback(DesktopDummyEventOpenRight, main_view->context);
            }
        }
    }

    if(event->key == InputKeyBack) {
        if(event->type == InputTypePress) {
            furi_timer_start(main_view->poweroff_timer, DESKTOP_MAIN_VIEW_POWEROFF_TIMEOUT);
        } else if(event->type == InputTypeRelease) {
            furi_timer_stop(main_view->poweroff_timer);
        }
    }

    return true;
}

DesktopMainView* desktop_main_alloc(void) {
    DesktopMainView* main_view = malloc(sizeof(DesktopMainView));

    main_view->callback = NULL;
    main_view->context = NULL;
    main_view->dummy_mode = false;
    main_view->view = view_alloc();
    view_set_context(main_view->view, main_view);
    view_allocate_model(main_view->view, ViewModelTypeLocking, sizeof(DesktopMainViewModel));
    with_view_model(
        main_view->view, DesktopMainViewModel * model, { model->dummy_mode = false; }, false);
    view_set_draw_callback(main_view->view, desktop_main_draw_callback);
    view_set_input_callback(main_view->view, desktop_main_input_callback);

    main_view->poweroff_timer =
        furi_timer_alloc(desktop_main_poweroff_timer_callback, FuriTimerTypeOnce, main_view);

    return main_view;
}

void desktop_main_free(DesktopMainView* main_view) {
    furi_assert(main_view);
    view_free(main_view->view);
    furi_timer_free(main_view->poweroff_timer);
    free(main_view);
}
