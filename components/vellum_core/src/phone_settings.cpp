// The phone page's Settings section: what its requests read and change on
// the card. The boards' servers call these, so each answers the same way.
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "vellum/catalogues.h"
#include "vellum/kosync.h"
#include "vellum/settings.h"
#include "vellum/upload_page.h"

namespace vellum {

bool phone_settings_json(Storage& card, const char* fonts, const char* home_network, char* out, size_t size) {
  Settings s;
  load_settings(card, s);
  size_t len = 0;
  auto add = [&](const char* text) {
    const size_t n = std::strlen(text);
    if (len + n + 1 > size) return false;
    std::memcpy(out + len, text, n + 1);
    len += n;
    return true;
  };
  // Each part is written in place, then the next one after it.
  auto part = [&](auto&& write) {
    if (!write(out + len, size - len)) return false;
    len += std::strlen(out + len);
    return true;
  };
  if (!add("{\"settings\":") || !part([&](char* at, size_t room) { return settings_json(s, fonts, at, room); }) ||
      !add(",\"home_network\":\"") || !json_escape(home_network != nullptr ? home_network : "", out, size, len) ||
      !add("\",\"sync\":")) {
    return false;
  }
  kosync::Account account;
  kosync::load_account(card, account);
  if (!part([&](char* at, size_t room) { return kosync::account_json(account, at, room); }) ||
      !add(",\"catalogues\":")) {
    return false;
  }
  std::unique_ptr<Catalogues> catalogues(new (std::nothrow) Catalogues);
  if (!catalogues) return false;
  catalogues->load(card);
  return part([&](char* at, size_t room) { return catalogues->write_json(at, room); }) && add("}");
}

PhoneSaved phone_post_settings(Storage& card, const char* body, size_t len) {
  Settings s;
  load_settings(card, s);
  if (!settings_from_form(body, len, s)) return PhoneSaved::Bad;
  return save_settings(card, s) ? PhoneSaved::Ok : PhoneSaved::NotSaved;
}

PhoneSaved phone_post_sync(Storage& card, const char* body, size_t len) {
  kosync::Account account;
  kosync::load_account(card, account);
  if (!kosync::account_from_form(body, len, account)) return PhoneSaved::Bad;
  return kosync::save_account(card, account) ? PhoneSaved::Ok : PhoneSaved::NotSaved;
}

PhoneSaved phone_post_catalogues(Storage& card, const char* body, size_t len) {
  std::unique_ptr<Catalogues> catalogues(new (std::nothrow) Catalogues);
  if (!catalogues) return PhoneSaved::NotSaved;
  catalogues->load(card);
  switch (catalogues->apply_form(body, len)) {
    case Catalogues::FormResult::Bad:
      return PhoneSaved::Bad;
    case Catalogues::FormResult::Full:
      return PhoneSaved::Full;
    case Catalogues::FormResult::Ok:
      break;
  }
  return catalogues->save(card) ? PhoneSaved::Ok : PhoneSaved::NotSaved;
}

}  // namespace vellum
