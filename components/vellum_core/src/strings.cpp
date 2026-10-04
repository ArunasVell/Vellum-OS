#include "vellum/strings.h"

#include <cstdio>

namespace vellum {
namespace {

constexpr Strings kEnglish = {
    .language_name = "English",
    .continue_reading = "CONTINUE READING",
    .start_reading = "START READING",
    .library_label = "LIBRARY",
    .library = "Library",
    .all_books = "All books",
    .settings = "Settings",
    .not_started = "Not started",
    .page_of = "Page %d of %d",
    .short_of = "%d of %d",
    .status_new = "New",
    .status_finished = "Finished",
    .hint_home = "Side buttons move · Power opens",
    .hint_list = "Side buttons move · Power opens · Home back",
    .hint_settings = "Side buttons move · Power changes · Home back",
    .general_label = "GENERAL",
    .reading_label = "READING",
    .preview_label = "PREVIEW",
    .language = "Language",
    .clock = "Clock",
    .clock_24 = "24-hour",
    .clock_12 = "12-hour",
    .sleep_after = "Sleep after",
    .sleep_names = {"5 min", "10 min", "30 min", "Never"},
    .sleeping = "SLEEPING",
    .wake_hint = "Press Power to wake",
    .text_size = "Text size",
    .size_names = {"Small", "Medium", "Large", "Extra large"},
    .line_spacing = "Line spacing",
    .word_spacing = "Word spacing",
    .spacing_names = {"Narrow", "Normal", "Wide"},
    .margins = "Margins",
    .margin_names = {"Narrow", "Normal", "Wide"},
    .alignment = "Alignment",
    .justified = "Justified",
    .left = "Left",
    .hyphenation = "Hyphenation",
    .on = "On",
    .off = "Off",
    .add_books = "Add books",
    .transfer_lead = "Send EPUB books from a phone or computer.",
    .step_join = "1  JOIN THIS WI-FI",
    .network = "Network",
    .password = "Password",
    .step_open = "2  OPEN IN A BROWSER",
    .scan_hint = "Or scan the codes with a phone camera.",
    .received_label = "RECEIVED",
    .waiting = "Waiting for books…",
    .receiving = "Receiving %s",
    .card_full = "The card is full.",
    .starting = "Starting Wi-Fi…",
    .no_wifi = "Wi-Fi could not start. Go back and try again.",
    .no_card = "Insert an SD card to add books.",
    .hint_transfer = "Home: done",
    .contents = "Contents",
    .back_to_book = "Back to the page",
    .add_bookmark = "Bookmark this page",
    .remove_bookmark = "Remove this bookmark",
    .bookmarks_label = "BOOKMARKS",
    .chapters_label = "CHAPTERS",
    .chapter_n = "Chapter %d",
    .battery = "Battery",
    .on_battery = "On battery",
    .charging = "Charging",
    .history_label = "LAST 7 DAYS",
    .estimates_label = "ESTIMATES",
    .time_left = "Time left",
    .while_reading = "While reading",
    .while_asleep = "While asleep",
    .measuring = "Still measuring",
    .per_hour = "%s%% an hour",
    .per_day = "%s%% a day",
    .clock_unset =
        "The clock is not set, so there is no history yet. Open Add books and load the page on your phone once: it "
        "sets the clock.",
    .hint_battery = "Power or Home: back",
    .front_light = "Front light",
    .brightness = "Brightness",
    .warmth = "Warmth",
    .cool = "Cool",
    .warm = "Warm",
    .done = "Done",
    .hint_light = "Side buttons change · Power next",
    .device_check = "Device check",
    .buttons_label = "BUTTONS",
    .key_names = {"Up", "Down", "Home", "Power"},
    .touch_label = "TOUCH",
    .touch_here = "Touch anywhere on the screen",
    .about_label = "THIS DEVICE",
    .device = "Model",
    .screen = "Screen",
    .memory_card = "Memory card",
    .wifi = "Wi-Fi",
    .present = "Yes",
    .missing = "Missing",
    .not_set = "Not set",
    .hint_check = "Press every button · Home twice: back",
    .preview_text =
        "The evening was quiet, and the river carried the last reflections of the sun slowly past the old "
        "willows, while somewhere beyond the meadow a dog barked twice and then fell silent.",
    .preview_language = "en",
};

constexpr Strings kLithuanian = {
    .language_name = "Lietuvių",
    .continue_reading = "TĘSTI SKAITYMĄ",
    .start_reading = "PRADĖTI SKAITYTI",
    .library_label = "BIBLIOTEKA",
    .library = "Biblioteka",
    .all_books = "Visos knygos",
    .settings = "Nustatymai",
    .not_started = "Nepradėta",
    .page_of = "%d psl. iš %d",
    .short_of = "%d iš %d",
    .status_new = "Nauja",
    .status_finished = "Perskaityta",
    .hint_home = "Šoniniai: rinktis · Įjungimo: atverti",
    .hint_list = "Šoniniai: rinktis · Įjungimo: atverti · Namų: atgal",
    .hint_settings = "Šoniniai: rinktis · Įjungimo: keisti · Namų: atgal",
    .general_label = "BENDRI",
    .reading_label = "SKAITYMAS",
    .preview_label = "PERŽIŪRA",
    .language = "Kalba",
    .clock = "Laikrodis",
    .clock_24 = "24 val.",
    .clock_12 = "12 val.",
    .sleep_after = "Užmigti po",
    .sleep_names = {"5 min.", "10 min.", "30 min.", "Niekada"},
    .sleeping = "MIEGA",
    .wake_hint = "Paspauskite įjungimo mygtuką",
    .text_size = "Teksto dydis",
    .size_names = {"Mažas", "Vidutinis", "Didelis", "Labai didelis"},
    .line_spacing = "Eilučių tarpai",
    .word_spacing = "Tarpai tarp žodžių",
    .spacing_names = {"Siauri", "Įprasti", "Platūs"},
    .margins = "Paraštės",
    .margin_names = {"Siauros", "Įprastos", "Plačios"},
    .alignment = "Lygiuotė",
    .justified = "Abipusė",
    .left = "Kairinė",
    .hyphenation = "Žodžių kėlimas",
    .on = "Įjungtas",
    .off = "Išjungtas",
    .add_books = "Įkelti knygas",
    .transfer_lead = "Siųskite EPUB knygas iš telefono ar kompiuterio.",
    .step_join = "1  PRISIJUNKITE PRIE WI-FI",
    .network = "Tinklas",
    .password = "Slaptažodis",
    .step_open = "2  ATVERKITE NARŠYKLĖJE",
    .scan_hint = "Arba nuskenuokite kodus telefono kamera.",
    .received_label = "GAUTA",
    .waiting = "Laukiama knygų…",
    .receiving = "Gaunama: %s",
    .card_full = "Kortelėje nebėra vietos.",
    .starting = "Įjungiamas Wi-Fi…",
    .no_wifi = "Nepavyko įjungti Wi-Fi. Grįžkite ir bandykite dar kartą.",
    .no_card = "Įdėkite SD kortelę, kad galėtumėte įkelti knygų.",
    .hint_transfer = "Namų: baigti",
    .contents = "Turinys",
    .back_to_book = "Grįžti į puslapį",
    .add_bookmark = "Pažymėti šį puslapį",
    .remove_bookmark = "Pašalinti šią žymę",
    .bookmarks_label = "ŽYMĖS",
    .chapters_label = "SKYRIAI",
    .chapter_n = "%d skyrius",
    .battery = "Baterija",
    .on_battery = "Veikia iš baterijos",
    .charging = "Kraunama",
    .history_label = "PASKUTINĖS 7 DIENOS",
    .estimates_label = "ĮVERČIAI",
    .time_left = "Liko",
    .while_reading = "Skaitant",
    .while_asleep = "Miegant",
    .measuring = "Dar matuojama",
    .per_hour = "%s %% per valandą",
    .per_day = "%s %% per parą",
    .clock_unset =
        "Laikrodis nenustatytas, todėl istorijos dar nėra. Atverkite „Įkelti knygas“ ir vieną kartą įkelkite "
        "puslapį telefone: jis nustatys laikrodį.",
    .hint_battery = "Įjungimo ar Namų: atgal",
    .front_light = "Apšvietimas",
    .brightness = "Ryškumas",
    .warmth = "Šiltumas",
    .cool = "Šalta",
    .warm = "Šilta",
    .done = "Baigti",
    .hint_light = "Šoniniai: keisti · Įjungimo: toliau",
    .device_check = "Įrenginio patikra",
    .buttons_label = "MYGTUKAI",
    .key_names = {"Aukštyn", "Žemyn", "Namai", "Įjungti"},
    .touch_label = "LIETIMAS",
    .touch_here = "Palieskite bet kurią ekrano vietą",
    .about_label = "ŠIS ĮRENGINYS",
    .device = "Modelis",
    .screen = "Ekranas",
    .memory_card = "Atminties kortelė",
    .wifi = "Wi-Fi",
    .present = "Yra",
    .missing = "Nėra",
    .not_set = "Nenustatytas",
    .hint_check = "Spauskite visus mygtukus · Namų 2 kartus: atgal",
    .preview_text =
        "Vakaras buvo tylus, o upė lėtai nešė paskutinius saulės atspindžius "
        "pro senus gluosnius, kol kažkur už pievos du kartus sulojęs šuo nutilo.",
    .preview_language = "lt",
};

}  // namespace

const Strings& strings(Language lang) { return lang == Language::Lithuanian ? kLithuanian : kEnglish; }

void format_book_count(Language lang, int n, char* out, size_t size) {
  if (lang == Language::Lithuanian) {
    // 1, 21, 31 knyga; 2-9, 22-29 knygos; 10-20, 30 knygų.
    const int tens = n % 100;
    const char* word = (n % 10 == 1 && tens != 11)                  ? "knyga"
                       : (n % 10 >= 2 && (tens < 10 || tens >= 20)) ? "knygos"
                                                                    : "knygų";
    std::snprintf(out, size, "%d %s", n, word);
    return;
  }
  std::snprintf(out, size, "%d %s", n, n == 1 ? "book" : "books");
}

void format_time_left(Language lang, int hours, char* out, size_t size) {
  const bool days = hours >= 48;
  const int n = days ? (hours + 12) / 24 : hours < 1 ? 1 : hours;
  if (lang == Language::Lithuanian) {
    // After "apie" (accusative): 1, 21 dieną; 2-9 dienas; 10-20 dienų.
    const int tens = n % 100;
    const int form = (n % 10 == 1 && tens != 11) ? 0 : (n % 10 >= 2 && (tens < 10 || tens >= 20)) ? 1 : 2;
    const char* const day_words[] = {"dieną", "dienas", "dienų"};
    const char* const hour_words[] = {"valandą", "valandas", "valandų"};
    std::snprintf(out, size, "Apie %d %s", n, (days ? day_words : hour_words)[form]);
    return;
  }
  std::snprintf(out, size, "About %d %s", n, days ? "days" : n == 1 ? "hour" : "hours");
}

void format_tenths(Language lang, int tenths, char* out, size_t size) {
  std::snprintf(out, size, "%d%c%d", tenths / 10, lang == Language::Lithuanian ? ',' : '.', tenths % 10);
}

}  // namespace vellum
