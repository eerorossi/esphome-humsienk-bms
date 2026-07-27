# BLE HCI snoop -kaappaus – tarkistuslista

Tavoite: kaapata Humsienk-appin lähettämät tavut latauksen/purun FET-ohjaukseen,
jotta FET-komennot (0x50/0x51) ja mahdollinen unlock voidaan varmistaa.

⚠️ **Turvallisuus:** tee kaappaus ilman kuormaa ja ilman laturia kytkettynä.

\---

## Ennen aloitusta (kerran)

* \[ ] Kehittäjäasetukset päällä: Asetukset → Tietoa puhelimesta → napauta *Koontiversion numero* 7×
* \[ ] Kehittäjäasetukset → **"Bluetooth HCI snoop -loki käytössä"** = PÄÄLLÄ
* \[ ] Bluetooth pois → päälle (aloittaa puhtaan lokin)
* \[ ] Humsienk-appi auki ja yhdistetty akkuun

\---

## Toimintosarja (kirjaa kellonaika sekunnin tarkkuudella)

Odota \~5 s jokaisen vaiheen välissä. Merkitse aika kun **painat** kytkintä.

|#|Kello (hh:mm:ss)|Toiminto appissa|Odotettu komento|
|-|-|-|-|
|1|**16:20**:20|Katso perustiedot \~10 s (älä koske)|luku-referenssi|
|2|**16:21**:00|**Purku (discharge) POIS**|0x51 \[0x00]|
|3|**16:21**:25|**Purku PÄÄLLE**|0x51 \[0x01]|
|4|**16:22**:00\_|**Lataus (charge) POIS**|0x50 \[0x00]|
|5|**16:22**:30|**Lataus PÄÄLLE**|0x50 \[0x01]|

* \[ ] Jos appi pyytää PIN/salasanan kytkimille → syötä se (paljastaa unlock-komennon).
Kirjaa tähän mitä pyydettiin: \_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_

\---

## Lokin haku

Valitse toinen:

* \[ ] **Ilman tietokonetta:** Kehittäjäasetukset → *Luo virheraportti* → **Täydellinen** → jaa zip.
Loki sisällä: `FS/data/misc/bluetooth/logs/btsnoop\_hci.log`
* \[ ] **adb:llä:** `adb bugreport humsienk\_snoop.zip`
(root: `adb pull /data/misc/bluetooth/logs/btsnoop\_hci.log`)

\---

## Toimitus Claudelle

* \[ ] Tallenna tiedosto tänne:
`/app/esphome-humsienk-bms/docs/humsienk-protocol/btsnoop\_hci.log`
(tai koko bugreport-zip samaan kansioon)
* \[ ] Kerro yllä kirjatut kellonajat (vaiheet 1–5).

Kun tiedosto on paikallaan → sano "loki valmis", niin analysoin sen heti.

\---

## Muistiinpanot kaappauksen aikana

<!-- esim. app-versio, akun nimi BLE:ssä (HS...?), yllättävät kyselyt -->

* 

