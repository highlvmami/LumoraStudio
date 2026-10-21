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
    <message>
        <source>The timeline has titles, transitions, colour grading, effects, audio changes or more than one track. These have to be drawn into the picture, which a lossless cut cannot do. Choose &quot;Match source&quot;, &quot;Lossless intermediate&quot; or &quot;Custom&quot;.</source>
        <translation>Zaman çizelgesinde başlık, geçiş, renk düzenlemesi, efekt, ses değişikliği ya da birden fazla iz var. Bunların görüntüye işlenmesi gerekir; kayıpsız kesim bunu yapamaz. &quot;Kaynakla aynı&quot;, &quot;Kayıpsız ara format&quot; ya da &quot;Özel&quot; seçin.</translation>
    </message>
    <message>
        <source>The LUT %1 cannot be read: %2</source>
        <translation>%1 LUT dosyası okunamıyor: %2</translation>
    </message>
    <message>
        <source>The %1 container cannot hold subtitles, so they are saved as .srt next to the video.</source>
        <translation>%1 kabı altyazı taşıyamaz, bu yüzden altyazılar videonun yanına .srt olarak kaydedilir.</translation>
    </message>
    <message>
        <source>The timeline has freeze frames. A held frame has to be encoded as new video, which a lossless cut cannot do. Choose &quot;Match source&quot; (untouched frames are converted directly from the source), &quot;Lossless intermediate&quot; or &quot;Custom&quot;.</source>
        <translation>Zaman çizelgesinde dondurulmuş kare var. Bekletilen kare yeni görüntü olarak kodlanmalıdır; kayıpsız kesim bunu yapamaz. &quot;Kaynakla aynı&quot; (dokunulmayan kareler doğrudan kaynaktan dönüştürülür), &quot;Kayıpsız ara format&quot; ya da &quot;Özel&quot; seçin.</translation>
    </message>
</context>
<context>
    <name>Lut</name>
    <message>
        <source>Line %1: table data before LUT_3D_SIZE or LUT_1D_SIZE</source>
        <translation>Satır %1: tablo verisi LUT_3D_SIZE ya da LUT_1D_SIZE satırından önce geliyor</translation>
    </message>
    <message>
        <source>Line %1: expected three numbers</source>
        <translation>Satır %1: üç sayı bekleniyordu</translation>
    </message>
    <message>
        <source>Line %1: invalid number</source>
        <translation>Satır %1: geçersiz sayı</translation>
    </message>
    <message>
        <source>Line %1: more table entries than the declared size</source>
        <translation>Satır %1: tabloda belirtilen boyuttan fazla değer var</translation>
    </message>
    <message>
        <source>The file declares more than one table</source>
        <translation>Dosyada birden fazla tablo tanımlanmış</translation>
    </message>
    <message>
        <source>Line %1: unsupported table size</source>
        <translation>Satır %1: desteklenmeyen tablo boyutu</translation>
    </message>
    <message>
        <source>Line %1: expected two numbers</source>
        <translation>Satır %1: iki sayı bekleniyordu</translation>
    </message>
    <message>
        <source>No LUT_3D_SIZE or LUT_1D_SIZE found</source>
        <translation>LUT_3D_SIZE ya da LUT_1D_SIZE bulunamadı</translation>
    </message>
    <message>
        <source>The table has %1 entries, %2 expected</source>
        <translation>Tabloda %1 değer var, %2 bekleniyordu</translation>
    </message>
    <message>
        <source>The file is too large</source>
        <translation>Dosya çok büyük</translation>
    </message>
    <message>
        <source>Only 3D tables can be saved</source>
        <translation>Yalnızca 3B tablolar kaydedilebilir</translation>
    </message>
</context>
<context>
    <name>SubtitleText</name>
    <message>
        <source>No subtitles found. Only SubRip (.srt) and WebVTT (.vtt) files can be read.</source>
        <translation>Altyazı bulunamadı. Yalnızca SubRip (.srt) ve WebVTT (.vtt) dosyaları okunabilir.</translation>
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
    <message>
        <source>Loudness %1 LUFS, brought to %2 LUFS (%3 dB).</source>
        <translation>Ses yüksekliği %1 LUFS idi, %2 LUFS&apos;a getirildi (%3 dB).</translation>
    </message>
    <message>
        <source>Cannot add the subtitles: %1</source>
        <translation>Altyazılar eklenemiyor: %1</translation>
    </message>
    <message>
        <source>The subtitle file could not be written: %1</source>
        <translation>Altyazı dosyası yazılamadı: %1</translation>
    </message>
    <message>
        <source>Subtitles saved to %1</source>
        <translation>Altyazılar kaydedildi: %1</translation>
    </message>
</context>
<context>
    <name>lumora::AudioPanel</name>
    <message>
        <source>Clip sound</source>
        <translation>Klibin sesi</translation>
    </message>
    <message>
        <source>Volume (dB)</source>
        <translation>Ses düzeyi (dB)</translation>
    </message>
    <message>
        <source>Low cut (Hz)</source>
        <translation>Alçak kesme (Hz)</translation>
    </message>
    <message>
        <source>Removes rumble, wind and handling noise below this frequency; 0 = off. 80 Hz suits most voices.</source>
        <translation>Bu frekansın altındaki uğultuyu, rüzgârı ve tutma gürültüsünü siler; 0 = kapalı. Çoğu konuşmaya 80 Hz uyar.</translation>
    </message>
    <message>
        <source>Noise reduction (%)</source>
        <translation>Gürültü azaltma (%)</translation>
    </message>
    <message>
        <source>Lowers steady background noise (hum, hiss, fans, air conditioning) between and under the words. High values can sound watery.</source>
        <translation>Sözlerin arasındaki ve altındaki sürekli arka plan gürültüsünü (vızıltı, cızırtı, fan, klima) azaltır. Yüksek değerler sesi sulu gösterebilir.</translation>
    </message>
    <message>
        <source>Equaliser</source>
        <translation>Ekolayzer</translation>
    </message>
    <message>
        <source>Bass (dB)</source>
        <translation>Bas (dB)</translation>
    </message>
    <message>
        <source>Middle (dB)</source>
        <translation>Orta (dB)</translation>
    </message>
    <message>
        <source>Middle at (Hz)</source>
        <translation>Orta frekans (Hz)</translation>
    </message>
    <message>
        <source>Treble (dB)</source>
        <translation>Tiz (dB)</translation>
    </message>
    <message>
        <source>Compressor</source>
        <translation>Kompresör</translation>
    </message>
    <message>
        <source>Evens out loud and quiet parts, so a voice stays at one level</source>
        <translation>Yüksek ve alçak yerleri dengeler, böylece konuşma tek bir düzeyde kalır</translation>
    </message>
    <message>
        <source>Threshold (dBFS)</source>
        <translation>Eşik (dBFS)</translation>
    </message>
    <message>
        <source>Ratio</source>
        <translation>Oran</translation>
    </message>
    <message>
        <source>Make-up gain (dB)</source>
        <translation>Telafi kazancı (dB)</translation>
    </message>
    <message>
        <source>Ducking</source>
        <translation>Kısma (ducking)</translation>
    </message>
    <message>
        <source>Lower under other sound (dB)</source>
        <translation>Başka ses varken kıs (dB)</translation>
    </message>
    <message>
        <source>For music: it gets quieter by itself while other clips (a voice) play.</source>
        <translation>Müzik için: başka klipler (bir konuşma) çalarken kendiliğinden kısılır.</translation>
    </message>
    <message>
        <source>Master (whole mix)</source>
        <translation>Ana çıkış (tüm miks)</translation>
    </message>
    <message>
        <source>Normalise loudness on export</source>
        <translation>Dışa aktarırken ses yüksekliğini eşitle</translation>
    </message>
    <message>
        <source>Measures the finished mix (EBU R128) and brings it to the target loudness. The limiter is then always on, so nothing clips.</source>
        <translation>Bitmiş miksi ölçer (EBU R128) ve hedef ses yüksekliğine getirir. Bu durumda limitleyici hep açıktır, hiçbir şey patlamaz.</translation>
    </message>
    <message>
        <source>-14 LUFS: YouTube, Spotify, social media</source>
        <translation>-14 LUFS: YouTube, Spotify, sosyal medya</translation>
    </message>
    <message>
        <source>-16 LUFS: podcasts, Apple</source>
        <translation>-16 LUFS: podcast, Apple</translation>
    </message>
    <message>
        <source>-23 LUFS: television (EBU R128)</source>
        <translation>-23 LUFS: televizyon (EBU R128)</translation>
    </message>
    <message>
        <source>Limiter</source>
        <translation>Limitleyici</translation>
    </message>
    <message>
        <source>Stops peaks from going above the ceiling, without audible clipping</source>
        <translation>Tepelerin tavanı aşmasını duyulur bir patlama olmadan engeller</translation>
    </message>
    <message>
        <source>Ceiling (dBFS)</source>
        <translation>Tavan (dBFS)</translation>
    </message>
    <message>
        <source>Measure loudness</source>
        <translation>Ses yüksekliğini ölç</translation>
    </message>
    <message>
        <source>Sound changes never re-encode the picture: on export the video is still copied as it is.</source>
        <translation>Ses değişiklikleri görüntüyü asla yeniden kodlamaz: dışa aktarırken video yine olduğu gibi kopyalanır.</translation>
    </message>
    <message>
        <source>Volume</source>
        <translation>Ses düzeyi</translation>
    </message>
    <message>
        <source>Low cut</source>
        <translation>Alçak kesme</translation>
    </message>
    <message>
        <source>Noise reduction</source>
        <translation>Gürültü azaltma</translation>
    </message>
    <message>
        <source>Turn on compressor</source>
        <translation>Kompresörü aç</translation>
    </message>
    <message>
        <source>Turn off compressor</source>
        <translation>Kompresörü kapat</translation>
    </message>
    <message>
        <source>Loudness normalisation</source>
        <translation>Ses yüksekliği eşitleme</translation>
    </message>
    <message>
        <source>Target loudness</source>
        <translation>Hedef ses yüksekliği</translation>
    </message>
    <message>
        <source>Limiter ceiling</source>
        <translation>Limitleyici tavanı</translation>
    </message>
    <message>
        <source>Select a clip with sound in the timeline.</source>
        <translation>Zaman çizelgesinde sesi olan bir klip seçin.</translation>
    </message>
    <message>
        <source>This clip has no sound.</source>
        <translation>Bu klibin sesi yok.</translation>
    </message>
    <message>
        <source>%1 LUFS</source>
        <translation>%1 LUFS</translation>
    </message>
    <message>
        <source>Measuring… (click to stop)</source>
        <translation>Ölçülüyor… (durdurmak için tıklayın)</translation>
    </message>
    <message>
        <source>The mix is silent.</source>
        <translation>Miks sessiz.</translation>
    </message>
    <message>
        <source>Loudness %1 LUFS, peak %2 dBFS.</source>
        <translation>Ses yüksekliği %1 LUFS, tepe %2 dBFS.</translation>
    </message>
    <message>
        <source>Normalising to %1 LUFS changes it by %2 dB.</source>
        <translation>%1 LUFS&apos;a eşitlemek onu %2 dB değiştirir.</translation>
    </message>
