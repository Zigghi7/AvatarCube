# Avatar Wardrobe — installazione e prova su MetaHuman (UE 5.6)

## Novità v1.0.4 — Anteprima texture delle varianti materiale

Ogni elemento di `Material Variants` espone ora `Preview Texture`. Il thumbnail del Content Drawer è editor-only e non viene incluso automaticamente nel gioco: assegna quindi una `Texture2D` che rappresenti il materiale. Il widget `ColorPreview` la carica come soft reference e la mostra mantenendo i colori originali. Se `Preview Texture` è vuota, resta disponibile il precedente fallback a tinta unita.

Configurazione consigliata della texture: `Texture Group = UI`, `Compression Settings = UserInterface2D (RGBA)`, `Mip Gen Settings = NoMipmaps`, `sRGB = true`.


Questa prima versione è un plugin runtime C++ autosufficiente. Gestisce:

- apertura/chiusura con il tasto **I**;
- categorie Tops, Bottoms, Shoes, Vests, Accessories;
- sottocategorie tramite menu a tendina;
- griglia UMG dei capi;
- caricamento asincrono delle Skeletal Mesh e dei materiali;
- equipaggiamento per categoria;
- varianti materiale e tinta tramite parametro del materiale;
- due modalità di posa:
  - **Leader Pose**, più leggera;
  - **Copy Pose**, per capi che devono eseguire un Anim Blueprint proprio, inclusi i capi Skeletal Mesh con cloth simulation.

## Limite della versione 1

Il plugin gestisce capi di tipo **USkeletalMesh**. Supporta una Skeletal Mesh con dati Chaos Cloth tramite la modalità Copy Pose. Non gestisce direttamente i nuovi asset parametrici **Chaos Outfit Asset / UChaosOutfitAsset**.

---

## 1. Installazione del plugin

1. Chiudi Unreal Editor.
2. Copia la cartella `AvatarWardrobe` dentro:

```text
TuoProgetto/Plugins/AvatarWardrobe/
```

La struttura finale deve essere:

```text
TuoProgetto/
├── TuoProgetto.uproject
└── Plugins/
    └── AvatarWardrobe/
        ├── AvatarWardrobe.uplugin
        └── Source/
```

3. Fai clic destro sul file `.uproject` e scegli **Generate Visual Studio project files**.
4. Apri la solution in Visual Studio 2022.
5. Compila la configurazione:

```text
Development Editor / Win64
```

6. Apri il progetto.
7. In `Edit > Plugins`, cerca **Avatar Wardrobe** e verifica che sia abilitato.
8. Riavvia Unreal se richiesto.

---

## 2. Aggiunta del componente al MetaHuman

1. Apri il Blueprint del MetaHuman che stai usando nel livello.
2. Nel pannello Components premi **Add**.
3. Cerca:

```text
Meta Human Wardrobe Component
```

4. Aggiungilo al Blueprint.
5. Seleziona il componente Skeletal Mesh principale del corpo, normalmente chiamato:

```text
Body
```

6. Nel Details Panel apri:

```text
Tags > Component Tags
```

7. Aggiungi il tag:

```text
WardrobeLeader
```

Il componente guardaroba cerca prima il tag `WardrobeLeader`; se non lo trova, cerca un componente chiamato `Body`.

8. Nelle Class Defaults del MetaHuman aggiungi anche l'Actor Tag:

```text
WardrobeTarget
```

In alternativa puoi aggiungere questo Actor Tag all'istanza del MetaHuman già posizionata nel livello.

---

## 3. Creazione del primo capo dati

Nel Content Browser:

1. Clic destro.
2. Seleziona:

```text
Miscellaneous > Data Asset
```

3. Scegli la classe:

```text
WardrobeItemData
```

4. Chiamalo, per esempio:

```text
DA_Wardrobe_Top_01
```

5. Imposta:

