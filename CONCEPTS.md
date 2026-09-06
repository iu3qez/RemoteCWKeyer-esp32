# Concepts

> Shared domain vocabulary for this project — entities, named processes, and status concepts with project-specific meaning. Seeded with core domain vocabulary, then accretes as ce-compound and ce-compound-refresh process learnings; direct edits are fine. Glossary only, not a spec or catch-all.

## Protocollo

### CWNet
Il protocollo di rete del Remote CW Keyer di DL4YHF, che trasporta keying CW, audio, controllo radio e telemetria su un'unica connessione TCP. È il golden standard verso cui il progetto dimostra la propria compatibilità: non se ne fa un clone, se ne replica il comportamento osservabile sul percorso CW.

### Golden standard
L'implementazione di riferimento — client e server DL4YHF in esecuzione — contro cui ogni comportamento che conta viene *dimostrato*, non *reso simile*. Il suo sorgente pubblicato dice cosa guardare, non cosa è vero: la verità sono i byte sul filo, non il codice né i suoi commenti.
*Avoid:* riferimento (quando ambiguo).

### Oracolo differenziale
Un artefatto **eseguibile** derivato dal riferimento — un modulo suo ricompilato, o il suo firmware fatto girare in un emulatore — che riceve lo stesso stimolo del nostro codice perché le due uscite si possano confrontare. Non è il golden standard: è un candidato che si guadagna il posto dimostrando di riprodurre una cattura del riferimento vero. Quando risponde con un conteggio di divergenze invece che con un verdetto, quel conteggio è un cancello e non un gradiente.

## Keying

### MORSE keying stream
Il flusso di keying CW trasportato da CWNet come sequenza di byte a 7 bit — non testo, non un timestamp assoluto per evento. Ogni byte porta lo stato del tasto (giù/su) e il tempo di attesa prima di applicarlo. Viaggia su un proprio comando dedicato, distinto da quelli di CI-V, spettro e audio.

### 7-bit timestamp
La codifica non lineare del tempo di attesa fra due transizioni del tasto: risoluzione fine per gli intervalli brevi, via via più grossolana per quelli lunghi, così da coprire un ampio arco temporale in sette bit. Il codec è per costruzione a perdita sugli intervalli lunghi — il protocollo stesso quantizza il timing.

### Over
Un turno di trasmissione continuo. La sua fine è segnalata da un secondo comando di key-up dopo che è trascorsa una soglia di silenzio; il silenzio *fra* un over e il successivo non viaggia sul filo, quindi il determinismo del keying si può pretendere dentro un over, non fra over diversi.

## Latenza

### PING
Lo scambio di misura del tempo di andata e ritorno, in tre fasi con tre timestamp: l'iniziatore segna la partenza, il peer inserisce il proprio, l'iniziatore segna l'arrivo. La differenza si calcola sempre sull'orologio di chi ha iniziato — il timestamp del peer serve solo alla diagnostica, mai a una sottrazione.

### Peak-hold latency
Il valore di latenza mostrato e usato per dimensionare il jitter buffer: sale immediatamente a ogni nuovo picco e scende lentamente, quindi diverge dal valore istantaneo di round-trip in presenza di jitter. È un parametro di controllo, non una misura istantanea.
