# Mimari

Bu belge 0.1, 0.2, 0.3 ve 0.4'ün nasıl kurulduğunu ve [özellik listesindeki](ozellikler.md) her şeyin ileride nereye oturacağını anlatır.

## Katmanlar

```
┌──────────────────────────── src/app (Qt Widgets) ────────────────────────────┐
│ MainWindow · çalışma alanları (Kurgu / Renk / Ses / Fotoğraf)                │
│ Paneller: Dosyalar, Önizleme, Özellikler, Zaman çizelgesi, Renk, Kapsamlar,  │
│           Ses                                                                │
│ PlaybackEngine (video iş parçacığı + ses iş parçacığı) · MediaCache          │
└───────────────┬──────────────────────────────────────────────────────────────┘
                │ yalnızca çekirdeğin genel API'si
┌───────────────▼──────────── src/core (lumora_core) ──────────────────────────┐
│ project/  Project ── media/MediaPool ── timeline/Timeline ── image/PhotoLib. │
│ undo/     komutlar (QUndoCommand)                                            │
│ export/   ExportPlanner (karar) → TimelineExporter / RenderExport (uygulama) │
│ render/   FrameRenderer · TitleRenderer · AudioMixer                         │
│ color/    ColorGrade · ColorProcessor (32-bit float) · Lut (.cube) · Scopes  │
│ audio/    AudioEffects · dsp (filtre, gürültü, kompresör, limit.) · Loudness │
│ image/    PhotoProcessor (renk + katmanlar + maskeler) · PhotoExporter · RAW │
│ media/    MediaInfo · VideoDecoder · AudioDecoder                            │
│ ffmpeg/   RAII sarmalayıcılar, sürüm uyumluluğu (FFmpeg 6.1 – 8.x)           │
└──────────────────────────────────────────────────────────────────────────────┘
```

Çekirdek arayüzü bilmez; testler ve ileride bir komut satırı aracı aynı kütüphaneyi kullanır.

## Temel kararlar

**Zaman tamsayıdır.** Tüm zamanlar `Ticks` (int64, saniyenin 1/705 600 000'i). Bu sayı 23.976, 24, 25, 29.97, 30, 50, 59.94, 60, 120 fps ve 44.1/48/96 kHz ses örneklerini tam böler; kayan nokta yuvarlama hatası birikmez.

**Düzenleme kararı ile piksel ayrıdır.** Klip yalnızca "şu dosyanın şu aralığı, zaman çizelgesinde şurada" bilgisidir. Fotoğraf ayarları birkaç sayıdır. Kaynak dosyalara hiçbir zaman yazılmaz.

**Önce plan, sonra çıktı.** `ExportPlanner`, zaman çizelgesi ve ayarlardan bir `ExportPlan` çıkarır: her bölüm için video/ses *kopyala* mı *kodla* mı, neden, hangi uyarılar. Kaliteyi düşürecek bir durum varsa bunu engel ya da not olarak bildirir, kendiliğinden düşürmez. Dışa aktarma penceresindeki "Ne olacak" listesi bu plandır. `TimelineExporter` yalnızca planı uygular.

**Önizleme ve çıktı aynı birleştiriciden, farklı hassasiyetle geçer.** `FrameRenderer` bir zaman anı için izleri aşağıdan yukarı birleştirir: klip karesi, dönüşüm (konum, ölçek, döndürme, opaklık), belirme/kararma, geçişler ve başlıklar. Oynatma motoru bunu önizleme boyutunda 8 bit (`Preview`) çağırır ve yavaşsa kare atlar; dışa aktarma tam boyutta, kanal başına 16 bit ve Lanczos ölçeklemeyle (`Final`) çağırır. Hiçbir efekt dokunmayan karelerde `passthroughFrame` kaynağın kendi YUV karesini verir, böylece 8 bit RGB'ye hiç inmeden doğrudan çıktı biçimine dönüştürülür.

**Ses de tek bir karıştırıcıdan geçer.** `AudioMixer` bütün ses izlerini 48 kHz kayan noktada toplar; ses düzeyi, belirme/kararma ve geçişlerde eşit güçte çapraz geçiş uygular. Oynatma ve dışa aktarma aynı sınıfı kullanır. Basit durumlarda (tek A1, efekt yok, uyumlu kodek) ses yine bit bit kopyalanır.

**Üç video stratejisi.** Planlayıcı her dışa aktarma için `Copy` (paket kopyalama), `Encode` (bölüm bölüm yeniden kodlama) ya da `Render` (birleştirici üzerinden sabit kare hızında kodlama) seçer. Başlık, geçiş, birden çok iz ya da dönüşüm varsa kayıpsız kesim engellenir ve neden yazılır. Kodlayıcı ayarları (`encoders::configure`) her kodlayıcının kendi kalite ölçeğine, bit hızı modlarına ve iki geçiş yöntemine çevrilir; donanım kodlayıcıları bir kez gerçekten açılarak denenir.

**Renk tek bir hattan geçer.** `ColorGrade` birkaç sayıdan (ışık, beyaz dengesi, doygunluk, lift/gamma/gain, LUT yolu ve miktarı) ibarettir ve video klipte (`Clip::color`), fotoğrafta (`PhotoAdjustments::color`) ve fotoğrafın ayar katmanlarında aynıdır. `ColorProcessor` bunu her piksel için 32-bit float'ta uygular: beyaz dengesi, pozlama ve kontrast doğrusal ışıkta; ton aralıkları algısal parlaklıkta; tekerlekler ve LUT ekran kodlu değerlerde (.cube dosyalarının neredeyse hepsi bunu bekler). Üs eğrileri yoğun tablolardan okunur (hata 1e-6'nın altında), böylece önizleme de tam hesabı yapar, ayrı bir "hızlı" yol yoktur. `FrameRenderer` klip karesini çıktı boyutuna dönüştürdükten hemen sonra, birleştirmeden önce rengi uygular; renk ayarı olan kare `passthroughFrame` yolundan geçmez, planlayıcı da kayıpsız kesimi engeller. Okunamayan bir LUT dışa aktarmayı başlamadan durdurur. Görünüm `Lut::sample` ile 3B tabloya çevrilip .cube olarak kaydedilebilir.