</context>
<context>
    <name>lumora::ClipColorPanel</name>
    <message>
        <source>Titles have no colour settings; select a video clip.</source>
        <translation>Başlıkların renk ayarı yok; bir video klibi seçin.</translation>
    </message>
    <message>
        <source>Select a video clip in the timeline to grade it.</source>
        <translation>Rengini düzenlemek için zaman çizelgesinde bir video klibi seçin.</translation>
    </message>
</context>
<context>
    <name>lumora::ColorDisc</name>
    <message>
        <source>Drag towards a colour; double-click to reset. Hold Shift for fine control.</source>
        <translation>Bir renge doğru sürükleyin; sıfırlamak için çift tıklayın. İnce ayar için Shift&apos;e basılı tutun.</translation>
    </message>
</context>
<context>
    <name>lumora::ColorPanel</name>
    <message>
        <source>Reset colour</source>
        <translation>Rengi sıfırla</translation>
    </message>
    <message>
        <source>Reset colour: set every colour setting back to neutral</source>
        <translation>Rengi sıfırla: bütün renk ayarlarını nötre döndür</translation>
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
        <source>Highlights</source>
        <translation>Vurgular</translation>
    </message>
    <message>
        <source>Shadows</source>
        <translation>Gölgeler</translation>
    </message>
    <message>
        <source>Whites</source>
        <translation>Beyazlar</translation>
    </message>
    <message>
        <source>Blacks</source>
        <translation>Siyahlar</translation>
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
        <source>Colour</source>
        <translation>Renk</translation>
    </message>
    <message>
        <source>Saturation</source>
        <translation>Doygunluk</translation>
    </message>
    <message>
        <source>Vibrance</source>
        <translation>Canlılık</translation>
    </message>
    <message>
        <source>Like saturation, but leaves colours that are already strong (and skin tones) mostly alone</source>
        <translation>Doygunluk gibi, ama zaten güçlü olan renklere (ve ten renklerine) pek dokunmaz</translation>
    </message>
    <message>
        <source>Colour wheels</source>
        <translation>Renk tekerlekleri</translation>
    </message>
    <message>
        <source>Lift moves the shadows, gamma the mid tones, gain the highlights. Drag a wheel towards a colour; the slider below makes the range brighter or darker.</source>
        <translation>Lift gölgeleri, gamma orta tonları, gain parlak alanları değiştirir. Tekerleği bir renge doğru sürükleyin; alttaki kaydırıcı o aralığı aydınlatır ya da koyulaştırır.</translation>
    </message>
    <message>
        <source>Lift</source>
        <translation>Lift</translation>
    </message>
    <message>
        <source>Gamma</source>
        <translation>Gamma</translation>
    </message>
    <message>
        <source>Gain</source>
        <translation>Gain</translation>
    </message>
    <message>
        <source>LUT</source>
        <translation>LUT</translation>
    </message>
    <message>
        <source>A .cube look-up table (3D or 1D), applied after the settings above</source>
        <translation>Yukarıdaki ayarlardan sonra uygulanan bir .cube renk tablosu (3B ya da 1B)</translation>
    </message>
    <message>
        <source>Amount (%)</source>
        <translation>Miktar (%)</translation>
    </message>
    <message>
        <source>Save look as LUT…</source>
        <translation>Görünümü LUT olarak kaydet…</translation>
    </message>
    <message>
        <source>Write these colour settings as a .cube file, to use the same look on other clips, photos or in other programs</source>
        <translation>Bu renk ayarlarını .cube dosyası olarak yazar; aynı görünümü başka kliplerde, fotoğraflarda ya da başka programlarda kullanabilirsiniz</translation>
    </message>
    <message>
        <source>Exposure</source>
        <translation>Pozlama</translation>
    </message>
    <message>
        <source>LUT amount</source>
        <translation>LUT miktarı</translation>
    </message>
    <message>
        <source>Remove LUT</source>
        <translation>LUT&apos;u kaldır</translation>
    </message>
    <message>
        <source>Apply LUT</source>
        <translation>LUT uygula</translation>
    </message>
    <message>
        <source>No LUT</source>
        <translation>LUT yok</translation>
    </message>
    <message>
        <source>Browse…</source>
        <translation>Gözat…</translation>
    </message>
    <message>
        <source>Cannot read this LUT: %1</source>
        <translation>Bu LUT okunamıyor: %1</translation>
    </message>
    <message>
        <source>%1 · 3D, %2 points per axis</source>
        <translation>%1 · 3B, eksen başına %2 nokta</translation>
    </message>
    <message>
        <source>%1 · 1D, %2 points</source>
        <translation>%1 · 1B, %2 nokta</translation>
    </message>
    <message>
        <source>Choose a LUT</source>
        <translation>LUT seçin</translation>
    </message>
    <message>
        <source>Cube LUT (*.cube);;All files (*)</source>
        <translation>Cube LUT (*.cube);;Tüm dosyalar (*)</translation>
    </message>
    <message>
        <source>Cannot read %1:
%2</source>
        <translation>%1 okunamıyor:
%2</translation>
    </message>
    <message>
        <source>Save look as LUT</source>
        <translation>Görünümü LUT olarak kaydet</translation>
    </message>
    <message>
        <source>My look</source>
        <translation>Benim görünümüm</translation>
    </message>
    <message>
        <source>Cube LUT (*.cube)</source>
        <translation>Cube LUT (*.cube)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>%1 yazılamıyor:
