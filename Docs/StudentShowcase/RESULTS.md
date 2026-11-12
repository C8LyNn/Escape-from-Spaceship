# RESULTS — scena pokazowa modeli i mechanik

Stan na 2026-11-12. **Zadanie nie jest ukończone.** Scena `L_ModelsAndMechanics_Showcase`
nie istnieje, żaden model nie został zaimportowany i żadna mechanika nie była testowana w PIE.
Powód: na komputerze nie ma Unreal Engine 5.5 (jest tylko 5.7.1), a konwersja projektu do 5.7
nie została dozwolona.

## Co jest zrobione

| Element | Stan |
|---|---|
| Czysta kopia bazy w `C8LyNn/Escape-from-Spaceship` (gałąź `main`) | ZROBIONE |
| Repozytorium wykładowcy `Mikoo2137/HorrorProject-5.5` | NIETKNIĘTE (tylko odczyt, commit `7975d17`) |
| Usunięcie tokena Android File Server z kopii | ZROBIONE |
| Porównanie wersji LDD | ZROBIONE |
| Weryfikacja licencji 46 odnośników | ZROBIONE |
| Inwentaryzacja modeli już obecnych w projekcie | ZROBIONE (wg nazw plików, bez podglądu w edytorze) |
| Statyczna kontrola Blueprintów mechanik | ZROBIONE (pliki istnieją, brak zgubionych odwołań `/Game/`) |
| Otwarcie projektu w UE 5.5 i test `Level1` | NIEZROBIONE — brak UE 5.5 |
| Pobranie i import modeli | NIEZROBIONE — brak logowania do Sketchfab, brak UE 5.5 |
| Nowa mapa, galeria, strefa mechanik | NIEZROBIONE — brak UE 5.5 |
| Test PIE, zrzuty ekranu | NIEZROBIONE — brak UE 5.5 |

## Blokery

1. **Brak Unreal Engine 5.5.** Epic Games Launcher ma zainstalowany tylko UE 5.7.1.
2. **Brak miejsca na dysku C:.** Wolne jest ok. 16 GB z 1,9 TB; instalacja UE 5.5 wymaga
   kilkudziesięciu GB.
3. **Brak logowania do Sketchfab.** Pobieranie modeli wymaga zalogowanego konta.

## Level Design Document

- `LEVEL_DESIGN_DOCUMENT_Grupa_2 (1).pdf` z Pobranych i PDF z paczki to ten sam plik
  (identyczna suma SHA-256).
- `LEVEL_DESIGN_DOCUMENT_Grupa_2 (1).docx` ma tę samą treść co PDF; różnice to wyłącznie
  łamanie wierszy w adresach. Oba zawierają te same 46 odnośników co `01_REFERENCJE_MODELI.csv`.

## Modele — liczby

| Miara | Liczba |
|---|---|
| Odnośniki z dokumentu | 46 |
| Licencja sprawdzona | 46 |
| Dozwolone do użycia | 44 |
| Zablokowane | 2 |
| Pobrane | 0 |
| Zaimportowane | 0 |
| Umieszczone w scenie | 0 |

Licencje 44 dozwolonych: 37 × CC BY 4.0, 2 × CC BY-NC 4.0, 1 × CC BY-SA 4.0,
4 × Sketchfab Free Standard. Wszystkie CC wymagają atrybucji autora (kolumna w CSV).

Zablokowane:

- **SRC-20** Industrial Exhaust Fans and vents pack — model płatny, niepobieralny bez zakupu.
- **SRC-21** Poppy Playtime Chapter 2 Walkables Vent — zawartość z komercyjnej gry;
  oznaczenie CC BY przez wgrywającego nie daje pewności praw.

Do potwierdzenia po pobraniu: SRC-40 „Sing” (prawdopodobnie „Sink”, umywalka),
skład pakietów SRC-09 (50+ mebli) i SRC-23 (modularne wnętrze statku).

## Typy elementów z dokumentu (75)

| Stan | Liczba |
|---|---|
| Jest kandydat w projekcie (do ponownego użycia) | 26 |
| Tylko odnośnik, model niepobrany | 25 |
| Brak modelu i brak odnośnika | 24 |

Typy bez modelu i bez odnośnika:

- Oświetlenie: włączniki światła, uszkodzone lampy, panele oświetleniowe, sygnalizatory stanu,
  wskaźniki sygnałowe.
- Konstrukcja statku: znaki ewakuacyjne, korytka kablowe, narożniki korytek kablowych,
  rozdzielacze chłodziwa, rampy techniczne, barierki.
- Drzwi i interakcje: drzwi przesuwne, przyciski, dźwignie, bezpieczniki.
- Zagrożenia i zniszczenia: dziury w podłodze, przepaście, mechanizmy zgniatające, plamy krwi,
  ślady pazurów.
- Wyposażenie statku: beczki, skrzynki narzędziowe.
- Przedmioty i dekoracje: butelki, zdjęcia załogi.

Cała kategoria „Zagrożenia i zniszczenia” nie ma w dokumencie ani jednego odnośnika.

## Mechaniki (14)

| Wynik | Liczba |
|---|---|
| PASS | 0 |
| FAIL | 0 |
| NOT TESTED | 14 |

Wszystkie Blueprinty wskazane w `03_TESTY_MECHANIK.csv` istnieją w projekcie.
Ustalenia ze statycznej analizy plików (do potwierdzenia w edytorze):

- `BP_PickupActor_Flashlight` to pickup **baterii** (mesh `Battery`), nie latarki.
  Latarka jest komponentem `BPC_Flashlight` i nie ma modelu 3D.
- `BP_PhysicsDoor` i `BP_Interaction_LightSwitch` są użyte tylko w `Level1.umap`.
- `DefaultEngine.ini` wskazuje `GlobalDefaultGameMode` i `GameInstanceClass` pod ścieżkami,
  pod którymi nie ma plików (`/Game/Blueprints/Level1/L1_GameMode`,
  `/Game/Blueprints/HG_GameInstance`; pliki leżą w `/Game/Blueprints/Game/...`).
  `Level1` odwołuje się do `L1_GameMode` bezpośrednio, więc nowa mapa będzie potrzebować
  ustawienia GameMode Override w World Settings.

## Pliki

- `01_REFERENCJE_MODELI.csv` — 46 odnośników: autor, licencja, statusy.
- `02_KATEGORIE_Z_DOKUMENTU.csv` — 75 typów: odnośniki, kandydaci w projekcie, stan.
- `03_TESTY_MECHANIK.csv` — 14 mechanik: obecność Blueprintów, stan testu.
- `04_ISTNIEJACE_MODELE_W_PROJEKCIE.csv` — 114 modeli obecnych w bazie.
