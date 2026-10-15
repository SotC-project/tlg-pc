// SPDX-License-Identifier: GPL-2.0-or-later

// The translations below are machine-made and have not been checked by native readers.

#include <string_view>
#include <unordered_map>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "core/i3/lang.h"

namespace I3::Lang {

namespace {

// The languages the game ships besides English, in the order of the columns below.
enum Column : int {
    French,
    Spanish,
    German,
    Italian,
    Dutch,
    Portuguese,
    Russian,
    Finnish,
    Swedish,
    Danish,
    Norwegian,
    Polish,
    Turkish,
    NumColumns,
};

struct Entry {
    const char* english;
    const char* text[NumColumns];
};

// Labels that the menu keeps in capitals for matching ("CONTINUE") are written here the way
// an entry is drawn ("Continuer"); headings are put in capitals by the menu (Upper below).
// A text with "{}" in it is a format string: every column keeps the same placeholders.
Entry Entries[] = {
    // ---- Start screen, pause menu, Options page: lines ----
    {"CONTINUE",
     {"Continuer", "Continuar", "Fortsetzen", "Continua", "Doorgaan", "Continuar", "Продолжить",
      "Jatka", "Fortsätt", "Fortsæt", "Fortsett", "Kontynuuj", "Devam Et"}},
    {"NEW GAME",
     {"Nouvelle partie", "Nueva partida", "Neues Spiel", "Nuova partita", "Nieuw spel",
      "Novo jogo", "Новая игра", "Uusi peli", "Nytt spel", "Nyt spil", "Nytt spill", "Nowa gra",
      "Yeni Oyun"}},
    {"LOAD GAME",
     {"Charger une partie", "Cargar partida", "Spiel laden", "Carica partita", "Spel laden",
      "Carregar jogo", "Загрузить игру", "Lataa peli", "Ladda spel", "Indlæs spil",
      "Last inn spill", "Wczytaj grę", "Oyun Yükle"}},
    {"RESUME",
     {"Reprendre", "Reanudar", "Fortsetzen", "Riprendi", "Hervatten", "Retomar", "Продолжить",
      "Jatka", "Återuppta", "Genoptag", "Fortsett", "Wznów", "Devam Et"}},
    {"OPTIONS",
     {"Options", "Opciones", "Optionen", "Opzioni", "Opties", "Opções", "Настройки", "Asetukset",
      "Alternativ", "Indstillinger", "Alternativer", "Opcje", "Seçenekler"}},
    {"ACHIEVEMENTS",
     {"Succès", "Logros", "Erfolge", "Obiettivi", "Prestaties", "Conquistas", "Достижения",
      "Saavutukset", "Prestationer", "Præstationer", "Prestasjoner", "Osiągnięcia",
      "Başarımlar"}},
    {"QUIT",
     {"Quitter", "Salir", "Beenden", "Esci", "Afsluiten", "Sair", "Выход", "Lopeta", "Avsluta",
      "Afslut", "Avslutt", "Wyjdź", "Çıkış"}},
    // Kept short: "Press again to confirm" is drawn on the same line.
    {"RETURN TO MAIN MENU",
     {"Menu principal", "Menú principal", "Zum Hauptmenü", "Menu principale", "Naar hoofdmenu",
      "Menu principal", "В главное меню", "Päävalikkoon", "Till huvudmenyn", "Til hovedmenuen",
      "Til hovedmenyen", "Menu główne", "Ana Menüye Dön"}},
    {"GRAPHICS",
     {"Graphismes", "Gráficos", "Grafik", "Grafica", "Beeld", "Gráficos", "Графика", "Grafiikka",
      "Grafik", "Grafik", "Grafikk", "Grafika", "Grafikler"}},
    {"CONTROLS",
     {"Commandes", "Controles", "Steuerung", "Comandi", "Besturing", "Controlos", "Управление",
      "Ohjaus", "Kontroller", "Styring", "Kontroller", "Sterowanie", "Kontroller"}},
    {"GAMEPLAY",
     {"Gameplay", "Juego", "Gameplay", "Gioco", "Gameplay", "Jogabilidade", "Игра", "Pelaaminen",
      "Spel", "Spil", "Spill", "Rozgrywka", "Oynanış"}},
    {"SOUND",
     {"Son", "Sonido", "Ton", "Audio", "Geluid", "Som", "Звук", "Ääni", "Ljud", "Lyd", "Lyd",
      "Dźwięk", "Ses"}},
    {"INTERFACE",
     {"Interface", "Interfaz", "Anzeige", "Interfaccia", "Interface", "Interface", "Интерфейс",
      "Käyttöliittymä", "Gränssnitt", "Brugerflade", "Grensesnitt", "Interfejs", "Arayüz"}},
    {"SAVE AND LOAD",
     {"Sauvegarder et charger", "Guardar y cargar", "Speichern und Laden", "Salva e carica",
      "Opslaan en laden", "Guardar e carregar", "Сохранение и загрузка", "Tallenna ja lataa",
      "Spara och ladda", "Gem og indlæs", "Lagre og last inn", "Zapis i wczytywanie",
      "Kaydet ve Yükle"}},
    {"BACK",
     {"Retour", "Atrás", "Zurück", "Indietro", "Terug", "Voltar", "Назад", "Takaisin", "Tillbaka",
      "Tilbage", "Tilbake", "Wstecz", "Geri"}},

    // ---- Page titles and section headings (drawn in capitals) ----
    {"PAUSED",
     {"Pause", "Pausa", "Pause", "Pausa", "Pauze", "Pausa", "Пауза", "Tauko", "Paus", "Pause",
      "Pause", "Pauza", "Duraklatıldı"}},
    {"GAME",
     {"Jeu", "Juego", "Spiel", "Gioco", "Spel", "Jogo", "Игра", "Peli", "Spel", "Spil", "Spill",
      "Gra", "Oyun"}},
    {"SETUP",
     {"Configuration", "Configuración", "Einstellungen", "Impostazioni", "Instellingen",
      "Configuração", "Параметры", "Asetukset", "Inställningar", "Opsætning", "Oppsett",
      "Ustawienia", "Ayarlar"}},
    {"EXIT",
     {"Quitter", "Salir", "Beenden", "Uscita", "Afsluiten", "Sair", "Выход", "Poistu", "Avsluta",
      "Afslut", "Avslutt", "Wyjście", "Çıkış"}},
    {"SAVE DATA",
     {"Sauvegardes", "Datos guardados", "Spielstände", "Salvataggi", "Opslagdata",
      "Dados guardados", "Сохранения", "Tallennukset", "Sparfiler", "Gemte data", "Lagringsdata",
      "Zapisy", "Kayıtlar"}},
    {"DISPLAY",
     {"Affichage", "Pantalla", "Anzeige", "Schermo", "Weergave", "Ecrã", "Экран", "Näyttö",
      "Skärm", "Skærm", "Skjerm", "Ekran", "Ekran"}},
    {"IMAGE",
     {"Image", "Imagen", "Bild", "Immagine", "Beeld", "Imagem", "Изображение", "Kuva", "Bild",
      "Billede", "Bilde", "Obraz", "Görüntü"}},
    {"EFFECTS",
     {"Effets", "Efectos", "Effekte", "Effetti", "Effecten", "Efeitos", "Эффекты", "Tehosteet",
      "Effekter", "Effekter", "Effekter", "Efekty", "Efektler"}},
    {"FRAME RATE",
     {"Fréquence d'images", "Fotogramas", "Bildrate", "Frame rate", "Framerate", "Fotogramas",
      "Частота кадров", "Kuvataajuus", "Bildfrekvens", "Billedhastighed", "Bildefrekvens",
      "Liczba klatek", "Kare Hızı"}},
    {"SHADOWS",
     {"Ombres", "Sombras", "Schatten", "Ombre", "Schaduwen", "Sombras", "Тени", "Varjot",
      "Skuggor", "Skygger", "Skygger", "Cienie", "Gölgeler"}},
    {"ACTIONS",
     {"Actions", "Acciones", "Aktionen", "Azioni", "Acties", "Ações", "Действия", "Toiminnot",
      "Handlingar", "Handlinger", "Handlinger", "Akcje", "Eylemler"}},
    {"BUTTONS",
     {"Touches", "Botones", "Tasten", "Pulsanti", "Knoppen", "Botões", "Кнопки", "Painikkeet",
      "Knappar", "Knapper", "Knapper", "Przyciski", "Düğmeler"}},
    {"MOVEMENT",
     {"Déplacement", "Movimiento", "Bewegung", "Movimento", "Beweging", "Movimento", "Движение",
      "Liikkuminen", "Rörelse", "Bevægelse", "Bevegelse", "Ruch", "Hareket"}},
    {"MOTION AND TOUCHPAD",
     {"Mouvement et pavé tactile", "Movimiento y panel táctil", "Bewegung und Touchpad",
      "Movimento e touchpad", "Beweging en touchpad", "Movimento e painel tátil",
      "Движение и тачпад", "Liike ja kosketuslevy", "Rörelse och styrplatta",
      "Bevægelse og touchpad", "Bevegelse og styreflate", "Ruch i panel dotykowy",
      "Hareket ve Dokunmatik Yüzey"}},
    {"LANGUAGE",
     {"Langue", "Idioma", "Sprache", "Lingua", "Taal", "Idioma", "Язык", "Kieli", "Språk", "Sprog",
      "Språk", "Język", "Dil"}},
    {"TROPHIES",
     {"Trophées", "Trofeos", "Trophäen", "Trofei", "Trofeeën", "Troféus", "Трофеи", "Pokaalit",
      "Troféer", "Trofæer", "Trofeer", "Trofea", "Kupalar"}},

    // ---- Key hints ----
    {"Resume",
     {"Reprendre", "Reanudar", "Fortsetzen", "Riprendi", "Hervatten", "Retomar", "Продолжить",
      "Jatka", "Återuppta", "Genoptag", "Fortsett", "Wznów", "Devam Et"}},
    {"Back",
     {"Retour", "Atrás", "Zurück", "Indietro", "Terug", "Voltar", "Назад", "Takaisin", "Tillbaka",
      "Tilbage", "Tilbake", "Wstecz", "Geri"}},
    {"Change",
     {"Modifier", "Cambiar", "Ändern", "Cambia", "Wijzigen", "Alterar", "Изменить", "Muuta",
      "Ändra", "Skift", "Endre", "Zmień", "Değiştir"}},
    {"Select",
     {"Sélectionner", "Seleccionar", "Auswählen", "Seleziona", "Selecteren", "Selecionar",
      "Выбрать", "Valitse", "Välj", "Vælg", "Velg", "Wybierz", "Seç"}},

    // ---- Graphics page: option names ----
    {"Frame Rate Cap",
     {"Limite de FPS", "Límite de FPS", "FPS-Limit", "Limite FPS", "FPS-limiet", "Limite de FPS",
      "Лимит FPS", "FPS-raja", "FPS-gräns", "FPS-grænse", "FPS-grense", "Limit FPS",
      "FPS Sınırı"}},
    {"Fullscreen",
     {"Plein écran", "Pantalla completa", "Vollbild", "Schermo intero", "Volledig scherm",
      "Ecrã inteiro", "Полный экран", "Koko näyttö", "Helskärm", "Fuld skærm", "Fullskjerm",
      "Pełny ekran", "Tam Ekran"}},
    {"Window Size",
     {"Taille de la fenêtre", "Tamaño de ventana", "Fenstergröße", "Dimensioni finestra",
      "Venstergrootte", "Tamanho da janela", "Размер окна", "Ikkunan koko", "Fönsterstorlek",
      "Vinduesstørrelse", "Vindusstørrelse", "Rozmiar okna", "Pencere Boyutu"}},
    {"Upscaling (FSR)",
     {"Mise à l'échelle (FSR)", "Reescalado (FSR)", "Hochskalierung (FSR)", "Upscaling (FSR)",
      "Opschaling (FSR)", "Upscaling (FSR)", "Масштабирование (FSR)", "Skaalaus (FSR)",
      "Uppskalning (FSR)", "Opskalering (FSR)", "Oppskalering (FSR)", "Skalowanie (FSR)",
      "Yükseltme (FSR)"}},
    {"Crouch",
     {"Accroupir", "Agacharse", "Ducken", "Accovacciati", "Hurken", "Agachar", "Присесть", "Kyykisty", "Huka", "Duk dig", "Huk deg", "Kucnij", "Çömel"}},
    {"Pick Up / Grab",
     {"Ramasser / Saisir", "Recoger / Agarrar", "Aufheben / Greifen", "Raccogli / Afferra", "Oppakken / Grijpen", "Apanhar / Agarrar", "Поднять / Схватить", "Poimi / Tartu", "Plocka upp / Greppa", "Saml op / Grib", "Plukk opp / Grip", "Podnieś / Chwyć", "Al / Tutun"}},
    {"Tackle / Throw",
     {"Plaquer / Lancer", "Placar / Lanzar", "Rempeln / Werfen", "Placca / Lancia", "Tackelen / Gooien", "Placar / Atirar", "Толкнуть / Бросить", "Taklaa / Heitä", "Tackla / Kasta", "Tackl / Kast", "Takle / Kast", "Powal / Rzuć", "Çelme / Fırlat"}},
    {"Call Trico",
     {"Appeler Trico", "Llamar a Trico", "Trico rufen", "Chiama Trico", "Trico roepen", "Chamar Trico", "Позвать Трико", "Kutsu Tricoa", "Kalla på Trico", "Kald på Trico", "Rop på Trico", "Zawołaj Trico", "Trico'yu Çağır"}},
    {"Contemplate (Hold)",
     {"Contempler (maintenir)", "Contemplar (mantener)", "Betrachten (halten)", "Contempla (tieni premuto)", "Beschouwen (ingedrukt)", "Contemplar (manter)", "Созерцать (удерживать)", "Mietiskele (pidä)", "Begrunda (håll)", "Betragt (hold)", "Betrakt (hold)", "Kontempluj (przytrzymaj)", "Seyret (basılı tut)"}},
    {"Wield / Put Away Mirror",
     {"Brandir / ranger le miroir", "Empuñar / guardar el espejo", "Spiegel ziehen / wegstecken", "Impugna / riponi lo specchio", "Spiegel pakken / wegstoppen", "Empunhar / guardar o espelho", "Достать / убрать зеркало", "Ota esiin / laita pois peili", "Ta fram / stoppa undan spegeln", "Tag / læg spejlet væk", "Ta fram / legg bort speilet", "Wyjmij / schowaj lustro", "Aynayı çıkar / kaldır"}},
    {"Graphics",
     {"Graphismes", "Gráficos", "Grafik", "Grafica", "Graphics", "Gráficos", "Графика", "Grafiikka",
      "Grafik", "Grafik", "Grafikk", "Grafika", "Grafik"}},
    {"Key Bindings",
     {"Touches", "Asignación de teclas", "Tastenbelegung", "Assegnazione tasti", "Toetsen",
      "Atribuição de teclas", "Назначение клавиш", "Näppäimet", "Tangentbindningar",
      "Tastebindinger", "Tastebindinger", "Przypisanie klawiszy", "Tuş Atamaları"}},
    {"Sharpening",
     {"Netteté", "Nitidez", "Schärfe", "Nitidezza", "Verscherping", "Nitidez", "Резкость",
      "Terävöitys", "Skärpa", "Skarphed", "Skarphet", "Wyostrzanie", "Keskinleştirme"}},
    {"Frame Rate Counter",
     {"Compteur de FPS", "Contador de FPS", "FPS-Anzeige", "Contatore FPS", "FPS-teller",
      "Contador de FPS", "Счётчик FPS", "FPS-laskuri", "FPS-räknare", "FPS-tæller", "FPS-teller",
      "Licznik FPS", "FPS Sayacı"}},
    {"Effects Preset",
     {"Préréglage d'effets", "Ajuste de efectos", "Effekt-Voreinstellung", "Preset effetti",
      "Effectenpreset", "Predefinição de efeitos", "Набор эффектов", "Tehosteiden esiasetus",
      "Effektförval", "Effektforvalg", "Effektforvalg", "Zestaw efektów", "Efekt Ön Ayarı"}},
    {"Anti-Aliasing",
     {"Anticrénelage", "Antialiasing", "Kantenglättung", "Anti-aliasing", "Anti-aliasing",
      "Anti-aliasing", "Сглаживание", "Reunanpehmennys", "Kantutjämning", "Kantudjævning",
      "Kantutjevning", "Wygładzanie krawędzi", "Kenar Yumuşatma"}},
    {"Wet Surface Reflections",
     {"Reflets sur sol mouillé", "Reflejos en mojado", "Reflexionen auf Nässe",
      "Riflessi sul bagnato", "Reflecties op nat", "Reflexos no molhado", "Отражения на мокром",
      "Märkien pintojen heijastukset", "Reflektioner på vått", "Refleksioner på vådt",
      "Refleksjoner på vått", "Odbicia na mokrym", "Islak Yüzey Yansımaları"}},
    {"Light Glow",
     {"Halo lumineux", "Resplandor de luces", "Lichtschein", "Bagliore luci", "Lichtgloed",
      "Brilho das luzes", "Свечение света", "Valon hehku", "Ljussken", "Lysskær", "Lysglød",
      "Poświata świateł", "Işık Parlaması"}},
    {"Game Frame Limit",
     {"Limite FPS du jeu", "Límite FPS del juego", "FPS-Limit des Spiels", "Limite FPS del gioco",
      "FPS-limiet van spel", "Limite FPS do jogo", "Лимит FPS игры", "Pelin FPS-raja",
      "Spelets FPS-gräns", "Spillets FPS-grænse", "Spillets FPS-grense", "Limit FPS gry",
      "Oyun FPS Sınırı"}},
    {"Shadow Preset",
     {"Préréglage d'ombres", "Ajuste de sombras", "Schatten-Voreinstellung", "Preset ombre",
      "Schaduwpreset", "Predefinição de sombras", "Набор теней", "Varjojen esiasetus",
      "Skuggförval", "Skyggeforvalg", "Skyggeforvalg", "Zestaw cieni", "Gölge Ön Ayarı"}},
    {"Sun Shadows",
     {"Ombres du soleil", "Sombras del sol", "Sonnenschatten", "Ombre del sole", "Zonschaduwen",
      "Sombras do sol", "Тени от солнца", "Auringon varjot", "Solskuggor", "Solskygger",
      "Solskygger", "Cienie słońca", "Güneş Gölgeleri"}},
    {"Light Shadows",
     {"Ombres des lumières", "Sombras de luces", "Lichtschatten", "Ombre delle luci",
      "Lichtschaduwen", "Sombras das luzes", "Тени от ламп", "Valojen varjot", "Ljusskuggor",
      "Lysskygger", "Lysskygger", "Cienie świateł", "Işık Gölgeleri"}},
    {"Shadow Detail",
     {"Détail des ombres", "Detalle de sombras", "Schattendetails", "Dettaglio ombre",
      "Schaduwdetail", "Detalhe das sombras", "Детализация теней", "Varjojen tarkkuus",
      "Skuggdetalj", "Skyggedetalje", "Skyggedetalj", "Szczegóły cieni", "Gölge Ayrıntısı"}},

    {"Language",
     {"Langue", "Idioma", "Sprache", "Lingua", "Taal", "Idioma", "Язык", "Kieli", "Språk", "Sprog",
      "Språk", "Język", "Dil"}},

    // ---- Graphics page: values ----
    {"On",
     {"Activé", "Activado", "An", "Attivo", "Aan", "Ligado", "Вкл.", "Päällä", "På", "Til", "På",
      "Wł.", "Açık"}},
    {"Off",
     {"Désactivé", "Desactivado", "Aus", "Disattivo", "Uit", "Desligado", "Выкл.", "Pois", "Av",
      "Fra", "Av", "Wył.", "Kapalı"}},
    {"Uncapped",
     {"Sans limite", "Sin límite", "Unbegrenzt", "Senza limite", "Onbeperkt", "Sem limite",
      "Без лимита", "Rajoittamaton", "Obegränsad", "Ubegrænset", "Ubegrenset", "Bez limitu",
      "Sınırsız"}},
    {"Custom",
     {"Personnalisé", "Personalizado", "Eigene", "Personalizzato", "Aangepast", "Personalizado",
      "Свой", "Mukautettu", "Anpassad", "Brugerdefineret", "Egendefinert", "Własny", "Özel"}},
    {"Unlimited",
     {"Illimité", "Ilimitado", "Unbegrenzt", "Illimitato", "Onbeperkt", "Ilimitado", "Без лимита",
      "Rajoittamaton", "Obegränsad", "Ubegrænset", "Ubegrenset", "Bez limitu", "Sınırsız"}},
    {"Game Setting",
     {"Réglage du jeu", "Ajuste del juego", "Spieleinstellung", "Impostazione gioco",
      "Spelinstelling", "Definição do jogo", "Как в игре", "Pelin asetus", "Spelets inställning",
      "Spillets indstilling", "Spillets innstilling", "Ustawienie gry", "Oyun Ayarı"}},
    {"Every Frame",
     {"Chaque image", "Cada fotograma", "Jedes Bild", "Ogni frame", "Elk frame", "Cada fotograma",
      "Каждый кадр", "Joka kuva", "Varje bild", "Hvert billede", "Hvert bilde", "Każda klatka",
      "Her Kare"}},
    {"Every 2nd Frame",
     {"Une image sur deux", "Cada 2 fotogramas", "Jedes 2. Bild", "Ogni 2 frame", "Elk 2e frame",
      "Cada 2 fotogramas", "Каждый 2-й кадр", "Joka 2. kuva", "Varannan bild", "Hvert 2. billede",
      "Hvert 2. bilde", "Co 2. klatka", "Her 2. Kare"}},
    {"Full",
     {"Complet", "Completo", "Voll", "Completo", "Volledig", "Completo", "Полная", "Täysi", "Full",
      "Fuld", "Full", "Pełne", "Tam"}},
    {"Reduced",
     {"Réduit", "Reducido", "Reduziert", "Ridotto", "Verlaagd", "Reduzido", "Сниженная",
      "Vähennetty", "Reducerad", "Reduceret", "Redusert", "Zmniejszone", "Azaltılmış"}},
    {"Low",
     {"Faible", "Bajo", "Niedrig", "Basso", "Laag", "Baixo", "Низкая", "Matala", "Låg", "Lav",
      "Lav", "Niskie", "Düşük"}},
    {"Medium",
     {"Moyen", "Medio", "Mittel", "Medio", "Gemiddeld", "Médio", "Средняя", "Keskitaso", "Medel",
      "Mellem", "Middels", "Średnie", "Orta"}},
    {"High",
     {"Élevé", "Alto", "Hoch", "Alto", "Hoog", "Alto", "Высокая", "Korkea", "Hög", "Høj", "Høy",
      "Wysokie", "Yüksek"}},
    {"Original",
     {"D'origine", "Original", "Original", "Originale", "Origineel", "Original", "Оригинал",
      "Alkuperäinen", "Original", "Original", "Original", "Oryginalny", "Orijinal"}},
    {"Balanced",
     {"Équilibré", "Equilibrado", "Ausgewogen", "Bilanciato", "Gebalanceerd", "Equilibrado",
      "Баланс", "Tasapainoinen", "Balanserad", "Afbalanceret", "Balansert", "Zrównoważony",
      "Dengeli"}},
    {"Fast",
     {"Rapide", "Rápido", "Schnell", "Veloce", "Snel", "Rápido", "Быстро", "Nopea", "Snabb",
      "Hurtig", "Rask", "Szybki", "Hızlı"}},
    {"Sharp",
     {"Net", "Nítido", "Scharf", "Nitido", "Scherp", "Nítido", "Чётко", "Terävä", "Skarp", "Skarp",
      "Skarp", "Ostry", "Keskin"}},
    {"Minimal",
     {"Minimal", "Mínimo", "Minimal", "Minimo", "Minimaal", "Mínimo", "Минимум", "Minimi",
      "Minimal", "Minimal", "Minimal", "Minimalny", "Asgari"}},

    // ---- Achievements page ----
    {"Locked",
     {"Verrouillé", "Bloqueado", "Gesperrt", "Bloccato", "Vergrendeld", "Bloqueado", "Закрыто",
      "Lukittu", "Låst", "Låst", "Låst", "Zablokowane", "Kilitli"}},
    {"Unlocked {}",
     {"Débloqué le {}", "Desbloqueado: {}", "Freigeschaltet: {}", "Sbloccato: {}",
      "Ontgrendeld: {}", "Desbloqueado: {}", "Открыто {}", "Avattu {}", "Upplåst {}",
      "Låst op {}", "Låst opp {}", "Odblokowano {}", "Açıldı: {}"}},
    {"Trophy {}",
     {"Trophée {}", "Trofeo {}", "Trophäe {}", "Trofeo {}", "Trofee {}", "Troféu {}", "Трофей {}",
      "Pokaali {}", "Trofé {}", "Trofæ {}", "Trofé {}", "Trofeum {}", "Kupa {}"}},

    // The unlock notice (core/libraries/np/trophy_ui.cpp).
    {"Trophy earned!",
     {"Trophée obtenu !", "¡Trofeo conseguido!", "Trophäe erhalten!", "Trofeo ottenuto!",
      "Trofee behaald!", "Troféu conquistado!", "Трофей получен!", "Pokaali ansaittu!",
      "Trofé erhållen!", "Trofæ optjent!", "Trofé oppnådd!", "Trofeum zdobyte!",
      "Kupa kazanıldı!"}},

    // ---- Confirm and capture prompts (drawn on the line itself: kept short) ----
    {"Press again to quit",
     {"Appuyez encore", "Pulsa otra vez", "Erneut drücken", "Premi di nuovo", "Druk nogmaals",
      "Prime de novo", "Нажмите ещё раз", "Paina uudelleen", "Tryck igen", "Tryk igen",
      "Trykk igjen", "Naciśnij ponownie", "Tekrar Bas"}},
    {"Press again to confirm",
     {"Appuyez encore", "Pulsa otra vez", "Erneut drücken", "Premi di nuovo", "Druk nogmaals",
      "Prime de novo", "Нажмите ещё раз", "Paina uudelleen", "Tryck igen", "Tryk igen",
      "Trykk igjen", "Naciśnij ponownie", "Tekrar Bas"}},
    {"Press a key...",
     {"Touche...", "Tecla...", "Taste...", "Tasto...", "Toets...", "Tecla...", "Клавиша...",
      "Näppäin...", "Tangent...", "Tast...", "Tast...", "Klawisz...", "Tuş..."}},

    // ---- Controls page: actions ----
    {"Main Attack",
     {"Attaque principale", "Ataque principal", "Hauptangriff", "Attacco principale",
      "Hoofdaanval", "Ataque principal", "Основная атака", "Päähyökkäys", "Huvudattack",
      "Hovedangreb", "Hovedangrep", "Atak główny", "Ana Saldırı"}},
    {"Aim / Zoom",
     {"Viser / Zoom", "Apuntar / Zoom", "Zielen / Zoom", "Mira / Zoom", "Richten / Zoom",
      "Apontar / Zoom", "Прицел / Зум", "Tähtää / Zoomaa", "Sikta / Zooma", "Sigt / Zoom",
      "Sikt / Zoom", "Celowanie / Zoom", "Nişan / Yakınlaştır"}},
    {"Heavy Attack",
     {"Attaque lourde", "Ataque fuerte", "Schwerer Angriff", "Attacco pesante", "Zware aanval",
      "Ataque forte", "Мощная атака", "Raskas hyökkäys", "Tung attack", "Tungt angreb",
      "Tungt angrep", "Silny atak", "Ağır Saldırı"}},
    {"Special Power",
     {"Pouvoir spécial", "Poder especial", "Spezialkraft", "Potere speciale", "Speciale kracht",
      "Poder especial", "Особая сила", "Erikoisvoima", "Specialkraft", "Specialkraft",
      "Spesialkraft", "Moc specjalna", "Özel Güç"}},
    {"Jump",
     {"Sauter", "Saltar", "Springen", "Salta", "Springen", "Saltar", "Прыжок", "Hyppää", "Hoppa",
      "Hop", "Hopp", "Skok", "Zıpla"}},
    {"Dash",
     {"Ruée", "Impulso", "Sprint", "Scatto", "Sprint", "Arranque", "Рывок", "Syöksy", "Rusning",
      "Spurt", "Spurt", "Zryw", "Atılma"}},
    {"Melee",
     {"Corps à corps", "Cuerpo a cuerpo", "Nahkampf", "Corpo a corpo", "Melee", "Corpo a corpo",
      "Ближний бой", "Lähitaistelu", "Närstrid", "Nærkamp", "Nærkamp", "Walka wręcz",
      "Yakın Dövüş"}},
    {"Karmic Actions",
     {"Actions karmiques", "Acciones kármicas", "Karma-Aktionen", "Azioni karmiche",
      "Karma-acties", "Ações cármicas", "Кармические действия", "Karmateot", "Karmahandlingar",
      "Karmahandlinger", "Karmahandlinger", "Akcje karmiczne", "Karma Eylemleri"}},
    {"Touchpad Press (Drain, Interact)",
     {"Appui pavé tactile (drain, action)", "Pulsar panel táctil (drenar, usar)",
      "Touchpad drücken (Entzug, Aktion)", "Premi touchpad (assorbi, usa)",
      "Touchpad-druk (absorberen, actie)", "Premir painel tátil (drenar, usar)",
      "Нажать тачпад (поглотить, действие)", "Kosketuslevyn painallus (ime, käytä)",
      "Styrplattetryck (dränera, använd)", "Tryk på touchpad (dræn, brug)",
      "Trykk på styreflate (tapp, bruk)", "Wciśnij panel (wchłoń, użyj)",
      "Dokunmatik Yüzeye Bas (Em, Etkileş)"}},
    {"Pause",
     {"Pause", "Pausa", "Pause", "Pausa", "Pauze", "Pausa", "Пауза", "Tauko", "Paus", "Pause",
      "Pause", "Pauza", "Duraklat"}},
    {"D-pad Up",
     {"Touche haut", "Cruceta arriba", "Steuerkreuz oben", "Croce direzionale su",
      "D-pad omhoog", "Direcional cima", "Крестовина вверх", "Suuntanäppäin ylös",
      "Styrknapp upp", "Retningsknap op", "Retningsknapp opp", "Krzyżak w górę",
      "Yön Tuşu Yukarı"}},
    {"D-pad Down",
     {"Touche bas", "Cruceta abajo", "Steuerkreuz unten", "Croce direzionale giù",
      "D-pad omlaag", "Direcional baixo", "Крестовина вниз", "Suuntanäppäin alas",
      "Styrknapp ned", "Retningsknap ned", "Retningsknapp ned", "Krzyżak w dół",
      "Yön Tuşu Aşağı"}},
    {"D-pad Left",
     {"Touche gauche", "Cruceta izquierda", "Steuerkreuz links", "Croce direzionale sinistra",
      "D-pad links", "Direcional esquerda", "Крестовина влево", "Suuntanäppäin vasen",
      "Styrknapp vänster", "Retningsknap venstre", "Retningsknapp venstre", "Krzyżak w lewo",
      "Yön Tuşu Sol"}},
    {"D-pad Right",
     {"Touche droite", "Cruceta derecha", "Steuerkreuz rechts", "Croce direzionale destra",
      "D-pad rechts", "Direcional direita", "Крестовина вправо", "Suuntanäppäin oikea",
      "Styrknapp höger", "Retningsknap højre", "Retningsknapp høyre", "Krzyżak w prawo",
      "Yön Tuşu Sağ"}},
    {"Move Forward",
     {"Avancer", "Avanzar", "Vorwärts", "Avanti", "Vooruit", "Avançar", "Вперёд", "Eteenpäin",
      "Framåt", "Fremad", "Fremover", "Do przodu", "İleri"}},
    {"Move Back",
     {"Reculer", "Retroceder", "Rückwärts", "Indietro", "Achteruit", "Recuar", "Назад",
      "Taaksepäin", "Bakåt", "Tilbage", "Bakover", "Do tyłu", "Geri"}},
    {"Move Left",
     {"Aller à gauche", "Mover a la izquierda", "Nach links", "A sinistra", "Naar links",
      "Mover para a esquerda", "Влево", "Vasemmalle", "Vänster", "Til venstre", "Til venstre",
      "W lewo", "Sola"}},
    {"Move Right",
     {"Aller à droite", "Mover a la derecha", "Nach rechts", "A destra", "Naar rechts",
      "Mover para a direita", "Вправо", "Oikealle", "Höger", "Til højre", "Til høyre", "W prawo",
      "Sağa"}},
    {"Hold Controller Sideways (Spray Can)",
     {"Manette de côté (bombe de peinture)", "Mando de lado (bote de espray)",
      "Controller seitlich (Sprühdose)", "Controller di lato (bomboletta)",
      "Controller zijwaarts (spuitbus)", "Comando de lado (lata de spray)",
      "Контроллер боком (баллончик)", "Ohjain sivuttain (spraymaali)",
      "Kontrollen i sidled (sprejburk)", "Controller sidelæns (spraydåse)",
      "Kontroller sidelengs (sprayboks)", "Kontroler bokiem (spray)",
      "Kumandayı Yan Tut (Sprey)"}},
    {"Shake Controller",
     {"Secouer la manette", "Agitar el mando", "Controller schütteln", "Scuoti il controller",
      "Controller schudden", "Agitar o comando", "Встряхнуть контроллер", "Ravista ohjainta",
      "Skaka kontrollen", "Ryst controlleren", "Rist kontrolleren", "Potrząśnij kontrolerem",
      "Kumandayı Salla"}},
    {"Touchpad Swipe Up",
     {"Balayage tactile haut", "Deslizar panel arriba", "Touchpad nach oben wischen",
      "Scorri touchpad su", "Touchpad omhoog vegen", "Deslizar painel para cima",
      "Свайп по тачпаду вверх", "Pyyhkäisy ylös", "Svep uppåt", "Stryg op", "Sveip opp",
      "Przesuń w górę", "Yukarı Kaydır"}},
    {"Touchpad Swipe Down",
     {"Balayage tactile bas", "Deslizar panel abajo", "Touchpad nach unten wischen",
      "Scorri touchpad giù", "Touchpad omlaag vegen", "Deslizar painel para baixo",
      "Свайп по тачпаду вниз", "Pyyhkäisy alas", "Svep nedåt", "Stryg ned", "Sveip ned",
      "Przesuń w dół", "Aşağı Kaydır"}},
    {"Touchpad Swipe Left",
     {"Balayage tactile gauche", "Deslizar panel a la izquierda", "Touchpad nach links wischen",
      "Scorri touchpad a sinistra", "Touchpad naar links vegen", "Deslizar painel para a esquerda",
      "Свайп по тачпаду влево", "Pyyhkäisy vasemmalle", "Svep åt vänster", "Stryg til venstre",
      "Sveip til venstre", "Przesuń w lewo", "Sola Kaydır"}},
    {"Touchpad Swipe Right",
     {"Balayage tactile droite", "Deslizar panel a la derecha", "Touchpad nach rechts wischen",
      "Scorri touchpad a destra", "Touchpad naar rechts vegen", "Deslizar painel para a direita",
      "Свайп по тачпаду вправо", "Pyyhkäisy oikealle", "Svep åt höger", "Stryg til højre",
      "Sveip til høyre", "Przesuń w prawo", "Sağa Kaydır"}},
    {"Touchpad Both Thumbs (Move Car)",
     {"Deux pouces sur le pavé (voiture)", "Dos pulgares en el panel (coche)",
      "Touchpad beide Daumen (Auto)", "Due pollici sul touchpad (auto)",
      "Touchpad beide duimen (auto)", "Dois polegares no painel (carro)",
      "Тачпад двумя пальцами (машина)", "Molemmat peukalot levyllä (auto)",
      "Båda tummarna på plattan (bil)", "Begge tommelfingre (flyt bil)",
      "Begge tomler (flytt bil)", "Oba kciuki na panelu (auto)", "İki Başparmak (Arabayı İt)"}},

    // ---- Controls page: key and mouse names (Shift, Ctrl, Alt and the like stay as they are) ----
    {"Left Mouse",
     {"Clic gauche", "Clic izquierdo", "Linke Maustaste", "Clic sinistro", "Linkermuisknop",
      "Botão esquerdo", "ЛКМ", "Hiiren vasen", "Vänster musknapp", "Venstre museknap",
      "Venstre museknapp", "LPM", "Sol Tık"}},
    {"Right Mouse",
     {"Clic droit", "Clic derecho", "Rechte Maustaste", "Clic destro", "Rechtermuisknop",
      "Botão direito", "ПКМ", "Hiiren oikea", "Höger musknapp", "Højre museknap",
      "Høyre museknapp", "PPM", "Sağ Tık"}},
    {"Middle Mouse",
     {"Clic molette", "Clic central", "Mittlere Maustaste", "Clic centrale", "Middelste muisknop",
      "Botão do meio", "СКМ", "Hiiren keski", "Mittenknapp", "Midterste museknap",
      "Midtre museknapp", "ŚPM", "Orta Tık"}},
    {"Mouse 4",
     {"Souris 4", "Ratón 4", "Maus 4", "Mouse 4", "Muis 4", "Rato 4", "Мышь 4", "Hiiri 4", "Mus 4",
      "Mus 4", "Mus 4", "Mysz 4", "Fare 4"}},
    {"Mouse 5",
     {"Souris 5", "Ratón 5", "Maus 5", "Mouse 5", "Muis 5", "Rato 5", "Мышь 5", "Hiiri 5", "Mus 5",
      "Mus 5", "Mus 5", "Mysz 5", "Fare 5"}},
    {"Wheel Up",
     {"Molette haut", "Rueda arriba", "Mausrad hoch", "Rotella su", "Wiel omhoog", "Roda cima",
      "Колесо вверх", "Rulla ylös", "Hjul upp", "Hjul op", "Hjul opp", "Kółko w górę",
      "Tekerlek Yukarı"}},
    {"Wheel Down",
     {"Molette bas", "Rueda abajo", "Mausrad runter", "Rotella giù", "Wiel omlaag", "Roda baixo",
      "Колесо вниз", "Rulla alas", "Hjul ned", "Hjul ned", "Hjul ned", "Kółko w dół",
      "Tekerlek Aşağı"}},
    {"Left Shift",
     {"Shift gauche", "Shift izq.", "Shift links", "Shift sx", "Linker Shift", "Shift esq.",
      "Левый Shift", "Vasen Shift", "Vänster Shift", "Venstre Shift", "Venstre Shift",
      "Lewy Shift", "Sol Shift"}},
    {"Right Shift",
     {"Shift droit", "Shift der.", "Shift rechts", "Shift dx", "Rechter Shift", "Shift dir.",
      "Правый Shift", "Oikea Shift", "Höger Shift", "Højre Shift", "Høyre Shift", "Prawy Shift",
      "Sağ Shift"}},
    {"Left Ctrl",
     {"Ctrl gauche", "Ctrl izq.", "Ctrl links", "Ctrl sx", "Linker Ctrl", "Ctrl esq.",
      "Левый Ctrl", "Vasen Ctrl", "Vänster Ctrl", "Venstre Ctrl", "Venstre Ctrl", "Lewy Ctrl",
      "Sol Ctrl"}},
    {"Right Ctrl",
     {"Ctrl droit", "Ctrl der.", "Ctrl rechts", "Ctrl dx", "Rechter Ctrl", "Ctrl dir.",
      "Правый Ctrl", "Oikea Ctrl", "Höger Ctrl", "Højre Ctrl", "Høyre Ctrl", "Prawy Ctrl",
      "Sağ Ctrl"}},
    {"Left Alt",
     {"Alt gauche", "Alt izq.", "Alt links", "Alt sx", "Linker Alt", "Alt esq.", "Левый Alt",
      "Vasen Alt", "Vänster Alt", "Venstre Alt", "Venstre Alt", "Lewy Alt", "Sol Alt"}},
    {"Right Alt",
     {"Alt droit", "Alt der.", "Alt rechts", "Alt dx", "Rechter Alt", "Alt dir.", "Правый Alt",
      "Oikea Alt", "Höger Alt", "Højre Alt", "Høyre Alt", "Prawy Alt", "Sağ Alt"}},
    {"Up",
     {"Haut", "Arriba", "Hoch", "Su", "Omhoog", "Cima", "Вверх", "Ylös", "Upp", "Op", "Opp",
      "W górę", "Yukarı"}},
    {"Down",
     {"Bas", "Abajo", "Runter", "Giù", "Omlaag", "Baixo", "Вниз", "Alas", "Ned", "Ned", "Ned",
      "W dół", "Aşağı"}},
    {"Left",
     {"Gauche", "Izquierda", "Links", "Sinistra", "Links", "Esquerda", "Влево", "Vasen", "Vänster",
      "Venstre", "Venstre", "W lewo", "Sol"}},
    {"Right",
     {"Droite", "Derecha", "Rechts", "Destra", "Rechts", "Direita", "Вправо", "Oikea", "Höger",
      "Højre", "Høyre", "W prawo", "Sağ"}},

    // ---- Help lines: start screen and pause menu ----
    {"Carry on from your last save",
     {"Reprendre depuis votre dernière sauvegarde", "Sigue desde tu última partida guardada",
      "Vom letzten Spielstand aus weiterspielen", "Riprendi dall'ultimo salvataggio",
      "Ga verder vanaf je laatste opslag", "Continua a partir da última gravação",
      "Продолжить с последнего сохранения", "Jatka viimeisimmästä tallennuksesta",
      "Fortsätt från din senaste sparfil", "Fortsæt fra dit seneste gemte spil",
      "Fortsett fra siste lagring", "Kontynuuj od ostatniego zapisu", "Son kayıttan devam et"}},
    {"Your last save loads first; the game's Options screen then opens on New Game (after a "
     "cutscene, if the save starts in one: press Options yourself)",
     {"Votre dernière sauvegarde se charge d'abord ; l'écran Options du jeu s'ouvre ensuite sur "
      "Nouvelle partie (si la sauvegarde commence par une cinématique, appuyez vous-même sur "
      "Options après celle-ci)",
      "Primero se carga tu última partida; luego se abre la pantalla Opciones del juego en "
      "Nueva partida (si la partida empieza con una escena, pulsa Options tú mismo cuando "
      "termine)",
      "Zuerst wird dein letzter Spielstand geladen; dann öffnet sich der Optionsbildschirm des "
      "Spiels bei Neues Spiel (beginnt der Spielstand mit einer Zwischensequenz, drücke danach "
      "selbst Options)",
      "Prima viene caricato l'ultimo salvataggio; poi la schermata Opzioni del gioco si apre su "
      "Nuova partita (se il salvataggio inizia con un filmato, premi tu Options al termine)",
      "Eerst wordt je laatste opslag geladen; daarna opent het optiescherm van de game op Nieuw "
      "spel (begint de opslag met een tussenfilmpje, druk daarna zelf op Options)",
      "Primeiro carrega a última gravação; depois o ecrã Opções do jogo abre em Novo jogo (se a "
      "gravação começar numa cena, prime tu Options quando terminar)",
      "Сначала загрузится последнее сохранение; затем экран настроек игры откроется на пункте "
      "«Новая игра» (если сохранение начинается с ролика, нажмите Options сами после него)",
      "Viimeisin tallennus ladataan ensin; sitten pelin asetusnäyttö avautuu kohtaan Uusi peli "
      "(jos tallennus alkaa välianimaatiolla, paina sen jälkeen itse Options)",
      "Din senaste sparfil laddas först; sedan öppnas spelets alternativskärm på Nytt spel (om "
      "sparfilen börjar med en mellansekvens, tryck själv på Options efteråt)",
      "Dit seneste gemte spil indlæses først; derefter åbner spillets indstillingsskærm på Nyt "
      "spil (starter det gemte spil med en mellemsekvens, så tryk selv på Options bagefter)",
      "Siste lagring lastes inn først; deretter åpnes spillets alternativskjerm på Nytt spill "
      "(starter lagringen med en filmsekvens, trykk selv på Options etterpå)",
      "Najpierw wczytuje się ostatni zapis; potem ekran opcji gry otwiera się na pozycji Nowa "
      "gra (jeśli zapis zaczyna się od przerywnika, po nim naciśnij Options samodzielnie)",
      "Önce son kaydın yüklenir; ardından oyunun Seçenekler ekranı Yeni Oyun satırında açılır "
      "(kayıt bir ara sahneyle başlıyorsa, bittikten sonra Options tuşuna kendin bas)"}},
    {"Your last save loads first; the game's Options screen then opens on Load Game (after a "
     "cutscene, if the save starts in one: press Options yourself)",
     {"Votre dernière sauvegarde se charge d'abord ; l'écran Options du jeu s'ouvre ensuite sur "
      "Charger une partie (si la sauvegarde commence par une cinématique, appuyez vous-même sur "
      "Options après celle-ci)",
      "Primero se carga tu última partida; luego se abre la pantalla Opciones del juego en "
      "Cargar partida (si la partida empieza con una escena, pulsa Options tú mismo cuando "
      "termine)",
      "Zuerst wird dein letzter Spielstand geladen; dann öffnet sich der Optionsbildschirm des "
      "Spiels bei Spiel laden (beginnt der Spielstand mit einer Zwischensequenz, drücke danach "
      "selbst Options)",
      "Prima viene caricato l'ultimo salvataggio; poi la schermata Opzioni del gioco si apre su "
      "Carica partita (se il salvataggio inizia con un filmato, premi tu Options al termine)",
      "Eerst wordt je laatste opslag geladen; daarna opent het optiescherm van de game op Spel "
      "laden (begint de opslag met een tussenfilmpje, druk daarna zelf op Options)",
      "Primeiro carrega a última gravação; depois o ecrã Opções do jogo abre em Carregar jogo "
      "(se a gravação começar numa cena, prime tu Options quando terminar)",
      "Сначала загрузится последнее сохранение; затем экран настроек игры откроется на пункте "
      "«Загрузить игру» (если сохранение начинается с ролика, нажмите Options сами после него)",
      "Viimeisin tallennus ladataan ensin; sitten pelin asetusnäyttö avautuu kohtaan Lataa peli "
      "(jos tallennus alkaa välianimaatiolla, paina sen jälkeen itse Options)",
      "Din senaste sparfil laddas först; sedan öppnas spelets alternativskärm på Ladda spel (om "
      "sparfilen börjar med en mellansekvens, tryck själv på Options efteråt)",
      "Dit seneste gemte spil indlæses først; derefter åbner spillets indstillingsskærm på "
      "Indlæs spil (starter det gemte spil med en mellemsekvens, så tryk selv på Options "
      "bagefter)",
      "Siste lagring lastes inn først; deretter åpnes spillets alternativskjerm på Last inn "
      "spill (starter lagringen med en filmsekvens, trykk selv på Options etterpå)",
      "Najpierw wczytuje się ostatni zapis; potem ekran opcji gry otwiera się na pozycji "
      "Wczytaj grę (jeśli zapis zaczyna się od przerywnika, po nim naciśnij Options "
      "samodzielnie)",
      "Önce son kaydın yüklenir; ardından oyunun Seçenekler ekranı Oyun Yükle satırında açılır "
      "(kayıt bir ara sahneyle başlıyorsa, bittikten sonra Options tuşuna kendin bas)"}},
    {"Start the game",
     {"Commencer le jeu", "Empezar el juego", "Spiel starten", "Inizia il gioco", "Start de game",
      "Começar o jogo", "Начать игру", "Aloita peli", "Starta spelet", "Start spillet",
      "Start spillet", "Rozpocznij grę", "Oyunu başlat"}},
    {"Return to the game",
     {"Retourner au jeu", "Volver al juego", "Zurück zum Spiel", "Torna al gioco",
      "Terug naar de game", "Voltar ao jogo", "Вернуться в игру", "Palaa peliin",
      "Tillbaka till spelet", "Tilbage til spillet", "Tilbake til spillet", "Wróć do gry",
      "Oyuna dön"}},
    {"Graphics, controls, gameplay, sound and interface",
     {"Graphismes, commandes, gameplay, son et interface",
      "Gráficos, controles, juego, sonido e interfaz",
      "Grafik, Steuerung, Gameplay, Ton und Anzeige",
      "Grafica, comandi, gioco, audio e interfaccia",
      "Beeld, besturing, gameplay, geluid en interface",
      "Gráficos, controlos, jogabilidade, som e interface",
      "Графика, управление, игра, звук и интерфейс",
      "Grafiikka, ohjaus, pelaaminen, ääni ja käyttöliittymä",
      "Grafik, kontroller, spel, ljud och gränssnitt", "Grafik, styring, spil, lyd og brugerflade",
      "Grafikk, kontroller, spill, lyd og grensesnitt",
      "Grafika, sterowanie, rozgrywka, dźwięk i interfejs",
      "Grafikler, kontroller, oynanış, ses ve arayüz"}},
    {"Graphics, controls, gameplay, sound, interface, save and load",
     {"Graphismes, commandes, gameplay, son, interface, sauvegarde et chargement",
      "Gráficos, controles, juego, sonido, interfaz, guardar y cargar",
      "Grafik, Steuerung, Gameplay, Ton, Anzeige, Speichern und Laden",
      "Grafica, comandi, gioco, audio, interfaccia, salvataggio e caricamento",
      "Beeld, besturing, gameplay, geluid, interface, opslaan en laden",
      "Gráficos, controlos, jogabilidade, som, interface, guardar e carregar",
      "Графика, управление, игра, звук, интерфейс, сохранение и загрузка",
      "Grafiikka, ohjaus, pelaaminen, ääni, käyttöliittymä, tallennus ja lataus",
      "Grafik, kontroller, spel, ljud, gränssnitt, spara och ladda",
      "Grafik, styring, spil, lyd, brugerflade, gem og indlæs",
      "Grafikk, kontroller, spill, lyd, grensesnitt, lagre og last inn",
      "Grafika, sterowanie, rozgrywka, dźwięk, interfejs, zapis i wczytywanie",
      "Grafikler, kontroller, oynanış, ses, arayüz, kaydetme ve yükleme"}},
    {"Trophies of the game unlocked in this build",
     {"Trophées du jeu débloqués dans cette version",
      "Trofeos del juego desbloqueados en esta versión",
      "In dieser Version freigeschaltete Trophäen des Spiels",
      "Trofei del gioco sbloccati in questa versione",
      "Trofeeën van de game die in deze versie zijn ontgrendeld",
      "Troféus do jogo desbloqueados nesta versão", "Трофеи игры, открытые в этой версии",
      "Tässä versiossa avatut pelin pokaalit",
      "Spelets troféer som låsts upp i den här versionen",
      "Spillets trofæer, der er låst op i denne version",
      "Spillets trofeer som er låst opp i denne versjonen", "Trofea gry odblokowane w tej wersji",
      "Bu sürümde açılan oyun kupaları"}},
    {"Close the game",
     {"Fermer le jeu", "Cerrar el juego", "Spiel schließen", "Chiudi il gioco", "Sluit de game",
      "Fechar o jogo", "Закрыть игру", "Sulje peli", "Stäng spelet", "Luk spillet",
      "Lukk spillet", "Zamknij grę", "Oyunu kapat"}},
    {"Progress since the last checkpoint is lost, and the game loads again (about 40 seconds)",
     {"La progression depuis le dernier point de contrôle est perdue et le jeu se recharge "
      "(environ 40 secondes)",
      "Se pierde el progreso desde el último punto de control y el juego vuelve a cargar (unos "
      "40 segundos)",
      "Der Fortschritt seit dem letzten Kontrollpunkt geht verloren und das Spiel lädt neu "
      "(etwa 40 Sekunden)",
      "I progressi dall'ultimo checkpoint vanno persi e il gioco si ricarica (circa 40 secondi)",
      "Voortgang sinds het laatste checkpoint gaat verloren en de game laadt opnieuw (ongeveer "
      "40 seconden)",
      "O progresso desde o último ponto de controlo perde-se e o jogo volta a carregar (cerca "
      "de 40 segundos)",
      "Прогресс с последней контрольной точки будет потерян, игра загрузится заново (около 40 "
      "секунд)",
      "Edistyminen viimeisimmän tarkistuspisteen jälkeen menetetään ja peli latautuu uudelleen "
      "(noin 40 sekuntia)",
      "Framsteg sedan senaste kontrollpunkten går förlorade och spelet laddas om (cirka 40 "
      "sekunder)",
      "Fremskridt siden sidste checkpoint går tabt, og spillet indlæses igen (cirka 40 "
      "sekunder)",
      "Fremgang siden siste sjekkpunkt går tapt, og spillet lastes inn på nytt (omtrent 40 "
      "sekunder)",
      "Postęp od ostatniego punktu kontrolnego przepadnie, a gra wczyta się ponownie (około 40 "
      "sekund)",
      "Son kontrol noktasından sonraki ilerleme kaybolur ve oyun yeniden yüklenir (yaklaşık 40 "
      "saniye)"}},
    {"Back to the start screen. Quit is there",
     {"Retour à l'écran de démarrage. Quitter s'y trouve",
      "Vuelve a la pantalla de inicio. Salir está allí",
      "Zurück zum Startbildschirm. Dort befindet sich Beenden",
      "Torna alla schermata iniziale. Esci si trova lì",
      "Terug naar het startscherm. Afsluiten staat daar", "Volta ao ecrã inicial. Sair está lá",
      "Назад к начальному экрану. Выход находится там",
      "Takaisin aloitusnäyttöön. Lopeta löytyy sieltä",
      "Tillbaka till startskärmen. Avsluta finns där",
      "Tilbage til startskærmen. Afslut findes der",
      "Tilbake til startskjermen. Avslutt finnes der",
      "Powrót do ekranu startowego. Tam jest Wyjdź", "Başlangıç ekranına döner. Çıkış oradadır"}},

    // ---- Help lines: Options page ----
    {"Opens the game's own Options screen there",
     {"Ouvre l'écran Options du jeu à cet endroit",
      "Abre ahí la pantalla Opciones del propio juego",
      "Öffnet dort den Optionsbildschirm des Spiels", "Apre lì la schermata Opzioni del gioco",
      "Opent daar het optiescherm van de game", "Abre aí o ecrã Opções do próprio jogo",
      "Открывает там экран настроек самой игры", "Avaa pelin oman asetusnäytön siitä kohdasta",
      "Öppnar spelets egen alternativskärm där", "Åbner spillets egen indstillingsskærm der",
      "Åpner spillets egen alternativskjerm der", "Otwiera tam ekran opcji samej gry",
      "Oyunun kendi Seçenekler ekranını orada açar"}},
    {"Your last save loads first; the game's Options screen then opens there",
     {"Votre dernière sauvegarde se charge d'abord ; l'écran Options du jeu s'ouvre ensuite à "
      "cet endroit",
      "Primero se carga tu última partida; luego se abre ahí la pantalla Opciones del juego",
      "Zuerst wird dein letzter Spielstand geladen; dann öffnet sich dort der Optionsbildschirm "
      "des Spiels",
      "Prima viene caricato l'ultimo salvataggio; poi la schermata Opzioni del gioco si apre lì",
      "Eerst wordt je laatste opslag geladen; daarna opent het optiescherm van de game daar",
      "Primeiro carrega a última gravação; depois o ecrã Opções do jogo abre aí",
      "Сначала загрузится последнее сохранение; затем экран настроек игры откроется там",
      "Viimeisin tallennus ladataan ensin; sitten pelin asetusnäyttö avautuu siitä kohdasta",
      "Din senaste sparfil laddas först; sedan öppnas spelets alternativskärm där",
      "Dit seneste gemte spil indlæses først; derefter åbner spillets indstillingsskærm der",
      "Siste lagring lastes inn først; deretter åpnes spillets alternativskjerm der",
      "Najpierw wczytuje się ostatni zapis; potem ekran opcji gry otwiera się tam",
      "Önce son kaydın yüklenir; ardından oyunun Seçenekler ekranı orada açılır"}},
    {"Available from the pause menu once a game has started",
     {"Disponible dans le menu pause une fois la partie commencée",
      "Disponible en el menú de pausa cuando haya empezado una partida",
      "Im Pausenmenü verfügbar, sobald ein Spiel begonnen wurde",
      "Disponibile dal menu di pausa a partita iniziata",
      "Beschikbaar in het pauzemenu zodra een spel is gestart",
      "Disponível no menu de pausa depois de começar um jogo",
      "Доступно в меню паузы после начала игры",
      "Käytettävissä taukovalikosta, kun peli on aloitettu",
      "Tillgängligt i pausmenyn när ett spel har startats",
      "Tilgængelig i pausemenuen, når et spil er startet",
      "Tilgjengelig i pausemenyen når et spill er startet",
      "Dostępne w menu pauzy po rozpoczęciu gry",
      "Oyun başladıktan sonra duraklatma menüsünden kullanılabilir"}},
    {"Frame rate, display, upscaling, effects and shadows",
     {"Fréquence d'images, affichage, mise à l'échelle, effets et ombres",
      "FPS, pantalla, reescalado, efectos y sombras",
      "Bildrate, Anzeige, Hochskalierung, Effekte und Schatten",
      "Frame rate, schermo, upscaling, effetti e ombre",
      "Framerate, weergave, opschaling, effecten en schaduwen",
      "Fotogramas, ecrã, upscaling, efeitos e sombras",
      "Частота кадров, экран, масштабирование, эффекты и тени",
      "Kuvataajuus, näyttö, skaalaus, tehosteet ja varjot",
      "Bildfrekvens, skärm, uppskalning, effekter och skuggor",
      "Billedhastighed, skærm, opskalering, effekter og skygger",
      "Bildefrekvens, skjerm, oppskalering, effekter og skygger",
      "Liczba klatek, ekran, skalowanie, efekty i cienie",
      "Kare hızı, ekran, yükseltme, efektler ve gölgeler"}},
    {"Change the keyboard and mouse bindings",
     {"Modifier les touches du clavier et de la souris",
      "Cambia las asignaciones de teclado y ratón", "Tastatur- und Mausbelegung ändern",
      "Cambia i comandi di tastiera e mouse", "Wijzig de toetsen van toetsenbord en muis",
      "Altera as teclas do teclado e do rato", "Изменить назначения клавиатуры и мыши",
      "Muuta näppäimistön ja hiiren määrityksiä", "Ändra tangentbords- och musbindningar",
      "Skift tastatur- og musetildelinger", "Endre tastatur- og musetildelinger",
      "Zmień przypisania klawiatury i myszy", "Klavye ve fare atamalarını değiştir"}},
    {"Difficulty, aim sensitivity, photo mode. {}",
     {"Difficulté, sensibilité de visée, mode photo. {}",
      "Dificultad, sensibilidad de apuntado, modo foto. {}",
      "Schwierigkeit, Zielempfindlichkeit, Fotomodus. {}",
      "Difficoltà, sensibilità della mira, modalità foto. {}",
      "Moeilijkheid, richtgevoeligheid, fotomodus. {}",
      "Dificuldade, sensibilidade da mira, modo de fotografia. {}",
      "Сложность, чувствительность прицела, фоторежим. {}",
      "Vaikeustaso, tähtäysherkkyys, valokuvatila. {}",
      "Svårighetsgrad, siktkänslighet, fotoläge. {}",
      "Sværhedsgrad, sigtefølsomhed, fototilstand. {}",
      "Vanskelighetsgrad, siktefølsomhet, fotomodus. {}",
      "Poziom trudności, czułość celowania, tryb foto. {}",
      "Zorluk, nişan hassasiyeti, fotoğraf modu. {}"}},
    {"Dialogue, music and effects volume, audio mix. {}",
     {"Volume des dialogues, de la musique et des effets, mixage audio. {}",
      "Volumen de diálogos, música y efectos, mezcla de audio. {}",
      "Lautstärke von Dialogen, Musik und Effekten, Audiomix. {}",
      "Volume di dialoghi, musica ed effetti, mix audio. {}",
      "Volume van dialogen, muziek en effecten, audiomix. {}",
      "Volume dos diálogos, música e efeitos, mistura de áudio. {}",
      "Громкость диалогов, музыки и эффектов, аудиомикс. {}",
      "Dialogin, musiikin ja tehosteiden voimakkuus, äänimiksaus. {}",
      "Volym för dialog, musik och effekter, ljudmix. {}",
      "Lydstyrke for dialog, musik og effekter, lydmix. {}",
      "Volum for dialog, musikk og effekter, lydmiks. {}",
      "Głośność dialogów, muzyki i efektów, miks dźwięku. {}",
      "Diyalog, müzik ve efekt sesi, ses karışımı. {}"}},
    {"Subtitles and the HUD. {}",
     {"Sous-titres et ATH. {}", "Subtítulos y HUD. {}", "Untertitel und HUD. {}",
      "Sottotitoli e HUD. {}", "Ondertitels en HUD. {}", "Legendas e HUD. {}",
      "Субтитры и HUD. {}", "Tekstitys ja HUD. {}", "Undertexter och HUD. {}",
      "Undertekster og HUD. {}", "Undertekster og HUD. {}", "Napisy i HUD. {}",
      "Altyazılar ve HUD. {}"}},
    {"Save Game, New Game and Load Game on the game's own Options screen",
     {"Sauvegarder, Nouvelle partie et Charger une partie sur l'écran Options du jeu",
      "Guardar partida, Nueva partida y Cargar partida en la pantalla Opciones del juego",
      "Spiel speichern, Neues Spiel und Spiel laden im Optionsbildschirm des Spiels",
      "Salva partita, Nuova partita e Carica partita nella schermata Opzioni del gioco",
      "Spel opslaan, Nieuw spel en Spel laden in het optiescherm van de game",
      "Guardar jogo, Novo jogo e Carregar jogo no ecrã Opções do jogo",
      "«Сохранить игру», «Новая игра» и «Загрузить игру» на экране настроек игры",
      "Tallenna peli, Uusi peli ja Lataa peli pelin omassa asetusnäytössä",
      "Spara spel, Nytt spel och Ladda spel på spelets egen alternativskärm",
      "Gem spil, Nyt spil og Indlæs spil på spillets egen indstillingsskærm",
      "Lagre spill, Nytt spill og Last inn spill på spillets egen alternativskjerm",
      "Zapisz grę, Nowa gra i Wczytaj grę na ekranie opcji gry",
      "Oyunun kendi Seçenekler ekranında Oyunu Kaydet, Yeni Oyun ve Oyun Yükle"}},
    {"Return to the previous menu",
     {"Retour au menu précédent", "Volver al menú anterior", "Zurück zum vorherigen Menü",
      "Torna al menu precedente", "Terug naar het vorige menu", "Voltar ao menu anterior",
      "Вернуться в предыдущее меню", "Palaa edelliseen valikkoon",
      "Tillbaka till föregående meny", "Tilbage til forrige menu", "Tilbake til forrige meny",
      "Wróć do poprzedniego menu", "Önceki menüye dön"}},

    // ---- Help lines: Controls page ----
    {"Confirm, then press the key or mouse button to use",
     {"Confirmez, puis appuyez sur la touche ou le bouton de souris à utiliser",
      "Confirma y pulsa la tecla o el botón del ratón que quieras usar",
      "Bestätigen, dann die gewünschte Taste oder Maustaste drücken",
      "Conferma, poi premi il tasto o il pulsante del mouse da usare",
      "Bevestig en druk dan op de gewenste toets of muisknop",
      "Confirma e depois prime a tecla ou o botão do rato a usar",
      "Подтвердите, затем нажмите нужную клавишу или кнопку мыши",
      "Vahvista ja paina sitten haluamaasi näppäintä tai hiiren painiketta",
      "Bekräfta och tryck sedan på önskad tangent eller musknapp",
      "Bekræft, og tryk derefter på den ønskede tast eller museknap",
      "Bekreft, og trykk deretter på ønsket tast eller museknapp",
      "Potwierdź, a potem naciśnij wybrany klawisz lub przycisk myszy",
      "Onayla, sonra kullanılacak tuşa veya fare düğmesine bas"}},
    {"Press the key or mouse button for this action. Esc keeps the current one.",
     {"Appuyez sur la touche ou le bouton de souris pour cette action. Esc conserve l'actuelle.",
      "Pulsa la tecla o el botón del ratón para esta acción. Esc mantiene la actual.",
      "Drücke die Taste oder Maustaste für diese Aktion. Esc behält die aktuelle bei.",
      "Premi il tasto o il pulsante del mouse per questa azione. Esc mantiene quello attuale.",
      "Druk op de toets of muisknop voor deze actie. Esc behoudt de huidige.",
      "Prime a tecla ou o botão do rato para esta ação. Esc mantém a atual.",
      "Нажмите клавишу или кнопку мыши для этого действия. Esc оставит текущую.",
      "Paina tämän toiminnon näppäintä tai hiiren painiketta. Esc säilyttää nykyisen.",
      "Tryck på tangenten eller musknappen för den här åtgärden. Esc behåller den nuvarande.",
      "Tryk på tasten eller museknappen til denne handling. Esc beholder den nuværende.",
      "Trykk på tasten eller museknappen for denne handlingen. Esc beholder den nåværende.",
      "Naciśnij klawisz lub przycisk myszy dla tej akcji. Esc zostawia obecny.",
      "Bu eylem için tuşa veya fare düğmesine bas. Esc mevcut olanı korur."}},

    // ---- Help lines: Graphics page ----
    {"Applies the next time the game starts",
     {"S'applique au prochain démarrage du jeu", "Se aplica la próxima vez que se inicie el juego",
      "Gilt ab dem nächsten Spielstart", "Si applica al prossimo avvio del gioco",
      "Geldt vanaf de volgende start van de game", "Aplica-se no próximo arranque do jogo",
      "Применится при следующем запуске игры", "Tulee voimaan pelin seuraavalla käynnistyksellä",
      "Gäller nästa gång spelet startas", "Gælder næste gang spillet startes",
      "Gjelder neste gang spillet startes", "Zadziała przy następnym uruchomieniu gry",
      "Oyun bir sonraki açılışta uygulanır"}},
    {"Applies at once",
     {"S'applique immédiatement", "Se aplica al instante", "Gilt sofort", "Si applica subito",
      "Geldt direct", "Aplica-se de imediato", "Применяется сразу", "Tulee voimaan heti",
      "Gäller direkt", "Gælder med det samme", "Gjelder med en gang", "Działa od razu",
      "Hemen uygulanır"}},
    {"Applies at once when not in fullscreen",
     {"S'applique immédiatement hors plein écran",
      "Se aplica al instante fuera de pantalla completa", "Gilt sofort, außer im Vollbild",
      "Si applica subito se non a schermo intero", "Geldt direct buiten volledig scherm",
      "Aplica-se de imediato fora do ecrã inteiro", "Применяется сразу, если не полный экран",
      "Tulee voimaan heti, kun koko näyttö ei ole käytössä", "Gäller direkt utanför helskärm",
      "Gælder med det samme uden for fuld skærm", "Gjelder med en gang utenfor fullskjerm",
      "Działa od razu poza pełnym ekranem", "Tam ekran dışında hemen uygulanır"}},
    {"Scales the 1080p picture to the window. Applies at once",
     {"Met l'image 1080p à l'échelle de la fenêtre. S'applique immédiatement",
      "Escala la imagen 1080p a la ventana. Se aplica al instante",
      "Skaliert das 1080p-Bild auf das Fenster. Gilt sofort",
      "Scala l'immagine 1080p alla finestra. Si applica subito",
      "Schaalt het 1080p-beeld naar het venster. Geldt direct",
      "Ajusta a imagem 1080p à janela. Aplica-se de imediato",
      "Масштабирует картинку 1080p под окно. Применяется сразу",
      "Skaalaa 1080p-kuvan ikkunaan. Tulee voimaan heti",
      "Skalar 1080p-bilden till fönstret. Gäller direkt",
      "Skalerer 1080p-billedet til vinduet. Gælder med det samme",
      "Skalerer 1080p-bildet til vinduet. Gjelder med en gang",
      "Skaluje obraz 1080p do okna. Działa od razu",
      "1080p görüntüyü pencereye ölçekler. Hemen uygulanır"}},
    {"Sharpening pass of the upscaler. Applies at once",
     {"Passe de netteté de la mise à l'échelle. S'applique immédiatement",
      "Pasada de nitidez del reescalado. Se aplica al instante",
      "Schärfungsdurchlauf der Hochskalierung. Gilt sofort",
      "Passaggio di nitidezza dell'upscaler. Si applica subito",
      "Verscherpingsstap van de opschaling. Geldt direct",
      "Passagem de nitidez do upscaling. Aplica-se de imediato",
      "Проход повышения резкости при масштабировании. Применяется сразу",
      "Skaalaimen terävöitysvaihe. Tulee voimaan heti", "Uppskalarens skärpepass. Gäller direkt",
      "Opskaleringens skarphedstrin. Gælder med det samme",
      "Oppskaleringens skarphetstrinn. Gjelder med en gang",
      "Etap wyostrzania przy skalowaniu. Działa od razu",
      "Yükselticinin keskinleştirme geçişi. Hemen uygulanır"}},
    {"Original keeps every effect; Sharp drops anti-aliasing; Minimal drops all",
     {"D'origine garde tous les effets ; Net retire l'anticrénelage ; Minimal les retire tous",
      "Original mantiene todos los efectos; Nítido quita el antialiasing; Mínimo los quita todos",
      "Original behält alle Effekte; Scharf entfernt die Kantenglättung; Minimal entfernt alle",
      "Originale mantiene tutti gli effetti; Nitido toglie l'anti-aliasing; Minimo li toglie "
      "tutti",
      "Origineel behoudt alle effecten; Scherp laat anti-aliasing weg; Minimaal laat alles weg",
      "Original mantém todos os efeitos; Nítido retira o anti-aliasing; Mínimo retira todos",
      "«Оригинал» сохраняет все эффекты; «Чётко» убирает сглаживание; «Минимум» убирает все",
      "Alkuperäinen säilyttää kaikki tehosteet; Terävä poistaa reunanpehmennyksen; Minimi "
      "poistaa kaikki",
      "Original behåller alla effekter; Skarp tar bort kantutjämning; Minimal tar bort alla",
      "Original beholder alle effekter; Skarp fjerner kantudjævning; Minimal fjerner alle",
      "Original beholder alle effekter; Skarp fjerner kantutjevning; Minimal fjerner alle",
      "Oryginalny zachowuje wszystkie efekty; Ostry wyłącza wygładzanie; Minimalny wyłącza "
      "wszystkie",
      "Orijinal tüm efektleri korur; Keskin kenar yumuşatmayı kaldırır; Asgari hepsini "
      "kaldırır"}},
    {"Edge smoothing pass. Off gives a sharper picture with visible stair-steps. Applies at once",
     {"Lissage des contours. Désactivé donne une image plus nette avec des crénelures visibles. "
      "S'applique immédiatement",
      "Suavizado de bordes. Desactivado da una imagen más nítida con dientes de sierra "
      "visibles. Se aplica al instante",
      "Kantenglättung. Aus ergibt ein schärferes Bild mit sichtbaren Treppenstufen. Gilt sofort",
      "Smussatura dei bordi. Disattivo dà un'immagine più nitida con scalettature visibili. Si "
      "applica subito",
      "Randverzachting. Uit geeft een scherper beeld met zichtbare kartels. Geldt direct",
      "Suavização de contornos. Desligado dá uma imagem mais nítida com serrilhado visível. "
      "Aplica-se de imediato",
      "Сглаживание краёв. «Выкл.» даёт более чёткую картинку с заметными лесенками. "
      "Применяется сразу",
      "Reunojen pehmennys. Pois antaa terävämmän kuvan, jossa näkyy sahalaitoja. Tulee voimaan "
      "heti",
      "Kantutjämning. Av ger skarpare bild med synliga trappsteg. Gäller direkt",
      "Kantudjævning. Fra giver et skarpere billede med synlige takker. Gælder med det samme",
      "Kantutjevning. Av gir et skarpere bilde med synlige trappetrinn. Gjelder med en gang",
      "Wygładzanie krawędzi. Wył. daje ostrzejszy obraz z widocznymi schodkami. Działa od razu",
      "Kenar yumuşatma geçişi. Kapalı, görünür basamaklarla daha keskin bir görüntü verir. "
      "Hemen uygulanır"}},
    {"Reflections on wet ground and puddles. Applies at once",
     {"Reflets sur le sol mouillé et les flaques. S'applique immédiatement",
      "Reflejos en suelo mojado y charcos. Se aplica al instante",
      "Spiegelungen auf nassem Boden und Pfützen. Gilt sofort",
      "Riflessi su terreno bagnato e pozzanghere. Si applica subito",
      "Reflecties op natte grond en plassen. Geldt direct",
      "Reflexos no chão molhado e em poças. Aplica-se de imediato",
      "Отражения на мокрой земле и в лужах. Применяется сразу",
      "Heijastukset märässä maassa ja lätäköissä. Tulee voimaan heti",
      "Reflektioner på våt mark och i pölar. Gäller direkt",
      "Refleksioner på våd jord og i vandpytter. Gælder med det samme",
      "Refleksjoner på våt bakke og i dammer. Gjelder med en gang",
      "Odbicia na mokrej ziemi i w kałużach. Działa od razu",
      "Islak zemin ve su birikintilerindeki yansımalar. Hemen uygulanır"}},
    {"Glow around fires and bright lights. Applies at once",
     {"Halo autour des feux et des lumières vives. S'applique immédiatement",
      "Resplandor alrededor de fuegos y luces intensas. Se aplica al instante",
      "Leuchten um Feuer und helle Lichter. Gilt sofort",
      "Bagliore intorno a fuochi e luci intense. Si applica subito",
      "Gloed rond vuur en felle lichten. Geldt direct",
      "Brilho em redor de fogos e luzes fortes. Aplica-se de imediato",
      "Свечение вокруг огня и ярких источников света. Применяется сразу",
      "Hehku tulien ja kirkkaiden valojen ympärillä. Tulee voimaan heti",
      "Sken kring eldar och starka ljus. Gäller direkt",
      "Skær omkring ild og kraftige lys. Gælder med det samme",
      "Glød rundt ild og sterke lys. Gjelder med en gang",
      "Poświata wokół ognia i jasnych świateł. Działa od razu",
      "Ateşlerin ve parlak ışıkların çevresindeki parlama. Hemen uygulanır"}},
    {"Unlimited lifts the game's own 30 FPS limit (Options > Video > Frame Rate). Applies at once",
     {"Illimité lève la limite de 30 FPS du jeu (Options > Vidéo > Fréquence d'images). "
      "S'applique immédiatement",
      "Ilimitado quita el límite de 30 FPS del propio juego (Opciones > Vídeo > Frecuencia de "
      "imagen). Se aplica al instante",
      "Unbegrenzt hebt das 30-FPS-Limit des Spiels auf (Optionen > Video > Bildrate). Gilt "
      "sofort",
      "Illimitato rimuove il limite di 30 FPS del gioco (Opzioni > Video > Frame rate). Si "
      "applica subito",
      "Onbeperkt heft de 30 FPS-limiet van de game op (Opties > Video > Framerate). Geldt direct",
      "Ilimitado remove o limite de 30 FPS do próprio jogo (Opções > Vídeo > Fotogramas). "
      "Aplica-se de imediato",
      "«Без лимита» снимает собственный лимит игры в 30 FPS (Настройки > Видео > Частота "
      "кадров). Применяется сразу",
      "Rajoittamaton poistaa pelin oman 30 FPS:n rajan (Asetukset > Video > Kuvataajuus). Tulee "
      "voimaan heti",
      "Obegränsad tar bort spelets egen gräns på 30 FPS (Alternativ > Video > Bildfrekvens). "
      "Gäller direkt",
      "Ubegrænset fjerner spillets egen grænse på 30 FPS (Indstillinger > Video > "
      "Billedhastighed). Gælder med det samme",
      "Ubegrenset fjerner spillets egen grense på 30 FPS (Alternativer > Video > "
      "Bildefrekvens). Gjelder med en gang",
      "Bez limitu znosi własny limit gry 30 FPS (Opcje > Wideo > Liczba klatek). Działa od razu",
      "Sınırsız, oyunun kendi 30 FPS sınırını kaldırır (Seçenekler > Video > Kare Hızı). Hemen "
      "uygulanır"}},
    {"Sets the three shadow options below. Original keeps the game's picture",
     {"Règle les trois options d'ombres ci-dessous. D'origine garde l'image du jeu",
      "Ajusta las tres opciones de sombras de abajo. Original mantiene la imagen del juego",
      "Stellt die drei Schattenoptionen darunter ein. Original behält das Bild des Spiels",
      "Imposta le tre opzioni delle ombre qui sotto. Originale mantiene l'immagine del gioco",
      "Stelt de drie schaduwopties hieronder in. Origineel behoudt het beeld van de game",
      "Define as três opções de sombras abaixo. Original mantém a imagem do jogo",
      "Задаёт три параметра теней ниже. «Оригинал» сохраняет картинку игры",
      "Asettaa alla olevat kolme varjoasetusta. Alkuperäinen säilyttää pelin kuvan",
      "Ställer in de tre skuggalternativen nedan. Original behåller spelets bild",
      "Indstiller de tre skyggeindstillinger nedenfor. Original bevarer spillets billede",
      "Stiller inn de tre skyggevalgene nedenfor. Original beholder spillets bilde",
      "Ustawia trzy opcje cieni poniżej. Oryginalny zachowuje obraz gry",
      "Aşağıdaki üç gölge seçeneğini ayarlar. Orijinal, oyunun görüntüsünü korur"}},
    {"How often sun shadows are redrawn: the largest cost of a frame. Applies at once",
     {"Fréquence de mise à jour des ombres du soleil : le plus gros coût d'une image. "
      "S'applique immédiatement",
      "Frecuencia con que se redibujan las sombras del sol: el mayor coste de un fotograma. Se "
      "aplica al instante",
      "Wie oft Sonnenschatten neu gezeichnet werden: der größte Kostenfaktor eines Bildes. Gilt "
      "sofort",
      "Frequenza di ridisegno delle ombre del sole: il costo maggiore di un frame. Si applica "
      "subito",
      "Hoe vaak zonschaduwen opnieuw worden getekend: de grootste kostenpost van een frame. "
      "Geldt direct",
      "Frequência com que as sombras do sol são redesenhadas: o maior custo de um fotograma. "
      "Aplica-se de imediato",
      "Как часто перерисовываются тени от солнца: самая затратная часть кадра. Применяется "
      "сразу",
      "Kuinka usein auringon varjot piirretään uudelleen: kuvan suurin kustannus. Tulee voimaan "
      "heti",
      "Hur ofta solskuggor ritas om: den största kostnaden i en bild. Gäller direkt",
      "Hvor ofte solskygger gentegnes: den største omkostning i et billede. Gælder med det "
      "samme",
      "Hvor ofte solskygger tegnes på nytt: den største kostnaden i et bilde. Gjelder med en "
      "gang",
      "Jak często cienie słońca są rysowane od nowa: największy koszt klatki. Działa od razu",
      "Güneş gölgelerinin ne sıklıkla yeniden çizildiği: bir karenin en büyük maliyeti. Hemen "
      "uygulanır"}},
    {"The same for shadows cast by lamps and fires. Applies at once",
     {"De même pour les ombres des lampes et des feux. S'applique immédiatement",
      "Lo mismo para las sombras de lámparas y fuegos. Se aplica al instante",
      "Dasselbe für Schatten von Lampen und Feuern. Gilt sofort",
      "Lo stesso per le ombre di lampade e fuochi. Si applica subito",
      "Hetzelfde voor schaduwen van lampen en vuur. Geldt direct",
      "O mesmo para as sombras de candeeiros e fogos. Aplica-se de imediato",
      "То же для теней от ламп и огня. Применяется сразу",
      "Sama lamppujen ja tulien varjoille. Tulee voimaan heti",
      "Samma för skuggor från lampor och eldar. Gäller direkt",
      "Det samme for skygger fra lamper og ild. Gælder med det samme",
      "Det samme for skygger fra lamper og ild. Gjelder med en gang",
      "To samo dla cieni rzucanych przez lampy i ogień. Działa od razu",
      "Lambaların ve ateşlerin gölgeleri için aynısı. Hemen uygulanır"}},
    {"Reduced and Low leave small objects out of the sun's shadows. Applies at once",
     {"Réduit et Faible excluent les petits objets des ombres du soleil. S'applique "
      "immédiatement",
      "Reducido y Bajo dejan los objetos pequeños fuera de las sombras del sol. Se aplica al "
      "instante",
      "Reduziert und Niedrig lassen kleine Objekte aus den Sonnenschatten weg. Gilt sofort",
      "Ridotto e Basso escludono gli oggetti piccoli dalle ombre del sole. Si applica subito",
      "Verlaagd en Laag laten kleine objecten weg uit de zonschaduwen. Geldt direct",
      "Reduzido e Baixo deixam os objetos pequenos fora das sombras do sol. Aplica-se de "
      "imediato",
      "«Сниженная» и «Низкая» исключают мелкие объекты из теней от солнца. Применяется сразу",
      "Vähennetty ja Matala jättävät pienet kohteet pois auringon varjoista. Tulee voimaan heti",
      "Reducerad och Låg utelämnar små föremål ur solskuggorna. Gäller direkt",
      "Reduceret og Lav udelader små genstande fra solskyggerne. Gælder med det samme",
      "Redusert og Lav utelater små gjenstander fra solskyggene. Gjelder med en gang",
      "Zmniejszone i Niskie pomijają małe obiekty w cieniach słońca. Działa od razu",
      "Azaltılmış ve Düşük, küçük nesneleri güneş gölgelerinin dışında bırakır. Hemen "
      "uygulanır"}},
    {"Frame rate cap and VSync changes take effect the next time the game starts.",
     {"Les changements de limite de FPS et de VSync s'appliquent au prochain démarrage du jeu.",
      "Los cambios de límite de FPS y VSync se aplican la próxima vez que se inicie el juego.",
      "Änderungen an FPS-Limit und VSync gelten ab dem nächsten Spielstart.",
      "Le modifiche a limite FPS e VSync si applicano al prossimo avvio del gioco.",
      "Wijzigingen in FPS-limiet en VSync gelden vanaf de volgende start van de game.",
      "As alterações ao limite de FPS e ao VSync aplicam-se no próximo arranque do jogo.",
      "Изменения лимита FPS и VSync применятся при следующем запуске игры.",
      "FPS-rajan ja VSyncin muutokset tulevat voimaan pelin seuraavalla käynnistyksellä.",
      "Ändringar av FPS-gräns och VSync gäller nästa gång spelet startas.",
      "Ændringer af FPS-grænse og VSync gælder næste gang spillet startes.",
      "Endringer av FPS-grense og VSync gjelder neste gang spillet startes.",
      "Zmiany limitu FPS i VSync zadziałają przy następnym uruchomieniu gry.",
      "FPS sınırı ve VSync değişiklikleri oyun bir sonraki açılışta geçerli olur."}},

    // ---- First start: the opening lines ----
    {"we waited, yet no one answered",
     {"nous avons attendu, mais personne n'a répondu", "esperamos, pero nadie respondió",
      "wir haben gewartet, doch niemand hat geantwortet",
      "abbiamo aspettato, ma nessuno ha risposto", "we wachtten, maar niemand antwoordde",
      "esperámos, mas ninguém respondeu", "мы ждали, но никто не ответил",
      "odotimme, mutta kukaan ei vastannut", "vi väntade, men ingen svarade",
      "vi ventede, men ingen svarede", "vi ventet, men ingen svarte",
      "czekaliśmy, lecz nikt nie odpowiedział", "bekledik, ama kimse cevap vermedi"}},
    {"sometimes, we have to do things ourselves",
     {"parfois, il faut faire les choses soi-même",
      "a veces, tenemos que hacer las cosas nosotros mismos",
      "manchmal müssen wir die Dinge selbst tun", "a volte, dobbiamo fare le cose da soli",
      "soms moeten we dingen zelf doen", "por vezes, temos de fazer as coisas nós mesmos",
      "иногда приходится всё делать самим", "joskus asiat on tehtävä itse",
      "ibland måste vi göra saker själva", "nogle gange må vi gøre tingene selv",
      "noen ganger må vi gjøre ting selv", "czasem musimy zrobić coś sami",
      "bazen işleri kendimiz yapmak zorundayız"}},
};

/// The column of a console language id, -1 for English and for languages the game lacks.
int ColumnOf(int language) {
    switch (language) {
    case 2:  // French
    case 22: // French (Canada)
        return French;
    case 3:  // Spanish
    case 20: // Spanish (Latin America)
        return Spanish;
    case 4:
        return German;
    case 5:
        return Italian;
    case 6:
        return Dutch;
    case 7:  // Portuguese (Portugal)
    case 17: // Portuguese (Brazil)
        return Portuguese;
    case 8:
        return Russian;
    case 12:
        return Finnish;
    case 13:
        return Swedish;
    case 14:
        return Danish;
    case 15:
        return Norwegian;
    case 16:
        return Polish;
    case 19:
        return Turkish;
    default: // 1 and 18 are English; the rest have no table
        return -1;
    }
}

int Placeholders(std::string_view text) {
    int count = 0;
    for (size_t at = text.find("{}"); at != std::string_view::npos; at = text.find("{}", at + 2)) {
        count++;
    }
    return count;
}

/// The table by English text. A text that is missing, or whose placeholders differ from the
/// English ones (fmt would throw on it), is dropped here so that English is shown instead.
const std::unordered_map<std::string_view, const Entry*>& Table() {
    static const auto table = [] {
        std::unordered_map<std::string_view, const Entry*> map;
        for (Entry& entry : Entries) {
            for (int column = 0; column < NumColumns; column++) {
                const char*& text = entry.text[column];
                if (text == nullptr || Placeholders(text) != Placeholders(entry.english)) {
                    LOG_ERROR(Frontend, "menu language: \"{}\" has no usable text in column {}",
                              entry.english, column);
                    text = nullptr;
                }
            }
            map.emplace(entry.english, &entry);
        }
        return map;
    }();
    return table;
}

} // namespace

int Current() {
    static const int language = EmulatorSettings.GetConsoleLanguage();
    return language;
}

const char* Tr(const char* english) {
    static const int column = ColumnOf(Current());
    if (column < 0 || english == nullptr) {
        return english;
    }
    const auto& table = Table();
    const auto it = table.find(english);
    if (it == table.end() || it->second->text[column] == nullptr) {
        return english;
    }
    return it->second->text[column];
}

std::string Tr(const std::string& english) {
    return Tr(english.c_str());
}

std::string Upper(const std::string& text) {
    // Turkish has a dotted capital for i; the dotless one belongs to its own small letter.
    const bool turkish = Current() == 19;
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == 'i' && turkish) {
                out += "İ";
            } else {
                out += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
            }
            continue;
        }
        if ((c & 0xE0) != 0xC0 || i + 1 >= text.size()) {
            // Not the start of a two-byte character: the letters handled here all have two.
            out += static_cast<char>(c);
            continue;
        }
        unsigned code = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(text[++i]) & 0x3Fu);
        if (code == 0xDF) { // sharp s
            out += "SS";
            continue;
        }
        if (code == 0x131) { // dotless i
            out += 'I';
            continue;
        }
        if (code >= 0xE0 && code <= 0xFE && code != 0xF7) {
            code -= 0x20; // Latin-1
        } else if (code == 0xFF) {
            code = 0x178;
        } else if ((code >= 0x100 && code <= 0x137) || (code >= 0x14A && code <= 0x177)) {
            code &= ~1u; // Latin Extended-A: capital even, small odd
        } else if ((code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E)) {
            code -= (code & 1u) == 0; // Latin Extended-A: capital odd, small even
        } else if (code >= 0x430 && code <= 0x44F) {
            code -= 0x20; // Cyrillic
        } else if (code >= 0x450 && code <= 0x45F) {
            code -= 0x50;
        }
        out += static_cast<char>(0xC0 | code >> 6);
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

} // namespace I3::Lang