**Eğriler ve HSL aynı renk hattının adımlarıdır.** Sıra: tekerlekler → HSL → ton eğrileri → LUT. Eğriler Fritsch–Carlson monoton kübik ara değerlemeyle noktaların arasında taşmaz; ana eğri ile kanal eğrisi tek bir 8192 adımlık tabloya birleştirilir. HSL 8 renk merkezi arasında yumuşak geçişle çalışır ve etkisini pikselin doygunluğuyla ölçekler, böylece griler yerinde kalır.

**Ses efektleri karıştırıcının içindedir.** Her ses klibinin kendi zinciri (`dsp::ClipChain`) vardır: 2×2. derece alçak kesme → spektral gürültü azaltma (STFT, en küçük istatistik ile gürültü tabanı) → üç bantlı ekolayzer → kompresör. Zincirin gecikmesi (gürültü azaltmada tam olarak bir pencere) karıştırıcının ileri okumasıyla telafi edilir, ses görüntüyle hizalı kalır. Kısma iki geçişlidir: önce kısılmayan kliplerin toplamından bir anahtar sinyal çıkar, sonra kısılacak klipler buna göre zarflanır. Ana çıkışta eşitleme kazancı ve ileriye bakan limitleyici vardır. `Loudness` ITU-R BS.1770-4 / EBU R128'i (K-ağırlıklama, 400 ms bloklar, -70 ve -10 LU kapıları) uygular; dışa aktarma önce miksi ölçer, sonra kazancı ayarlar. Planlayıcı yalnızca ses değiştiğinde görüntüyü `Copy` olarak bırakır (`audioMixed`): video paketleri bit bit kopyalanırken ses karıştırıcıdan kodlanıp aynı dosyaya araya yazılır.

**Altyazılar ayrı bir iz türüdür.** `TrackType::Subtitle` izleri medya taşımayan `ClipKind::Subtitle` klipleri tutar; düzenleme işlemleri başlıklarla aynıdır. Dışa aktarma planı satırları çıktı zamanına çevirir (kayıpsız kesimde anahtar kare kaymaları dahil) ve üç yoldan birini seçer: kabın desteklediği metin kodeğiyle (mov_text / SubRip / WebVTT) ayrı akış, `FrameRenderer` ile görüntüye yakma (o zaman `Render`) ya da yanına .srt. Proje dosyası altyazı izi varsa sürüm 3 yazılır; yoksa 2 kalır, böylece 0.3 o projeleri açabilir.

**RAW.** `image/RawDecoder` LibRaw ile (isteğe bağlı derleme bağımlılığı) kameranın beyaz dengesi ve renk matrisiyle 16 bit sRGB'ye geliştirir; `PhotoProcessor::load`, `probeMedia` ve küçük resimler bunu kullanır. Dosya Qt ile okunup LibRaw'a bellekten verilir, böylece Türkçe karakterli yollar her sistemde açılır.

**Fotoğraf katmanları.** `PhotoAdjustments` = genel renk + kırpma + katman listesi (alttan üste). Ayar katmanı, alttaki her şeyin renk ayarlı kopyasını maske ve opaklıkla karıştırır; resim katmanı başka bir resmi konum/boyutla yerleştirir. İkisi de karışım modunu (W3C tanımları) ekran kodlu değerlerde uygular. Maskeler (dikdörtgen, elips, degrade, fırça; yumuşak kenar, ters) kırpılmamış fotoğrafın normalize koordinatlarındadır, böylece önizleme vekilinde ve tam çözünürlükte aynı yere düşer. Kırpma en son yapılır. Katmanı olmayan ve rengi değişmeyen fotoğraf yine bit bit aynı kaydedilir.

