#include "menu.h"

#include <gui/elements.h>
#include <assets_icons.h>
#include <furi.h>
#include <m-array.h>

struct Menu {
    View* view;
};

typedef struct {
    const char* label;
    IconAnimation* icon;
    uint32_t index;
    MenuItemCallback callback;
    void* callback_context;
} MenuItem;

ARRAY_DEF(MenuItemArray, MenuItem, M_POD_OPLIST); //-V658

#define M_OPL_MenuItemArray_t() ARRAY_OPLIST(MenuItemArray, M_POD_OPLIST)

typedef struct {
    MenuItemArray_t items;
    FuriString* header;
    size_t position;
} MenuModel;

static void menu_process_up(Menu* menu);
static void menu_process_down(Menu* menu);
static void menu_process_ok(Menu* menu);

static void menu_draw_fitted_label(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    Align horizontal,
    const char* label) {
    FuriString* text = furi_string_alloc_set(label);
    elements_string_fit_width(canvas, text, width);
    canvas_draw_str_aligned(canvas, x, y, horizontal, AlignBottom, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void menu_draw_callback(Canvas* canvas, void* _model) {
    MenuModel* model = _model;

    canvas_clear(canvas);

    size_t position = model->position;
    size_t items_count = MenuItemArray_size(model->items);
    if(items_count) {
        const uint8_t header_height = furi_string_empty(model->header) ? 0 : 13;
        if(header_height) {
            canvas_draw_box(canvas, 0, 0, 128, header_height);
            canvas_set_color(canvas, ColorWhite);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 4, 10, furi_string_get_cstr(model->header));
            canvas_set_color(canvas, ColorBlack);
        }

        MenuItem* item = MenuItemArray_get(model->items, position);
        const uint8_t card_y = header_height + 4;
        canvas_draw_box(canvas, 0, card_y, 123, 31);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_icon_animation(canvas, 7, card_y + 8, item->icon);
        canvas_set_font(canvas, FontPrimary);
        menu_draw_fitted_label(canvas, 27, card_y + 20, 91, AlignLeft, item->label);

        char position_text[24];
        snprintf(
            position_text,
            sizeof(position_text),
            "%02u / %02u",
            (unsigned)(position + 1),
            (unsigned)items_count);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 118, card_y + 28, AlignRight, AlignBottom, position_text);
        canvas_set_color(canvas, ColorBlack);

        const size_t next_position = (position + 1) % items_count;
        item = MenuItemArray_get(model->items, next_position);
        canvas_draw_str(canvas, 7, 60, "> NEXT");
        menu_draw_fitted_label(canvas, 119, 60, 75, AlignRight, item->label);
        elements_scrollbar(canvas, position, items_count);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 32, "Empty");
        elements_scrollbar(canvas, 0, 0);
    }
}

static bool menu_input_callback(InputEvent* event, void* context) {
    Menu* menu = context;
    bool consumed = false;

    if(event->type == InputTypeShort) {
        if(event->key == InputKeyUp) {
            consumed = true;
            menu_process_up(menu);
        } else if(event->key == InputKeyDown) {
            consumed = true;
            menu_process_down(menu);
        } else if(event->key == InputKeyOk) {
            consumed = true;
            menu_process_ok(menu);
        }
    } else if(event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            consumed = true;
            menu_process_up(menu);
        } else if(event->key == InputKeyDown) {
            consumed = true;
            menu_process_down(menu);
        }
    }

    return consumed;
}

static void menu_enter(void* context) {
    Menu* menu = context;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_start(item->icon);
            }
        },
        false);
}

static void menu_exit(void* context) {
    Menu* menu = context;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_stop(item->icon);
            }
        },
        false);
}

Menu* menu_alloc(void) {
    Menu* menu = malloc(sizeof(Menu));
    menu->view = view_alloc();
    view_set_context(menu->view, menu);
    view_allocate_model(menu->view, ViewModelTypeLocking, sizeof(MenuModel));
    view_set_draw_callback(menu->view, menu_draw_callback);
    view_set_input_callback(menu->view, menu_input_callback);
    view_set_enter_callback(menu->view, menu_enter);
    view_set_exit_callback(menu->view, menu_exit);

    with_view_model(
        menu->view,
        MenuModel * model,
        {
            MenuItemArray_init(model->items);
            model->header = furi_string_alloc();
            model->position = 0;
        },
        true);

    return menu;
}

void menu_free(Menu* menu) {
    furi_check(menu);

    menu_reset(menu);
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            furi_string_free(model->header);
            MenuItemArray_clear(model->items);
        },
        false);
    view_free(menu->view);

    free(menu);
}

View* menu_get_view(Menu* menu) {
    furi_check(menu);
    return menu->view;
}

void menu_set_header(Menu* menu, const char* header) {
    furi_check(menu);
    furi_check(header);

    with_view_model(
        menu->view, MenuModel * model, { furi_string_set_str(model->header, header); }, true);
}

void menu_add_item(
    Menu* menu,
    const char* label,
    const Icon* icon,
    uint32_t index,
    MenuItemCallback callback,
    void* context) {
    furi_check(menu);
    furi_check(label);

    MenuItem* item = NULL;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            item = MenuItemArray_push_new(model->items);
            item->label = label;
            item->icon = icon ? icon_animation_alloc(icon) : icon_animation_alloc(&A_Plugins_14);
            view_tie_icon_animation(menu->view, item->icon);
            item->index = index;
            item->callback = callback;
            item->callback_context = context;
        },
        true);
}

void menu_reset(Menu* menu) {
    furi_check(menu);
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            for
                M_EACH(item, model->items, MenuItemArray_t) {
                    icon_animation_stop(item->icon);
                    icon_animation_free(item->icon);
                }

            MenuItemArray_reset(model->items);
            model->position = 0;
        },
        true);
}

void menu_set_selected_item(Menu* menu, uint32_t index) {
    furi_check(menu);

    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(index < MenuItemArray_size(model->items)) {
                model->position = index;
            }
        },
        true);
}

static void menu_process_up(Menu* menu) {
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_stop(item->icon);

                if(model->position > 0) {
                    model->position--;
                } else {
                    model->position = MenuItemArray_size(model->items) - 1;
                }

                item = MenuItemArray_get(model->items, model->position);
                icon_animation_start(item->icon);
            }
        },
        true);
}

static void menu_process_down(Menu* menu) {
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_stop(item->icon);

                if(model->position < MenuItemArray_size(model->items) - 1) {
                    model->position++;
                } else {
                    model->position = 0;
                }

                item = MenuItemArray_get(model->items, model->position);
                icon_animation_start(item->icon);
            }
        },
        true);
}

static void menu_process_ok(Menu* menu) {
    MenuItem* item = NULL;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                item = MenuItemArray_get(model->items, model->position);
            }
        },
        true);
    if(item && item->callback) {
        item->callback(item->callback_context, item->index);
    }
}
