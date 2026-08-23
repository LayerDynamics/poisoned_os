import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class PoisonUiSourceTests(unittest.TestCase):
    def test_desktop_owns_rendering_and_all_shortcuts(self):
        source = (ROOT / "applications/services/desktop/views/desktop_view_main.c").read_text()

        self.assertIn("view_set_draw_callback(main_view->view, desktop_main_draw_callback);", source)
        self.assertIn("DesktopMainEventOpenFavoriteRightShort", source)
        self.assertIn('"POISONED"', source)
        self.assertIn('"FIELD READY"', source)

    def test_desktop_draw_callback_has_initialized_view_model(self):
        source = (ROOT / "applications/services/desktop/views/desktop_view_main.c").read_text()

        model_allocation = source.index(
            "view_allocate_model(main_view->view, ViewModelTypeLocking, "
            "sizeof(DesktopMainViewModel));"
        )
        model_initialization = source.index("model->dummy_mode = false;")
        callback_registration = source.index(
            "view_set_draw_callback(main_view->view, desktop_main_draw_callback);"
        )
        draw_callback = source[
            source.index("static void desktop_main_draw_callback") : source.index(
                "static void desktop_main_poweroff_timer_callback"
            )
        ]
        self.assertLess(model_allocation, callback_registration)
        self.assertLess(model_initialization, callback_registration)
        self.assertIn("DesktopMainViewModel* model = _model;", draw_callback)
        self.assertNotIn("DesktopMainView* main_view = context;", draw_callback)

    def test_branded_animation_is_below_navigation_chrome(self):
        source = (ROOT / "applications/services/desktop/desktop.c").read_text()

        animation = source.index(
            "view_stack_add_view(desktop->main_view_stack, poison_animation_view);"
        )
        chrome = source.index(
            "view_stack_add_view(desktop->main_view_stack, "
            "desktop_main_get_view(desktop->main_view));"
        )
        self.assertLess(animation, chrome)

    def test_poison_animation_pack_replaces_upstream_pack(self):
        source = (ROOT / "assets/SConscript").read_text()
        self.assertIn('Dir("poison")', source)

        required = {
            "internal": {"L1_Tv_128x47", "L1_BadBattery_128x47", "L1_NoSd_128x49"},
            "blocking": {
                "L0_NoDb_128x51",
                "L0_SdBad_128x51",
                "L0_SdOk_128x51",
                "L0_Url_128x51",
                "L0_NewMail_128x51",
            },
            "external": {
                "P1_Assay_128x64",
                "P1_Containment_128x64",
                "P2_Link_128x64",
                "P3_Signal_128x64",
            },
        }
        for category, names in required.items():
            manifest = (ROOT / "assets/poison" / category / "manifest.txt").read_text()
            for name in names:
                self.assertIn(f"Name: {name}", manifest)
                animation = ROOT / "assets/poison" / category / name
                self.assertTrue((animation / "meta.txt").is_file())
                self.assertTrue((animation / "frame_0.png").is_file())

    def test_update_levelup_and_about_surfaces_use_poison_assets(self):
        options = (ROOT / "fbt_options.py").read_text()
        manager = (
            ROOT / "applications/services/desktop/animations/animation_manager.c"
        ).read_text()
        about = (ROOT / "applications/settings/about/about.c").read_text()

        self.assertIn('UPDATE_SPLASH = "poison_update"', options)
        self.assertTrue((ROOT / "assets/slideshow/poison_update/frame_00.png").is_file())
        self.assertIn("A_PoisonLevelup1_128x64", manager)
        self.assertIn("A_PoisonLevelup2_128x64", manager)
        self.assertIn('"PoisonedOS"', about)
        self.assertIn("I_PoisonFlask_32x32", about)

    def test_app_launcher_uses_branded_header_api(self):
        menu_header = (ROOT / "applications/services/gui/modules/menu.h").read_text()
        loader = (ROOT / "applications/services/loader/loader_menu.c").read_text()

        self.assertIn("void menu_set_header(Menu* menu, const char* header);", menu_header)
        self.assertIn('menu_set_header(app->primary_menu, "POISON // APPS");', loader)
        self.assertIn('submenu_set_header(app->settings_menu, "POISON // SETTINGS");', loader)

    def test_lock_surfaces_use_poisoned_os_language(self):
        locked = (ROOT / "applications/services/desktop/views/desktop_view_locked.c").read_text()
        controls = (
            ROOT / "applications/services/desktop/views/desktop_view_lock_menu.c"
        ).read_text()

        self.assertIn('"SECURE"', locked)
        self.assertIn('"3x BACK TO RELEASE"', locked)
        self.assertIn('"POISON // CONTROL"', controls)
        self.assertIn('"Cover mode"', controls)

    def test_settings_rows_use_square_assay_selection(self):
        source = (
            ROOT / "applications/services/gui/modules/variable_item_list.c"
        ).read_text()

        self.assertIn("canvas_draw_box(canvas, 0, item_y, item_width, item_height);", source)
        self.assertIn("canvas_draw_line(canvas, 68, item_y, 68, item_y + item_height - 1);", source)
        self.assertNotIn("elements_slightly_rounded_box", source)

    def test_status_bar_uses_segmented_field_rail(self):
        source = (ROOT / "applications/services/gui/gui.c").read_text()

        self.assertIn(
            "for(uint8_t tick = 3; tick < GUI_DISPLAY_WIDTH; tick += 8)", source
        )
        self.assertNotIn("&I_Background_128x11", source)
        self.assertNotIn("canvas_draw_rframe", source)

    def test_power_and_update_lifecycle_use_operational_language(self):
        power_off = (
            ROOT / "applications/services/power/power_service/views/power_off.c"
        ).read_text()
        unplug = (
            ROOT / "applications/services/power/power_service/views/power_unplug_usb.c"
        ).read_text()
        updater = (ROOT / "applications/system/updater/views/updater_main.c").read_text()

        self.assertIn('"POWER / CRITICAL"', power_off)
        self.assertNotIn("I_FaceNopower", power_off)
        self.assertIn('"POWER ISOLATED"', unplug)
        self.assertNotIn("I_Unplug_bg", unplug)
        self.assertIn('"POISON // UPDATE"', updater)

    def test_security_timeout_and_diagnostics_are_branded(self):
        timeout = (
            ROOT / "applications/services/desktop/views/desktop_view_pin_timeout.c"
        ).read_text()
        diagnostics = (
            ROOT / "applications/services/desktop/views/desktop_view_debug.c"
        ).read_text()

        self.assertIn('"ACCESS / DELAY"', timeout)
        self.assertIn('"INPUT REJECTED"', timeout)
        self.assertIn('"POISON // DIAGNOSTIC"', diagnostics)


if __name__ == "__main__":
    unittest.main()
