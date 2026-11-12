# Escape from Spaceship

Projekt uczelniany (Grupa 2) w Unreal Engine **5.5**, Blueprint-only.

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

Otwórz `HorrorProject.uproject` w Unreal Engine 5.5. Nie konwertuj projektu do nowszej
wersji silnika bez uzgodnienia w grupie.

## Dokumentacja sceny pokazowej

Stan prac, manifest modeli i wyniki testów mechanik: `Docs/StudentShowcase/`.
