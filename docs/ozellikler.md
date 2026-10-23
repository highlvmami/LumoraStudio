# LumoraStudio — Tam özellik listesi

Bu liste projenin yol haritasıdır. Sürümlerde neyin hazır olduğu [README](../README.md#sürüm-03te-neler-var) içinde; hazır olanlar aşağıda ✅ ile işaretli, mimarinin bu özellikleri nasıl taşıyacağı [mimari.md](mimari.md) içinde anlatılıyor.

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

- ✅ Dondurma karesi (0.4.1: oynatma imlecindeki kare seçilen saniye kadar bekletilir)
- Sabit hız değiştirme, ters oynatma
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
- ✅ Çıkartmalar: videonun üstüne resim (saydam PNG/WebP), hareketli GIF ya da video; önizlemede sürükleyerek taşıma, boyutlandırma, döndürme; kısa klipler döngüde tekrar
- ✅ Sabitleme (stabilizasyon)
- ✅ Hareket takibi (nesneye yazı/klip yapıştırma; efekt yapıştırma sonra)
- Dikey / kare / yatay formata otomatik yeniden çerçeveleme

### 2.5 Renk

- ✅ Temel düzeltme: pozlama, kontrast, vurgular, gölgeler, beyazlar, siyahlar, beyaz dengesi, doygunluk, canlılık
- ✅ Renk tekerlekleri (lift / gamma / gain)
- ✅ Eğriler (ana ton eğrisi ve R/G/B)
- ✅ HSL ile tek renk ayarı (8 renk aralığı)
- ✅ LUT içe aktarma (.cube), kendi LUT'unu kaydetme
- ✅ Kapsamlar: histogram, dalga formu, vektörskop, parade
- Renk eşleştirme (bir klibi diğerine benzetme)
- Düğüm tabanlı renk düzeni (ileri seviye)

### 2.6 Efektler ve geçişler

- ✅ Geçişler: yumuşak geçiş, kaydırma, yakınlaştırma, bulanık, özel
- Bulanıklaştırma, keskinleştirme, gürültü azaltma (zamansal + uzamsal)
- Vinyet, film grenı, kromatik sapma, glitch
- ✅ Yüz / plaka bulanıklaştırma (otomatik takip ile)
- Efekt ön ayarları kaydetme ve paylaşma
- Eklenti sistemi (OpenFX desteği)

### 2.7 Yazı ve grafik

- ✅ Başlık editörü: font, kontur, gölge, arka plan, animasyonlu giriş/çıkış
- Hazır başlık şablonları, alt bilgi bantları
- Şekiller, oklar, vurgular
- ✅ Altyazı: SRT/VTT içe-dışa aktarma, videoya gömme, görüntüye yakma veya ayrı .srt
- ✅ Otomatik altyazı (yerel konuşma tanıma, Whisper), Türkçe destekli
- ✅ Altyazı çevirisi (yerel, Opus-MT): 9 dil arasında, yeni altyazı izine

### 2.8 Ses

- ✅ Ses izleri, dalga formu, ses seviyesi anahtar kareleri
- ✅ Geçişte ses yumuşatma (fade)
- ✅ Gürültü giderme, ses normalleştirme (LUFS hedefi)
- Eko azaltma
- ✅ Ekolayzer, kompresör, limitör
- ✅ Başka klipler (konuşma) çalarken müziği otomatik kısma (ducking)
- ✅ Seslendirme kaydı
- ✅ Sessiz kısımları ve "ııı", "eee" duraksamalarını otomatik silme
- ✅ Elektrik uğultusu (50/60 Hz) ve rüzgâr temizleme
- ✅ Müziğin ritmini bulma, kesimleri vuruşlara taşıma
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

- Biçimler: ✅ JPEG, PNG, WebP, TIFF, ✅ HEIC/HEIF (iPhone) ve AVIF okuma, ✅ RAW (CR2/CR3, NEF, ARW, RAF, ORF, RW2, DNG …); PSD (okuma) sonra
- Tahribatsız düzenleme: orijinal dosya hiç değişmez, ayarlar ayrı saklanır
- Geçmiş paneli, anlık görüntüler (snapshot), önce/sonra karşılaştırma
- Toplu düzenleme: bir fotoğrafın ayarını yüzlercesine uygulama, toplu yeniden boyutlandırma ve adlandırma
- Kütüphane: klasör, yıldız, etiket, renk etiketi, EXIF'e göre filtreleme

### 3.2 Temel ayarlar (Lightroom tarzı)

- ✅ Pozlama, kontrast, vurgular, gölgeler, beyazlar, siyahlar
- ✅ Beyaz dengesi (sıcaklık, ton)
- Beyaz dengesini damlalık ile seçme
- ✅ Canlılık, doygunluk, renk tekerlekleri, LUT
- Netlik, doku, pus giderme
- ✅ Ton eğrisi, HSL / renk karıştırıcı
- Renk derecelendirme
- Keskinleştirme, gürültü azaltma
- Lens düzeltme (bozulma, vinyet, kromatik sapma), perspektif düzeltme
- Kırpma, düzleştirme, en-boy oranı şablonları

### 3.3 Katmanlı düzenleme (Photoshop tarzı)

- ✅ Katmanlar (ayar ve resim katmanları), karışım modları, opaklık
- ✅ Çıkartmalar: fotoğrafın üstüne başka bir resim ya da bir videodan kare; fotoğraf üstünde taşıma, boyutlandırma, döndürme
- Katman grupları
- ✅ Katman maskeleri: dikdörtgen, elips, degrade; yumuşak kenar, ters çevirme
- ✅ Fırçayla maske boyama (boyut, sertlik, akış, silgi)
- Seçim araçları: dikdörtgen, kement, sihirli değnek, renk aralığı
- ✅ Otomatik konu seçimi ve arka plan kaldırma (yerel model; videoda da)
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
- ✅ Ortak renk ön ayarları (bir LUT hem fotoğrafta hem videoda kullanılır; her görünüm .cube olarak kaydedilebilir)
- Koyu / açık tema, özelleştirilebilir panel düzeni
- Türkçe ve İngilizce arayüz
- ✅ Uygulama içi "Nasıl kullanılır" kitapçığı: solda sabit bölüm menüsü, her bölüm adım adım anlatım (F1)
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
- Paneller taşınabilir, boyutlandırılabilir, düzen kaydedilebilir. Üstte Düzenleme / Renk / Ses / Fotoğraf / Nasıl kullanılır sekmeleri.

## 5. Teknik öneri (tartışmaya açık)

- Çekirdek: C++ veya Rust; video için FFmpeg (libav*) kütüphaneleri
- Görüntü işleme: GPU üzerinde (Vulkan / DirectX 12 veya OpenGL shader'ları), 32-bit kayan nokta
- RAW: LibRaw; renk: OpenColorIO, LittleCMS
- Arayüz: Qt 6 (C++) ya da Rust tarafında Slint / egui
- Yapay zeka: ONNX Runtime ile yerel modeller (Whisper, arka plan kaldırma, süper çözünürlük)

## 6. Önerilen aşamalar

1. ✅ **Sürüm 0.1:** Video aç, kes, birleştir, kayıpsız dışa aktar (yeniden kodlamasız). Fotoğraf aç, kırp, temel ayarlar, kayıpsız kaydet.
2. ✅ **Sürüm 0.2:** Çok izli zaman çizelgesi, yazı, geçişler, dışa aktarma ayarları, donanım hızlandırma.
3. ✅ **Sürüm 0.3:** Renk paneli, kapsamlar, LUT; fotoğrafta katmanlar ve maskeler.
4. ✅ **Sürüm 0.4:** Ses araçları, altyazı, RAW desteği; eğriler, HSL ve fırça maskesi. 0.4.1: dondurma karesi.
5. ✅ **Sürüm 0.5 (ilk dilim):** Görüntü sabitleme, hareket takibi, Whisper ile yerel otomatik altyazı.
6. ✅ **Sürüm 0.6 (ilk dilim):** Yüz ve plaka bulanıklaştırma (otomatik takip ile), arka plan kaldırma (video ve fotoğraf); hepsi yerel modellerle (ONNX Runtime). 0.6.1: tek tıkla arka plan silme; 0.6.2: tam ekran; 0.6.3: çıkartmalar; 0.6.4: nasıl kullanılır kitapçığı; 0.6.5: HEIC/HEIF (iPhone) ve AVIF fotoğraflar; 0.6.6: logo (exe simgesi) ve masaüstü kısayolu; 0.6.7: tekerlekle kaydırırken ayarlar kazara değişmez; 0.6.8: önizleme sesi değişen ses çıkışını takip eder.
7. ✅ **Sürüm 0.7:** Yeni sekmeli Özellikler paneli (açılır kapanır bölümler); hız, ters oynatma, hız rampası ve akıcı ağır çekim (optik akış ile ara kare); yeşil perde; gürültü azaltma, keskinleştirme, bulanıklık, vinyet, film greni; 15 hazır renk filtresi ve otomatik renk/ışık; 11 yeni geçiş ve 12 hazır başlık şablonu; kırpma; GIF dışa aktarma; video kolaj; toplu fotoğraf işleme (boyut, filigran, format, ad); ekran kaydı ve sesli anlatım kaydı. 0.7.1: seçili kliplerin sesini kapatma; 0.7.2: 4K/HEVC/yüksek kare hızlı videolarda akıcı önizleme (ekran kartıyla çözme, çok çekirdekli küçültme, dışa aktarma kalitesi aynı); 0.7.3: 4K ve üstü videolar için arka planda hazırlanan akıcı önizleme kopyası (proxy), dışa aktarma orijinalden.
8. ✅ **Sürüm 0.8.0 (akıllı kurgu ve ses):** Sesteki elektrik uğultusunu (50/60 Hz) ve rüzgârı temizleme; sessiz yerleri ve "ııı", "eee" duraksamalarını tek tıkla kesme (konuşma tanıma bilgisayarda); müziğin ritmini bulup kesimleri vuruşlara taşıma; konuşanı kadrajda tutarak dikey 9:16, 4:5 ya da kare formata çevirme.
9. ✅ **Sürüm 0.8.1 (akıllı görüntü):** Fotoğrafta nesne silme fırçası (LaMa), eski/bulanık fotoğraflarda yüz onarma (GFPGAN), dışa aktarırken yapay zekâyla 2 ya da 4 kat büyütme (Real-ESRGAN). Hepsi yerel, ilk kullanımda bir kez indirilir.
10. ✅ **Sürüm 0.8.2 (akıllı hareket):** Videoda seçilen bir nesneyi kutuyla işaretleyip klip boyunca takip ederek kesme (EfficientSAM), yapay zekâyla ara kare (RAFT) ile daha akıcı ağır çekim, konuşmadan altyazıyı doğrudan İngilizce yazma (Whisper çevirisi). Hepsi yerel, ilk kullanımda bir kez indirilir.
11. ✅ **Sürüm 0.8.3 (altyazı çevirisi):** Yazılı bir altyazı izini başka bir dile çevirip aynı zamanlarla yeni bir ize yazma (Opus-MT, yerel). Türkçe, İngilizce, Almanca, Fransızca, İspanyolca, İtalyanca, Felemenkçe, Rusça ve Arapça arasında her yöne; doğrudan model yoksa İngilizce üzerinden. Her dil çiftinin modeli ilk kullanımda bir kez indirilir.
12. **Sonrası:** Çoklu kamera (ses ile eşitleme), OpenFX eklentileri, HDR10/HLG ve ton eşleme, OpenColorIO/ACES.
