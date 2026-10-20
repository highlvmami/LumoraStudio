<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="tr_TR">
<context>
    <name>ExportPlanner</name>
    <message>
        <source>The timeline is empty.</source>
        <translation>Zaman çizelgesi boş.</translation>
    </message>
    <message>
        <source>A clip refers to media that is missing from the project.</source>
        <translation>Bir klip, projede bulunmayan bir medyaya başvuruyor.</translation>
    </message>
    <message>
        <source>Nothing to export in the selected range.</source>
        <translation>Seçilen aralıkta dışa aktarılacak bir şey yok.</translation>
    </message>
    <message>
        <source>Unknown output container for &quot;%1&quot;.</source>
        <translation>&quot;%1&quot; için çıktı kapsayıcısı bilinmiyor.</translation>
    </message>
    <message>
        <source>Cannot read &quot;%1&quot;.</source>
        <translation>&quot;%1&quot; okunamıyor.</translation>
    </message>
    <message>
        <source>The clips use different video encodings, so they cannot be joined without re-encoding. Choose &quot;Match source&quot; or &quot;Lossless intermediate&quot;.</source>
        <translation>Klipler farklı video kodlamaları kullanıyor, bu yüzden yeniden kodlamadan birleştirilemezler. &quot;Kaynakla aynı&quot; ya da &quot;Kayıpsız ara format&quot; seçeneğini kullanın.</translation>
    </message>
    <message>
        <source>The %1 container cannot hold %2 video without re-encoding.</source>
        <translation>%1 kapsayıcısı %2 videoyu yeniden kodlamadan taşıyamaz.</translation>
    </message>
    <message numerus="yes">
        <source>%n cut(s) do not start on a key frame. Without re-encoding the clip must start at the previous key frame, so up to %1 s extra is included at the start. Use &quot;Snap cuts to key frames&quot; to see exactly what will be exported.</source>
        <translation>
            <numerusform>%n kesim bir anahtar karede başlamıyor. Yeniden kodlama olmadan klip bir önceki anahtar karede başlamak zorunda, bu yüzden başa en fazla %1 sn fazladan görüntü eklenir. Tam olarak neyin dışa aktarılacağını görmek için &quot;Kesimleri anahtar karelere hizala&quot; seçeneğini kullanın.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>The video uses B-frames: an out point that falls inside a frame group can end up to %n frame(s) earlier so that no damaged frame is ever written.</source>
        <translation>
            <numerusform>Video B-kareleri kullanıyor: bir kare grubunun içine düşen çıkış noktası, hiçbir bozuk kare yazılmasın diye en fazla %n kare erken bitebilir.</numerusform>
        </translation>
    </message>
    <message>
        <source>No %1 encoder is available in this FFmpeg build; H.264 is used instead.</source>
        <translation>Bu FFmpeg derlemesinde %1 kodlayıcı yok; onun yerine H.264 kullanılıyor.</translation>
    </message>
    <message>
        <source>No encoder available for %1.</source>
        <translation>%1 için kodlayıcı yok.</translation>
    </message>
    <message>
        <source>The %1 container cannot hold %2 video.</source>
        <translation>%1 kapsayıcısı %2 videoyu taşıyamaz.</translation>
    </message>
    <message>
        <source>Video is re-encoded with the source&apos;s settings (resolution, frame rate, bit depth, colour and bit rate). Every re-encode loses a little detail; use the lossless cut when you only cut and join.</source>
        <translation>Video, kaynağın ayarlarıyla (çözünürlük, kare hızı, bit derinliği, renk ve bit hızı) yeniden kodlanır. Her yeniden kodlama biraz ayrıntı kaybettirir; yalnızca kesip birleştiriyorsanız kayıpsız kesimi kullanın.</translation>
    </message>
    <message>
        <source>The clips use different audio formats, so the audio cannot be copied.</source>
        <translation>Klipler farklı ses biçimleri kullanıyor, bu yüzden ses kopyalanamıyor.</translation>
    </message>
    <message>
        <source>Some clips have no audio, so the audio cannot be copied. Turn off audio or choose a mode that re-encodes.</source>
        <translation>Bazı kliplerde ses yok, bu yüzden ses kopyalanamıyor. Sesi kapatın ya da yeniden kodlayan bir mod seçin.</translation>
    </message>
    <message>
        <source>No audio encoder available.</source>
        <translation>Ses kodlayıcı yok.</translation>
    </message>
    <message>
        <source>Audio-only clips cannot be joined without re-encoding, so they are left out of a lossless cut. Choose another mode to include them.</source>
        <translation>Yalnızca ses içeren klipler yeniden kodlanmadan birleştirilemez, bu yüzden kayıpsız kesime alınmaz. Onları da eklemek için başka bir mod seçin.</translation>
    </message>
    <message>
        <source>The timeline has titles, transitions, effects, audio changes or more than one track. These have to be drawn into the picture, which a lossless cut cannot do. Choose &quot;Match source&quot;, &quot;Lossless intermediate&quot; or &quot;Custom&quot;.</source>
        <translation>Zaman çizelgesinde başlık, geçiş, efekt, ses değişikliği ya da birden fazla iz var. Bunların görüntüye işlenmesi gerekir ve kayıpsız kesim bunu yapamaz. &quot;Kaynakla aynı&quot;, &quot;Kayıpsız ara format&quot; ya da &quot;Özel&quot; seçeneğini seçin.</translation>
    </message>
    <message>
        <source>The picture is scaled from %1×%2 to %3×%4%5.</source>
        <translation>Görüntü %1×%2 boyutundan %3×%4 boyutuna ölçeklenir%5.</translation>
    </message>
    <message>
        <source> with black bars to fill %1×%2</source>
        <translation>; %1×%2 alanı siyah bantlarla doldurulur</translation>
    </message>
    <message>
        <source>The %1 encoder does not work on this computer; %2 is used instead.</source>
        <translation>%1 kodlayıcısı bu bilgisayarda çalışmıyor; yerine %2 kullanılır.</translation>
    </message>
    <message>
        <source>The %1 encoder cannot write %2-bit video.</source>
        <translation>%1 kodlayıcısı %2 bit video yazamaz.</translation>
    </message>
    <message>
        <source>%1 MB is too small for %2 s of video.</source>
        <translation>%1 MB, %2 sn video için çok küçük.</translation>
    </message>
    <message>
        <source>The %1 encoder has no constant quality mode; choose a bit rate.</source>
        <translation>%1 kodlayıcısının sabit kalite modu yok; bir bit hızı seçin.</translation>
    </message>
    <message>
        <source>%1 cannot encode in two passes; one pass is used.</source>
        <translation>%1 iki geçişte kodlayamaz; tek geçiş kullanılır.</translation>
    </message>
    <message>
        <source>The %1 container cannot hold %2 audio.</source>
        <translation>%1 kabı %2 sesi taşıyamaz.</translation>
    </message>
    <message>
        <source>The audio is mixed (several clips, gain, fades or crossfades) and encoded as %1.</source>
        <translation>Ses karıştırılır (birden çok klip, ses düzeyi, kararmalar ya da çapraz geçişler) ve %1 olarak kodlanır.</translation>
    </message>
    <message>
        <source>The timeline is rendered with its titles, transitions and effects and encoded with the source&apos;s settings. Frames without effects are converted directly from the source.</source>
        <translation>Zaman çizelgesi başlıkları, geçişleri ve efektleriyle işlenir ve kaynağın ayarlarıyla kodlanır. Efekt içermeyen kareler doğrudan kaynaktan dönüştürülür.</translation>
    </message>
