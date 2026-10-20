# LumoraStudio — Tam özellik listesi

Bu liste projenin yol haritasıdır. Sürümlerde neyin hazır olduğu [README](../README.md#sürüm-02de-neler-var) içinde; hazır olanlar aşağıda ✅ ile işaretli, mimarinin bu özellikleri nasıl taşıyacağı [mimari.md](mimari.md) içinde anlatılıyor.

## 1. Kalite ilkesi (projenin kalbi)

Hedef: kullanıcı istemedikçe hiçbir aşamada kalite düşmez.

- **Kayıpsız kesme (yeniden kodlamadan):** Sadece kesip birleştirme yapıldıysa video yeniden sıkıştırılmaz, akış olduğu gibi kopyalanır. Kalite birebir aynı kalır, dışa aktarma saniyeler sürer.
- **Akıllı render:** Efekt uygulanan kısımlar yeniden kodlanır, dokunulmayan kısımlar olduğu gibi kopyalanır.
- ✅ **Kaynağa göre dışa aktarma:** "Kaynakla aynı" seçeneği; çözünürlük, kare hızı, bit derinliği, renk alanı ve bit hızı kaynaktan otomatik alınır.
- ✅ **Bit hızı kontrolü:** Sabit kalite (CRF/CQ), sabit bit hızı (CBR), değişken bit hızı (VBR), iki geçişli kodlama, hedef dosya boyutu ("en fazla 50 MB olsun").
- **Kayıpsız ve ara kodekler:** FFV1, ProRes, DNxHR, PNG/TIFF dizisi; düzenleme ortasında kayıpsız ara dosya.
- **Yüksek bit derinliği:** İç işlem 32-bit kayan nokta; 10-bit / 12-bit video, 16-bit fotoğraf.
- **HDR:** HDR10, HLG; HDR'yi SDR'a doğru dönüştürme (ton eşleme).
- **Renk yönetimi:** Rec.709, Rec.2020, sRGB, Display P3; renk profili (ICC) okuma/yazma; OpenColorIO / ACES desteği.
- ✅ **Önizleme ile çıktı ayrı:** Önizleme düşük çözünürlüklü olabilir (akıcılık için) ama dışa aktarma her zaman tam kalitede yapılır.
- **Proxy düzenleme:** 4K/8K dosyalar için hafif kopya üretip onunla düzenleme, çıktıda asıl dosyaya dönme.
- **Kalite karşılaştırma:** Kaynak ve çıktıyı yan yana / bölünmüş ekranda karşılaştırma, PSNR/SSIM/VMAF skoru.
- **Meta veri koruma:** EXIF, tarih, konum, kamera bilgisi, HDR meta verisi korunur (isteğe bağlı silme ile).

## 2. Video editörü

### 2.1 Proje ve medya

- Proje dosyası, otomatik kayıt, sürüm geçmişi, çökme sonrası kurtarma
- Medya kütüphanesi: klasör, etiket, yıldız, arama, küçük resim ve dalga formu önizleme
- Desteklenen biçimler: MP4, MOV, MKV, WebM, AVI, MXF; H.264, H.265/HEVC, AV1, VP9, ProRes, DNxHR
- Değişken kare hızlı (telefon) videoları doğru işleme
- Eksik medyayı yeniden bağlama

### 2.2 Zaman çizelgesi

- ✅ Sınırsız video, ses ve başlık izi
- ✅ Kesme, bölme, kırpma, ripple / roll / slip / slide düzenleme
- Mıknatıs (snap), işaretçiler, bölüm işaretleri
- Grup / iç içe zaman çizelgesi (compound clip)
- ✅ Bağlantılı ses-video, ayırma
- Çoklu kamera düzenleme (sese göre otomatik eşitleme)
- ✅ İz kilitleme, gizleme, sessize alma, tekleme
- ✅ Geri al / yinele (sınırsız), geçmiş paneli
- Klavye kısayolları (özelleştirilebilir, Premiere / DaVinci düzenleri)

### 2.3 Hız ve zaman

- Sabit hız değiştirme, ters oynatma, dondurma karesi
- Hız rampası (eğri ile)
- Optik akışla yumuşak ağır çekim (ara kare üretme)
- Kare hızı dönüştürme

### 2.4 Dönüşüm ve kompozisyon

- Konum, ölçek, döndürme, kırpma, anahtar karelerle animasyon
- Eğri editörü (bezier ile yumuşatma)
- ✅ Karışım modları, opaklık
- Maskeler (dikdörtgen, elips, serbest çizim), maske takibi
- Yeşil perde (chroma key), luma key
- ✅ Resim içinde resim, bölünmüş ekran şablonları
- Sabitleme (stabilizasyon)
- Hareket takibi (nesneye yazı/efekt yapıştırma)
- Dikey / kare / yatay formata otomatik yeniden çerçeveleme

### 2.5 Renk

- Temel düzeltme: pozlama, kontrast, beyaz dengesi, doygunluk
- Renk tekerlekleri (lift / gamma / gain), eğriler (RGB, ton, doygunluk)
- HSL ile tek renk ayarı
- LUT içe aktarma (.cube), kendi LUT'unu kaydetme
- Kapsamlar: histogram, dalga formu, vektörskop, parade
- Renk eşleştirme (bir klibi diğerine benzetme)
- Düğüm tabanlı renk düzeni (ileri seviye)

### 2.6 Efektler ve geçişler

- ✅ Geçişler: yumuşak geçiş, kaydırma, yakınlaştırma, bulanık, özel
- Bulanıklaştırma, keskinleştirme, gürültü azaltma (zamansal + uzamsal)
- Vinyet, film grenı, kromatik sapma, glitch
- Yüz / plaka bulanıklaştırma (otomatik takip ile)
- Efekt ön ayarları kaydetme ve paylaşma
- Eklenti sistemi (OpenFX desteği)

### 2.7 Yazı ve grafik

- ✅ Başlık editörü: font, kontur, gölge, arka plan, animasyonlu giriş/çıkış
- Hazır başlık şablonları, alt bilgi bantları
- Şekiller, oklar, vurgular
- Altyazı: SRT/VTT içe-dışa aktarma, videoya gömme veya ayrı iz
- Otomatik altyazı (yerel konuşma tanıma, ör. Whisper), Türkçe destekli

### 2.8 Ses

- ✅ Ses izleri, dalga formu, ses seviyesi anahtar kareleri
- ✅ Geçişte ses yumuşatma (fade)
- Gürültü giderme, eko azaltma, ses normalleştirme (LUFS hedefi)
- Ekolayzer, kompresör, limitör
- Konuşma algılandığında müziği otomatik kısma (ducking)
- Seslendirme kaydı
- Sessiz kısımları otomatik silme
- Çok kanallı ses, 48 kHz / 24-bit kayıpsız iş akışı

### 2.9 Dışa aktarma

- ✅ Ön ayarlar: YouTube, Instagram Reels, TikTok, X, arşiv (kayıpsız), "kaynakla aynı"
- ✅ Özel ayar: kodek, kapsayıcı, çözünürlük, kare hızı, bit hızı modu, renk alanı, ses biçimi
- ✅ Donanım hızlandırma: NVIDIA NVENC, AMD AMF, Intel Quick Sync (kalite ayarı açık şekilde)
- Dışa aktarma kuyruğu, arka planda render
- ✅ Sadece seçili aralığı veya tek kareyi dışa aktarma
- GIF / animasyonlu WebP çıktısı

## 3. Fotoğraf editörü

### 3.1 Dosya ve iş akışı

- Biçimler: JPEG, PNG, WebP, AVIF, HEIC, TIFF, PSD (okuma), RAW (CR2/CR3, NEF, ARW, DNG)
- Tahribatsız düzenleme: orijinal dosya hiç değişmez, ayarlar ayrı saklanır
- Geçmiş paneli, anlık görüntüler (snapshot), önce/sonra karşılaştırma
- Toplu düzenleme: bir fotoğrafın ayarını yüzlercesine uygulama, toplu yeniden boyutlandırma ve adlandırma
- Kütüphane: klasör, yıldız, etiket, renk etiketi, EXIF'e göre filtreleme

### 3.2 Temel ayarlar (Lightroom tarzı)

- Pozlama, kontrast, vurgular, gölgeler, beyazlar, siyahlar
- Beyaz dengesi (sıcaklık, ton), damlalık ile seçme
- Netlik, doku, pus giderme, canlılık, doygunluk
- Ton eğrisi, HSL / renk karıştırıcı, renk derecelendirme
- Keskinleştirme, gürültü azaltma
- Lens düzeltme (bozulma, vinyet, kromatik sapma), perspektif düzeltme
- Kırpma, düzleştirme, en-boy oranı şablonları

### 3.3 Katmanlı düzenleme (Photoshop tarzı)

- Katmanlar, gruplar, karışım modları, opaklık
- Katman maskeleri, ayar katmanları
- Seçim araçları: dikdörtgen, kement, sihirli değnek, renk aralığı
- Otomatik konu seçimi ve arka plan kaldırma (yerel model)
- Fırça, silgi, damga (klonlama), iyileştirme fırçası
- İçeriğe duyarlı doldurma / nesne silme
- Yazı, şekil, degrade
- Dönüştürme: ölçek, döndürme, çarpıtma, perspektif

### 3.4 Gelişmiş

- HDR birleştirme (farklı pozlamalı fotoğraflardan)
- Panorama birleştirme
- Odak yığınlama
- Kalite kaybetmeden büyütme (yerel süper çözünürlük modeli)
- Yüz rötuşu (cilt yumuşatma, göz parlatma) ölçülü ve kapatılabilir şekilde

### 3.5 Dışa aktarma

- Biçim, kalite yüzdesi, renk profili, bit derinliği seçimi
- "Kayıpsız" seçenek (PNG, TIFF 16-bit)
- Hedef boyut ("en fazla 2 MB")
- Filigran ekleme, EXIF koruma / silme
- Web, Instagram, baskı ön ayarları

## 4. Ortak özellikler

- Fotoğrafı videoya, videodan kareyi fotoğrafa aktarma
- Ortak renk ön ayarları (bir LUT hem fotoğrafta hem videoda kullanılır)
- Koyu / açık tema, özelleştirilebilir panel düzeni
- Türkçe ve İngilizce arayüz
- Tamamen yerel çalışma: hesap yok, bulut yok, yapay zeka modelleri de bilgisayarda çalışır
- Otomatik güncelleme (GitHub Releases)
- Performans paneli: GPU / RAM kullanımı, önbellek temizleme

### 4.1 Arayüz düzeni (taslak)

- Sol üst: Dosyalar paneli (klasör ağacı + küçük resim/liste görünümü, arama)
- Orta üst: Önizleme (oynatma kontrolleri, zaman kodu, kapsamlar)
- Sağ üst: Özellikler paneli (seçili klibin dönüşüm, renk, efekt, ses, hız ayarları)
- Alt: Zaman çizelgesi (video, ses, başlık izleri)
- Dosyalar panelinden zaman çizelgesine sürükle-bırak ile ekleme; önizlemeye bırakınca da eklenir. Bırakırken yerleştirme, araya ekleme ve üzerine yazma seçenekleri.
- Dosyaya çift tıklayınca önizlemede açılır, giriş/çıkış noktası (I/O) seçilip sadece o kısım eklenir.
- Paneller taşınabilir, boyutlandırılabilir, düzen kaydedilebilir. Üstte Düzenleme / Renk / Ses / Fotoğraf çalışma alanı sekmeleri.

## 5. Teknik öneri (tartışmaya açık)

- Çekirdek: C++ veya Rust; video için FFmpeg (libav*) kütüphaneleri
- Görüntü işleme: GPU üzerinde (Vulkan / DirectX 12 veya OpenGL shader'ları), 32-bit kayan nokta
- RAW: LibRaw; renk: OpenColorIO, LittleCMS
- Arayüz: Qt 6 (C++) ya da Rust tarafında Slint / egui
- Yapay zeka: ONNX Runtime ile yerel modeller (Whisper, arka plan kaldırma, süper çözünürlük)

## 6. Önerilen aşamalar

1. ✅ **Sürüm 0.1:** Video aç, kes, birleştir, kayıpsız dışa aktar (yeniden kodlamasız). Fotoğraf aç, kırp, temel ayarlar, kayıpsız kaydet.
2. ✅ **Sürüm 0.2:** Çok izli zaman çizelgesi, yazı, geçişler, dışa aktarma ayarları, donanım hızlandırma.
3. **Sürüm 0.3:** Renk paneli, kapsamlar, LUT; fotoğrafta katmanlar ve maskeler.
4. **Sürüm 0.4:** Ses araçları, altyazı, RAW desteği.
5. **Sonrası:** Yapay zeka araçları, takip, çoklu kamera, eklentiler, HDR.
