# Mimari

Bu belge 0.1 ve 0.2'nin nasıl kurulduğunu ve [özellik listesindeki](ozellikler.md) her şeyin ileride nereye oturacağını anlatır.

## Katmanlar

```
┌──────────────────────────── src/app (Qt Widgets) ────────────────────────────┐
│ MainWindow · çalışma alanları (Kurgu / Renk / Ses / Fotoğraf)                │
│ Paneller: Dosyalar, Önizleme, Özellikler, Zaman çizelgesi · Pencereler       │
│ PlaybackEngine (video iş parçacığı + ses iş parçacığı) · MediaCache          │
└───────────────┬──────────────────────────────────────────────────────────────┘
                │ yalnızca çekirdeğin genel API'si
┌───────────────▼──────────── src/core (lumora_core) ──────────────────────────┐
│ project/  Project ── media/MediaPool ── timeline/Timeline ── image/PhotoLib. │
│ undo/     komutlar (QUndoCommand)                                            │
│ export/   ExportPlanner (karar) → TimelineExporter / RenderExport (uygulama) │
│ render/   FrameRenderer · TitleRenderer · AudioMixer                         │
│ image/    PhotoProcessor (32-bit float hattı) · PhotoExporter                │
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

**Geri al her şeyi kapsar.** Zaman çizelgesi değişiklikleri anlık görüntü (snapshot) komutlarıyla, fotoğraf ayarları birleşebilen komutlarla (`mergeWith`) kaydedilir. Video ve fotoğrafın ayrı geçmişi vardır (`QUndoGroup`).

**Proje dosyası okunabilir ve sürümlüdür.** `.lumora` JSON'dur; `version` alanı sayesinde eski projeler ileride dönüştürülerek açılır.

## Gelecek özellikler nereye oturur

| Özellik (ozellikler.md) | Yeri |
|---|---|
| Akıllı render (yalnızca efektli kısmı kodla) | `SegmentPlan` bölüm bölümdür; efektli bölümler `Render`, diğerleri `Copy` olabilir. GOP sınırında yeniden kodlama `TimelineExporter`'a eklenir. |
| Ripple/roll/slip/slide, iç içe zaman çizelgesi | Yeni düzenleme işlemleri `Timeline` metotları + snapshot geri alma olarak eklenir. |
| PNG/TIFF dizisi, GIF/WebP | `encoders` tablosuna yeni çıktı türü; `RenderExport` kare kare yazar. |
| Efektler, renk, anahtar kareler | `ClipEffects` (başlık, dönüşüm, geçiş, ses) bir efekt yığınına ve anahtar karelere genişler; `FrameRenderer` GPU üzerinde 32-bit float işleyen bir render grafiğine dönüşür. Fotoğraf hattı (`PhotoProcessor`) aynı doğrusal ışık / float modelini kullanır, böylece LUT ve renk ayarları ortak olur. |
| Renk yönetimi, HDR, OCIO/ACES | `VideoStreamInfo` renk alanı, aktarım ve HDR bilgisini zaten taşır; dışa aktarma bunları korur. Dönüşümler render grafiğine düğüm olarak gelir. |
| Ses araçları, LUFS, ducking | `AudioDecoder` + ayrı bir ses grafiği; A izleri zaten var. |
| Proxy düzenleme | `MediaItem`'a proxy yolu; oynatma proxy'yi, dışa aktarma asıl dosyayı okur (önizleme/çıktı ayrımı zaten var). |
| Fotoğraf: katmanlar, maskeler, RAW, HEIC/AVIF | `PhotoAdjustments` bir katman yığınına dönüşür; okuyucular (LibRaw vb.) `PhotoProcessor::load` arkasına eklenir. |
| EXIF / meta veri koruma | Video meta verisi ve yan veriler (HDR, döndürme) şimdiden kopyalanıyor; fotoğraf EXIF'i 0.2'de `PhotoExporter`'a eklenecek. |
| Otomatik kayıt, kurtarma | `Project::toJson` hazır; zamanlayıcı ile ayrı bir dosyaya yazılır. |
| Yapay zeka araçları | Ayrı bir `src/ai` modülü (ONNX Runtime); çekirdekle yalnızca kare/ses arabellekleri üzerinden konuşur. |

## Kayıpsız kesim kuralları

- Giriş noktası bir önceki anahtar kareye geri çekilir. Açık GOP'larda anahtar kareden önce gösterilen kareler atılır.
- Çıkış noktası, çözme sırasına göre istenen sondan sonra gelen ilk pakette durur; böylece referansı eksik hiçbir kare yazılmaz. B-kareli videoda bu, kesimin birkaç kare erken bitmesi demektir ve kullanıcıya önceden söylenir.
- Paketlerin zaman damgaları birleştirme boyunca kaydırılır, DTS her zaman artan tutulur.
- Bölümlerin kodek parametreleri aynı değilse kopyalama reddedilir ve "Kaynakla aynı" ya da "Kayıpsız ara format" önerilir.