</context>
<context>
    <name>TimelineExporter</name>
    <message>
        <source>Could not finish the file: %1</source>
        <translation>Dosya tamamlanamadı: %1</translation>
    </message>
    <message>
        <source>Export cancelled.</source>
        <translation>Dışa aktarma iptal edildi.</translation>
    </message>
    <message>
        <source>Cannot open %1: %2</source>
        <translation>%1 açılamıyor: %2</translation>
    </message>
    <message>
        <source>The first clip has no video stream.</source>
        <translation>İlk klipte video akışı yok.</translation>
    </message>
    <message>
        <source>Cannot create the output file: %1</source>
        <translation>Çıktı dosyası oluşturulamıyor: %1</translation>
    </message>
    <message>
        <source>The first clip has no audio stream.</source>
        <translation>İlk klipte ses akışı yok.</translation>
    </message>
    <message>
        <source>Cannot write %1: %2</source>
        <translation>%1 yazılamıyor: %2</translation>
    </message>
    <message>
        <source>Cannot write the file header: %1</source>
        <translation>Dosya başlığı yazılamıyor: %1</translation>
    </message>
    <message>
        <source>Encoder %1 is not available.</source>
        <translation>%1 kodlayıcısı kullanılamıyor.</translation>
    </message>
    <message>
        <source>Cannot start the %1 encoder: %2</source>
        <translation>%1 kodlayıcısı başlatılamıyor: %2</translation>
    </message>
    <message>
        <source>Cannot create the audio converter.</source>
        <translation>Ses dönüştürücü oluşturulamıyor.</translation>
    </message>
    <message>
        <source>Write error: %1</source>
        <translation>Yazma hatası: %1</translation>
    </message>
    <message>
        <source>Video encoding failed: %1</source>
        <translation>Video kodlama başarısız: %1</translation>
    </message>
    <message>
        <source>Out of memory while buffering audio.</source>
        <translation>Ses arabelleğe alınırken bellek yetmedi.</translation>
    </message>
    <message>
        <source>Audio encoding failed: %1</source>
        <translation>Ses kodlama başarısız: %1</translation>
    </message>
    <message>
        <source>%1 has no video stream.</source>
        <translation>%1 içinde video akışı yok.</translation>
    </message>
    <message>
        <source>Cannot decode %1: %2</source>
        <translation>%1 çözülemiyor: %2</translation>
    </message>
    <message>
        <source>Cannot decode the audio of %1: %2</source>
        <translation>%1 dosyasının sesi çözülemiyor: %2</translation>
    </message>
    <message>
        <source>Cannot convert the video frames.</source>
        <translation>Video kareleri dönüştürülemiyor.</translation>
    </message>
    <message>
        <source>No key frame found in %1.</source>
        <translation>%1 içinde anahtar kare bulunamadı.</translation>
    </message>
