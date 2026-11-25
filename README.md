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

Po zaakceptowaniu zaproszenia do prywatnego repozytorium pobierz gałąź ze sceną testową.

```sh
git clone --single-branch --branch gameplay/ldd-ue-5.8 https://github.com/C8LyNn/Escape-from-Spaceship.git
```

Jeśli masz już kopię projektu, przełącz się na gałąź ze sceną w terminalu w folderze tej kopii.

```sh
git remote set-branches --add origin gameplay/ldd-ue-5.8
git fetch origin
git switch gameplay/ldd-ue-5.8
git pull --ff-only origin gameplay/ldd-ue-5.8
```

Po pierwszym pobraniu projektu zezwól edytorowi na zbudowanie modułu `HorrorProject`. Na Windows kompilacja wymaga narzędzi C++ dla Unreal Engine w Visual Studio i zestawu Windows SDK.

Otwórz `HorrorProject.uproject` w Unreal Engine 5.8.4 i naciśnij **Play**. Mapa startowa `/Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase` ma sześć działów galerii na wprost i czternaście stanowisk testowych za miejscem startu. Turkusowa linia prowadzi przez galerię, bursztynowe oznaczenia prowadzą przez stanowiska. Oryginalną mapę ćwiczeń można otworzyć pod `/Game/Maps/Level1`.

Ruch obsługują WASD i mysz. Shift służy do sprintu, Ctrl do kucania, E do interakcji, F do latarki, a Tab do ekwipunku. Przy drzwiach fizycznych przytrzymaj lewy przycisk myszy i przesuń mysz. Kod do stanowiska z zamkiem to **9516**.

## Stanowiska mechanik z LDD

Od startu idź za tablicą **STANOWISKA LDD** do stołu. Zabierz **E** latarkę z filtrami i akumulator. Otwórz plecak pod **Tab** i kliknij latarkę, następnie włącz ją klawiszem **F**. Na tablicy **CZTERY FILTRY** każdy kolor odsłania pozycję i cyfrę. Przesuwaj wiązkę po napisie, **Q** przechodź do następnego koloru. Wpisz cyfry od pozycji 1 do 4 na pobliskim terminalu i naciśnij **Enter**.

| Klawisz | Działanie |
| --- | --- |
| **Tab / 1–8** | Otwórz plecak i kliknij przedmiot lub wybierz numer jego pola. Wybrany przedmiot trafi do ręki, a poprzedni zajmie zwolnione pole. Numer pustego pola chowa trzymany przedmiot. |
| **F** | Włącz lub zgaś latarkę. |
| **Q / kółko myszy** | Q wybiera kolejny kolor. Kółkiem możesz przechodzić w obie strony między czerwonym, zielonym, niebieskim i fioletowym. |
| **Z / X** | Zmniejsz lub zwiększ moc światła. |
| **C / V** | Zmniejsz lub zwiększ promień. Zmienia się szerokość stożka i jego zasięg. |
| **E** | Podnieś narzędzie lub notatkę, użyj ładowarki albo terminala. Ze spawarką WD01 w ręce przytrzymaj E przy spoinie. |
| **LPM** | Rzuć trzymany glowstick, przeczytaj trzymaną notatkę lub użyj bandaża albo zapasowej baterii osobistej. Skrzynię możesz zawsze chwycić, przytrzymując E. |
| **Shift + klik w plecaku** | Otwórz opcje przedmiotu z paczki, np. użycie lub odłożenie stosu na podłogę. |

Moc i promień mają po pięć ustawień. Obie latarki zachowują wybrane ustawienia przy zmianie koloru lub narzędzia. Większa moc i większy promień niezależnie przyspieszają rozładowanie baterii. Plecak pod **Tab** pokazuje bieżący pobór. Zgaszona latarka nie pobiera energii.

Plecak pod **Tab** ma osiem pól wspólnych dla sprzętu, notatek, przedmiotów z paczki i pojemnika na glowsticki. Do naprawy zabierz spawarkę WD01 i akumulator, wybierz spawarkę w plecaku i przytrzymaj **E** przy drzwiach. Zebrane notatki są stronami jednego notesu. Z notesem w ręce **LPM** otwiera tekst, **← / →** zmienia stronę, **Tab / Esc** zamyka czytanie.

