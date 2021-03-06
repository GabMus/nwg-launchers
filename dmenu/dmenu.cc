/*
 * GTK-based dmenu
 * Copyright (c) 2020 Piotr Miller
 * e-mail: nwg.piotr@gmail.com
 * Website: http://nwg.pl
 * Project: https://github.com/nwg-piotr/nwg-launchers
 * License: GPL3
 * */

#include <sys/time.h>
#include <unistd.h>

#include <charconv>

#include "nwg_tools.h"
#include "nwg_classes.h"
#include "on_event.h"
#include "dmenu.h"

#define ROWS_DEFAULT 20

#define STR_EXPAND(x) #x
#define STR(x) STR_EXPAND(x)

int image_size {72};                        // make linker happy

std::string h_align {""};                   // horizontal alignment
std::string v_align {""};                   // vertical alignment
RGBA background = {0.0, 0.0, 0.0, 0.3};
std::string wm {""};                        // detected or forced window manager name
std::string settings_file {""};

int rows = ROWS_DEFAULT;                    // number of menu items to display
std::vector<Glib::ustring> all_commands {};

bool dmenu_run = false;
bool show_searchbox = true;
bool case_sensitive = true;

const char* const HELP_MESSAGE =
"GTK dynamic menu: nwgdmenu " VERSION_STR " (c) Piotr Miller & Contributors 2020\n\n\
<input> | nwgdmenu - displays newline-separated stdin input as a GTK menu\n\
nwgdmenu - creates a GTK menu out of commands found in $PATH\n\n\
Options:\n\
-h               show this help message and exit\n\
-n               no search box\n\
-ha <l>|<r>      horizontal alignment left/right (default: center)\n\
-va <t>|<b>      vertical alignment top/bottom (default: middle)\n\
-r <rows>        number of rows (default: " STR(ROWS_DEFAULT) ")\n\
-c <name>        css file name (default: style.css)\n\
-o <opacity>     background opacity (0.0 - 1.0, default 0.3)\n\
-b <background>  background colour in RRGGBB or RRGGBBAA format (RRGGBBAA alpha overrides <opacity>)\n\
-wm <wmname>     window manager name (if can not be detected)\n\
-run             ignore stdin, always build from commands in $PATH\n\n\
Hotkeys:\n\
Delete        clear search box\n\
Insert        switch case sensitivity\n";