</context>
<context>
    <name>lumora::ExportDialog</name>
    <message>
        <source>Export video</source>
        <translation>Videoyu dışa aktar</translation>
    </message>
    <message>
        <source>Quality</source>
        <translation>Kalite</translation>
    </message>
    <message>
        <source>Lossless cut (no re-encoding) — recommended</source>
        <translation>Kayıpsız kesim (yeniden kodlamasız) — önerilen</translation>
    </message>
    <message>
        <source>Video and audio are copied bit for bit. Quality is exactly the source&apos;s and export takes seconds. Cuts start on key frames.</source>
        <translation>Video ve ses bit bit kopyalanır. Kalite birebir kaynakla aynıdır ve dışa aktarma saniyeler sürer. Kesimler anahtar karelerde başlar.</translation>
    </message>
    <message>
        <source>Same as source (re-encode)</source>
        <translation>Kaynakla aynı (yeniden kodla)</translation>
    </message>
    <message>
        <source>Frame-exact cuts. Codec, resolution, frame rate, bit depth, colour and bit rate are taken from the source. Audio is copied when possible.</source>
        <translation>Kare hassasiyetinde kesim. Kodek, çözünürlük, kare hızı, bit derinliği, renk ve bit hızı kaynaktan alınır. Mümkünse ses kopyalanır.</translation>
    </message>
    <message>
        <source>Lossless intermediate (FFV1 / MKV)</source>
        <translation>Kayıpsız ara format (FFV1 / MKV)</translation>
    </message>
    <message>
        <source>Frame-exact and mathematically lossless. Very large files; ideal for archiving or further editing.</source>
        <translation>Kare hassasiyetinde ve matematiksel olarak kayıpsız. Dosyalar çok büyük olur; arşivleme ya da sonraki düzenlemeler için ideal.</translation>
    </message>
    <message>
        <source>Browse…</source>
        <translation>Gözat…</translation>
    </message>
    <message>
        <source>File:</source>
        <translation>Dosya:</translation>
    </message>
    <message>
        <source>Include audio</source>
        <translation>Sesi dahil et</translation>
    </message>
    <message>
        <source>What will happen</source>
        <translation>Ne olacak</translation>
    </message>
    <message>
        <source>Snap cuts to key frames</source>
        <translation>Kesimleri anahtar karelere hizala</translation>
    </message>
    <message>
        <source>Moves each clip&apos;s start back to the previous key frame on the timeline, so the timeline shows exactly what the lossless export contains. Can be undone.</source>
        <translation>Her klibin başlangıcını zaman çizelgesinde bir önceki anahtar kareye çeker; böylece zaman çizelgesi kayıpsız dışa aktarmanın tam olarak ne içereceğini gösterir. Geri alınabilir.</translation>
    </message>
    <message>
        <source>Export</source>
        <translation>Dışa aktar</translation>
    </message>
    <message>
        <source>Video files (*.mp4 *.mov *.mkv *.webm *.avi *.mxf *.ts);;All files (*)</source>
        <translation>Video dosyaları (*.mp4 *.mov *.mkv *.webm *.avi *.mxf *.ts);;Tüm dosyalar (*)</translation>
    </message>
    <message>
        <source>Video: copied unchanged (no quality loss)</source>
        <translation>Video: değiştirilmeden kopyalanır (kalite kaybı yok)</translation>
    </message>
    <message>
        <source>Video: encoded losslessly with %1</source>
        <translation>Video: %1 ile kayıpsız kodlanır</translation>
    </message>
    <message>
        <source>Video: re-encoded with %1 using the source settings</source>
        <translation>Video: kaynak ayarlarıyla %1 kullanılarak yeniden kodlanır</translation>
    </message>
    <message>
        <source>Audio: copied unchanged</source>
        <translation>Ses: değiştirilmeden kopyalanır</translation>
    </message>
    <message>
        <source>Audio: encoded with %1</source>
        <translation>Ses: %1 ile kodlanır</translation>
    </message>
    <message>
        <source>Audio: none</source>
        <translation>Ses: yok</translation>
    </message>
    <message numerus="yes">
        <source>Container: %1 · %n clip(s) · about %2</source>
        <translation>
            <numerusform>Kapsayıcı: %1 · %n klip · yaklaşık %2</numerusform>
        </translation>
    </message>
    <message>
        <source>The output file cannot be one of the source files.</source>
        <translation>Çıktı dosyası kaynak dosyalardan biri olamaz.</translation>
    </message>
    <message>
        <source>%1 already exists. Replace it?</source>
        <translation>%1 zaten var. Üzerine yazılsın mı?</translation>
    </message>
    <message>
        <source>Exporting…</source>
        <translation>Dışa aktarılıyor…</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>İptal</translation>
    </message>
    <message>
        <source>Export failed</source>
        <translation>Dışa aktarma başarısız</translation>
    </message>
    <message>
        <source>Export finished</source>
        <translation>Dışa aktarma bitti</translation>
    </message>
    <message>
        <source>Saved %1 (%2) in %3 s.</source>
        <translation>%1 (%2) %3 sn içinde kaydedildi.</translation>
    </message>
    <message>
        <source>Show in folder</source>
        <translation>Klasörde göster</translation>
    </message>
    <message>
        <source>Custom: codec, size and quality of your choice</source>
        <translation>Özel: kodek, boyut ve kalite sizin seçiminiz</translation>
    </message>
    <message>
        <source>Ready-made settings for YouTube, Instagram, TikTok, ProRes and more, or choose everything yourself, including your graphics card&apos;s encoder.</source>
        <translation>YouTube, Instagram, TikTok, ProRes ve daha fazlası için hazır ayarlar; ya da ekran kartınızın kodlayıcısı dahil her şeyi kendiniz seçin.</translation>
    </message>
    <message>
        <source>Custom settings</source>
        <translation>Özel ayarlar</translation>
    </message>
    <message>
        <source>Custom</source>
        <translation>Özel</translation>
    </message>
    <message>
        <source>Preset</source>
        <translation>Hazır ayar</translation>
    </message>
    <message>
        <source>Video codec</source>
        <translation>Video kodeği</translation>
    </message>
    <message>
        <source>Profile</source>
        <translation>Profil</translation>
    </message>
    <message>
        <source>Quality mode</source>
        <translation>Kalite modu</translation>
    </message>
    <message>
        <source>Bit rate</source>
        <translation>Bit hızı</translation>
    </message>
    <message>
        <source>automatic</source>
        <translation>otomatik</translation>
    </message>
    <message>
        <source>Peak bit rate</source>
        <translation>En yüksek bit hızı</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>Dosya boyutu</translation>
    </message>
    <message>
        <source>Two passes (better quality for the same size, takes longer)</source>
        <translation>İki geçiş (aynı boyutta daha iyi kalite, daha uzun sürer)</translation>
    </message>
    <message>
        <source>Same as timeline</source>
        <translation>Zaman çizelgesiyle aynı</translation>
    </message>
    <message>
        <source>1080 × 1920 (vertical 9:16)</source>
        <translation>1080 × 1920 (dikey 9:16)</translation>
    </message>
    <message>
        <source>1080 × 1350 (portrait 4:5)</source>
        <translation>1080 × 1350 (portre 4:5)</translation>
    </message>
    <message>
        <source>1080 × 1080 (square)</source>
        <translation>1080 × 1080 (kare)</translation>
    </message>
    <message>
        <source>Other…</source>
        <translation>Diğer…</translation>
    </message>
    <message>
        <source>Fit, black bars</source>
        <translation>Sığdır, siyah bantlı</translation>
    </message>
    <message>
        <source>Fit inside, no bars</source>
        <translation>İçine sığdır, bantsız</translation>
    </message>
    <message>
        <source>Frame size</source>
        <translation>Kare boyutu</translation>
    </message>
    <message>
        <source>Frame rate / depth</source>
        <translation>Kare hızı / derinlik</translation>
    </message>
    <message>
        <source>Faster</source>
        <translation>Daha hızlı</translation>
    </message>
    <message>
        <source>Smaller file</source>
        <translation>Daha küçük dosya</translation>
    </message>
    <message>
        <source>Encoding speed</source>
        <translation>Kodlama hızı</translation>
    </message>
    <message>
        <source>Copy when possible, else AAC</source>
        <translation>Mümkünse kopyala, değilse AAC</translation>
    </message>
    <message>
        <source>FLAC (lossless)</source>
        <translation>FLAC (kayıpsız)</translation>
    </message>
    <message>
        <source>PCM 24-bit (uncompressed)</source>
        <translation>PCM 24 bit (sıkıştırmasız)</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation>Ses</translation>
    </message>
    <message>
        <source>visually lossless</source>
        <translation>gözle kayıpsız</translation>
    </message>
    <message>
        <source>high</source>
        <translation>yüksek</translation>
    </message>
    <message>
        <source>smaller file</source>
        <translation>daha küçük dosya</translation>
    </message>
    <message>
        <source>Constant quality (recommended)</source>
        <translation>Sabit kalite (önerilen)</translation>
    </message>
    <message>
        <source>Variable bit rate</source>
        <translation>Değişken bit hızı</translation>
    </message>
    <message>
        <source>Constant bit rate</source>
        <translation>Sabit bit hızı</translation>
    </message>
    <message>
        <source>Target file size</source>
        <translation>Hedef dosya boyutu</translation>
    </message>
    <message>
        <source>Same bit depth as source</source>
        <translation>Kaynakla aynı bit derinliği</translation>
    </message>
    <message>
        <source> (graphics card)</source>
        <translation> (ekran kartı)</translation>
    </message>
    <message>
        <source>Video: rendered and encoded losslessly with %1</source>
        <translation>Video: işlenir ve %1 ile kayıpsız kodlanır</translation>
    </message>
    <message>
        <source>Video: rendered and encoded with %1 using the source settings</source>
        <translation>Video: işlenir ve kaynak ayarlarıyla %1 kullanılarak kodlanır</translation>
    </message>
    <message>
        <source>Video: rendered and encoded with %1</source>
        <translation>Video: işlenir ve %1 ile kodlanır</translation>
    </message>
    <message>
        <source> · two passes</source>
        <translation> · iki geçiş</translation>
    </message>
    <message>
        <source>Audio: mixed and encoded with %1</source>
        <translation>Ses: karıştırılır ve %1 ile kodlanır</translation>
    </message>
    <message>
        <source>Container: %1 · about %2</source>
        <translation>Kap: %1 · yaklaşık %2</translation>
    </message>