Pojemnik mieści cztery glowsticki. Każdy rzucony **LPM** odbija się, osiada i oświetla otoczenie przez 45 sekund. Co 12 sekund odnawia się jedna sztuka, także po schowaniu pojemnika. [Test plecaka, glowsticków i zagadek](Tests/Gameplay/test_shared_backpack.py)

Przy pozostałych stanowiskach możesz sprawdzić naprawę drzwi, zamek szyfrowy, dwie płyty obciążane skrzyniami, osobne rzędy płyt **A → B → C** oraz **A → B → C → D**, dźwięki za drzwiami, śmiertelną pułapkę oraz wyjście odblokowywane kartą i kodem z notatki. **R** po śmierci lub udanej próbie uruchamia scenę od nowa.

**Drabina serwisowa** stoi przy stanowisku wyposażenia. Podejdź do dolnych szczebli i naciśnij **E**. Przytrzymaj **W**, aby wejść na podest na wysokości 2,8 m. Przy krawędzi podestu **E** pozwala ponownie chwycić drabinę, a **S** zejść. W połowie drogi możesz puścić klawisz ruchu, rozejrzeć się, użyć latarki, puścić drabinę przez **E** albo odskoczyć **Spacją**. Zajęty podest wstrzymuje wejście na górę. Do kolejnego stanowiska możesz skopiować aktora **Ship Ladder**, ustawić wysokość i prędkość wspinania i umieścić podest za jego górną krawędzią.

**Podczas gry** trzy paski w lewym dolnym rogu pokazują zdrowie, wytrzymałość i baterię. Wypełnienie paska baterii ma kolor wybranego filtra, także po zgaszeniu latarki. Długość wypełnienia odpowiada ładunkowi używanego zestawu. Klawiatura numeryczna pokazuje liczbę wpisanych cyfr. Krótki kod można dokończyć, a po błędnym pełnym kodzie można od razu wpisać następny. Przy spawaniu widać pozostały czas, a przy płytach liczbę zaliczonych stanowisk w grupie. Zamykające się drzwi przesuwne zatrzymują się przed graczem lub skrzynią i ruszają dalej po zwolnieniu przejścia.

**Ekwipunek z paczki prowadzącego.** W menu pod **Tab** można odłożyć bandaż, baterię lub klucz na podłogę i ponownie podnieść przedmiot przez **E**. Stos zachowuje liczbę sztuk. Gdy zabraknie miejsca w plecaku, reszta zostaje na podłodze. Drzwi na klucz pobierają jedną sztukę przy pierwszym odblokowaniu. Można je potem zamykać i otwierać, zachowując pozostałe klucze.

**Zdrowie postaci.** W bezpiecznym miejscu stan zdrowia jest stały. Pole obrażeń na stanowisku **MECH-11** pozwala sprawdzić obrażenia i leczenie bandażem. Jeśli chcecie wykorzystać odliczanie zdrowia w innym prototypie, w ustawieniach postaci **L1_Character** włączcie **Enable Overtime Damage**. Postać będzie wtedy dodatkowo traciła 0,1 punktu zdrowia na sekundę.

**Kłódka z czterema tarczami** na stanowisku **MECH-08** zapamiętuje poprawny kod od razu po ostatnim obrocie. Strzałki znikają, rygiel się wysuwa, a po animacji wraca sterowanie postacią. Podczas wpisywania kodu **Backspace** oddaje kamerę graczowi i pozwala później wrócić do próby.

W testach rozgrywki w UE 5.8.4 przeszło 58 sprawdzeń mechanik, 5 sprawdzeń zakończenia i restartu oraz wszystkie 14 stanowisk bazowych. Zmierzony pobór latarki wyniósł od 0,16% do 1,46% pełnej baterii na sekundę, zależnie od ustawień. [Cztery kolory na tej samej tablicy w grze](Docs/StudentShowcase/Kolory-latarki.jpg).

## Opcjonalne mechaniki światła