%2</translation>
    </message>
    <message>
        <source>Curves</source>
        <translation>Eğriler</translation>
    </message>
    <message>
        <source>HSL mixer</source>
        <translation>HSL karıştırıcı</translation>
    </message>
    <message>
        <source>Change one colour range at a time: its hue, how strong it is and how bright</source>
        <translation>Her seferinde bir renk aralığını değiştirin: tonunu, ne kadar canlı ve ne kadar parlak olduğunu</translation>
    </message>
    <message>
        <source>Red</source>
        <translation>Kırmızı</translation>
    </message>
    <message>
        <source>Orange</source>
        <translation>Turuncu</translation>
    </message>
    <message>
        <source>Yellow</source>
        <translation>Sarı</translation>
    </message>
    <message>
        <source>Green</source>
        <translation>Yeşil</translation>
    </message>
    <message>
        <source>Aqua</source>
        <translation>Camgöbeği</translation>
    </message>
    <message>
        <source>Blue</source>
        <translation>Mavi</translation>
    </message>
    <message>
        <source>Purple</source>
        <translation>Mor</translation>
    </message>
    <message>
        <source>Magenta</source>
        <translation>Eflatun</translation>
    </message>
    <message>
        <source>Hue</source>
        <translation>Ton</translation>
    </message>
    <message>
        <source>Luminance</source>
        <translation>Parlaklık</translation>
    </message>
    <message>
        <source>Curve</source>
        <translation>Eğri</translation>
    </message>
    <message>
        <source>HSL</source>
        <translation>HSL</translation>
    </message>
</context>
<context>
    <name>lumora::ColorWheelWidget</name>
    <message>
        <source>Brightness of this range; double-click to reset</source>
        <translation>Bu aralığın parlaklığı; sıfırlamak için çift tıklayın</translation>
    </message>
    <message>
        <source>R %1 · G %2 · B %3 · brightness %4</source>
        <translation>K %1 · Y %2 · M %3 · parlaklık %4</translation>
    </message>
</context>
<context>
    <name>lumora::CurvesWidget</name>
    <message>
        <source>RGB</source>
        <translation>RGB</translation>
    </message>
    <message>
        <source>Red</source>
        <translation>Kırmızı</translation>
    </message>
    <message>
        <source>Green</source>
        <translation>Yeşil</translation>
    </message>
    <message>
        <source>Blue</source>
        <translation>Mavi</translation>
    </message>
    <message>
        <source>Master curve: brightness of all channels</source>
        <translation>Ana eğri: tüm kanalların parlaklığı</translation>
    </message>
    <message>
        <source>Red channel</source>
        <translation>Kırmızı kanal</translation>
    </message>
    <message>
        <source>Green channel</source>
        <translation>Yeşil kanal</translation>
    </message>
    <message>
        <source>Blue channel</source>
        <translation>Mavi kanal</translation>
    </message>
    <message>
        <source>Click to add a point and drag it. Double-click or right-click a point to remove it; double-click elsewhere to reset the curve.</source>
        <translation>Nokta eklemek için tıklayıp sürükleyin. Bir noktayı silmek için üzerine çift ya da sağ tıklayın; eğriyi sıfırlamak için boş yere çift tıklayın.</translation>
    </message>
</context>
<context>
    <name>lumora::DockTitleBar</name>
    <message>
        <source>Detach or attach the panel</source>
        <translation>Paneli ayır ya da yerine tak</translation>
    </message>
    <message>
        <source>Close the panel</source>
        <translation>Paneli kapat</translation>
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
    <message>
        <source>Embed as a subtitle track (can be turned on and off)</source>
        <translation>Altyazı izi olarak göm (açılıp kapatılabilir)</translation>
    </message>
    <message>
        <source>Burn into the picture (always visible, re-encodes)</source>
        <translation>Görüntüye yak (hep görünür, yeniden kodlanır)</translation>
    </message>
    <message>
        <source>Save as an .srt file next to the video</source>
        <translation>Videonun yanına .srt dosyası olarak kaydet</translation>
    </message>
    <message>
        <source>Leave out</source>
        <translation>Ekleme</translation>
    </message>
    <message>
        <source>Subtitles:</source>
        <translation>Altyazılar:</translation>
    </message>
    <message>
        <source>Audio: mixed with your sound settings and encoded with %1; the picture is not touched</source>
        <translation>Ses: ses ayarlarınızla karıştırılır ve %1 ile kodlanır; görüntüye dokunulmaz</translation>
    </message>
    <message>
        <source>Subtitles: drawn into the picture</source>
        <translation>Altyazılar: görüntünün içine çizilir</translation>
    </message>
    <message numerus="yes">
        <source>Subtitles: %n cue(s) as a track%1</source>
        <translation>
            <numerusform>Altyazılar: iz olarak %n satır%1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Subtitles: %n cue(s) in a separate .srt file</source>
        <translation>
            <numerusform>Altyazılar: ayrı bir .srt dosyasında %n satır</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>lumora::FullScreenViewer</name>
    <message>
        <source>Leave full screen (Esc)</source>
        <translation>Tam ekrandan çık (Esc)</translation>
    </message>
    <message>
        <source>Play / pause (Space)</source>
        <translation>Oynat / duraklat (Boşluk)</translation>
    </message>
    <message>
        <source>Previous frame (Left)</source>
        <translation>Önceki kare (Sol)</translation>
    </message>
    <message>
        <source>Next frame (Right)</source>
        <translation>Sonraki kare (Sağ)</translation>
    </message>
    <message>
        <source>Previous photo (Left)</source>
        <translation>Önceki fotoğraf (Sol)</translation>
    </message>
    <message>
        <source>Next photo (Right)</source>
        <translation>Sonraki fotoğraf (Sağ)</translation>
    </message>