</context>
<context>
    <name>lumora::MainWindow</name>
    <message>
        <source>Edit</source>
        <translation>Kurgu</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>Renk</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation>Ses</translation>
    </message>
    <message>
        <source>Photo</source>
        <translation>Fotoğraf</translation>
    </message>
    <message>
        <source>Colour wheels, curves, LUTs and scopes arrive in version 0.3</source>
        <translation>Renk tekerlekleri, eğriler, LUT ve skoplar 0.3 sürümünde geliyor</translation>
    </message>
    <message>
        <source>Audio tools arrive in version 0.4</source>
        <translation>Ses araçları 0.4 sürümünde geliyor</translation>
    </message>
    <message>
        <source>Workspaces</source>
        <translation>Çalışma alanları</translation>
    </message>
    <message>
        <source>FFmpeg %1 · Qt %2</source>
        <translation>FFmpeg %1 · Qt %2</translation>
    </message>
    <message>
        <source>&amp;New project</source>
        <translation>&amp;Yeni proje</translation>
    </message>
    <message>
        <source>&amp;Open project…</source>
        <translation>Projeyi &amp;aç…</translation>
    </message>
    <message>
        <source>&amp;Save project</source>
        <translation>Projeyi &amp;kaydet</translation>
    </message>
    <message>
        <source>Save project &amp;as…</source>
        <translation>Projeyi &amp;farklı kaydet…</translation>
    </message>
    <message>
        <source>&amp;Import media…</source>
        <translation>&amp;Medya içe aktar…</translation>
    </message>
    <message>
        <source>&amp;Export video…</source>
        <translation>Videoyu &amp;dışa aktar…</translation>
    </message>
    <message>
        <source>Export &amp;photo…</source>
        <translation>&amp;Fotoğrafı dışa aktar…</translation>
    </message>
    <message>
        <source>&amp;Quit</source>
        <translation>Çı&amp;kış</translation>
    </message>
    <message>
        <source>Undo</source>
        <translation>Geri al</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>Yinele</translation>
    </message>
    <message>
        <source>&amp;Split at playhead</source>
        <translation>Oynatma kafasından &amp;böl</translation>
    </message>
    <message>
        <source>&amp;Delete clip (close gap)</source>
        <translation>Klibi &amp;sil (boşluğu kapat)</translation>
    </message>
    <message>
        <source>&amp;Join with next clip</source>
        <translation>Sonraki kliple &amp;birleştir</translation>
    </message>
    <message>
        <source>Rejoin a clip with the next one when they are consecutive parts of the same file</source>
        <translation>Aynı dosyanın ardışık parçalarıysa klibi bir sonrakiyle yeniden birleştirir</translation>
    </message>
    <message>
        <source>Play / pause</source>
        <translation>Oynat / duraklat</translation>
    </message>
    <message>
        <source>Previous frame</source>
        <translation>Önceki kare</translation>
    </message>
    <message>
        <source>Next frame</source>
        <translation>Sonraki kare</translation>
    </message>
    <message>
        <source>Go to start</source>
        <translation>Başa git</translation>
    </message>
    <message>
        <source>Go to end</source>
        <translation>Sona git</translation>
    </message>
    <message>
        <source>Set in point</source>
        <translation>Giriş noktası koy</translation>
    </message>
    <message>
        <source>Set out point</source>
        <translation>Çıkış noktası koy</translation>
    </message>
    <message>
        <source>Add source range to timeline</source>
        <translation>Kaynak aralığını zaman çizelgesine ekle</translation>
    </message>
    <message>
        <source>Snapping</source>
        <translation>Yapışma</translation>
    </message>
    <message>
        <source>Snap the playhead to clip edges (N)</source>
        <translation>Oynatma kafasını klip kenarlarına yapıştır (N)</translation>
    </message>
    <message>
        <source>Zoom in</source>
        <translation>Yakınlaştır</translation>
    </message>
    <message>
        <source>Zoom out</source>
        <translation>Uzaklaştır</translation>
    </message>
    <message>
        <source>Reset panel layout</source>
        <translation>Panel düzenini sıfırla</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>&amp;Dosya</translation>
    </message>
    <message>
        <source>&amp;Edit</source>
        <translation>Dü&amp;zen</translation>
    </message>
    <message>
        <source>&amp;Playback</source>
        <translation>&amp;Oynatma</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>&amp;Görünüm</translation>
    </message>
    <message>
        <source>Theme</source>
        <translation>Tema</translation>
    </message>
    <message>
        <source>Dark</source>
        <translation>Koyu</translation>
    </message>
    <message>
        <source>Light</source>
        <translation>Açık</translation>
    </message>
    <message>
        <source>The theme changes the next time LumoraStudio starts.</source>
        <translation>Tema, LumoraStudio bir sonraki açılışında değişir.</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>Dil</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>Sistem varsayılanı</translation>
    </message>
    <message>
        <source>Panels</source>
        <translation>Paneller</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>&amp;Yardım</translation>
    </message>
    <message>
        <source>Keyboard shortcuts</source>
        <translation>Klavye kısayolları</translation>
    </message>
    <message>
        <source>Feature list (roadmap)</source>
        <translation>Özellik listesi (yol haritası)</translation>
    </message>
    <message>
        <source>About LumoraStudio</source>
        <translation>LumoraStudio hakkında</translation>
    </message>
    <message>
        <source>Files</source>
        <translation>Dosyalar</translation>
    </message>
    <message>
        <source>Properties</source>
        <translation>Özellikler</translation>
    </message>
    <message>
        <source>Timeline</source>
        <translation>Zaman çizelgesi</translation>
    </message>
    <message>
        <source>Untitled</source>
        <translation>Adsız</translation>
    </message>
    <message>
        <source>Unsaved changes</source>
        <translation>Kaydedilmemiş değişiklikler</translation>
    </message>
    <message>
        <source>Save the changes to this project?</source>
        <translation>Bu projedeki değişiklikler kaydedilsin mi?</translation>
    </message>
    <message>
        <source>Open project</source>
        <translation>Projeyi aç</translation>
    </message>
    <message>
        <source>LumoraStudio projects (*.lumora)</source>
        <translation>LumoraStudio projeleri (*.lumora)</translation>
    </message>
    <message>
        <source>Cannot open %1:
%2</source>
        <translation>%1 açılamıyor:
%2</translation>
    </message>
    <message>
        <source>Missing media</source>
        <translation>Eksik medya</translation>
    </message>
    <message>
        <source>These files could not be found. They stay in the project; move them back or relink them in a later version:

%1</source>
        <translation>Bu dosyalar bulunamadı. Projede kalmaya devam ediyorlar; yerlerine geri taşıyın ya da sonraki bir sürümde yeniden bağlayın:

%1</translation>
    </message>
    <message>
        <source>Save project</source>
        <translation>Projeyi kaydet</translation>
    </message>
    <message>
        <source>Project saved</source>
        <translation>Proje kaydedildi</translation>
    </message>
    <message>
        <source>Import media</source>
        <translation>Medya içe aktar</translation>
    </message>
    <message>
        <source>Media files (%1);;All files (*)</source>
        <translation>Medya dosyaları (%1);;Tüm dosyalar (*)</translation>
    </message>
    <message>
        <source>Some files could not be opened:

%1</source>
        <translation>Bazı dosyalar açılamadı:

%1</translation>
    </message>
    <message numerus="yes">
        <source>Imported %n file(s)</source>
        <translation>
            <numerusform>%n dosya içe aktarıldı</numerusform>
        </translation>
    </message>
    <message>
        <source>Photos are edited in the Photo workspace; photo-to-video arrives in a later version.</source>
        <translation>Fotoğraflar Fotoğraf çalışma alanında düzenlenir; fotoğraftan videoya sonraki bir sürümde geliyor.</translation>
    </message>
    <message>
        <source>Add clip</source>
        <translation>Klip ekle</translation>
    </message>
    <message>
        <source>Remove from project</source>
        <translation>Projeden kaldır</translation>
    </message>
    <message>
        <source>This file is used on the timeline. Delete its clips first.</source>
        <translation>Bu dosya zaman çizelgesinde kullanılıyor. Önce kliplerini silin.</translation>
    </message>
    <message>
        <source>Export video</source>
        <translation>Videoyu dışa aktar</translation>
    </message>
    <message>
        <source>The timeline is empty. Add a video first.</source>
        <translation>Zaman çizelgesi boş. Önce bir video ekleyin.</translation>
    </message>
    <message>
        <source>Split</source>
        <translation>Böl</translation>
    </message>
    <message>
        <source>Move the playhead over a clip to split it.</source>
        <translation>Bölmek için oynatma kafasını bir klibin üzerine getirin.</translation>
    </message>
    <message>
        <source>Delete clip</source>
        <translation>Klibi sil</translation>
    </message>
    <message>
        <source>Join clips</source>
        <translation>Klipleri birleştir</translation>
    </message>
    <message>
        <source>The language changes the next time LumoraStudio starts.</source>
        <translation>Dil, LumoraStudio bir sonraki açılışında değişir.</translation>
    </message>
    <message>
        <source>wheel</source>
        <translation>tekerlek</translation>
    </message>
    <message>
        <source>Zoom the timeline</source>
        <translation>Zaman çizelgesini yakınlaştır</translation>
    </message>
    <message>
        <source>&lt;h3&gt;LumoraStudio %1&lt;/h3&gt;&lt;p&gt;A free video and photo editor whose first rule is: quality never drops unless you ask for it.&lt;/p&gt;&lt;p&gt;Cut and join without re-encoding, export with the source&apos;s own settings, edit photos non-destructively and save them losslessly.&lt;/p&gt;&lt;p&gt;FFmpeg %2 · Qt %3&lt;/p&gt;</source>
        <translation>&lt;h3&gt;LumoraStudio %1&lt;/h3&gt;&lt;p&gt;İlk kuralı şu olan ücretsiz bir video ve fotoğraf düzenleyici: siz istemedikçe kalite asla düşmez.&lt;/p&gt;&lt;p&gt;Yeniden kodlamadan kesin ve birleştirin, kaynağın kendi ayarlarıyla dışa aktarın, fotoğrafları orijinale dokunmadan düzenleyip kayıpsız kaydedin.&lt;/p&gt;&lt;p&gt;FFmpeg %2 · Qt %3&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Remove transition</source>
        <translation>Geçişi kaldır</translation>
    </message>
    <message>
        <source>Add &amp;title</source>
        <translation>&amp;Başlık ekle</translation>
    </message>
    <message>
        <source>Add a title at the playhead on a track above the storyline (T)</source>
        <translation>Oynatma kafasına, ana izin üstündeki bir ize başlık ekle (T)</translation>
    </message>
    <message>
        <source>Add t&amp;ransition</source>
        <translation>&amp;Geçiş ekle</translation>
    </message>
    <message>
        <source>Cross dissolve into the selected clip, or at the cut nearest the playhead (Ctrl+T)</source>
        <translation>Seçili klibe ya da oynatma kafasına en yakın kesime çapraz geçiş ekle (Ctrl+T)</translation>
    </message>
    <message>
        <source>Add video track</source>
        <translation>Video izi ekle</translation>
    </message>
    <message>
        <source>Add audio track</source>
        <translation>Ses izi ekle</translation>
    </message>
    <message>
        <source>Export current &amp;frame…</source>
        <translation>Geçerli &amp;kareyi dışa aktar…</translation>
    </message>
    <message>
        <source>This file has no sound; drop it on a video track.</source>
        <translation>Bu dosyada ses yok; bir video izine bırakın.</translation>
    </message>
    <message>
        <source>Add title</source>
        <translation>Başlık ekle</translation>
    </message>
    <message>
        <source>Your title</source>
        <translation>Başlığınız</translation>
    </message>
    <message>
        <source>A transition needs two clips next to each other on the storyline.</source>
        <translation>Geçiş için ana izde yan yana iki klip gerekir.</translation>
    </message>
    <message>
        <source>Add transition</source>
        <translation>Geçiş ekle</translation>
    </message>
    <message>
        <source>There is no extra footage beyond this cut, so the transition cannot be shown. Trim the clips to leave some media on both sides.</source>
        <translation>Bu kesimin ötesinde fazladan görüntü yok, bu yüzden geçiş gösterilemez. Her iki tarafta biraz malzeme kalacak şekilde klipleri kırpın.</translation>
    </message>
    <message>
        <source>Export current frame</source>
        <translation>Geçerli kareyi dışa aktar</translation>
    </message>
    <message>
        <source>PNG image (*.png);;TIFF image (*.tif *.tiff)</source>
        <translation>PNG görüntüsü (*.png);;TIFF görüntüsü (*.tif *.tiff)</translation>
    </message>
    <message>
        <source>Could not save %1.</source>
        <translation>%1 kaydedilemedi.</translation>
    </message>
    <message>
        <source>Frame saved to %1</source>
        <translation>Kare %1 konumuna kaydedildi</translation>
    </message>
</context>
<context>
    <name>lumora::MediaBinPanel</name>
    <message>
        <source>Search files</source>
        <translation>Dosyalarda ara</translation>
    </message>
    <message>
        <source>Import media (Ctrl+I)</source>
        <translation>Medya içe aktar (Ctrl+I)</translation>
    </message>
    <message>
        <source>List</source>
        <translation>Liste</translation>
    </message>
    <message>
        <source>Switch between thumbnails and a list</source>
        <translation>Küçük resimlerle liste arasında geçiş yap</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>Aç</translation>
    </message>
    <message>
        <source>Add to timeline</source>
        <translation>Zaman çizelgesine ekle</translation>
    </message>
    <message>
        <source>Remove from project</source>
        <translation>Projeden kaldır</translation>
    </message>
</context>
<context>
    <name>lumora::MonitorPanel</name>
    <message>
        <source>Source</source>
        <translation>Kaynak</translation>
    </message>
    <message>
        <source>Show the selected file. Double-click a file to open it here.</source>
        <translation>Seçili dosyayı gösterir. Burada açmak için bir dosyaya çift tıklayın.</translation>
    </message>
    <message>
        <source>Timeline</source>
        <translation>Zaman çizelgesi</translation>
    </message>
    <message>
        <source>Show the edited timeline</source>
        <translation>Düzenlenen zaman çizelgesini gösterir</translation>
    </message>
    <message>
        <source>Import videos with File → Import media, or drag them here.</source>
        <translation>Videoları Dosya → Medya içe aktar ile ekleyin ya da buraya sürükleyin.</translation>
    </message>
    <message>
        <source>Go to start (Home)</source>
        <translation>Başa git (Home)</translation>
    </message>
    <message>
        <source>Previous frame (Left)</source>
        <translation>Önceki kare (Sol)</translation>
    </message>
    <message>
        <source>Play / pause (Space)</source>
        <translation>Oynat / duraklat (Boşluk)</translation>
    </message>
    <message>
        <source>Next frame (Right)</source>
        <translation>Sonraki kare (Sağ)</translation>
    </message>
    <message>
        <source>Go to end (End)</source>
        <translation>Sona git (End)</translation>
    </message>
    <message>
        <source>In</source>
        <translation>Giriş</translation>
    </message>
    <message>
        <source>Set in point (I)</source>
        <translation>Giriş noktası koy (I)</translation>
    </message>
    <message>
        <source>Out</source>
        <translation>Çıkış</translation>
    </message>
    <message>
        <source>Set out point (O)</source>
        <translation>Çıkış noktası koy (O)</translation>
    </message>
    <message>
        <source>Add to timeline</source>
        <translation>Zaman çizelgesine ekle</translation>
    </message>
    <message>
        <source>Add the range between the in and out points to the end of the timeline</source>
        <translation>Giriş ve çıkış noktaları arasındaki aralığı zaman çizelgesinin sonuna ekler</translation>
    </message>
    <message>
        <source>Add to timeline (%1)</source>
        <translation>Zaman çizelgesine ekle (%1)</translation>
    </message>
