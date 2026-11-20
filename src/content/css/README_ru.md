# CSS-движок (`XER::UI::CSS`)

[English](README_en.md) | [Русский](README_ru.md)

Движок расчёта и инвалидации стилей в стиле Chromium. Упакован как
`YA.DesginCSS.dll`.

## Сопоставление

- `css_selector.*` — парсер и представление селекторов. Компаунды
  хранятся справа налево. Поддерживается: tag, `.class`, `#id`,
  универсальный `*`, `[attr]`, `[attr=value]` (в кавычках или без),
  `:focus`, `:hover`, `:active`, `:first-child`, `:last-child`,
  `:nth-child(an+b)` (`odd`/`even`/`2n+1`/`-n+3`); комбинаторы
  descendant и `>`; `ParseSelectorList` для альтернатив через запятую.
- `selector_checker.*` — рекурсивное совпадение компаундов с подъёмом по
  цепочке предков для комбинаторов descendant/child.
- `element_rule_collector.*` — `CollectMatchingRulesForList`: обходит
  список `RuleData` и записывает совпавшие правила.
- `css_element.h` — интерфейс-адаптер DOM (`CSSElement`): tag, классы,
  id, `AttributeValue`, `IsHovered`/`IsActive`/`HasFocus`,
  `NthChildIndex`/`SiblingCount`, обход родителя.
- `css_node.h` — конкретный `CSSElement` (`CSSNode`) с битами состояния
  инвалидации/пересчёта стилей и картой атрибутов.

## Хранение правил и каскад

- `style_rule.h` — `StyleRule` (селектор + декларации + origin).
- `rule_set.*` — `RuleSet`/`RuleData` (хранится в `std::deque` ради
  стабильности указателей). Индексация по class/id/tag/universal — ключ
  берётся из **right-most компаунда**; псевдо-фичи получают самый
  низкий ранг индекса (как `:focus` в Blink). `CollectInvalidation`
  дополнительно строит индекс ancestor-класс → `InvalidationSet`
  (`.c1 div.c2` → ключ `c1` → self-фичи `div.c2`).
- `computed_style.h` — карта свойство/значение, типизированные
  акцессоры и флаги `AffectedByHover/Active/Focus`, выставляемые
  резолвером.
- `style_resolver.*` — каскад по возрастанию: UA < user < author →
  специфичность `(id, class, tag)` → позиция в источнике.
- `style_engine.*` — `StyleEngine`: активные таблицы стилей,
  `StyleForElement`, `InvalidationSetsForClassChange` — точка входа
  для мутаций DOM.
- `stylesheet_parser.*` — `ParseStyleSheet(text)`: комментарии `/* */`,
  блоки `sel { prop: val; }`; списки через запятую раскрываются в
  отдельные правила с общими декларациями.

## Инвалидация

- `invalidation_set.*` — self-фичи (class/tag/id/focus/whole-subtree),
  `InvalidationSet::FromSelector`.
- `pending_invalidations.h` — `PendingInvalidationsMap`:
  накопление/изъятие наборов инвалидации по узлам до чтения стилей.
- `style_invalidator.*` — `StyleInvalidator::Invalidate` — DFS: узлы,
  совпадающие с self-фичами, получают `needs_recalc`;
  `child_needs_recalc` распространяется вверх.
- `tree_scope.h` — `TreeScope`/`ScopedStyleResolver`: резолвер +
  активные таблицы + отложенные инвалидации скоупа.
- `document_style.*` — `DocumentStyle::UpdateStyleAndLayoutTree`:
  **push отложенных инвалидаций → пересчёт в порядке документа**
  (цикл Blink).

## Тесты

`tests/media_smoke.cc` (`XER_Media_Smoke.exe`) покрывает: матчинг
`.c1 div.c2`, специфичность против tag-fallback, инвалидацию после
смены ancestor-класса (пересчитывается только затронутый потомок),
парсер stylesheet, универсальные/псевдо/attr/nth-child селекторы —
всего 21 проверка.