</context>
<context>
    <name>lumora::LayersPanel</name>
    <message>
        <source>Normal</source>
        <translation>Normal</translation>
    </message>
    <message>
        <source>Multiply</source>
        <translation>Çarp</translation>
    </message>
    <message>
        <source>Screen</source>
        <translation>Ekran</translation>
    </message>
    <message>
        <source>Overlay</source>
        <translation>Kaplama</translation>
    </message>
    <message>
        <source>Soft light</source>
        <translation>Yumuşak ışık</translation>
    </message>
    <message>
        <source>Darken</source>
        <translation>Koyulaştır</translation>
    </message>
    <message>
        <source>Lighten</source>
        <translation>Aydınlat</translation>
    </message>
    <message>
        <source>Add</source>
        <translation>Ekle</translation>
    </message>
    <message>
        <source>Difference</source>
        <translation>Fark</translation>
    </message>
    <message>
        <source>Adjustment</source>
        <translation>Ayar</translation>
    </message>
    <message>
        <source>Add an adjustment layer: colour settings applied through a mask</source>
        <translation>Ayar katmanı ekle: bir maske üzerinden uygulanan renk ayarları</translation>
    </message>
    <message>
        <source>Move the layer up</source>
        <translation>Katmanı yukarı taşı</translation>
    </message>
    <message>
        <source>Move the layer down</source>
        <translation>Katmanı aşağı taşı</translation>
    </message>
    <message>
        <source>Delete the layer</source>
        <translation>Katmanı sil</translation>
    </message>
    <message>
        <source>Top layer first. The tick shows or hides a layer; the photo&apos;s own settings are below all layers.</source>
        <translation>En üstteki katman başta. İşaret katmanı gösterir ya da gizler; fotoğrafın kendi ayarları bütün katmanların altındadır.</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>Ad</translation>
    </message>
    <message>
        <source>Blend</source>
        <translation>Karışım</translation>
    </message>
    <message>
        <source>Opacity (%)</source>
        <translation>Opaklık (%)</translation>
    </message>
    <message>
        <source>Placement</source>
        <translation>Yerleşim</translation>
    </message>
    <message>
        <source>Horizontal (%)</source>
        <translation>Yatay (%)</translation>
    </message>
    <message>
        <source>Vertical (%)</source>
        <translation>Dikey (%)</translation>
    </message>
    <message>
        <source>Size (%)</source>
        <translation>Boyut (%)</translation>
    </message>
    <message>
        <source>Mask</source>
        <translation>Maske</translation>
    </message>
    <message>
        <source>Whole picture</source>
        <translation>Bütün resim</translation>
    </message>
    <message>
        <source>Rectangle</source>
        <translation>Dikdörtgen</translation>
    </message>
    <message>
        <source>Ellipse</source>
        <translation>Elips</translation>
    </message>
    <message>
        <source>Gradient</source>
        <translation>Degrade</translation>
    </message>
    <message>
        <source>Drag the outline on the photo to move the mask.</source>
        <translation>Maskeyi taşımak için fotoğraf üzerindeki çerçeveyi sürükleyin.</translation>
    </message>
    <message>
        <source>Centre X (%)</source>
        <translation>Merkez X (%)</translation>
    </message>
    <message>
        <source>Centre Y (%)</source>
        <translation>Merkez Y (%)</translation>
    </message>
    <message>
        <source>Width (%)</source>
        <translation>Genişlik (%)</translation>
    </message>
    <message>
        <source>Height (%)</source>
        <translation>Yükseklik (%)</translation>
    </message>
    <message>
        <source>Angle (°)</source>
        <translation>Açı (°)</translation>
    </message>
    <message>
        <source>Softness (%)</source>
        <translation>Yumuşaklık (%)</translation>
    </message>
    <message>
        <source>Invert</source>
        <translation>Ters çevir</translation>
    </message>
    <message>
        <source>Layer colour</source>
        <translation>Katman rengi</translation>
    </message>
    <message>
        <source>Show layer</source>
        <translation>Katmanı göster</translation>
    </message>
    <message>
        <source>Hide layer</source>
        <translation>Katmanı gizle</translation>
    </message>
    <message>
        <source>Rename layer</source>
        <translation>Katmanı yeniden adlandır</translation>
    </message>
    <message>
        <source>Blend mode</source>
        <translation>Karışım modu</translation>
    </message>
    <message>
        <source>Layer opacity</source>
        <translation>Katman opaklığı</translation>
    </message>
    <message>
        <source>Move picture</source>
        <translation>Resmi taşı</translation>
    </message>
    <message>
        <source>Picture size</source>
        <translation>Resim boyutu</translation>
    </message>
    <message>
        <source>Move mask</source>
        <translation>Maskeyi taşı</translation>
    </message>
    <message>
        <source>Mask size</source>
        <translation>Maske boyutu</translation>
    </message>
    <message>
        <source>Mask angle</source>
        <translation>Maske açısı</translation>
    </message>
    <message>
        <source>Mask softness</source>
        <translation>Maske yumuşaklığı</translation>
    </message>
    <message>
        <source>Mask shape</source>
        <translation>Maske şekli</translation>
    </message>
    <message>
        <source>Invert mask</source>
        <translation>Maskeyi ters çevir</translation>
    </message>
    <message>
        <source>picture</source>
        <translation>çıkartma</translation>
    </message>
    <message>
        <source>adjustment</source>
        <translation>ayar</translation>
    </message>
    <message>
        <source>, masked</source>
        <translation>, maskeli</translation>
    </message>
    <message>
        <source>Add adjustment layer</source>
        <translation>Ayar katmanı ekle</translation>
    </message>
    <message>
        <source>Delete layer</source>
        <translation>Katmanı sil</translation>
    </message>
    <message>
        <source>Move layer up</source>
        <translation>Katmanı yukarı taşı</translation>
    </message>
    <message>
        <source>Move layer down</source>
        <translation>Katmanı aşağı taşı</translation>
    </message>
    <message>
        <source>Brush</source>
        <translation>Fırça</translation>
    </message>
    <message>
        <source>Paint on the photo where the layer should show. Hold Alt to erase.</source>
        <translation>Katmanın görünmesini istediğiniz yerleri fotoğrafın üzerine boyayın. Silmek için Alt&apos;a basılı tutun.</translation>
    </message>
    <message>
        <source>Brush size (%)</source>
        <translation>Fırça boyutu (%)</translation>
    </message>
    <message>
        <source>Hardness (%)</source>
        <translation>Sertlik (%)</translation>
    </message>
    <message>
        <source>Flow (%)</source>
        <translation>Akış (%)</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Sil</translation>
    </message>
    <message>
        <source>Take away from the mask instead of adding (or hold Alt while painting)</source>
        <translation>Maskeye eklemek yerine maskeden sil (ya da boyarken Alt&apos;a basılı tutun)</translation>
    </message>
    <message>
        <source>Show the mask on the photo</source>
        <translation>Maskeyi fotoğrafın üzerinde göster</translation>
    </message>
    <message>
        <source>Clear brush strokes</source>
        <translation>Fırça darbelerini temizle</translation>
    </message>
    <message>
        <source>Sticker…</source>
        <translation>Çıkartma…</translation>
    </message>
    <message>
        <source>Put another picture, or a frame of a video, on top of the photo as a sticker</source>
        <translation>Fotoğrafın üstüne çıkartma olarak başka bir resim ya da bir videodan kare koy</translation>
    </message>
    <message>
        <source>Rotation (°)</source>
        <translation>Döndürme (°)</translation>
    </message>
    <message>
        <source>Video frame (s)</source>
        <translation>Video karesi (sn)</translation>
    </message>
    <message>
        <source>Drag the sticker on the photo to move it, a corner to resize it, the round knob to turn it.</source>
        <translation>Taşımak için çıkartmayı fotoğrafın üstünde sürükleyin; boyutlandırmak için bir köşesini, döndürmek için yuvarlak tutamağı sürükleyin.</translation>
    </message>
    <message>
        <source>Turn picture</source>
        <translation>Resmi döndür</translation>
    </message>
    <message>
        <source>Sticker frame</source>
        <translation>Çıkartma karesi</translation>
    </message>
    <message>
        <source>Add sticker</source>
        <translation>Çıkartma ekle</translation>
    </message>
    <message>
        <source>Pictures and videos (%1)</source>
        <translation>Resimler ve videolar (%1)</translation>
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
    <message>
        <source>Check for updates…</source>
        <translation>Güncellemeleri denetle…</translation>
    </message>
    <message>
        <source>&amp;Settings…</source>
        <translation>&amp;Ayarlar…</translation>
    </message>
    <message>
        <source>Colour grading: wheels, LUTs and scopes for the selected clip</source>
        <translation>Renk düzenleme: seçili klip için tekerlekler, LUT&apos;lar ve kapsamlar</translation>
    </message>
    <message>
        <source>Scopes</source>
        <translation>Kapsamlar</translation>
    </message>
    <message>
        <source>Sound: clean up, equalise and level clips, duck music, set the loudness of the mix</source>
        <translation>Ses: klipleri temizleyin, ekolayzerden geçirin ve dengeleyin, müziği kısın, miksin ses yüksekliğini ayarlayın</translation>
    </message>
    <message>
        <source>Add &amp;subtitle</source>
        <translation>&amp;Altyazı ekle</translation>
    </message>
    <message>
        <source>Add a subtitle at the playhead (Shift+T)</source>
        <translation>Oynatma çizgisine altyazı ekle (Shift+T)</translation>
    </message>
    <message>
        <source>Add subtitle track</source>
        <translation>Altyazı izi ekle</translation>
    </message>
    <message>
        <source>Import subtitles (SRT, VTT)…</source>
        <translation>Altyazı içe aktar (SRT, VTT)…</translation>
    </message>
    <message>
        <source>Export subtitles…</source>
        <translation>Altyazıları dışa aktar…</translation>
    </message>
    <message>
        <source>Subtitles</source>
        <translation>Altyazılar</translation>
    </message>
    <message>
        <source>Add subtitle</source>
        <translation>Altyazı ekle</translation>
    </message>
    <message>
        <source>Import subtitles</source>
        <translation>Altyazı içe aktar</translation>
    </message>
    <message>
        <source>Subtitles (*.srt *.vtt);;All files (*)</source>
        <translation>Altyazılar (*.srt *.vtt);;Tüm dosyalar (*)</translation>
    </message>
    <message>
        <source>%1 has no subtitles.</source>
        <translation>%1 içinde altyazı yok.</translation>
    </message>
    <message>
        <source>Cannot read %1:
%2</source>
        <translation>%1 okunamıyor:
%2</translation>
    </message>
    <message numerus="yes">
        <source>%n subtitle(s) imported.</source>
        <translation>
            <numerusform>%n altyazı içe aktarıldı.</numerusform>
        </translation>
    </message>
    <message>
        <source>Export subtitles</source>
        <translation>Altyazıları dışa aktar</translation>
    </message>
    <message>
        <source>There are no subtitles to export yet.</source>
        <translation>Henüz dışa aktarılacak altyazı yok.</translation>
    </message>
    <message>
        <source>SubRip (*.srt);;WebVTT (*.vtt)</source>
        <translation>SubRip (*.srt);;WebVTT (*.vtt)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>%1 yazılamıyor:
