# Mimari

Bu belge 0.1'in nasıl kurulduğunu ve [özellik listesindeki](ozellikler.md) her şeyin ileride nereye oturacağını anlatır.

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
│ export/   ExportPlanner (karar) → TimelineExporter (uygulama)                │
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

**Önizleme çıktıdan ayrıdır.** Oynatma motoru 8 bit RGB önizleme üretir ve yavaşsa kare atlar; dışa aktarma ise kaynağın kendi piksel biçimiyle çalışır.

**Geri al her şeyi kapsar.** Zaman çizelgesi değişiklikleri anlık görüntü (snapshot) komutlarıyla, fotoğraf ayarları birleşebilen komutlarla (`mergeWith`) kaydedilir. Video ve fotoğrafın ayrı geçmişi vardır (`QUndoGroup`).

**Proje dosyası okunabilir ve sürümlüdür.** `.lumora` JSON'dur; `version` alanı sayesinde eski projeler ileride dönüştürülerek açılır.

## Gelecek özellikler nereye oturur

| Özellik (ozellikler.md) | Yeri |
|---|---|
| Akıllı render (yalnızca efektli kısmı kodla) | `SegmentPlan` zaten bölüm bölümdür; efektli bölümler `Encode`, diğerleri `Copy` olur. GOP sınırında yeniden kodlama `TimelineExporter`'a eklenir. |
| Bit hızı modları, iki geçiş, hedef boyut, ön ayarlar | `ExportSettings` genişler; planlayıcı kararı verir. |
| ProRes, DNxHR, PNG/TIFF dizisi | `ExportPlanner::encodersFor` ve kapsayıcı tablosu. |
| Donanım hızlandırma | Kodlayıcı seçimi şu an donanım kodlayıcılarını bilerek atlar; 0.2'de açık kalite ayarıyla eklenir. |
| Çok izli zaman çizelgesi, başlık izleri, ripple/roll/slip/slide | `TimelineState` zaten iz listesi tutar; mıknatıs `Track::magnetic` ile iz başınadır. Yeni düzenleme işlemleri `Timeline` metotları + snapshot geri alma olarak eklenir. |
| Efektler, geçişler, renk, dönüşüm, anahtar kareler | Klibe eklenecek bir efekt yığını (parametreler + anahtar kareler) ve GPU üzerinde 32-bit float işleyen bir render grafiği. Fotoğraf hattı (`PhotoProcessor`) aynı doğrusal ışık / float modelini kullanır, böylece LUT ve renk ayarları ortak olur. |
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