**Geri al her şeyi kapsar.** Zaman çizelgesi değişiklikleri anlık görüntü (snapshot) komutlarıyla, fotoğraf ayarları birleşebilen komutlarla (`mergeWith`) kaydedilir. Video ve fotoğrafın ayrı geçmişi vardır (`QUndoGroup`).

**Görünüm tek yerden gelir.** `src/app/Theme.*` "Gece" ve "Gündüz" temalarının adlandırılmış renklerini (`themeColors()`), paleti, uygulama genelindeki stil sayfasını ve gömülü Inter yazı tipini tanımlar. Kendini çizen bileşenler (zaman çizelgesi, önizleme, küçük resimler, kapsamlar, renk tekerlekleri) renklerini buradan alır; panel kartları `makePanelSurface`, ana eylem düğmeleri `markPrimary` ile işaretlenir.

**Proje dosyası okunabilir ve sürümlüdür.** `.lumora` JSON'dur; `version` alanı sayesinde eski projeler ileride dönüştürülerek açılır.

## Gelecek özellikler nereye oturur

| Özellik (ozellikler.md) | Yeri |
|---|---|
| Akıllı render (yalnızca efektli kısmı kodla) | `SegmentPlan` bölüm bölümdür; efektli bölümler `Render`, diğerleri `Copy` olabilir. GOP sınırında yeniden kodlama `TimelineExporter`'a eklenir. |
| Ripple/roll/slip/slide, iç içe zaman çizelgesi | Yeni düzenleme işlemleri `Timeline` metotları + snapshot geri alma olarak eklenir. |
| PNG/TIFF dizisi, GIF/WebP | `encoders` tablosuna yeni çıktı türü; `RenderExport` kare kare yazar. |
| Efektler, anahtar kareler | `ClipEffects` (başlık, dönüşüm, geçiş, ses, renk) bir efekt yığınına ve anahtar karelere genişler; `FrameRenderer` GPU üzerinde 32-bit float işleyen bir render grafiğine dönüşür. |
| Renk yönetimi, HDR, OCIO/ACES | `VideoStreamInfo` renk alanı, aktarım ve HDR bilgisini zaten taşır; dışa aktarma bunları korur. Dönüşümler render grafiğine düğüm olarak gelir. |
| Eko azaltma, seslendirme kaydı, sessiz kısımları silme | `dsp::ClipChain`'e yeni adım; kayıt `QAudioSource` ile yeni bir medya dosyası üretir; sessizlik tespiti `Loudness` bloklarını kullanır. |
| Otomatik altyazı | `src/ai` (Whisper) satırları üretir, `Timeline::addSubtitleTrack` ile iz olarak eklenir. |
| Proxy düzenleme | `MediaItem`'a proxy yolu; oynatma proxy'yi, dışa aktarma asıl dosyayı okur (önizleme/çıktı ayrımı zaten var). |
| Fotoğraf: seçim araçları, HEIC/AVIF | Sihirli değnek ve konu seçimi `LayerMask`'a bitmap maske türü olarak eklenir (fırça darbeleriyle birleşir); okuyucular `PhotoProcessor::load` arkasına RAW gibi eklenir. |
| EXIF / meta veri koruma | Video meta verisi ve yan veriler (HDR, döndürme) şimdiden kopyalanıyor; fotoğraf EXIF'i 0.2'de `PhotoExporter`'a eklenecek. |
| Otomatik kayıt, kurtarma | `Project::toJson` hazır; zamanlayıcı ile ayrı bir dosyaya yazılır. |
| Yapay zeka araçları | Ayrı bir `src/ai` modülü (ONNX Runtime); çekirdekle yalnızca kare/ses arabellekleri üzerinden konuşur. |

## Kayıpsız kesim kuralları

- Giriş noktası bir önceki anahtar kareye geri çekilir. Açık GOP'larda anahtar kareden önce gösterilen kareler atılır.
- Çıkış noktası, çözme sırasına göre istenen sondan sonra gelen ilk pakette durur; böylece referansı eksik hiçbir kare yazılmaz. B-kareli videoda bu, kesimin birkaç kare erken bitmesi demektir ve kullanıcıya önceden söylenir.
- Paketlerin zaman damgaları birleştirme boyunca kaydırılır, DTS her zaman artan tutulur.
- Bölümlerin kodek parametreleri aynı değilse kopyalama reddedilir ve "Kaynakla aynı" ya da "Kayıpsız ara format" önerilir.
