#include "power_unplug_usb.h"
#include <furi.h>

struct PowerUnplugUsb {
    View* view;
};

static void power_unplug_usb_draw_callback(Canvas* canvas, void* _model) {
    UNUSED(_model);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 64);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 15, 64);
    canvas_set_color(canvas, ColorBlack);
    for(uint8_t y = 4; y < 61; y += 9) {
        canvas_draw_box(canvas, 4, y, 7, 3);
    }
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 21, 27, "POWER ISOLATED");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 21, 41, "SAFE TO DISCONNECT USB");
    canvas_draw_line(canvas, 21, 47, 123, 47);
    canvas_draw_str(canvas, 21, 58, "POISONEDOS / LOCAL");
}

PowerUnplugUsb* power_unplug_usb_alloc(void) {
    PowerUnplugUsb* power_unplug_usb = malloc(sizeof(PowerUnplugUsb));

    power_unplug_usb->view = view_alloc();
    view_set_context(power_unplug_usb->view, power_unplug_usb);
    view_set_draw_callback(power_unplug_usb->view, power_unplug_usb_draw_callback);
    view_set_input_callback(power_unplug_usb->view, NULL);

    return power_unplug_usb;
}

void power_unplug_usb_free(PowerUnplugUsb* power_unplug_usb) {
    furi_assert(power_unplug_usb);
    view_free(power_unplug_usb->view);
    free(power_unplug_usb);
}

View* power_unplug_usb_get_view(PowerUnplugUsb* power_unplug_usb) {
    furi_assert(power_unplug_usb);
    return power_unplug_usb->view;
}