Za stołem z wyposażeniem idź za drogowskazem **Stanowiska optyczne**. Zabierz obie latarki i baterię. Stanowiska możesz wypróbować w dowolnej kolejności.

| Stanowisko | Próba |
| --- | --- |
| Zatrzask świetlny | Utrzymaj czerwone światło o mocy co najmniej 3/5 przez 1,6 s. Przerwanie wiązki zeruje postęp. Po otwarciu użyj **E**, żeby powtórzyć próbę. |
| Migawka | Niebieska wiązka trzyma przejście otwarte. Po odwróceniu latarki masz jeszcze 1,2 s na przejście. |
| Zamek sekwencyjny | Oświetl odbiornik kolejno na czerwono, niebiesko i fioletowo. Panel odlicza 6 s między krokami. Możesz odwrócić lub zgasić latarkę na czas zmiany koloru. Przy częściowo wpisanej sekwencji **E** zaczyna próbę od nowa. |
| Farba z poświatą | Fioletowy (UV) ujawnia napis. Po zabraniu wiązki pozostaje czytelny i stopniowo gaśnie przez 5 s. |
| Czujnik ciemności | Podejdź i zostaw czujnik w ciemności przez 2 s. Skierowanie na niego światła dowolnej barwy albo odejście zamyka przejście. |
| Przełącznik dwukolorowy | Zielony otwiera drzwi, czerwony je zamyka. Drzwi pamiętają położenie po zabraniu światła. |
| Alarm światła i ruchu | Z bliska skieruj na czujnik dowolny kolor o mocy co najmniej 4/5 albo przebiegnij obok. Alarm zamknie drzwi. Po 3 s bez bodźca znów je otworzy. |
| Dynamo awaryjne | Przytrzymaj **E**, żeby ręcznie doładować nawet pustą baterię do 35%. Zielone światło na 5 s zwiększa tempo ładowania z 2% do 8% na sekundę. Możesz wtedy zgasić latarkę i kręcić dalej. |

**Własna konfiguracja.** Każda próba ma osobny folder w Outlinerze pod `Mechaniki LDD/Optyka opcjonalna`. Możesz skopiować wybrane stanowisko, pominąć je albo wyłączyć odbiornik w Details przez **Station Enabled**. Przy kopiowaniu ustaw nową, jednakową wartość **Channel** na odbiorniku i jego drzwiach. Do samodzielnego stanowiska dodaj aktora **Ship Light Station**, wybierz **Mode** i ustaw kolor lub czasy reakcji. Obsługę latarek zapewnia **Ship Survivor** obecny na scenie. Żaden z tych kanałów nie jest wymagany przez wyjście.

Testy rozgrywki w UE 5.8.4 objęły 76 sprawdzeń nowych stanowisk i ich współpracy z dotychczasową sceną. Sprawdzono m.in. przejście przez pierwotne wyjście przed rozwiązaniem zagadek optycznych, fizyczny ruch drzwi, zanikanie napisu i tempo ładowania po puszczeniu klawisza.

W kolejnej serii PIE przeszło 31 prób drabiny, 31 prób podnoszenia, upuszczania i używania kluczy oraz wszystkie 14 stanowisk z paczki prowadzącego. Próby objęły wejście i zejście oryginalną postacią, zablokowany podest, restart, upuszczenie przedmiotu tuż przy stole i ponowne zebranie go przez **E**. Przy pełnym zdrowiu lub naładowaniu oryginalne menu zachowało obie sztuki bandaża lub baterii, a uzupełnienie zasobu zużyło jedną. W teście kłódki uruchomionym w tle naciśnięcia strzałek wywołano przez zdarzenia widocznych przycisków UMG. Sprawdzono obrót tarcz, ruch rygla, blokadę ponownej aktywacji i powrót sterowania.

## Dokumentacja sceny pokazowej

Wyniki testów PIE, zrzuty ekranu i wykaz braków zawiera [raport](Docs/StudentShowcase/RESULTS.md). Autorów i licencje pobranych modeli podają [atrybucje](Docs/StudentShowcase/CREDITS.md).