```text
Item Id: top_01
Display Name: Maglia 01
Category: Tops
Subcategory: TShirts
Skeletal Mesh: la Skeletal Mesh della maglia
Pose Mode: Leader Pose - Lightweight
Default Variant Index: 0
```

Per il primo test lascia vuoto:

```text
Components To Hide
```

In questo modo puoi verificare prima che il capo venga caricato e segua il corpo. Dopo il test puoi aggiungere `Torso`, `Legs` o `Feet` in base ai nomi effettivi dei componenti del tuo MetaHuman.

### Requisito della mesh

La mesh del capo deve essere stata skinnata per lo scheletro compatibile con il corpo MetaHuman. Leader Pose richiede che le ossa necessarie del capo siano compatibili con quelle del componente `Body`.

---

## 4. Creazione delle varianti materiale

Dentro `DA_Wardrobe_Top_01`, espandi:

```text
Material Variants
```

Aggiungi almeno due elementi.

### Variante con materiale completo

```text
Display Name: Rosso
Material Slot Index: 0
Material Override: MI_Top_Red
Apply Tint: false
```

### Variante con colore parametrico

```text
Display Name: Blu
Material Slot Index: 0
Material Override: materiale base o istanza base
Apply Tint: true
Tint Parameter Name: Tint
Tint Color: blu
```

Per la variante parametrica, il materiale deve contenere un **Vector Parameter** con lo stesso nome indicato in `Tint Parameter Name`, per esempio `Tint`.

Una variante senza Material Override ripristina il materiale predefinito dello slot della Skeletal Mesh prima di applicare l'eventuale tinta.

---

## 5. Creazione del catalogo

1. Clic destro nel Content Browser.
2. Seleziona:

```text
Miscellaneous > Data Asset
```

3. Scegli:

```text
WardrobeCatalogData
```

4. Chiamalo:

```text
DA_WardrobeCatalog
```

5. Nell'array `Items` aggiungi:

```text
DA_Wardrobe_Top_01
```

Aggiungi in seguito tutti gli altri Data Asset dei vestiti.

---

## 6. Widget della singola icona

Crea un Widget Blueprint con parent class:

```text
WardrobeItemEntryWidget
```

Chiamalo:

```text
WBP_WardrobeItemEntry
```

Usa questa gerarchia consigliata:

```text
SelectionBorder         Border, Is Variable
└── ItemButton          Button, Is Variable
    └── VerticalBox
        ├── ThumbnailImage  Image, Is Variable
        └── NameText        TextBlock, Is Variable
```

Nomi obbligatori:

```text
ItemButton
```

Nomi facoltativi ma consigliati:

```text
SelectionBorder
ThumbnailImage
NameText
```

Imposta una dimensione indicativa dell'entry, per esempio 130 × 150 px.

---

## 7. Widget della variante materiale

Crea un Widget Blueprint con parent class:

```text
WardrobeMaterialVariantWidget
```

Chiamalo:

```text
WBP_WardrobeMaterialVariant
```

Gerarchia consigliata:

```text
SelectionBorder         Border, Is Variable
└── VariantButton       Button, Is Variable
    └── HorizontalBox
        ├── ColorPreview      Image, Is Variable
        └── VariantNameText   TextBlock, Is Variable
```

Nome obbligatorio:

```text
VariantButton
```

Nomi facoltativi ma consigliati:

```text
SelectionBorder
ColorPreview
VariantNameText
```

---

## 8. Widget principale del guardaroba

Crea un Widget Blueprint con parent class:

```text
WardrobeMenuWidget
```

Chiamalo:

```text
WBP_WardrobeMenu
```

La grafica può essere costruita come nell'immagine di riferimento. Gli elementi C++ devono avere esattamente questi nomi e devono essere marcati **Is Variable**:

### Obbligatori

