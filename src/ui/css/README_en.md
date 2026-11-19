# CSS engine (`XER::UI::CSS`)

[English](README_en.md) | [Русский](README_ru.md)

Chromium-style style calculation and invalidation engine. Packaged as
`YA.DesginCSS.dll`.

## Matching

- `css_selector.*` — selector parser + representation. Compounds are
  stored right-to-left. Supported: tag, `.class`, `#id`, `*` universal,
  `[attr]`, `[attr=value]` (quoted or bare), `:focus`, `:hover`,
  `:active`, `:first-child`, `:last-child`, `:nth-child(an+b)`
  (`odd`/`even`/`2n+1`/`-n+3`); descendant and `>` combinators;
  `ParseSelectorList` for comma-separated alternatives.
- `selector_checker.*` — recursive compound matching, walking the
  ancestor chain for descendant/child combinators.
- `element_rule_collector.*` — `CollectMatchingRulesForList`: iterates a
  `RuleData` list and records matched rules.
- `css_element.h` — DOM adapter interface (`CSSElement`): tag, classes,
  id, `AttributeValue`, `IsHovered`/`IsActive`/`HasFocus`,
  `NthChildIndex`/`SiblingCount`, parent traversal.
- `css_node.h` — concrete `CSSElement` (`CSSNode`) with style
  invalidation/recalc state bits and an attribute map.

## Rule storage and cascade

- `style_rule.h` — `StyleRule` (selector + declarations + origin).
- `rule_set.*` — `RuleSet`/`RuleData` (stored in `std::deque` for pointer
  stability). Indexed by class/id/tag/universal — the key comes from the
  **right-most compound**; pseudo-features fall back to the lowest index
  rank (like `:focus` in Blink). `CollectInvalidation` additionally
  builds an ancestor-class → `InvalidationSet` index (`.c1 div.c2` keys
  `c1` → self-features `div.c2`).
- `computed_style.h` — property/value map, typed accessors, and
  `AffectedByHover/Active/Focus` flags set by the resolver.
- `style_resolver.*` — cascade in ascending order: UA < user < author →
  specificity `(id, class, tag)` → source position.
- `style_engine.*` — `StyleEngine`: active stylesheets,
  `StyleForElement`, `InvalidationSetsForClassChange` — the DOM-mutation
  entry point.
- `stylesheet_parser.*` — `ParseStyleSheet(text)`: `/* */` comments,
  `sel { prop: val; }` blocks; comma lists expand into separate rules
  sharing the declarations.

## Invalidation

- `invalidation_set.*` — self-features (class/tag/id/focus/whole-subtree),
  `InvalidationSet::FromSelector`.
- `pending_invalidations.h` — `PendingInvalidationsMap`: schedule/take
  per-node invalidation sets before style reads.
- `style_invalidator.*` — `StyleInvalidator::Invalidate` — DFS: nodes
  matching self-features get `needs_recalc`; `child_needs_recalc`
  propagates upward.
- `tree_scope.h` — `TreeScope`/`ScopedStyleResolver`: resolver + active
  sheets + pending invalidations for a scope.
- `document_style.*` — `DocumentStyle::UpdateStyleAndLayoutTree`:
  **push pending invalidations → recalc in document order** (the Blink
  cycle).

## Tests

`tests/media_smoke.cc` (`XER_Media_Smoke.exe`) covers: `.c1 div.c2`
matching, specificity vs tag fallback, invalidation after an ancestor
class change (only the affected descendant recalcs), stylesheet parser,
universal/pseudo/attr/nth-child selectors — 21 checks total.
