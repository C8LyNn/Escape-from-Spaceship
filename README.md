# Escape from Spaceship

Projekt uczelniany Grupy 2. Gałąź `gameplay/ldd-ue-5.8` zawiera mechaniki z LDD na istniejącej scenie testowej w **Unreal Engine 5.8.4**. Wcześniejsza wersja sceny jest na gałęzi `showcase/ue-5.8`, a baza wykładowcy w wersji 5.5 na `main`.

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

Po pierwszym pobraniu projektu zezwól edytorowi na zbudowanie modułu `HorrorProject`. Na Windows kompilacja wymaga narzędzi C++ dla Unreal Engine w Visual Studio i zestawu Windows SDK.

Otwórz `HorrorProject.uproject` w Unreal Engine 5.8.4 i naciśnij **Play**. Mapa startowa `/Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase` ma sześć działów galerii na wprost i czternaście stanowisk testowych za miejscem startu. Turkusowa linia prowadzi przez galerię, bursztynowe oznaczenia prowadzą przez stanowiska. Oryginalną mapę ćwiczeń można otworzyć pod `/Game/Maps/Level1`.

Ruch obsługują WASD i mysz. Shift służy do sprintu, Ctrl do kucania, E do interakcji, F do latarki, a Tab do ekwipunku. Przy drzwiach fizycznych przytrzymaj lewy przycisk myszy i przesuń mysz. Kod do stanowiska z zamkiem to **9516**.

## Stanowiska mechanik z LDD

Od miejsca startu idź w lewo za tablicą **STANOWISKA LDD** do stołu ze sprzętem. Weź białą latarkę, latarkę z kolorami i wspólną baterię, celując w każdy przedmiot i naciskając **E**. Podejdź do tablicy **CZTERY FILTRY**. Każda z czterech barw odsłania w tym samym miejscu inny znak farby i cyfrę kodu. Ustawienie promienia decyduje także o tym, jak daleko i jak szeroko od środka wiązki można ujawnić ślad.

| Klawisz | Działanie |
| --- | --- |
| **1 / 2 / 3** | Wybierz białą latarkę, latarkę z kolorami lub spawarkę. |
| **F** | Włącz lub zgaś latarkę. |
| **Q** | Przełącz kolor w pętli czerwony → zielony → niebieski → fioletowy (UV). |
| **Z / X** | Zmniejsz lub zwiększ moc światła. |
| **C / V** | Zmniejsz lub zwiększ promień. Zmienia się szerokość stożka i jego zasięg. |
| **E** | Podnieś przedmiot, przeczytaj notatkę, użyj ładowarki lub terminala. Przytrzymaj przy uszkodzonych drzwiach z wybraną spawarką. |
| **LPM** | Przytrzymaj, by chwycić skrzynię. Puść, by odstawić ją na płytę. |
| **I / G** | Otwórz wyposażenie i dziennik / przełącz zestaw testowy. |

Moc i promień mają po pięć ustawień. Obie latarki zachowują wybrane ustawienia przy zmianie koloru lub narzędzia. Większa moc i większy promień niezależnie przyspieszają rozładowanie baterii. Panel pokazuje bieżący pobór. Zgaszona latarka nie pobiera energii.

Przy pozostałych stanowiskach możesz sprawdzić naprawę drzwi, zamek szyfrowy, dwie płyty obciążane skrzyniami, sekwencje trzech i czterech płyt, dźwięki za drzwiami, śmiertelną pułapkę oraz wyjście odblokowywane kartą i kodem z notatki. **R** po śmierci lub udanej próbie uruchamia scenę od nowa.

W testach rozgrywki w UE 5.8.4 przeszło 58 sprawdzeń mechanik, 5 sprawdzeń zakończenia i restartu oraz wszystkie 14 stanowisk bazowych. Zmierzony pobór latarki wyniósł od 0,16% do 1,46% pełnej baterii na sekundę, zależnie od ustawień. [Cztery kolory na tej samej tablicy w grze](Docs/StudentShowcase/Kolory-latarki.jpg).

## Opcjonalne mechaniki światła

Za stołem z wyposażeniem idź za drogowskazem **Stanowiska optyczne**. Zabierz obie latarki i baterię. Stanowiska możesz wypróbować w dowolnej kolejności.

| Stanowisko | Próba |
| --- | --- |
| Zatrzask świetlny | Utrzymaj czerwone światło o mocy co najmniej 3/5 przez 1,6 s. Przerwanie wiązki zeruje postęp. Po otwarciu użyj **E**, żeby powtórzyć próbę. |
| Migawka | Niebieska wiązka trzyma przejście otwarte. Po odwróceniu latarki masz jeszcze 1,2 s na przejście. |
| Zamek sekwencyjny | Oświetl odbiornik kolejno na czerwono, niebiesko i fioletowo. Między krokami masz 6 s. Możesz odwrócić lub zgasić latarkę na czas zmiany koloru. |
| Farba z poświatą | Fioletowy (UV) ujawnia napis. Po zabraniu wiązki pozostaje czytelny i stopniowo gaśnie przez 5 s. |
| Czujnik ciemności | Podejdź i zostaw czujnik w ciemności przez 2 s. Skierowanie na niego światła dowolnej barwy albo odejście zamyka przejście. |
| Przełącznik dwukolorowy | Zielony otwiera drzwi, czerwony je zamyka. Drzwi pamiętają położenie po zabraniu światła. |
| Alarm światła i ruchu | Z bliska skieruj na czujnik dowolny kolor o mocy co najmniej 4/5 albo przebiegnij obok. Alarm zamknie drzwi. Po 3 s bez bodźca znów je otworzy. |
| Dynamo awaryjne | Przytrzymaj **E**, żeby ręcznie doładować nawet pustą baterię do 35%. Zielone światło na 5 s zwiększa tempo ładowania z 2% do 8% na sekundę. Możesz wtedy zgasić latarkę i kręcić dalej. |

**Własna konfiguracja.** Każda próba ma osobny folder w Outlinerze pod `Mechaniki LDD/Optyka opcjonalna`. Możesz skopiować wybrane stanowisko, pominąć je albo wyłączyć odbiornik w Details przez **Station Enabled**. Przy kopiowaniu ustaw nową, jednakową wartość **Channel** na odbiorniku i jego drzwiach. Do samodzielnego stanowiska dodaj aktora **Ship Light Station**, wybierz **Mode** i ustaw kolor lub czasy reakcji. Obsługę latarek zapewnia **Ship Survivor** obecny na scenie. Żaden z tych kanałów nie jest wymagany przez wyjście.

Testy rozgrywki w UE 5.8.4 objęły 76 sprawdzeń nowych stanowisk i ich współpracy z dotychczasową sceną. Sprawdzono m.in. przejście przez pierwotne wyjście przed rozwiązaniem zagadek optycznych, fizyczny ruch drzwi, zanikanie napisu i tempo ładowania po puszczeniu klawisza.

## Dokumentacja sceny pokazowej

Wyniki testów PIE, zrzuty ekranu i wykaz braków zawiera [raport](Docs/StudentShowcase/RESULTS.md). Autorów i licencje pobranych modeli podają [atrybucje](Docs/StudentShowcase/CREDITS.md).
