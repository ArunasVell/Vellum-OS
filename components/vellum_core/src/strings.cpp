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

}  // namespace vellum