%2</translation>
    </message>
    <message>
        <source>Subtitles saved to %1</source>
        <translation>Altyazılar kaydedildi: %1</translation>
    </message>
    <message>
        <source>&amp;Freeze frame…</source>
        <translation>Kareyi &amp;dondur…</translation>
    </message>
    <message>
        <source>Hold the frame under the playhead for a number of seconds; the rest of the clip continues after it</source>
        <translation>Oynatma imlecinin altındaki kareyi seçtiğiniz saniye kadar bekletir; klibin geri kalanı ardından devam eder</translation>
    </message>
    <message>
        <source>Move the playhead over a video clip to freeze its frame.</source>
        <translation>Karesini dondurmak için oynatma imlecini bir video klibin üzerine getirin.</translation>
    </message>
    <message>
        <source>Freeze frame</source>
        <translation>Kareyi dondur</translation>
    </message>
    <message>
        <source>Hold the frame for (seconds):</source>
        <translation>Kare kaç saniye kalsın:</translation>
    </message>
    <message>
        <source>&amp;Automatic subtitles from speech…</source>
        <translation>Konuşmadan &amp;otomatik altyazı…</translation>
    </message>
    <message>
        <source>Writes subtitles from what is said, on this computer (Whisper)</source>
        <translation>Söylenenleri bu bilgisayarda altyazıya döker (Whisper)</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>İptal</translation>
    </message>
    <message>
        <source>Automatic subtitles</source>
        <translation>Otomatik altyazı</translation>
    </message>
    <message>
        <source>This build of LumoraStudio has no speech recognition.</source>
        <translation>LumoraStudio&apos;nun bu sürümünde konuşma tanıma yok.</translation>
    </message>
    <message>
        <source>LumoraStudio listens to the timeline and writes what is said as a new subtitle track. Recognition runs on this computer; nothing is sent anywhere. The model is downloaded once.</source>
        <translation>LumoraStudio zaman çizelgesini dinler ve söylenenleri yeni bir altyazı izine yazar. Tanıma bu bilgisayarda yapılır, hiçbir şey dışarı gönderilmez. Model yalnızca bir kez indirilir.</translation>
    </message>
    <message>
        <source>Turkish</source>
        <translation>Türkçe</translation>
    </message>
    <message>
        <source>English</source>
        <translation>İngilizce</translation>
    </message>
    <message>
        <source>German</source>
        <translation>Almanca</translation>
    </message>
    <message>
        <source>French</source>
        <translation>Fransızca</translation>
    </message>
    <message>
        <source>Spanish</source>
        <translation>İspanyolca</translation>
    </message>
    <message>
        <source>Arabic</source>
        <translation>Arapça</translation>
    </message>
    <message>
        <source>Russian</source>
        <translation>Rusça</translation>
    </message>
    <message>
        <source>Detect automatically</source>
        <translation>Kendisi bulsun</translation>
    </message>
    <message>
        <source>Spoken language</source>
        <translation>Konuşulan dil</translation>
    </message>
    <message>
        <source>%1 · downloaded</source>
        <translation>%1 · indirildi</translation>
    </message>
    <message>
        <source>%1 · %2 MB download</source>
        <translation>%1 · %2 MB indirilecek</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>Model</translation>
    </message>
    <message>
        <source>Write subtitles</source>
        <translation>Altyazıyı yaz</translation>
    </message>
    <message>
        <source>Downloading the speech recognition model…</source>
        <translation>Konuşma tanıma modeli indiriliyor…</translation>
    </message>
    <message>
        <source>The model could not be downloaded: %1</source>
        <translation>Model indirilemedi: %1</translation>
    </message>
    <message>
        <source>Listening and writing subtitles…</source>
        <translation>Dinleniyor ve altyazı yazılıyor…</translation>
    </message>
    <message>
        <source>No speech was heard on the timeline.</source>
        <translation>Zaman çizelgesinde konuşma duyulmadı.</translation>
    </message>
    <message numerus="yes">
        <source>%n subtitle(s) written. Check them: select a cue to correct its text in Properties.</source>
        <translation>
            <numerusform>%n altyazı yazıldı. Kontrol edin: düzeltmek için bir satırı seçip metnini Özellikler panelinde değiştirin.</numerusform>
        </translation>
    </message>
    <message>
        <source>Measuring the camera movement…</source>
        <translation>Kamera hareketi ölçülüyor…</translation>
    </message>
    <message>
        <source>Stabilise</source>
        <translation>Sabitle</translation>
    </message>
    <message>
        <source>The camera movement could not be measured in this clip.</source>
        <translation>Bu klipte kamera hareketi ölçülemedi.</translation>
    </message>
    <message>
        <source>Following the object…</source>
        <translation>Nesne takip ediliyor…</translation>
    </message>
    <message>
        <source>Follow an object</source>
        <translation>Nesneyi takip et</translation>
    </message>
    <message>
        <source>The clip now follows the object. Play it back to check.</source>
        <translation>Klip artık nesneyi takip ediyor. Kontrol etmek için oynatın.</translation>
    </message>
    <message>
        <source>Draw a box around the object to follow (Esc cancels)</source>
        <translation>Takip edilecek nesnenin çevresine bir kutu çizin (Esc: vazgeç)</translation>
    </message>
    <message>
        <source>Hide faces and plates</source>
        <translation>Yüzleri ve plakaları gizle</translation>
    </message>
    <message>
        <source>Looking for faces and number plates…</source>
        <translation>Yüzler ve plakalar aranıyor…</translation>
    </message>
    <message>
        <source>No faces or number plates were found.</source>
        <translation>Yüz ya da plaka bulunamadı.</translation>
    </message>
    <message>
        <source>Found %1 face(s) and %2 number plate(s). Play the clip back to check.</source>
        <translation>%1 yüz ve %2 plaka bulundu. Kontrol etmek için klibi oynatın.</translation>
    </message>
    <message>
        <source>Remove the background</source>
        <translation>Arka planı kaldır</translation>
    </message>
    <message>
        <source>Full-screen &amp;preview</source>
        <translation>Tam ekran ö&amp;nizleme</translation>
    </message>
    <message>
        <source>Show the video or photo on the whole screen (F, Esc leaves)</source>
        <translation>Videoyu ya da fotoğrafı tüm ekranda göster (F, Esc ile çık)</translation>
    </message>
    <message>
        <source>Full-screen &amp;window</source>
        <translation>Tam ekran &amp;pencere</translation>
    </message>
    <message>
        <source>Fill the whole screen with LumoraStudio (F11)</source>
        <translation>LumoraStudio tüm ekranı kaplasın (F11)</translation>
    </message>
    <message>
        <source>Leave full screen (F11)</source>
        <translation>Tam ekrandan çık (F11)</translation>
    </message>
    <message>
        <source>Move picture</source>
        <translation>Resmi taşı</translation>
    </message>
    <message>
        <source>Add stic&amp;ker…</source>
        <translation>Çı&amp;kartma ekle…</translation>
    </message>
    <message>
        <source>Put a picture, animated GIF or video on top of the video at the playhead (Shift+K)</source>
        <translation>Oynatma imlecindeki videonun üstüne resim, hareketli GIF ya da video koy (Shift+K)</translation>
    </message>
    <message>
        <source>Pictures go on a video track.</source>
        <translation>Resimler bir video izine konur.</translation>
    </message>
    <message>
        <source>Add picture</source>
        <translation>Resim ekle</translation>
    </message>
    <message>
        <source>Add sticker</source>
        <translation>Çıkartma ekle</translation>
    </message>
    <message>
        <source>Pictures and videos (%1);;All files (*)</source>
        <translation>Resimler ve videolar (%1);;Tüm dosyalar (*)</translation>
    </message>
    <message>
        <source>The file could not be opened:

%1</source>
        <translation>Dosya açılamadı:

%1</translation>
    </message>
    <message>
        <source>This file has no picture to show as a sticker.</source>
        <translation>Bu dosyada çıkartma olarak gösterilecek bir görüntü yok.</translation>
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
    <message>
        <source>Full screen (F or double-click the picture)</source>
        <translation>Tam ekran (F ya da görüntüye çift tıklayın)</translation>
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
    <name>lumora::PhotoView</name>
    <message>
        <source>inverted</source>
        <translation>ters</translation>
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
    <message>
        <source>Adjust</source>
        <translation>Ayarla</translation>
    </message>
    <message>
        <source>Layers</source>
        <translation>Katmanlar</translation>
    </message>
    <message>
        <source>Move mask</source>
        <translation>Maskeyi taşı</translation>
    </message>
    <message>
        <source>Erase from mask</source>
        <translation>Maskeden sil</translation>
    </message>
    <message>
        <source>Paint mask</source>
        <translation>Maske boya</translation>
    </message>
    <message>
        <source>Smart</source>
        <translation>Akıllı</translation>
    </message>
    <message>
        <source>Remove the background</source>
        <translation>Arka planı kaldır</translation>
    </message>
    <message>
        <source>Hide faces and plates</source>
        <translation>Yüzleri ve plakaları gizle</translation>
    </message>
    <message>
        <source>Looking for faces and number plates…</source>
        <translation>Yüzler ve plakalar aranıyor…</translation>
    </message>
    <message>
        <source>No faces or number plates were found.</source>
        <translation>Yüz ya da plaka bulunamadı.</translation>
    </message>
    <message>
        <source>Found %1 face(s) and %2 number plate(s).</source>
        <translation>%1 yüz ve %2 plaka bulundu.</translation>
    </message>
    <message>
        <source>Full screen (F or double-click the photo)</source>
        <translation>Tam ekran (F ya da fotoğrafa çift tıklayın)</translation>
    </message>
    <message>
        <source>Remove background</source>
        <translation>Arka planı sil</translation>
    </message>
    <message>
        <source>Cut the person or object out and make the rest transparent, in one click (B)</source>
        <translation>Kişiyi ya da nesneyi tek tıkla ayırır, gerisini saydam yapar (B)</translation>
    </message>
    <message>
        <source>Background removed</source>
        <translation>Arka plan silindi</translation>
    </message>
    <message>
        <source>Keep the background</source>
        <translation>Arka planı koru</translation>
    </message>
    <message>
        <source>Background removed. Export as PNG to keep the transparency; click again to bring it back.</source>
        <translation>Arka plan silindi. Saydamlığı korumak için PNG olarak dışa aktarın; geri getirmek için yeniden tıklayın.</translation>
    </message>
    <message>
        <source>Bring the background back</source>
        <translation>Arka planı geri getir</translation>
    </message>
    <message>
        <source>Move sticker</source>
        <translation>Çıkartmayı taşı</translation>
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
    <message>
        <source>Subtitle</source>
        <translation>Altyazı</translation>
    </message>
    <message>
        <source>Type what is said; two lines at most read best</source>
        <translation>Söyleneni yazın; en fazla iki satır en rahat okunur</translation>
    </message>
    <message>
        <source>Subtitle track</source>
        <translation>Altyazı izi</translation>
    </message>
    <message>
        <source>e.g. tur, eng</source>
        <translation>ör. tur, eng</translation>
    </message>
    <message>
        <source>Language of the track (ISO 639-2 code or a name); players show it in their subtitle menu</source>
        <translation>İzin dili (ISO 639-2 kodu ya da adı); oynatıcılar bunu altyazı menüsünde gösterir</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>Dil</translation>
    </message>
    <message>
        <source>A dark box behind the text</source>
        <translation>Metnin arkasında koyu bir kutu</translation>
    </message>
    <message>
        <source>Where the bottom line sits, from the top of the frame</source>
        <translation>Alt satırın karenin üstünden itibaren yeri</translation>
    </message>
    <message>
        <source>Height in frame</source>
        <translation>Karedeki yükseklik</translation>
    </message>
    <message>
        <source>The style is used when subtitles are burnt into the picture and in the preview. Embedded subtitles are styled by the player.</source>
        <translation>Bu stil, altyazılar görüntüye yakıldığında ve önizlemede kullanılır. Gömülü altyazıların görünüşünü oynatıcı belirler.</translation>
    </message>
    <message>
        <source>Edit subtitle</source>
        <translation>Altyazıyı düzenle</translation>
    </message>
    <message>
        <source>Change subtitle length</source>
        <translation>Altyazı süresini değiştir</translation>
    </message>
    <message>
        <source>Subtitle language</source>
        <translation>Altyazı dili</translation>
    </message>
    <message>
        <source>Subtitle style</source>
        <translation>Altyazı stili</translation>
    </message>
    <message>
        <source>Freeze frame</source>
        <translation>Dondurulmuş kare</translation>
    </message>
    <message>
        <source>Hold for</source>
        <translation>Süre</translation>
    </message>
    <message>
        <source>Change freeze length</source>
        <translation>Dondurma süresini değiştir</translation>
    </message>
    <message>
        <source>Shows the frame at %1 of the source.</source>
        <translation>Kaynağın %1 anındaki kareyi gösterir.</translation>
    </message>
    <message>
        <source>Stabilisation</source>
        <translation>Sabitleme</translation>
    </message>
    <message>
        <source>Steady the shaky camera</source>
        <translation>Titreyen kamerayı sabitle</translation>
    </message>
    <message>
        <source>How much camera movement is smoothed away; larger values also remove slow wobble</source>
        <translation>Kamera hareketinin ne kadarının yumuşatılacağı; büyük değerler yavaş salınımı da giderir</translation>
    </message>
    <message>
        <source>Smoothing</source>
        <translation>Yumuşatma</translation>
    </message>
    <message>
        <source>Enlarges the picture so its moving edges stay outside the frame</source>
        <translation>Görüntüyü büyütür, böylece oynayan kenarları kadrajın dışında kalır</translation>
    </message>
    <message>
        <source>Motion tracking</source>
        <translation>Hareket takibi</translation>
    </message>
    <message>
        <source>Follow an object…</source>
        <translation>Bir nesneyi takip et…</translation>
    </message>
    <message>
        <source>Draw a box around something in the picture; this clip then moves with it</source>
        <translation>Görüntüdeki bir şeyin çevresine kutu çizin; bu klip onunla birlikte hareket eder</translation>
    </message>
    <message>
        <source>Stop following</source>
        <translation>Takibi kaldır</translation>
    </message>
    <message>
        <source>Stabilise</source>
        <translation>Sabitle</translation>
    </message>
    <message>
        <source>Turn off stabilisation</source>
        <translation>Sabitlemeyi kapat</translation>
    </message>
    <message>
        <source>Change smoothing</source>
        <translation>Yumuşatmayı değiştir</translation>
    </message>
    <message>
        <source>Change stabilisation zoom</source>
        <translation>Sabitleme yakınlaştırmasını değiştir</translation>
    </message>
    <message>
        <source>The camera movement is measured once when you turn it on.</source>
        <translation>Açtığınızda kamera hareketi bir kez ölçülür.</translation>
    </message>
    <message>
        <source>Make this clip move with something in the picture below it, e.g. a name above a person.</source>
        <translation>Bu klibi alttaki görüntüde bir şeyle birlikte hareket ettirin, örneğin bir kişinin üstündeki ismi.</translation>
    </message>
    <message>
        <source>This clip follows a tracked object (%1 positions).</source>
        <translation>Bu klip takip edilen bir nesneyi izliyor (%1 konum).</translation>
    </message>
    <message>
        <source>Hide faces and plates</source>
        <translation>Yüzleri ve plakaları gizle</translation>
    </message>
    <message>
        <source>Find faces and plates</source>
        <translation>Yüzleri ve plakaları bul</translation>
    </message>
    <message>
        <source>Looks through the clip on this computer and follows every face and number plate</source>
        <translation>Klibi bu bilgisayarda tarar, her yüzü ve plakayı hareket ettikçe takip eder</translation>
    </message>
    <message>
        <source>Remove</source>
        <translation>Kaldır</translation>
    </message>
    <message>
        <source>Faces</source>
        <translation>Yüzler</translation>
    </message>
    <message>
        <source>Number plates</source>
        <translation>Plakalar</translation>
    </message>
    <message>
        <source>Hide</source>
        <translation>Gizle</translation>
    </message>
    <message>
        <source>Pixelate</source>
        <translation>Pikselleştir</translation>
    </message>
    <message>
        <source>Black box</source>
        <translation>Siyah kutu</translation>
    </message>
    <message>
        <source>Look</source>
        <translation>Görünüm</translation>
    </message>
    <message>
        <source>Strength</source>
        <translation>Güç</translation>
    </message>
    <message>
        <source>Untick what should stay visible</source>
        <translation>Görünür kalması gerekenlerin işaretini kaldırın</translation>
    </message>
    <message>
        <source>Remove the background</source>
        <translation>Arka planı kaldır</translation>
    </message>
    <message>
        <source>Keeps the person or object in front and makes the rest transparent</source>
        <translation>Öndeki kişiyi ya da nesneyi bırakır, gerisini saydam yapar</translation>
    </message>
    <message>
        <source>Fast</source>
        <translation>Hızlı</translation>
    </message>
    <message>
        <source>Detailed (slow)</source>
        <translation>Ayrıntılı (yavaş)</translation>
    </message>
    <message>
        <source>The detailed model finds hair and fine edges better; the preview uses the fast one</source>
        <translation>Ayrıntılı model saçı ve ince kenarları daha iyi bulur; önizlemede hızlı model kullanılır</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>Model</translation>
    </message>
    <message>
        <source>Edge softness</source>
        <translation>Kenar yumuşaklığı</translation>
    </message>
    <message>
        <source>Remove hidden regions</source>
        <translation>Gizlenen bölgeleri kaldır</translation>
    </message>
    <message>
        <source>Change how regions are hidden</source>
        <translation>Gizleme biçimini değiştir</translation>
    </message>
    <message>
        <source>Change hiding strength</source>
        <translation>Gizleme gücünü değiştir</translation>
    </message>
    <message>
        <source>Hide region</source>
        <translation>Bölgeyi gizle</translation>
    </message>
    <message>
        <source>Show region</source>
        <translation>Bölgeyi göster</translation>
    </message>
    <message>
        <source>Keep the background</source>
        <translation>Arka planı koru</translation>
    </message>
    <message>
        <source>Change background model</source>
        <translation>Arka plan modelini değiştir</translation>
    </message>
    <message>
        <source>Change edge softness</source>
        <translation>Kenar yumuşaklığını değiştir</translation>
    </message>
    <message>
        <source>Blurs every face and number plate in the clip and follows them as they move. Runs on this computer.</source>
        <translation>Klipteki her yüzü ve plakayı bulanıklaştırır, hareket ettikçe takip eder. Bu bilgisayarda çalışır.</translation>
    </message>
    <message>
        <source>No faces or number plates were found in this clip.</source>
        <translation>Bu klipte yüz ya da plaka bulunamadı.</translation>
    </message>
    <message>
        <source>Found %1 face(s) and %2 number plate(s).</source>
        <translation>%1 yüz ve %2 plaka bulundu.</translation>
    </message>
    <message>
        <source>The clip was lengthened since: search again to cover the new part.</source>
        <translation>Klip o zamandan beri uzatıldı: yeni kısmı da kapsamak için yeniden arayın.</translation>
    </message>
    <message>
        <source>Search again</source>
        <translation>Yeniden ara</translation>
    </message>
    <message>
        <source>Face %1</source>
        <translation>Yüz %1</translation>
    </message>
    <message>
        <source>Plate %1</source>
        <translation>Plaka %1</translation>
    </message>
    <message>
        <source>The model is not on this computer yet: turn this off and on again to download it.</source>
        <translation>Model henüz bu bilgisayarda yok: indirmek için kapatıp yeniden açın.</translation>
    </message>
    <message>
        <source>On the main track the background turns black. Put the clip on a track above another clip to show that clip behind it.</source>
        <translation>Ana izde arka plan siyah olur. Arkada başka bir klibin görünmesi için klibi onun üstündeki bir ize koyun.</translation>
    </message>
    <message>
        <source>Cuts the person or object out of every frame. Playback gets slower.</source>
        <translation>Kişiyi ya da nesneyi her karede arka plandan ayırır. Oynatma yavaşlar.</translation>
    </message>
    <message>
        <source>Picture</source>
        <translation>Resim</translation>
    </message>
    <message>
        <source>Repeat</source>
        <translation>Tekrarla</translation>
    </message>
    <message>
        <source>Play over and over</source>
        <translation>Durmadan tekrar oynat</translation>
    </message>
    <message>
        <source>Repeat the clip, e.g. an animated sticker; drag its end on the timeline to make it longer</source>
        <translation>Klibi tekrarla (ör. hareketli bir çıkartma); uzatmak için zaman çizelgesinde sonunu sürükleyin</translation>
    </message>
    <message>
        <source>Drag the end of the clip on the timeline to make it as long as you like. The sound is not repeated.</source>
        <translation>Klibi istediğiniz kadar uzatmak için zaman çizelgesinde sonunu sürükleyin. Ses tekrarlanmaz.</translation>
    </message>
    <message>
        <source>Change picture length</source>
        <translation>Resim süresini değiştir</translation>
    </message>
    <message>
        <source>Repeat clip</source>
        <translation>Klibi tekrarla</translation>
    </message>
    <message>
        <source>Stop repeating clip</source>
        <translation>Klibi tekrarlamayı bırak</translation>
    </message>