</context>
<context>
    <name>lumora::PhotoExportDialog</name>
    <message>
        <source>Export photo</source>
        <translation>Fotoğrafı dışa aktar</translation>
    </message>
    <message>
        <source>PNG — lossless</source>
        <translation>PNG — kayıpsız</translation>
    </message>
    <message>
        <source>TIFF — lossless</source>
        <translation>TIFF — kayıpsız</translation>
    </message>
    <message>
        <source>JPEG — smaller, lossy</source>
        <translation>JPEG — daha küçük, kayıplı</translation>
    </message>
    <message>
        <source>WebP — smaller, lossy</source>
        <translation>WebP — daha küçük, kayıplı</translation>
    </message>
    <message>
        <source>Format:</source>
        <translation>Biçim:</translation>
    </message>
    <message>
        <source>Automatic (keep quality)</source>
        <translation>Otomatik (kaliteyi koru)</translation>
    </message>
    <message>
        <source>8-bit</source>
        <translation>8 bit</translation>
    </message>
    <message>
        <source>16-bit</source>
        <translation>16 bit</translation>
    </message>
    <message>
        <source>Bit depth:</source>
        <translation>Bit derinliği:</translation>
    </message>
    <message>
        <source>Quality:</source>
        <translation>Kalite:</translation>
    </message>
    <message>
        <source>Browse…</source>
        <translation>Gözat…</translation>
    </message>
    <message>
        <source>File:</source>
        <translation>Dosya:</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>Kaydet</translation>
    </message>
    <message>
        <source>JPEG and WebP discard detail to make files smaller. Choose PNG or TIFF to keep full quality.</source>
        <translation>JPEG ve WebP, dosyaları küçültmek için ayrıntıdan feda eder. Tam kaliteyi korumak için PNG ya da TIFF seçin.</translation>
    </message>
    <message>
        <source>Lossless: the pixels are written exactly as in the original (only the crop is applied).</source>
        <translation>Kayıpsız: pikseller orijinaldeki gibi birebir yazılır (yalnızca kırpma uygulanır).</translation>
    </message>
    <message>
        <source>Lossless 16-bit: the adjustments are rendered in 32-bit float and stored with 16 bits per channel, so no banding is introduced.</source>
        <translation>Kayıpsız 16 bit: ayarlar 32 bit kayan noktada hesaplanır ve kanal başına 16 bit saklanır, böylece bantlaşma oluşmaz.</translation>
    </message>
    <message>
        <source>8-bit output after adjustments: smaller, but subtle gradients may show slight banding.</source>
        <translation>Ayarlardan sonra 8 bit çıktı: daha küçük, ama yumuşak geçişlerde hafif bantlaşma görülebilir.</translation>
    </message>
    <message>
        <source>The colour profile (%1) is kept.</source>
        <translation>Renk profili (%1) korunur.</translation>
    </message>
    <message>
        <source>Choose a different file: the original is never overwritten.</source>
        <translation>Farklı bir dosya seçin: orijinalin üzerine asla yazılmaz.</translation>
    </message>
    <message>
        <source>%1 already exists. Replace it?</source>
        <translation>%1 zaten var. Üzerine yazılsın mı?</translation>
    </message>
    <message>
        <source>Could not save: %1</source>
        <translation>Kaydedilemedi: %1</translation>
    </message>
</context>
<context>
    <name>lumora::PhotoWorkspace</name>
    <message>
        <source>Open photos…</source>
        <translation>Fotoğraf aç…</translation>
    </message>
    <message>
        <source>Open a photo (JPEG, PNG, TIFF, WebP) with &quot;Open photos…&quot; or drag it here.

The original file is never changed: edits are stored in the project and applied when you export.</source>
        <translation>&quot;Fotoğraf aç…&quot; ile bir fotoğraf (JPEG, PNG, TIFF, WebP) açın ya da buraya sürükleyin.

Orijinal dosya asla değişmez: düzenlemeler projede saklanır ve dışa aktarırken uygulanır.</translation>
    </message>
    <message>
        <source>Light</source>
        <translation>Işık</translation>
    </message>
    <message>
        <source>Exposure (EV)</source>
        <translation>Pozlama (EV)</translation>
    </message>
    <message>
        <source>Contrast</source>
        <translation>Kontrast</translation>
    </message>
    <message>
        <source>White balance</source>
        <translation>Beyaz dengesi</translation>
    </message>
    <message>
        <source>Temperature</source>
        <translation>Sıcaklık</translation>
    </message>
    <message>
        <source>Tint</source>
        <translation>Ton</translation>
    </message>
    <message>
        <source>Crop</source>
        <translation>Kırp</translation>
    </message>
    <message>
        <source>Show the whole photo and drag the frame (C)</source>
        <translation>Fotoğrafın tamamını göster ve çerçeveyi sürükle (C)</translation>
    </message>
    <message>
        <source>Free</source>
        <translation>Serbest</translation>
    </message>
    <message>
        <source>Original</source>
        <translation>Orijinal</translation>
    </message>
    <message>
        <source>Remove the crop</source>
        <translation>Kırpmayı kaldır</translation>
    </message>
    <message>
        <source>Before</source>
        <translation>Önce</translation>
    </message>
    <message>
        <source>Reset</source>
        <translation>Sıfırla</translation>
    </message>
    <message>
        <source>Export photo…</source>
        <translation>Fotoğrafı dışa aktar…</translation>
    </message>
    <message>
        <source>Exposure</source>
        <translation>Pozlama</translation>
    </message>
    <message>
        <source>Remove crop</source>
        <translation>Kırpmayı kaldır</translation>
    </message>
    <message>
        <source>Open photos</source>
        <translation>Fotoğraf aç</translation>
    </message>
    <message>
        <source>Photos (%1)</source>
        <translation>Fotoğraflar (%1)</translation>
    </message>
    <message>
        <source>Open photo</source>
        <translation>Fotoğraf aç</translation>
    </message>
    <message>
        <source>Cannot open %1: %2</source>
        <translation>%1 açılamıyor: %2</translation>
    </message>
    <message>
        <source>%1 × %2 px · %3-bit source · output %4 × %5 px</source>
        <translation>%1 × %2 px · %3 bit kaynak · çıktı %4 × %5 px</translation>
    </message>
    <message>
        <source>Reset photo</source>
        <translation>Fotoğrafı sıfırla</translation>
    </message>
    <message>
        <source>Saved %1</source>
        <translation>%1 kaydedildi</translation>
    </message>
    <message>
        <source>Compare with the original (\)</source>
        <translation>Orijinalle karşılaştır (\)</translation>
    </message>
</context>
<context>
    <name>lumora::Project</name>
    <message>
        <source>This project was saved by a newer LumoraStudio version.</source>
        <translation>Bu proje daha yeni bir LumoraStudio sürümüyle kaydedilmiş.</translation>
    </message>