int main(int argc, char *argv[]) {
    std::string custom_css_file {"style.css"};

    /* For now the settings file only determines if case_sensitive was turned on.
     * Let's just check if the file exists.
     **/
    settings_file = get_settings_path();
    if (std::ifstream(settings_file)) {
        case_sensitive = false;
    }

    pid_t pid = getpid();
    std::string mypid = std::to_string(pid);

    std::string pid_file = "/var/run/user/" + std::to_string(getuid()) + "/nwgdmenu.pid";

    int saved_pid {};
    if (std::ifstream(pid_file)) {
        try {
            saved_pid = std::stoi(read_file_to_string(pid_file));
            if (kill(saved_pid, 0) != -1) {  // found running instance!
                kill(saved_pid, 9);
                save_string_to_file(mypid, pid_file);
                std::exit(0);
            }
        } catch (...) {
            std::cerr << "\nError reading pid file\n\n";
        }
    }
    save_string_to_file(mypid, pid_file);

    InputParser input(argc, argv);
    if (input.cmdOptionExists("-h")){
        std::cout << HELP_MESSAGE;
        std::exit(0);
    }

    // We will build dmenu out of commands found in $PATH if nothing has been passed by stdin
    dmenu_run = isatty(fileno(stdin)) == 1;

    if (input.cmdOptionExists("-run")){
        dmenu_run = true;
    }

    // Otherwise let's build from stdin input
    if (!dmenu_run) {
        all_commands = {};
        for (std::string line; std::getline(std::cin, line);) {
            all_commands.emplace_back(std::move(line));
        }
    }

    if (input.cmdOptionExists("-n")){
        show_searchbox = false;
    }

    auto halign = input.getCmdOption("-ha");
    if (halign == "l" || halign == "left") {
        h_align = "l";
    } else if (halign == "r" || halign == "right") {
        h_align = "r";
    }

    auto valign = input.getCmdOption("-va");
    if (valign == "t" || valign == "top") {
        v_align = "t";
    } else if (valign == "b" || valign == "bottom") {
        v_align = "b";
    }

    auto css_name = input.getCmdOption("-c");
    if (!css_name.empty()){
        custom_css_file = css_name;
    }

    auto wm_name = input.getCmdOption("-wm");
    if (!wm_name.empty()){
        wm = wm_name;
    }

    auto opa = input.getCmdOption("-o");
    if (!opa.empty()){
        try {
            auto o = std::stod(std::string{opa});
            if (o >= 0.0 && o <= 1.0) {
                background.alpha = o;
            } else {
                std::cerr << "\nERROR: Opacity must be in range 0.0 to 1.0\n\n";
            }
        } catch (...) {
            std::cerr << "\nERROR: Invalid opacity value\n\n";
        }
    }

    std::string_view bcg = input.getCmdOption("-b");
    if (!bcg.empty()) {
        set_background(bcg);
    }

    auto rw = input.getCmdOption("-r");
    if (!rw.empty()){
        int r;
        auto from = rw.data();
        auto to = from + rw.size();
        auto [p, ec] = std::from_chars(from, to, r);
        if (ec == std::errc()) {
            if (r > 0 && r <= 100) {
                rows = r;
            } else {
                std::cerr << "\nERROR: Number of rows must be in range 1 - 100\n\n";
            }
        } else {
            std::cerr << "\nERROR: Invalid rows number\n\n";
        }
    }

    std::string config_dir = get_config_dir("nwgdmenu");
    if (!fs::is_directory(config_dir)) {
        std::cout << "Config dir not found, creating...\n";
        fs::create_directories(config_dir);
    }

    // default and custom style sheet
    std::string default_css_file = config_dir + "/style.css";
    // css file to be used
    std::string css_file = config_dir + "/" + custom_css_file;
    // copy default file if not found
    if (!fs::exists(default_css_file)) {
        try {
            fs::copy_file(DATA_DIR_STR "/nwgdmenu/style.css", default_css_file, fs::copy_options::overwrite_existing);
        } catch (...) {
            std::cerr << "Failed copying default style.css\n";
        }
    }

    /* get current WM name if not forced */
    if (wm.empty()) {
        wm = detect_wm();
    }

    if (dmenu_run) {
        /* get a list of paths to all commands from all application dirs */
        std::vector<std::string> commands = list_commands();
        std::cout << commands.size() << " commands found\n";

        /* Create a vector of commands (w/o path) */
        all_commands = {};
        for (auto&& command : commands) {
            auto cmd = take_last_by(command, "/");
            if (cmd.find(".") != 0 && cmd.size() != 1) {
                all_commands.emplace_back(cmd.data(), cmd.size());
            }
        }

        /* Sort case insensitive */
        std::sort(all_commands.begin(), all_commands.end(), [](auto& a, auto& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](auto a, auto b) {
                return std::tolower(a) < std::tolower(b);
            });
        });
    }

    /* turn off borders, enable floating on sway */
    if (wm == "sway") {
        auto* cmd = "swaymsg -q for_window [title=\"~nwgdmenu*\"] floating enable";
        std::system(cmd);
        cmd = "swaymsg -q for_window [title=\"~nwgdmenu*\"] border none";
        std::system(cmd);
    }

    Gtk::Main kit(argc, argv);

    auto provider = Gtk::CssProvider::create();
    auto display = Gdk::Display::get_default();
    auto screen = display->get_default_screen();
    if (!provider || !display || !screen) {
        std::cerr << "ERROR: Failed to initialize GTK\n";
        return EXIT_FAILURE;
    }
    Gtk::StyleContext::add_provider_for_screen(screen, provider, GTK_STYLE_PROVIDER_PRIORITY_USER);

    if (std::filesystem::is_regular_file(css_file)) {
        provider->load_from_path(css_file);
        std::cout << "Using " << css_file << '\n';
    } else {
        provider->load_from_path(default_css_file);
        std::cout << "Using " << default_css_file << '\n';
    }

    MainWindow window;
    // For openbox and similar we'll need the window x, y coordinates
    window.show();

    DMenu menu;
    Anchor anchor(&menu);
    window.anchor = &anchor;

    window.signal_button_press_event().connect(sigc::ptr_fun(&on_window_clicked));

    /* Detect focused display geometry: {x, y, width, height} */
    auto geometry = display_geometry(wm, display, window.get_window());
    std::cout << "Focused display: " << geometry.x << ", " << geometry.y << ", " << geometry.width << ", "
    << geometry.height << '\n';

    int x = geometry.x;
    int y = geometry.y;
    int w = geometry.width;
    int h = geometry.height;

    if (wm == "sway" || wm == "i3") {
        window.resize(w, h);
        window.move(x, y);
        window.hide();
    } else {
        window.hide();
        int x_org;
        int y_org;
        window.resize(1, 1);
        if (!h_align.empty() || !v_align.empty()) {
            window.move(x, y);
            window.get_position(x_org, y_org);
        }
        // We assume that the window has been opened at mouse pointer coordinates
        window.get_position(x_org, y_org);

        if (h_align == "l") {
            window.move(x, y_org);
            window.get_position(x_org, y_org);
        }
        if (h_align == "r") {
            window.move(x + w - 50, y_org);
            window.get_position(x_org, y_org);
        }
        if (v_align == "t") {
            window.move(x_org, y);
            window.get_position(x_org, y_org);
        }
        if (v_align == "b") {
            window.move(x_org, y + h);
        }
        //~ window.hide();
    }

    if (show_searchbox) {
        auto search_item = new Gtk::MenuItem();
        search_item -> add(menu.searchbox);
        search_item -> set_name("search_item");
        search_item -> set_sensitive(false);
        menu.append(*search_item);
    }

    menu.signal_deactivate().connect(sigc::ptr_fun(Gtk::Main::quit));

    int cnt = 0;
    for (auto& command : all_commands) {
        auto item = new Gtk::MenuItem();
        item -> set_label(command);
        item -> signal_activate().connect(sigc::bind<std::string>(sigc::ptr_fun(&on_item_clicked),
                                                               std::move(command)));

        menu.append(*item);
        cnt++;
        if (cnt > rows - 1) {
            break;
        }
    }

    menu.set_reserve_toggle_size(false);
    menu.set_property("width_request", w / 8);

    Gtk::Box outer_box(Gtk::ORIENTATION_VERTICAL);
    outer_box.set_spacing(15);

    Gtk::VBox inner_vbox;

    Gtk::HBox inner_hbox;

    if (h_align == "l") {
        inner_hbox.pack_start(anchor, false, false);
    } else if (h_align == "r") {
        inner_hbox.pack_end(anchor, false, false);
    } else {
        inner_hbox.pack_start(anchor, true, false);
    }

    if (v_align == "t") {
        inner_vbox.pack_start(inner_hbox, false, false);
    } else if (v_align == "b") {
        inner_vbox.pack_end(inner_hbox, false, false);
    } else {
        inner_vbox.pack_start(inner_hbox, true, false);
    }
    outer_box.pack_start(inner_vbox, Gtk::PACK_EXPAND_WIDGET);

    window.add(outer_box);
    window.show_all_children();

    menu.show_all();

    Gtk::Main::run(window);

    return 0;
}
