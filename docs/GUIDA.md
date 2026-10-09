# Guida a LMU Overlay

Un overlay leggero per Le Mans Ultimate: classifica, relative, delta, settori, strategia, danni e
altro, disegnati sopra il gioco. È un programma separato, quindi non carica niente dentro LMU e non
dovrebbe causare micro-scatti.

Indice

- [Installazione e aggiornamenti](#installazione-e-aggiornamenti)
- [Primi passi](#primi-passi)
- [Profili: prove, qualifica, gara](#profili-prove-qualifica-gara)
- [La finestra Impostazioni](#la-finestra-impostazioni)
- [Colori dei tempi](#colori-dei-tempi)
- [I widget](#i-widget), uno per uno
- [Opzioni comuni a tutti i widget](#opzioni-comuni-a-tutti-i-widget)
- [Tasti del volante](#tasti-del-volante)
- [Problemi frequenti](#problemi-frequenti)
- [Dove salva i dati](#dove-salva-i-dati)

---

## Installazione e aggiornamenti

1. Scarica `LmuOverlay.exe` dall'[ultima release](https://github.com/Pagh/LmuOverlay/releases/latest).
2. Mettilo in una **cartella tutta sua** (es. `Documenti\LmuOverlay`): accanto all'exe salva impostazioni,
   profili, record dei giri e log.
3. Avvialo. Windows SmartScreen può avvisare perché l'exe non è firmato: *Ulteriori informazioni* →
   *Esegui comunque*.

Puoi avviarlo prima o dopo LMU. Se lo avvii di nuovo mentre è già aperto, si apre la finestra Impostazioni.

**Aggiornamenti.** All'avvio l'overlay controlla se c'è una versione nuova e, se c'è, mostra una notifica
vicino all'orologio. Per installarla: Impostazioni → **General** → **Updates** → **Update now**. L'exe
viene sostituito e riavviato da solo; impostazioni, profili e record restano.

## Primi passi

**Tasti rapidi**

| Tasti | Cosa fa |
|---|---|
| Ctrl+Alt+E | Sposta i widget (trascinali col mouse; la posizione si salva da sola). Premi di nuovo per finire |
| Ctrl+Alt+O | Mostra / nascondi l'overlay |
| Ctrl+Alt+D | Cambia il giro di riferimento del delta: tuo best (LMU) → best della sessione → best di sempre → giro più veloce della lobby → ultimo giro |
| Ctrl+Alt+P | Passa al profilo successivo (finché non cambia il tipo di sessione) |
| Ctrl+Alt+Q | Chiudi l'overlay |

**Icona vicino all'orologio**: doppio clic apre le Impostazioni; clic destro apre il menu (Impostazioni,
Sposta widget, Mostra overlay, Ricarica impostazioni, Esci).

**Spostare i widget.** Premi Ctrl+Alt+E: lo schermo si scurisce e tutti i widget attivi compaiono con un
bordo giallo, anche quelli che normalmente appaiono solo in certe situazioni. Trascinali dove vuoi; si
agganciano a una griglia di 4 px. In gioco, apri prima un menu o metti in pausa così LMU libera il mouse.

**Monitor.** L'overlay segue automaticamente il monitor su cui c'è LMU. Le posizioni predefinite sono
pensate per 1920×1080: su risoluzioni diverse sposta i widget o usa **Global scale** (General).

## Profili: prove, qualifica, gara

Un profilo è un layout completo: quali widget sono accesi, dove stanno e come sono configurati.
Al primo avvio vengono creati tre profili, **Practice**, **Qualifying** e **Race**, e l'overlay passa da
solo a quello giusto quando cambia la sessione.

Idee utili:

- In **qualifica** su LMU sei da solo in pista: conviene un profilo pulito, solo classifica, delta,
  settori, gomme (con il badge READY TO PUSH) e danni. Spegni relative, radar, mappa e avviso classe.
- In **gara** accendi tutto quello che riguarda il traffico (relative, radar, avviso classe più veloce,
  mappa) e la strategia.
- Dalla scheda **Profiles** puoi creare, duplicare, rinominare ed eliminare profili e scegliere quale usare
  per ogni tipo di sessione (oppure sempre lo stesso).

## La finestra Impostazioni

Si apre con doppio clic sull'icona o rilanciando l'exe. Le modifiche si applicano subito e si salvano da
sole. Con **Preview** attivo l'overlay resta visibile mentre modifichi, con dati finti se non sei in pista.

- **Widgets**: spunta un widget per accenderlo, clicca il nome per configurarlo. *Reset to defaults*
  rimette le opzioni originali; *Copy to all profiles* copia l'aspetto del widget (non la posizione) in
  tutti i profili.
- **Profiles**: gestione dei profili e scelta del profilo per Practice / Qualifying / Race.
- **General**:
  - *Display*: monitor, **Global scale** (ingrandisce tutto), **Show only while driving** (nasconde
    l'overlay nei menu e in garage).
  - *Performance*: frequenza di lettura dei dati, massimo ridisegni al secondo, priorità bassa, limitare
    l'overlay a certi thread della CPU. I valori predefiniti vanno bene per quasi tutti.
  - *LMU REST API*: legge da LMU danni aerodinamici, sospensioni e tempo di riparazione. Lascialo acceso.
  - *Startup*: aprire questa finestra all'avvio, notifica di avvio.
  - *Wheel buttons*: tasti del volante (vedi sotto).
  - *Updates*: versione installata, controllo all'avvio, **Check now**, **Update now**.
  - *Files*: **Open settings folder** apre la cartella con impostazioni, profili e log.

## Colori dei tempi

Valgono in tutti i widget:

- **Viola**: il più veloce della tua classe in questa sessione.
- **Verde**: il miglior tempo personale di quel pilota.
- **Giallo** (settori): più lento del riferimento.

---

## I widget

Le opzioni hanno i nomi in inglese come nella finestra Impostazioni. Tra parentesi il valore predefinito.

### Standings (classifica)

In alto la **scheda sessione**: tipo di sessione, giro attuale / giri stimati (es. `Lap 5 / ≈ 24`), tempo
rimanente, durata della gara, se serve un pit stop oppure no, temperatura pista / aria e pioggia.

**Prima della partenza** mostra **RACE LENGTH** (giri stimati) e **FILL UP WITH** (quanta benzina o energia
mettere, calcolata per la gara più un giro di margine). Così sai sempre quanto caricare.

Sotto, la tua classe: i primi della classe più le macchine intorno a te, con numero, marca, miglior giro,
ultimo giro, distacco, **carburante ed energia virtuale** rimasti (`62  48%`: carburante a sinistra, energia
in blu a destra) e **gomme** (mescola S / M / H / W, anteriore e posteriore se diverse, e battistrada
rimasto). In gara il distacco è il tempo in pista; in prova e qualifica è la differenza tra i migliori giri.

| Opzione | Cosa fa |
|---|---|
| Rows (10) | Righe mostrate |
| Always show top (3) | Quanti leader mostrare sempre prima delle macchine intorno a te |
| Session info / Track / air temperature and rain | Mostra la scheda sessione e il meteo |
| Column: car number / car make / best lap / last lap / fuel / tyres | Accende o spegne le colonne |
| Show pit stop count | Numero di soste di ogni pilota |
| Gap to (leader) | Distacco dal leader o dalla macchina davanti |
| Gap decimals / Lap time decimals (3) | Decimali di distacchi e tempi |

### Delta

Una barra larga e bassa: a sinistra il riferimento, al centro il **delta** grande (verde se sei più
veloce, rosso se più lento), a destra il **tempo previsto del giro** (riferimento + delta). Sotto, una
barra sottile che si riempie in base al delta.

- Se tagli e il giro viene invalidato, il tempo resta visibile con la scritta `LAP · INVALID`.
- **Uscita dai box**: se hai già un giro registrato, il delta riparte dal settore 2
  (`OUT LAP · delta from S2`), così vedi subito come stai andando.
- Il riferimento si cambia al volo con Ctrl+Alt+D o con un tasto del volante.

Riferimenti disponibili:

- **your best (LMU)**: il delta di LMU sul tuo miglior giro.
- **session best**: il tuo miglior giro di questa sessione.
- **all-time best**: il tuo miglior giro di sempre su questa pista con una macchina della stessa
  classe (es. tutte le LMP3 insieme), salvato tra una sessione e l'altra. I consumi invece restano
  per singola macchina. Per recuperare i giri fatti prima dell'overlay (anche con TinyPedal o GoFast)
  dai risultati salvati da LMU: `py tools\import_results.py --write`, a overlay chiuso.
- **lobby fastest lap**: il giro più veloce della tua classe nella lobby. Funziona per i giri fatti mentre
  l'overlay era aperto.
- **last lap**: il tuo ultimo giro valido, per essere **costante** in gara. Accanto al nome compare anche
  la **dispersione** degli ultimi 5 giri puliti (`vs LAST LAP ±0.21`): più è bassa, più sei costante.
  I giri dei box, nel traffico o con un errore (oltre il 107 % del tuo best) non contano.

| Opzione | Cosa fa |
|---|---|
| Reference lap (your best) | Riferimento iniziale |
| Decimals (3) | Decimali del delta |
| Width (440 px) | Larghezza della barra |
| Bar range (2.0 s) | Delta a cui la barra è piena |
| Show bar / Show reference / Show the lap you're on | Accende le singole parti |

### Track info (info pista)

Una piccola barra sotto il delta che compare **solo quando serve**:

- **Pericoli davanti a te con la distanza in metri**: `STOPPED CAR 140 m`, `SLOW CAR 90 m`,
  `YELLOW in 180 m`. Quello che è dietro non viene mai segnalato.
- Bandiera blu, Full Course Yellow, bandiera rossa, fasi della gara (griglia, giro di formazione, partenza).
- Punti track limits (`TL 4/12`) e penalità (`PEN`).
- In prova e qualifica: `✓ LAP VALID` / `✕ LAP INVALID`.
- Per 2,5 secondi mostra anche il profilo o il riferimento del delta appena scelto.

| Opzione | Cosa fa |
|---|---|
| Warn about dangers ahead within (250 m) | Distanza massima entro cui avvisare |
| Slow car below (60 km/h) | Sotto questa velocità una macchina è un pericolo |
| Track-limit points / Track limits even at 0 | Mostra i punti track limits (anche a zero) |
| Penalties / Lap valid / invalid | Mostra penalità e validità del giro |

### Sectors (settori)

**Compact** (predefinito): tre caselle con i settori del giro in corso confrontati con il riferimento
(verde più veloce, giallo più lento, viola miglior settore della classe), più ultimo giro, miglior giro e
**IDEAL**. IDEAL è la somma dei tuoi migliori settori di sempre su questa pista: il tuo giro teorico.

**Table**: una tabella con giro attuale, ultimo giro, best della sessione, best di sempre e best della lobby.

Il riferimento viene fissato all'inizio di ogni giro, quindi il confronto non cambia a metà giro. In uscita
dai box S1 mostra `OUT LAP` e il confronto parte da S2.

| Opzione | Cosa fa |
|---|---|
| Layout (compact) | Compact o table |
| Compare this lap to (same as Delta) | Riferimento: lo stesso del Delta, best sessione, best di sempre, best lobby, ultimo giro |
| Table row: ... | Righe della tabella |
| Show ideal lap | Mostra IDEAL |
| Decimals (3) | Decimali |

### Track map (mappa)

Il tracciato con tutte le macchine nel colore della loro classe e i settori in giallo evidenziati. La mappa
si **impara da sola** dalle posizioni delle macchine: la prima volta su una pista nuova si completa in un
giro o due, poi resta salvata.

Opzioni: dimensione (200 px), evidenziare i settori gialli, dimensione dei punti, colori.

### Relative

Le macchine più vicine a te in pista, davanti e dietro: posizione, numero, marca, distacco, ultimo giro,
miglior giro e i **settori in tempo reale**. I settori già fatti nel giro in corso sono accesi, quelli del
giro precedente sono attenuati.

- La colonna **PER LAP** dice quanto il distacco è cambiato nell'ultimo giro (`▲` / `▼`): capisci subito se
  stai recuperando o perdendo.
- In gara, arancione = macchina che ti ha doppiato, azzurro = macchina che hai doppiato.
- I distacchi si aggiornano in continuo, non solo quando LMU aggiorna la classifica.

| Opzione | Cosa fa |
|---|---|
| Cars ahead / behind (3) | Macchine mostrate per lato |
| Position shown (class) | Posizione di classe o assoluta |
| Column: ... | Numero, marca, ultimo / miglior giro, settori, PER LAP |
| Column: fuel / tyres (spente) | Carburante / energia e gomme degli altri, come nella classifica. Allargano il widget: controlla che non copra Inputs |
| Class colour stripe | Striscia col colore della classe |

### Faster class warning (avviso classe più veloce)

Una pillola rossa quando una macchina di una classe più veloce si avvicina da dietro, per esempio
`HYPERCAR 1.8 s behind`. Opzione: **Warn when closer than** (3,0 s).

### Radar

Due barre laterali che si accendono quando hai una macchina affiancata: `CAR LEFT`, `CAR RIGHT`, `3-WIDE`.

| Opzione | Cosa fa |
|---|---|
| Distance between the bars (880 px) | Quanto sono distanti le due barre |
| Bar height (180 px) | Altezza |
| Car length (4,9 m) / Side distance (6 m) | Quando una macchina conta come affiancata |
| CAR LEFT / RIGHT text | Mostra la scritta |

### Inputs & car (comandi e auto)

- Barra dei giri motore con luce di cambiata, marcia e velocità.
- Griglia aiuti: **TC**, **CUT**, **SLIP**, **ABS**, **BIAS** (ripartizione freni), **MAP**, temperatura
  **WATER** e **OIL** (o batteria sulle ibride). Si illuminano quando intervengono o sono troppo alti.
- Grafico di **acceleratore e freno** degli ultimi 12 secondi, utile per vedere come freni e come riapri.
  I **bloccaggi** sono puntini gialli sulla linea del freno (una ruota gira molto più piano della
  macchina), il **pattinamento** puntini azzurri sulla linea dell'acceleratore (una ruota gira più veloce).

| Opzione | Cosa fa |
|---|---|
| Speed unit (km/h) | km/h o mph |
| Driver aids, bias, temperatures | Mostra la griglia aiuti |
| Throttle / brake trace / Trace length (12 s) / Trace width (340 px) | Grafico dei pedali |
| Steering in the trace | Aggiunge lo sterzo al grafico |
| Mark lock-ups and wheel spin / thresholds (15 %) | Puntini di bloccaggio e pattinamento; la soglia è lo slittamento minimo |
| Shift light: yellow / red / shift now | A che frazione del limitatore si accende la luce |
| Water / Oil hot above (105 / 125 °C) | Soglie di allarme |

### Tyres & brakes (gomme e freni)

Per ogni gomma: temperatura (con sinistra / centro / destra), pressione, battistrada rimasto e
temperatura del freno. Blu = fredda, verde = in finestra, rosso = calda. La finestra parte dalla
**temperatura ottimale della mescola** che LMU comunica (ottimale −20 / +15 °C). Con **"Ready to push" badge**
mostra `READY TO PUSH`, `WARMING UP` o `TOO HOT`: comodo in qualifica.

LMU espone tre temperature diverse: **Inner layer** (strato di gomma sopra la carcassa, stabile: è
quella da guardare), **Surface** (pelle del battistrada, sale e scende dentro una curva) e
**Carcass** (carcassa, lentissima). A ogni giro il log (`bin\logs\overlay.log`, riga `tyres`) le
scrive tutte e tre, con l'ottimale.

| Opzione | Cosa fa |
|---|---|
| Temperature shown (Inner layer) | Quale temperatura mostrare |
| Window from the tyre's optimal temperature (sì) / below (20) / above (15 °C) | Finestra intorno all'ottimale |
| Fixed: tyre cold below / hot above (70 / 105 °C) | Finestra fissa, se l'ottimale non c'è o l'opzione è spenta |
| Pressure unit (kPa) | kPa, psi o bar |
| Show brake temperature / Brake hot above (800 °C) | Freni |
| "Ready to push" badge | Badge pronto a spingere |

### Strategy (strategia)

**In gara** usa la gara vera; in prova e qualifica usa il piano gara che imposti tu (vedi sotto). LMU non dice se il pit stop è obbligatorio, quindi l'overlay lo calcola: giri stimati
della gara × consumo per giro, confrontato con un pieno (o il 100 % di energia virtuale). Nelle gare a
tempo conta che, allo scadere, il leader assoluto finisce il giro.

- **Serve una sosta**: mostra carburante / energia, giri alla fine, quante soste, la finestra di pit
  (`Pit window`, `BOX THIS LAP` quando è il momento), quanto aggiungere alla sosta e il tempo della sosta.
- **Non serve**: una sola riga di controllo carburante (`+1.2 lap spare` o quanto manca).

Il consumo viene misurato sui giri puliti e salvato per pista e macchina. Finché non c'è, usa la stima
del garage di LMU. Il calcolo del passo usa i tuoi giri di gara, altrimenti il tuo best della sessione o
di sempre.

Opzione: **One-line fuel check when no stop is needed** (acceso).

**In prova e qualifica: il piano gara.** Prova e qualifica non sanno che gara verrà dopo, quindi la
imposti tu: Impostazioni → General → **Race plan** → *Time* (minuti) oppure *Laps* (giri). Strategy
mostra allora `RACE PLAN · 1 h` con:

- giri che farai (al tuo passo della sessione) e consumo per giro di carburante / energia;
- quanti giri dura un pieno;
- con cosa partire, oppure `full` se serve una sosta;
- `NO STOP` con i giri di margine all'arrivo, oppure il numero di soste, entro che giro fermarti la
  prima volta, quanto aggiungere a ogni sosta e il tempo della sosta.

Si aggiorna giro dopo giro mentre il passo e il consumo si stabilizzano. Finché manca un giro pulito
mostra `drive a clean lap to measure`. Nella finestra Impostazioni, sotto il piano, vedi subito il
risultato (es. "about 29 laps, 1 pit stop").

### Damage (danni)

Compare **solo dopo un contatto**: sagoma dell'auto con le zone colpite, danno aerodinamico e alle
sospensioni in %, tempo di riparazione e **quanto tempo perdi al giro** rispetto a prima del danno.
Aero, sospensioni e riparazione richiedono *LMU REST API* acceso (General).

Dopo **ogni tocco** (con un'altra macchina o un muro) compare per qualche secondo anche se non ci sono
danni: `CONTACT · NO DAMAGE` con la forza dell'urto (`hit 240`), così sai subito se il contatto ti ha
fatto danni. In basso conta i contatti della sessione.

| Opzione | Cosa fa |
|---|---|
| Show when there's no damage (spento) | Sempre visibile |
| Show after a contact (acceso) / Contact shown for (6 s) | Compare dopo un tocco e per quanto |
| Ignore contacts weaker than (20) | Ignora i colpetti (cordoli, sfioramenti) |

### Fuel / Energy (carburante, spento di default)

Tabella dettagliata di carburante ed energia. In gara Strategy mostra già tutto, quindi è spento.

### Performance (spento di default)

Quanto costa l'overlay: tempo di disegno e quanto a lungo è stato tenuto il blocco dei dati di LMU. Serve
solo per diagnosi.

---

## Opzioni comuni a tutti i widget

| Opzione | Cosa fa |
|---|---|
| Enabled | Acceso / spento |
| X position / Y position | Posizione in pixel dall'angolo in alto a sinistra del monitor (più comodo spostarli con Ctrl+Alt+E) |
| Scale | Ingrandisce o rimpicciolisce il singolo widget (0,5–3) |
| Update interval (ms) | Ogni quanto controlla se è cambiato qualcosa. Ridisegna solo quando un valore cambia |
| Background | Colore e trasparenza dello sfondo |
| Colours | I colori specifici del widget |

## Tasti del volante

Puoi usare i tasti del volante o di una button box per:

- riferimento del delta successivo / precedente;
- profilo successivo;
- mostra / nascondi overlay.

Impostazioni → General → **Wheel buttons** → **Set**, poi premi il tasto. I tasti vengono letti in
sottofondo senza togliere il controllo del volante a LMU, quindi puoi usarne uno che in gioco non fa nulla.

## Problemi frequenti

**Non vedo l'overlay in gioco.** Imposta LMU in modalità finestra senza bordi (*Borderless*): lo schermo
intero esclusivo copre le altre finestre. Controlla anche che *Show only while driving* non lo nasconda
nei menu e che non l'abbia nascosto con Ctrl+Alt+O.

**I widget sono fuori posto / troppo piccoli.** Le posizioni predefinite sono per 1920×1080: usa
Ctrl+Alt+E per spostarli e *Global scale* per ingrandire tutto.

**Il delta "lobby" o la mappa non ci sono.** Si costruiscono mentre l'overlay è aperto: servono almeno un
giro della lobby e un giro o due sulla pista nuova.

**Strategy dice "measuring use per lap…".** Fai un giro pulito: il consumo viene misurato e poi ricordato
per quella pista e macchina.

**Qualcosa non torna.** Manda il file `logs\overlay.log`: contiene sessioni, una riga per giro con i calcoli
della strategia e, ogni minuto, le prestazioni dell'overlay. Registra anche i blocchi di LMU e i reset del
driver video segnalati da Windows.

## Dove salva i dati

Tutto nella cartella dell'exe (Impostazioni → General → **Open settings folder**):

| File | Contenuto |
|---|---|
| `settings.ini` | Impostazioni generali e quale profilo usare per ogni sessione |
| `profiles\*.ini` | I profili, un blocco per widget |
| `records\` | Migliori giri e settori di sempre, consumi, mappe delle piste |
| `logs\overlay.log` | Log diagnostico |

Per rimettere un widget com'era usa *Reset to defaults*. Per ripartire da zero con tutti i profili, chiudi
l'overlay e svuota la cartella `profiles`: al prossimo avvio vengono ricreati Practice, Qualifying e Race
con i valori predefiniti. Gli aggiornamenti non toccano questi file.

## Salvare una sessione

LMU salva da solo ogni sessione in `UserData\Log\Results` (un file `.xml` con tutti i giri) e il replay
in `UserData\Replays`, ma i replay vecchi prima o poi spariscono. Per tenere un evento (prove, qualifica
e gara) con un riassunto leggibile, a gioco chiuso o nel menu:

```
py tools\session_report.py
```

Crea la cartella `LMU - Sessioni\<data> <pista> - <macchina>` accanto alla cartella dell'overlay con i
file dei risultati, i replay e `report.html`: posizione, i tuoi giri con i settori (migliori in viola),
giro ideale, media dei giri puliti e costanza, penalità, track limits e la classifica di classe.
`--no-replays` salva solo risultati e report (i replay di una gara lunga pesano quasi 1 GB). Per
rivedere un replay salvato, rimetti il `.Vcr` in `UserData\Replays`.