```text
Button_Tops             Button
Button_Bottoms          Button
Button_Shoes            Button
Button_Vests            Button
Button_Accessories      Button
Button_Close            Button
Combo_Subcategory       ComboBoxString
ItemsWrapBox             WrapBox
MaterialVariantsBox     VerticalBox
```

### Facoltativi

```text
Button_Unequip          Button
Text_CategoryTitle      TextBlock
Text_SelectedItem       TextBlock
```

Gerarchia suggerita:

```text
CanvasPanel
└── Border
    └── HorizontalBox
        ├── VerticalBox categorie
        │   ├── Button_Tops
        │   ├── Button_Bottoms
        │   ├── Button_Shoes
        │   ├── Button_Vests
        │   └── Button_Accessories
        │
        ├── VerticalBox contenuti
        │   ├── Text_CategoryTitle
        │   ├── Combo_Subcategory
        │   └── ScrollBox
        │       └── ItemsWrapBox
        │
        └── VerticalBox varianti
            ├── Text_SelectedItem
            ├── MaterialVariantsBox
            ├── Button_Unequip
            └── Button_Close
```

Nelle **Class Defaults** di `WBP_WardrobeMenu`, assegna:

```text
Item Entry Widget Class: WBP_WardrobeItemEntry
Material Variant Widget Class: WBP_WardrobeMaterialVariant
```

---

## 9. Player Controller e tasto I

Crea un Blueprint con parent class:

```text
WardrobePlayerController
```

Chiamalo:

```text
BP_WardrobePlayerController
```

Nelle Class Defaults assegna:

```text
Wardrobe Widget Class: WBP_WardrobeMenu
Wardrobe Catalog: DA_WardrobeCatalog
Wardrobe Target Actor Tag: WardrobeTarget
Block Move And Look Input While Open: true
```

Il tasto **I** è già associato nel C++.

Ora apri il GameMode utilizzato dalla mappa e imposta:

```text
Player Controller Class: BP_WardrobePlayerController
```

Puoi farlo anche in:

```text
World Settings > GameMode Override
```

---

## 10. Prima prova

1. Posiziona il MetaHuman nel livello.
2. Verifica che abbia:

```text
MetaHumanWardrobeComponent
Actor Tag: WardrobeTarget
Body Component Tag: WardrobeLeader
```

3. Avvia Play In Editor.
4. Premi:

```text
I
```

5. Premi `Tops`.
6. Seleziona `Maglia 01`.
7. Seleziona una variante materiale.
8. Premi nuovamente `I` oppure il pulsante Close.

---

## 11. Attivazione di Components To Hide

Dopo che il primo capo funziona, puoi evitare la sovrapposizione con l'abbigliamento originale.

Apri il Blueprint MetaHuman e annota i nomi esatti dei componenti, per esempio:

```text
Torso
Legs
Feet
```

Nel Data Asset del capo aggiungi i nomi in:

```text
Components To Hide
```

Esempi:

```text
Top       -> Torso
Bottoms   -> Legs
Shoes     -> Feet
```

Usa i nomi reali presenti nel tuo Blueprint. Il sistema ripristina automaticamente la visibilità originale quando il capo viene rimosso o sostituito.

Non nascondere `Body` se il capo non contiene anche tutte le parti di pelle che devono rimanere visibili.

---

## 12. Modalità Copy Pose per cloth

Per un capo Skeletal Mesh che deve eseguire cloth simulation o possiede ossa aggiuntive:

1. Crea un Animation Blueprint sullo scheletro del capo/MetaHuman.
2. Chiamalo:

```text
ABP_WardrobeCopyPose
```

3. Nell'AnimGraph inserisci:

```text
Copy Pose From Mesh
    -> Output Pose
```

4. Seleziona il nodo `Copy Pose From Mesh`.
5. Nel Details Panel attiva:

```text
Use Attached Parent
```

6. Nel Data Asset del capo imposta:

```text
Pose Mode: Copy Pose - Cloth/Extra Bones
Copy Pose Anim Class: ABP_WardrobeCopyPose
```

