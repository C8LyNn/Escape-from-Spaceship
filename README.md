# Escape from Spaceship

Projekt uczelniany Grupy 2. Gałąź `showcase/ue-5.8` zawiera grywalną scenę pokazową w **Unreal Engine 5.8**. Czysta baza wykładowcy w wersji 5.5 jest na gałęzi `main`.

## Pochodzenie bazy

Baza projektu to kopia edukacyjnego projektu wykładowcy
[`Mikoo2137/HorrorProject-5.5`](https://github.com/Mikoo2137/HorrorProject-5.5)
(commit `7975d17`, „Initial commit”). Oryginalne repozytorium jest tylko do odczytu
i nie było modyfikowane.

Różnice względem oryginału w czystej bazie:

- przeniesiono tylko `HorrorProject.uproject`, `Config/` i `Content/`
  (bez `Saved/`, `Intermediate/`, logów, zrzutów awarii i cache),
- z `Config/DefaultEngine.ini` usunięto wpis `SecurityToken` wtyczki Android File Server
  i wyłączono tę wtyczkę (dotyczy wyłącznie Androida, nie wpływa na PC),
- dodano `.gitignore` i `.gitattributes`.

## Uruchomienie

Otwórz `HorrorProject.uproject` w Unreal Engine 5.8.4 i naciśnij **Play**. Mapa startowa `/Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase` ma sześć działów galerii na wprost i czternaście stanowisk testowych za miejscem startu. Turkusowa linia prowadzi przez galerię, bursztynowe oznaczenia prowadzą przez stanowiska. Oryginalną mapę ćwiczeń można otworzyć pod `/Game/Maps/Level1`.

Ruch obsługują WASD i mysz. Shift służy do sprintu, Ctrl do kucania, E do interakcji, F do latarki, a Tab do ekwipunku. Przy drzwiach fizycznych przytrzymaj lewy przycisk myszy i przesuń mysz. Kod do stanowiska z zamkiem to **9516**.

## Dokumentacja sceny pokazowej

Wyniki testów PIE, zrzuty ekranu i wykaz braków zawiera [raport](Docs/StudentShowcase/RESULTS.md). Autorów i licencje pobranych modeli podają [atrybucje](Docs/StudentShowcase/CREDITS.md).