</context>
<context>
    <name>lumora::PropertiesPanel</name>
    <message>
        <source>Select a clip or a file to see its properties.</source>
        <translation>Özelliklerini görmek için bir klip ya da dosya seçin.</translation>
    </message>
    <message>
        <source>Clip</source>
        <translation>Klip</translation>
    </message>
    <message>
        <source>In</source>
        <translation>Giriş</translation>
    </message>
    <message>
        <source>Out</source>
        <translation>Çıkış</translation>
    </message>
    <message>
        <source>Duration</source>
        <translation>Süre</translation>
    </message>
    <message>
        <source>Timeline position</source>
        <translation>Zaman çizelgesindeki yeri</translation>
    </message>
    <message>
        <source>Lossless cut</source>
        <translation>Kayıpsız kesim</translation>
    </message>
    <message>
        <source>starts on a key frame</source>
        <translation>anahtar karede başlıyor</translation>
    </message>
    <message numerus="yes">
        <source>starts %n frame(s) after a key frame</source>
        <translation>
            <numerusform>anahtar kareden %n kare sonra başlıyor</numerusform>
        </translation>
    </message>
    <message>
        <source>File</source>
        <translation>Dosya</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>Ad</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>Klasör</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>Biçim</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Boyut</translation>
    </message>
    <message>
        <source>Resolution</source>
        <translation>Çözünürlük</translation>
    </message>
    <message>
        <source>Bit depth</source>
        <translation>Bit derinliği</translation>
    </message>
    <message>
        <source>Bit rate</source>
        <translation>Bit hızı</translation>
    </message>
    <message>
        <source>Video</source>
        <translation>Video</translation>
    </message>
    <message>
        <source>Codec</source>
        <translation>Kodek</translation>
    </message>
    <message>
        <source>Frame rate</source>
        <translation>Kare hızı</translation>
    </message>
    <message>
        <source>variable</source>
        <translation>değişken</translation>
    </message>
    <message>
        <source>Pixel format</source>
        <translation>Piksel biçimi</translation>
    </message>
    <message>
        <source>Colour</source>
        <translation>Renk</translation>
    </message>
    <message>
        <source>Range</source>
        <translation>Aralık</translation>
    </message>
    <message>
        <source>full</source>
        <translation>tam</translation>
    </message>
    <message>
        <source>limited</source>
        <translation>sınırlı</translation>
    </message>
    <message>
        <source>HDR</source>
        <translation>HDR</translation>
    </message>
    <message>
        <source>Rotation</source>
        <translation>Döndürme</translation>
    </message>
    <message>
        <source>B-frames</source>
        <translation>B-kareleri</translation>
    </message>
    <message>
        <source>yes</source>
        <translation>evet</translation>
    </message>
    <message>
        <source>no</source>
        <translation>hayır</translation>
    </message>
    <message>
        <source>Key frames</source>
        <translation>Anahtar kareler</translation>
    </message>
    <message>
        <source>%1 (every %2 s on average)</source>
        <translation>%1 (ortalama %2 sn’de bir)</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation>Ses</translation>
    </message>
    <message>
        <source>Sample rate</source>
        <translation>Örnekleme hızı</translation>
    </message>
    <message>
        <source>Channels</source>
        <translation>Kanallar</translation>
    </message>
    <message>
        <source>Sample format</source>
        <translation>Örnek biçimi</translation>
    </message>
    <message>
        <source>not tagged</source>
        <translation>etiketsiz</translation>
    </message>
    <message>
        <source>Title</source>
        <translation>Başlık</translation>
    </message>
    <message>
        <source>Type the title text</source>
        <translation>Başlık metnini yazın</translation>
    </message>
    <message>
        <source>Font</source>
        <translation>Yazı tipi</translation>
    </message>
    <message>
        <source>Text height as a percentage of the frame height</source>
        <translation>Metin yüksekliği, kare yüksekliğinin yüzdesi olarak</translation>
    </message>
    <message>
        <source>Text colour</source>
        <translation>Metin rengi</translation>
    </message>
    <message>
        <source>Left</source>
        <translation>Sol</translation>
    </message>
    <message>
        <source>Centre</source>
        <translation>Orta</translation>
    </message>
    <message>
        <source>Right</source>
        <translation>Sağ</translation>
    </message>
    <message>
        <source>Alignment</source>
        <translation>Hizalama</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>Konum</translation>
    </message>
    <message>
        <source>Outline width as a percentage of the text size</source>
        <translation>Kontur kalınlığı, metin boyutunun yüzdesi olarak</translation>
    </message>
    <message>
        <source>Outline</source>
        <translation>Kontur</translation>
    </message>
    <message>
        <source>Background</source>
        <translation>Arka plan</translation>
    </message>
    <message>
        <source>Soft shadow</source>
        <translation>Yumuşak gölge</translation>
    </message>
    <message>
        <source>None</source>
        <translation>Yok</translation>
    </message>
    <message>
        <source>Fade</source>
        <translation>Solma</translation>
    </message>
    <message>
        <source>Slide up</source>
        <translation>Yukarı kay</translation>
    </message>
    <message>
        <source>Pop</source>
        <translation>Belirme</translation>
    </message>
    <message>
        <source>Typewriter</source>
        <translation>Daktilo</translation>
    </message>
    <message>
        <source>Animation in</source>
        <translation>Giriş animasyonu</translation>
    </message>
    <message>
        <source>Animation out</source>
        <translation>Çıkış animasyonu</translation>
    </message>
    <message>
        <source>Animation length</source>
        <translation>Animasyon süresi</translation>
    </message>
    <message>
        <source>Position and size</source>
        <translation>Konum ve boyut</translation>
    </message>
    <message>
        <source>Scale</source>
        <translation>Ölçek</translation>
    </message>
    <message>
        <source>Opacity</source>
        <translation>Opaklık</translation>
    </message>
    <message>
        <source>Reset</source>
        <translation>Sıfırla</translation>
    </message>
    <message>
        <source>Transition from the previous clip</source>
        <translation>Önceki klipten geçiş</translation>
    </message>
    <message>
        <source>None (cut)</source>
        <translation>Yok (kesme)</translation>
    </message>
    <message>
        <source>Cross dissolve</source>
        <translation>Çapraz geçiş</translation>
    </message>
    <message>
        <source>Dip to black</source>
        <translation>Siyaha geçiş</translation>
    </message>
    <message>
        <source>Dip to white</source>
        <translation>Beyaza geçiş</translation>
    </message>
    <message>
        <source>Wipe left</source>
        <translation>Sola silme</translation>
    </message>
    <message>
        <source>Wipe right</source>
        <translation>Sağa silme</translation>
    </message>
    <message>
        <source>Wipe up</source>
        <translation>Yukarı silme</translation>
    </message>
    <message>
        <source>Wipe down</source>
        <translation>Aşağı silme</translation>
    </message>
    <message>
        <source>Push left</source>
        <translation>Sola itme</translation>
    </message>
    <message>
        <source>Push right</source>
        <translation>Sağa itme</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>Yakınlaştırma</translation>
    </message>
    <message>
        <source>Blur</source>
        <translation>Bulanıklaştırma</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>Tür</translation>
    </message>
    <message>
        <source>Fades</source>
        <translation>Kararmalar</translation>
    </message>
    <message>
        <source>Fade in</source>
        <translation>Belirme</translation>
    </message>
    <message>
        <source>Fade out</source>
        <translation>Kararma</translation>
    </message>
    <message>
        <source>Sound</source>
        <translation>Ses</translation>
    </message>
    <message>
        <source>Volume</source>
        <translation>Ses düzeyi</translation>
    </message>
    <message>
        <source>Choose a colour</source>
        <translation>Bir renk seçin</translation>
    </message>
    <message>
        <source>Change title length</source>
        <translation>Başlık süresini değiştir</translation>
    </message>
    <message>
        <source>Reset position and size</source>
        <translation>Konumu ve boyutu sıfırla</translation>
    </message>
    <message>
        <source>Change volume</source>
        <translation>Ses düzeyini değiştir</translation>
    </message>
    <message>
        <source>Edit title</source>
        <translation>Başlığı düzenle</translation>
    </message>
    <message>
        <source>Change position and size</source>
        <translation>Konumu ve boyutu değiştir</translation>
    </message>
    <message>
        <source>Change transition</source>
        <translation>Geçişi değiştir</translation>
    </message>
    <message>
        <source>Change fades</source>
        <translation>Kararmaları değiştir</translation>
    </message>
    <message>
        <source>Video clip</source>
        <translation>Video klibi</translation>
    </message>
    <message>
        <source>Audio clip</source>
        <translation>Ses klibi</translation>
    </message>
    <message>
        <source>Not shown: there is no footage beyond this cut. Trim the clips to leave media on both sides.</source>
        <translation>Gösterilmiyor: bu kesimin ötesinde görüntü yok. Her iki tarafta malzeme kalacak şekilde klipleri kırpın.</translation>
    </message>
    <message>
        <source>Shortened to %1 s by the footage available around the cut.</source>
        <translation>Kesimin çevresindeki görüntü yetmediği için %1 sn&apos;ye kısaltıldı.</translation>
    </message>
