# Pioneer Production Planner 1.7.0

## Deutsch

### Neu
- **Nebenprodukte verwerten:** Freie Überschüsse des aktuellen Plans können über passende freigeschaltete Rezepte weiterverarbeitet werden. Vorschläge zeigen die zusätzliche Produktion; eine optionale Vorschau berechnet Maschinen, zusätzliche Rohstoffe und Leistungsbedarf.
- **Vorschläge direkt im Graphen:** Nebenprodukt anklicken und passende Rezepte auswählen. Gestrichelte Vorschlagskarten unterscheiden Vorschläge vom aktiven Plan. „Produkt hinzufügen“ übernimmt die Verarbeitung direkt.
- **Nebenprodukte für Strom nutzen:** Bei passenden Brennstoffen führt „Strom hinzufügen“ die Planung über die Brennstoffproduktion bis zum Generator, beispielsweise Schwerölrückstand → Treibstoff → Strom. Eigenverbrauch und zusätzliche Lieferketten werden berücksichtigt.
- **Getrennte Verarbeitungsbereiche:** Hinzugefügte Zweige stehen rechts in eigenen beschrifteten Rahmen. Nach dem Hinzufügen aus dem Graph-Menü wird der neue Bereich angezeigt. Gruppierung und erweiterter Graph bleiben beim Speichern und Laden erhalten.
- **Tab 7 „Nebenprodukte“:** Alternative Listenansicht mit Suche nach Produkt, Rezept oder Zutat und manueller Seitenauswahl.
- **Pipeline-Auswahl:** Unter „Weitere Einstellungen“ stehen automatische Auswahl und konkrete verfügbare Rohrstufen zur Verfügung. Mod-Rohre wie Pioneer Pipelines Mk.5 mit 1200 m³/min werden über ihre Laufzeitdaten berücksichtigt. Die Auswahl gilt auch für Strompläne und Nebenprodukt-Verarbeitung.

### Korrekturen
- Erkennung der Pioneer-Pipelines-Ölförderpumpe 1200 ergänzt; Fördermengen berücksichtigen Gebäudedaten, Reinheit und Takt.
- Bei begrenztem Materialangebot wird auch eine kleinere machbare Verwertungsmenge geprüft. Vorhandene Produktion wird nicht vergrößert, um mehr Nebenprodukt zu erzeugen.
- Überschüsse, gemeinsame Eingangslimits und begrenzte Rohstoffquellen werden vor der Übernahme erneut geprüft. Bestehender Baufortschritt bleibt erhalten.
- Slate-Buildfehler des Graph-Menüs behoben und veraltete Mausfreigabe ersetzt.
- Neue Oberfläche auf Deutsch und Englisch.

### Hinweise
- Eine ausdrückliche Neuberechnung ersetzt hinzugefügte Verwertungszweige. Speichern und Laden erhält sie.
- Ältere Pläne bleiben ladbar. Bereits vor R3 gespeicherte Erweiterungen besitzen keine Gruppenzuordnung; für die getrennten Bereiche einmal neu berechnen und die Verarbeitung erneut hinzufügen.
- Die Stromsuche prüft bis zu acht konstante, nichtmodulare Generatorvarianten bei 100 % Takt. Sie liefert einen machbaren Vorschlag, kein garantiertes globales Maximum.
- Client und Multiplayer-Server benötigen beide **1.7.0**.

## English

### New
- **By-product processing:** Use unlocked recipes to process unused surplus from the current plan. Suggestions describe additional production; optional previews calculate machines, extra resources and power demand.
- **Suggestions directly in the graph:** Click a by-product to choose matching recipes. Dashed suggestion cards distinguish proposals from the active plan. “Add product” applies processing directly.
- **Generate power from by-products:** For compatible fuels, “Add power” plans the chain through fuel production to generators, for example Heavy Oil Residue → Fuel → Power. Self-consumption and additional supply chains are included.
- **Separate processing regions:** Added branches appear in their own labelled frames on the right. Adding from the graph menu focuses the new region. Saved plans retain the grouping and expanded graph.
- **Tab 7 “By-products”:** Alternative list view with product, recipe or ingredient search and manual pagination.
- **Pipeline selection:** “More settings” offers automatic selection and specific available pipe tiers. Modded pipes, including Pioneer Pipelines Mk.5 at 1200 m³/min, are recognized through runtime data. The selection also applies to power plans and by-product processing.

### Fixes
- Added recognition for Pioneer Pipelines Oil Extractor 1200, respecting building data, purity and clock speed.
- Processing also searches for a smaller feasible amount when materials are limited. Existing production is not expanded to create additional by-products.
- Surplus, shared input budgets and limited extraction capacities are checked again before applying an extension. Existing build progress is retained.
- Fixed the graph menu's Slate build error and replaced deprecated pointer capture handling.
- German and English interface for the new features.

### Notes
- Explicit recalculation replaces added processing branches. Saving and loading preserves them.
- Older plans remain loadable. Extensions saved before R3 have no grouping metadata; recalculate once and add processing again to obtain separate regions.
- Power search considers up to eight steady, nonmodular generator variants at 100% clock. It produces a feasible suggestion, not a guaranteed global maximum.
- Client and multiplayer server must both use **1.7.0**.
