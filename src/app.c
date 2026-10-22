#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <stdio.h>

#include "runner.h"
#include "scenes/scene.h"

/* Menu indices: 0 = Play Demo, 1..scene_count = scenes, then setup and
 * about (listed second and third). */
#define MENU_PLAY_ALL 0
#define MENU_SETUP    1000
#define MENU_ABOUT    1001
#define MENU_NONE     0xFFFFFFFFu

#define VIEW_MENU  0
#define VIEW_ABOUT 1

static const char about_text[] = "Second Reality by\n"
                                 "Future Crew\n"
                                 "\n"
                                 "Adapted to the\n"
                                 "Flipper Zero by\n"
                                 "Roberto De Feo\n"
                                 "github.com/rdefeo";

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* about;
    bool in_about;
    char (*labels)[32];
    uint32_t selected;
    GrayConfig cfg;
} App;

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    if(index == MENU_ABOUT) { /* shown in the dispatcher, no runner */
        app->in_about = true;
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_ABOUT);
        return;
    }
    app->selected = index;
    view_dispatcher_stop(app->dispatcher);
}

static bool back_callback(void* context) {
    App* app = context;
    if(app->in_about) {
        app->in_about = false;
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_MENU);
        return true;
    }
    app->selected = MENU_NONE;
    view_dispatcher_stop(app->dispatcher);
    return true;
}

int32_t second_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    app->gui = furi_record_open(RECORD_GUI);
    gray_config_load(&app->cfg);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Second Reality");
    submenu_add_item(app->menu, "Play Demo", MENU_PLAY_ALL, menu_callback, app);
    submenu_add_item(app->menu, "Display Setup", MENU_SETUP, menu_callback, app);
    submenu_add_item(app->menu, "About", MENU_ABOUT, menu_callback, app);
    app->labels = malloc(scene_count * sizeof(*app->labels));
    for(size_t i = 0; i < scene_count; i++) {
        snprintf(app->labels[i], sizeof(app->labels[i]), "%u. %s", i + 1, scene_list[i]->name);
        submenu_add_item(app->menu, app->labels[i], i + 1, menu_callback, app);
    }

    app->about = text_box_alloc();
    text_box_set_font(app->about, TextBoxFontText);
    text_box_set_text(app->about, about_text);
    app->in_about = false;

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, back_callback);
    view_dispatcher_add_view(app->dispatcher, VIEW_MENU, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, VIEW_ABOUT, text_box_get_view(app->about));
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, VIEW_MENU);

    /* The menu runs until something is picked; the runner then owns the
     * screen (direct draw) until Back brings us back here. */
    for(;;) {
        app->selected = MENU_NONE;
        view_dispatcher_run(app->dispatcher);
        if(app->selected == MENU_NONE) break;
        submenu_set_selected_item(app->menu, app->selected);

        if(app->selected == MENU_PLAY_ALL)
            runner_run(app->gui, &app->cfg, RunnerModePlayAll, 0);
        else if(app->selected == MENU_SETUP)
            runner_run(app->gui, &app->cfg, RunnerModeSetup, 0);
        else
            runner_run(app->gui, &app->cfg, RunnerModeSingle, app->selected - 1);
    }

    view_dispatcher_remove_view(app->dispatcher, VIEW_MENU);
    view_dispatcher_remove_view(app->dispatcher, VIEW_ABOUT);
    view_dispatcher_free(app->dispatcher);
    submenu_free(app->menu);
    text_box_free(app->about);
    free(app->labels);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