</context>
<context>
    <name>lumora::RenderSession</name>
    <message>
        <source>Export cancelled.</source>
        <translation>Dışa aktarma iptal edildi.</translation>
    </message>
    <message>
        <source>Could not finish the file: %1</source>
        <translation>Dosya tamamlanamadı: %1</translation>
    </message>
    <message>
        <source>Encoder %1 is not available.</source>
        <translation>%1 kodlayıcısı kullanılamıyor.</translation>
    </message>
    <message>
        <source>Cannot start the %1 encoder: %2</source>
        <translation>%1 kodlayıcısı başlatılamıyor: %2</translation>
    </message>
    <message>
        <source>Out of memory.</source>
        <translation>Bellek yetersiz.</translation>
    </message>
    <message>
        <source>Cannot create the output file: %1</source>
        <translation>Çıktı dosyası oluşturulamıyor: %1</translation>
    </message>
    <message>
        <source>Cannot write %1: %2</source>
        <translation>%1 yazılamıyor: %2</translation>
    </message>
    <message>
        <source>Cannot write the file header: %1</source>
        <translation>Dosya başlığı yazılamıyor: %1</translation>
    </message>
    <message>
        <source>Cannot open %1: %2</source>
        <translation>%1 açılamıyor: %2</translation>
    </message>
    <message>
        <source>%1 has no audio stream.</source>
        <translation>%1 içinde ses akışı yok.</translation>
    </message>
    <message>
        <source>Cannot create the audio converter.</source>
        <translation>Ses dönüştürücü oluşturulamıyor.</translation>
    </message>
    <message>
        <source>Cannot convert the video frames.</source>
        <translation>Video kareleri dönüştürülemiyor.</translation>
    </message>
    <message>
        <source>Could not render the frame at %1 s.</source>
        <translation>%1. saniyedeki kare işlenemedi.</translation>
    </message>
    <message>
        <source>Video encoding failed: %1</source>
        <translation>Video kodlama başarısız: %1</translation>
    </message>
    <message>
        <source>Write error: %1</source>
        <translation>Yazma hatası: %1</translation>
    </message>
    <message>
        <source>Out of memory while buffering audio.</source>
        <translation>Ses arabelleğe alınırken bellek yetmedi.</translation>
    </message>
    <message>
        <source>Audio encoding failed: %1</source>
        <translation>Ses kodlama başarısız: %1</translation>
    </message>
</context>
<context>
    <name>lumora::SliderRow</name>
    <message>
        <source>Double-click to reset</source>
        <translation>Sıfırlamak için çift tıklayın</translation>
    </message>
</context>
<context>
    <name>lumora::TimelinePanel</name>
    <message>
        <source>Zoom out (-)</source>
        <translation>Uzaklaştır (-)</translation>
    </message>
    <message>
        <source>Zoom (Ctrl + mouse wheel)</source>
        <translation>Yakınlaştırma (Ctrl + fare tekerleği)</translation>
    </message>
    <message>
        <source>Zoom in (+)</source>
        <translation>Yakınlaştır (+)</translation>
    </message>
    <message>
        <source>Fit</source>
        <translation>Sığdır</translation>
    </message>
    <message>
        <source>Show the whole timeline</source>
        <translation>Zaman çizelgesinin tamamını göster</translation>
    </message>
</context>
<context>
    <name>lumora::TimelineView</name>
    <message>
        <source>Missing media</source>
        <translation>Eksik medya</translation>
    </message>
    <message>
        <source>Video</source>
        <translation>Video</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation>Ses</translation>
    </message>
    <message>
        <source>Drag files here from the Files panel, or double-click a file and press &quot;Add to timeline&quot;.</source>
        <translation>Dosyaları Dosyalar panelinden buraya sürükleyin ya da bir dosyaya çift tıklayıp &quot;Zaman çizelgesine ekle&quot;ye basın.</translation>
    </message>
    <message>
        <source>Move clip</source>
        <translation>Klibi taşı</translation>
    </message>
    <message>
        <source>Trim clip</source>
        <translation>Klibi kırp</translation>
    </message>
    <message>
        <source>Main</source>
        <translation>Ana</translation>
    </message>
    <message>
        <source>Title</source>
        <translation>Başlık</translation>
    </message>
    <message>
        <source>Lock track</source>
        <translation>İzi kilitle</translation>
    </message>
    <message>
        <source>Unlock track</source>
        <translation>İzin kilidini aç</translation>
    </message>
    <message>
        <source>Hide track</source>
        <translation>İzi gizle</translation>
    </message>
    <message>
        <source>Show track</source>
        <translation>İzi göster</translation>
    </message>
    <message>
        <source>Mute track</source>
        <translation>İzi sustur</translation>
    </message>
    <message>
        <source>Unmute track</source>
        <translation>İzin sesini aç</translation>
    </message>
    <message>
        <source>Move clip to the storyline</source>
        <translation>Klibi ana ize taşı</translation>
    </message>
    <message>
        <source>Add title here</source>
        <translation>Buraya başlık ekle</translation>
    </message>
    <message>
        <source>Add video track</source>
        <translation>Video izi ekle</translation>
    </message>
    <message>
        <source>Add audio track</source>
        <translation>Ses izi ekle</translation>
    </message>
    <message>
        <source>Remove track %1</source>
        <translation>%1 izini kaldır</translation>
    </message>
</context>
<context>
    <name>lumora::encoders</name>
    <message>
        <source>FFV1 (lossless)</source>
        <translation>FFV1 (kayıpsız)</translation>
    </message>
    <message>
        <source>x264 (software)</source>
        <translation>x264 (yazılım)</translation>
    </message>
    <message>
        <source>x265 (software)</source>
        <translation>x265 (yazılım)</translation>
    </message>
    <message>
        <source>SVT-AV1 (software)</source>
        <translation>SVT-AV1 (yazılım)</translation>
    </message>
    <message>
        <source>libaom (software, slow)</source>
        <translation>libaom (yazılım, yavaş)</translation>
    </message>
    <message>
        <source>libvpx (software)</source>
        <translation>libvpx (yazılım)</translation>
    </message>
    <message>
        <source>%1 (software)</source>
        <translation>%1 (yazılım)</translation>
    </message>
    <message>
        <source>%1 has no constant bit rate mode; a variable bit rate with the same average is used.</source>
        <translation>%1 sabit bit hızı modunu desteklemiyor; aynı ortalamaya sahip değişken bit hızı kullanılır.</translation>
    </message>
    <message>
        <source>YouTube 1080p</source>
        <translation>YouTube 1080p</translation>
    </message>
    <message>
        <source>YouTube 4K</source>
        <translation>YouTube 4K</translation>
    </message>
    <message>
        <source>Instagram Reels / Stories (9:16)</source>
        <translation>Instagram Reels / Hikâyeler (9:16)</translation>
    </message>
    <message>
        <source>Instagram feed (4:5)</source>
        <translation>Instagram akışı (4:5)</translation>
    </message>
    <message>
        <source>TikTok (9:16)</source>
        <translation>TikTok (9:16)</translation>
    </message>
    <message>
        <source>X (Twitter)</source>
        <translation>X (Twitter)</translation>
    </message>
    <message>
        <source>Archive (HEVC 10-bit, original size)</source>
        <translation>Arşiv (HEVC 10 bit, özgün boyut)</translation>
    </message>
    <message>
        <source>Apple ProRes 422 HQ (editing)</source>
        <translation>Apple ProRes 422 HQ (kurgu için)</translation>
    </message>
    <message>
        <source>Avid DNxHR HQ (editing)</source>
        <translation>Avid DNxHR HQ (kurgu için)</translation>
    </message>
</context>
</TS>
