#include "ui_metrics.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace penchat;
int main() {
  const UiMetrics identity;
  for (int v : {-123, -2, 0, 1, 21, 44, 960}) assert(identity.dp(v) == v);
  assert(identity.dp(std::numeric_limits<int>::max()) == std::numeric_limits<int>::max());
  assert(identity.dp(std::numeric_limits<int>::min()) == std::numeric_limits<int>::min());
  const UiFont roles[] = {UiFont::Body, UiFont::Button, UiFont::PageTitle, UiFont::FieldTitle,
                         UiFont::FieldValue, UiFont::SettingsTitle, UiFont::Detail};
  const int sizes[] = {21, 20, 26, 15, 19, 20, 16};
  for (int i = 0; i < 7; ++i) assert(identity.fontSize(roles[i]) == sizes[i]);
  for (int percent : {75, 100, 125, 150}) {
    const UiMetrics m(100, percent);
    assert(m.dp(44) == (44 * percent + 50) / 100);
    assert(m.dp(-44) == -m.dp(44));
    assert(m.dp(0) == 0);
    assert(m.button().captionPadding == (24 * percent + 50) / 100);
    assert(m.field().height == (48 * percent + 50) / 100);
    assert(m.page().headerHeight == m.field().height);
    assert(m.settings().height == (56 * percent + 50) / 100);
    assert(m.fontSize(UiFont::Button) == (20 * percent + 50) / 100);
  }
  assert(UiMetrics(125, 125).dp(1) == 2); // round only after composing both factors
  assert(UiMetrics(100, 150).dp(-1) == -2);
  assert(UiMetrics(1, 1).fontSize(UiFont::Body) == 1);
  bool invalid = false, overflow = false;
  try { UiMetrics m(0); } catch (const std::invalid_argument &) { invalid = true; }
  try { UiMetrics(1000, 1000).dp(std::numeric_limits<int>::max()); }
  catch (const std::overflow_error &) { overflow = true; }
  assert(invalid && overflow);
  std::cout << "UI_METRICS_IDENTITY_ROUNDING_ROLES_OK\n";
}