Il plugin attacca il componente del capo direttamente al componente `Body`; il nodo Copy Pose usa quindi il parent allegato come sorgente.

Non assegnare `Copy Pose` senza compilare `Copy Pose Anim Class`: il plugin farà fallback a Leader Pose e scriverà un warning nel log.

---

## 13. Problemi comuni

### Premo I e non succede niente

Controlla:

```text
World Settings > GameMode Override
Player Controller Class = BP_WardrobePlayerController
```

### Il menu si apre ma è vuoto

Controlla:

```text
BP_WardrobePlayerController > Wardrobe Catalog
DA_WardrobeCatalog > Items
Category del capo
```

### Il log dice che non trova il leader mesh

Seleziona il componente `Body` del MetaHuman e aggiungi:

```text
Component Tag = WardrobeLeader
```

Apri `Window > Developer Tools > Output Log` e cerca:

```text
LogAvatarWardrobe
```

### La mesh appare ferma in T-pose

Controlla:

- compatibilità dello scheletro;
- Pose Mode;
- per Copy Pose, presenza di `ABP_WardrobeCopyPose`;
- `Use Attached Parent` sul nodo Copy Pose From Mesh.

### La mesh scompare quando seleziono il capo

Lascia temporaneamente vuoto `Components To Hide`. Probabilmente stai nascondendo il componente sbagliato.

### La mesh attraversa il corpo

Il guardaroba non effettua fitting automatico. Il capo deve essere modellato/skinnato per quella corporatura. Per il primo test usa un capo creato per lo stesso MetaHuman.

### Il colore non cambia

Controlla:

- `Material Slot Index`;
- nome esatto del Vector Parameter;
- che il parametro esista nel materiale;
- che `Apply Tint` sia attivo.

### Errore di compilazione su classi non trovate

Verifica che il plugin sia nella cartella corretta, rigenera i project files e compila `Development Editor / Win64` con Unreal chiuso.

---

## Riferimenti Epic

- Modular characters e differenze tra Leader/Master Pose e Copy Pose:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/modular-characters-in-unreal-engine
- `FAnimNode_CopyPoseFromMesh` e proprietà `use_attached_parent`:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimNode_CopyPoseFromMesh
- Asset Manager e Streamable Manager:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UAssetManager
- UMG UI Designer:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-ui-designer-for-unreal-engine

## Correzione UE 5.6 - FStreamableHandle

In UE 5.6 `FStreamableHandle` è dichiarato come `struct`. Nel file `MetaHumanWardrobeComponent.h` la forward declaration deve quindi essere `struct FStreamableHandle;` e non `class FStreamableHandle;`.

## Persistenza outfit (v1.0.3)

La versione 1.0.3 salva automaticamente, per ogni categoria, il `ItemId` del capo e l'indice della variante materiale/colore selezionata. Al riavvio, il Player Controller carica lo slot `AvatarWardrobe` e richiama `EquipItem`; di conseguenza viene nuovamente emesso `OnItemEquipped`, quindi eventuale logica Blueprint già collegata a quell'evento continua a funzionare.

Nel Blueprint derivato da `WardrobePlayerController`, in `Class Defaults > Wardrobe > Persistence`, lascia attivi:

- `Auto Load Wardrobe`
- `Auto Save Wardrobe`
- `Wardrobe Save Slot Name = AvatarWardrobe`
- `Wardrobe Save User Index = 0`

Ogni `WardrobeItemData` deve avere un `ItemId` univoco e stabile. Non riordinare le varianti materiale dopo aver distribuito il progetto, perché il colore viene ripristinato tramite `MaterialVariantIndex`.

Per cancellare il salvataggio durante un test, richiama la funzione Blueprint `Delete Wardrobe Save` sul Player Controller oppure elimina il file nella cartella `Saved/SaveGames` del progetto.