</context>
<context>
    <name>lumora::ScopesWidget</name>
    <message>
        <source>Waveform</source>
        <translation>Dalga formu</translation>
    </message>
    <message>
        <source>RGB parade</source>
        <translation>RGB parade</translation>
    </message>
    <message>
        <source>Vectorscope</source>
        <translation>Vektörskop</translation>
    </message>
    <message>
        <source>Histogram</source>
        <translation>Histogram</translation>
    </message>
</context>
<context>
    <name>lumora::SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>Ayarlar</translation>
    </message>
    <message>
        <source>Updates</source>
        <translation>Güncellemeler</translation>
    </message>
    <message>
        <source>Installed version: %1</source>
        <translation>Yüklü sürüm: %1</translation>
    </message>
    <message>
        <source>Check for updates</source>
        <translation>Güncellemeleri denetle</translation>
    </message>
    <message>
        <source>Check automatically once a day</source>
        <translation>Günde bir kez otomatik denetle</translation>
    </message>
    <message>
        <source>Last checked: %1</source>
        <translation>Son denetim: %1</translation>
    </message>
    <message>
        <source>Not checked yet</source>
        <translation>Henüz denetlenmedi</translation>
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
    <name>lumora::SmartToolsPanel</name>
    <message>
        <source>Hide faces and plates</source>
        <translation>Yüzleri ve plakaları gizle</translation>
    </message>
    <message>
        <source>Find faces and plates</source>
        <translation>Yüzleri ve plakaları bul</translation>
    </message>
    <message>
        <source>Remove</source>
        <translation>Kaldır</translation>
    </message>
    <message>
        <source>Faces</source>
        <translation>Yüzler</translation>
    </message>
    <message>
        <source>Number plates</source>
        <translation>Plakalar</translation>
    </message>
    <message>
        <source>Hide</source>
        <translation>Gizle</translation>
    </message>
    <message>
        <source>Blur</source>
        <translation>Bulanıklaştır</translation>
    </message>
    <message>
        <source>Pixelate</source>
        <translation>Pikselleştir</translation>
    </message>
    <message>
        <source>Black box</source>
        <translation>Siyah kutu</translation>
    </message>
    <message>
        <source>Look</source>
        <translation>Görünüm</translation>
    </message>
    <message>
        <source>Strength</source>
        <translation>Güç</translation>
    </message>
    <message>
        <source>Untick what should stay visible</source>
        <translation>Görünür kalması gerekenlerin işaretini kaldırın</translation>
    </message>
    <message>
        <source>Background</source>
        <translation>Arka plan</translation>
    </message>
    <message>
        <source>Remove the background</source>
        <translation>Arka planı kaldır</translation>
    </message>
    <message>
        <source>Keeps the person or object in front and makes the rest transparent</source>
        <translation>Öndeki kişiyi ya da nesneyi bırakır, gerisini saydam yapar</translation>
    </message>
    <message>
        <source>Fast</source>
        <translation>Hızlı</translation>
    </message>
    <message>
        <source>Detailed (slow)</source>
        <translation>Ayrıntılı (yavaş)</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>Model</translation>
    </message>
    <message>
        <source>Edge softness</source>
        <translation>Kenar yumuşaklığı</translation>
    </message>
    <message>
        <source>Export as PNG, TIFF or WebP to keep the transparency; JPEG puts the subject on white.</source>
        <translation>Saydamlığı korumak için PNG, TIFF ya da WebP olarak dışa aktarın; JPEG&apos;de konu beyaz zemine konur.</translation>
    </message>
    <message>
        <source>Remove hidden regions</source>
        <translation>Gizlenen bölgeleri kaldır</translation>
    </message>
    <message>
        <source>Change how regions are hidden</source>
        <translation>Gizleme biçimini değiştir</translation>
    </message>
    <message>
        <source>Change hiding strength</source>
        <translation>Gizleme gücünü değiştir</translation>
    </message>
    <message>
        <source>Hide region</source>
        <translation>Bölgeyi gizle</translation>
    </message>
    <message>
        <source>Show region</source>
        <translation>Bölgeyi göster</translation>
    </message>
    <message>
        <source>Keep the background</source>
        <translation>Arka planı koru</translation>
    </message>
    <message>
        <source>Change background model</source>
        <translation>Arka plan modelini değiştir</translation>
    </message>
    <message>
        <source>Change edge softness</source>
        <translation>Kenar yumuşaklığını değiştir</translation>
    </message>
    <message>
        <source>Blurs every face and number plate in the photo. Runs on this computer.</source>
        <translation>Fotoğraftaki her yüzü ve plakayı bulanıklaştırır. Bu bilgisayarda çalışır.</translation>
    </message>
    <message>
        <source>Found %1 face(s) and %2 number plate(s).</source>
        <translation>%1 yüz ve %2 plaka bulundu.</translation>
    </message>
    <message>
        <source>No faces or number plates were found in this photo.</source>
        <translation>Bu fotoğrafta yüz ya da plaka bulunamadı.</translation>
    </message>
    <message>
        <source>Search again</source>
        <translation>Yeniden ara</translation>
    </message>
    <message>
        <source>Plate %1</source>
        <translation>Plaka %1</translation>
    </message>
    <message>
        <source>Face %1</source>
        <translation>Yüz %1</translation>
    </message>
</context>
<context>
    <name>lumora::Tasks</name>
    <message>
        <source>This build of LumoraStudio has no AI tools.</source>
        <translation>LumoraStudio&apos;nun bu sürümünde yapay zeka araçları yok.</translation>
    </message>
    <message>
        <source>Downloading the %1 model (%2 MB, once)…</source>
        <translation>%1 modeli indiriliyor (%2 MB, bir kez)…</translation>
    </message>
    <message>
        <source>The %1 model could not be downloaded: %2</source>
        <translation>%1 modeli indirilemedi: %2</translation>
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
    <message>
        <source>Subtitles</source>
        <translation>Altyazılar</translation>
    </message>
    <message>
        <source>(empty subtitle)</source>
        <translation>(boş altyazı)</translation>
    </message>
    <message>
        <source>Add subtitle here</source>
        <translation>Buraya altyazı ekle</translation>
    </message>
    <message>
        <source>Add subtitle track</source>
        <translation>Altyazı izi ekle</translation>
    </message>
    <message>
        <source>Freeze frame</source>
        <translation>Dondurulmuş kare</translation>
    </message>
</context>
<context>
    <name>lumora::Updater</name>
    <message>
        <source>Check for updates</source>
        <translation>Güncellemeleri denetle</translation>
    </message>
    <message>
        <source>Could not reach GitHub: %1</source>
        <translation>GitHub&apos;a ulaşılamadı: %1</translation>
    </message>
    <message>
        <source>LumoraStudio %1 is the latest version.</source>
        <translation>LumoraStudio %1 en güncel sürüm.</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Güncelleme var</translation>
    </message>
    <message>
        <source>LumoraStudio %1 is available. You have %2.</source>
        <translation>LumoraStudio %1 çıktı. Sizdeki sürüm: %2.</translation>
    </message>
    <message>
        <source>Install it now? LumoraStudio will close, update itself and start again. You will be asked to save your project first.</source>
        <translation>Şimdi yüklensin mi? LumoraStudio kapanacak, kendini güncelleyip yeniden açılacak. Önce projenizi kaydetmeniz istenecek.</translation>
    </message>
    <message>
        <source>Open the download page?</source>
        <translation>İndirme sayfası açılsın mı?</translation>
    </message>
    <message>
        <source>Update now</source>
        <translation>Şimdi güncelle</translation>
    </message>
    <message>
        <source>Open download page</source>
        <translation>İndirme sayfasını aç</translation>
    </message>
    <message>
        <source>Later</source>
        <translation>Sonra</translation>
    </message>
    <message>
        <source>Don&apos;t check automatically</source>
        <translation>Otomatik denetleme</translation>
    </message>
    <message>
        <source>Update</source>
        <translation>Güncelleme</translation>
    </message>
    <message>
        <source>Cannot write %1.</source>
        <translation>%1 yazılamıyor.</translation>
    </message>
    <message>
        <source>Downloading LumoraStudio %1…</source>
        <translation>LumoraStudio %1 indiriliyor…</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>İptal</translation>
    </message>
    <message>
        <source>The download failed: %1</source>
        <translation>İndirme başarısız: %1</translation>
    </message>
    <message>
        <source>The downloaded file is damaged (its checksum does not match). Nothing was changed; please try again later.</source>
        <translation>İndirilen dosya bozuk (sağlama toplamı tutmuyor). Hiçbir şey değiştirilmedi; lütfen daha sonra yeniden deneyin.</translation>
    </message>
    <message>
        <source>The downloaded package could not be unpacked.</source>
        <translation>İndirilen paket açılamadı.</translation>
    </message>
    <message>
        <source>LumoraStudio could not be updated, so the current version was kept. Close any other copy of LumoraStudio and try again.</source>
        <translation>LumoraStudio güncellenemedi, mevcut sürüm korundu. Açık başka bir LumoraStudio penceresi varsa kapatıp yeniden deneyin.</translation>
    </message>
    <message>
        <source>Preparing the update…</source>
        <translation>Güncelleme hazırlanıyor…</translation>
    </message>
</context>
<context>
    <name>lumora::ai</name>
    <message>
        <source>This build of LumoraStudio has no AI tools.</source>
        <translation>LumoraStudio&apos;nun bu sürümünde yapay zeka araçları yok.</translation>
    </message>
    <message>
        <source>The %1 model has not been downloaded yet.</source>
        <translation>%1 modeli henüz indirilmedi.</translation>
    </message>
    <message>
        <source>The %1 model cannot be read (%2). Download it again.</source>
        <translation>%1 modeli okunamıyor (%2). Yeniden indirin.</translation>
    </message>
    <message>
        <source>face detection</source>
        <translation>Yüz algılama</translation>
    </message>
    <message>
        <source>number plate detection</source>
        <translation>Plaka algılama</translation>
    </message>
    <message>
        <source>fast background removal</source>
        <translation>Hızlı arka plan kaldırma</translation>
    </message>
    <message>
        <source>detailed background removal</source>
        <translation>Ayrıntılı arka plan kaldırma</translation>
    </message>
    <message>
        <source>The face detection model has not been downloaded yet.</source>
        <translation>Yüz algılama modeli henüz indirilmedi.</translation>
    </message>
    <message>
        <source>The number plate detection model has not been downloaded yet.</source>
        <translation>Plaka algılama modeli henüz indirilmedi.</translation>
    </message>
    <message>
        <source>The video cannot be read.</source>
        <translation>Video okunamıyor.</translation>
    </message>
</context>
<context>
    <name>lumora::speech</name>
    <message>
        <source>Fast (base)</source>
        <translation>Hızlı (base)</translation>
    </message>
    <message>
        <source>Balanced (small)</source>
        <translation>Dengeli (small)</translation>
    </message>
    <message>
        <source>Most accurate (large v3 turbo)</source>
        <translation>En doğru (large v3 turbo)</translation>
    </message>
    <message>
        <source>This build of LumoraStudio has no speech recognition.</source>
        <translation>LumoraStudio&apos;nun bu sürümünde konuşma tanıma yok.</translation>
    </message>
    <message>
        <source>There is nothing to listen to.</source>
        <translation>Dinlenecek bir şey yok.</translation>
    </message>
    <message>
        <source>The speech recognition model is missing.</source>
        <translation>Konuşma tanıma modeli bulunamadı.</translation>
    </message>
    <message>
        <source>The timeline has no sound.</source>
        <translation>Zaman çizelgesinde ses yok.</translation>
    </message>
    <message>
        <source>The speech recognition model cannot be read. Download it again.</source>
        <translation>Konuşma tanıma modeli okunamıyor. Yeniden indirin.</translation>
    </message>
    <message>
        <source>Speech recognition failed.</source>
        <translation>Konuşma tanıma başarısız oldu.</translation>
    </message>
</context>
</TS>
