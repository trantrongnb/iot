# ESP32-S3 hỗ trợ người khiếm thị — gọi trực tiếp OpenRouter

Firmware chính nằm trong **`sketch_oct7a.ino`**; Wi-Fi và API key được lưu riêng trong `secrets.h`, không đưa lên Git. Quy trình: **nhấn nút → chụp JPEG → ESP32 gọi VLM trên OpenRouter → nhận mô tả tiếng Việt → ESP32 gọi TTS trên OpenRouter → nhận PCM → phát loa qua MAX98357A**. Hai API dùng chung key OpenRouter, không cần tự viết máy chủ trung gian.

OpenRouter nhận ảnh qua [Chat Completions với image_url/Base64](https://openrouter.ai/blog/tutorials/send-image-to-llm/) và chuyển văn bản thành giọng nói qua [Text-to-Speech](https://openrouter.ai/docs/guides/overview/multimodal/tts). Đây là hai lần gọi API riêng trong một lần nhấn nút.

## Cấu hình trước khi nạp

Sau khi clone, sao chép `secrets.example.h` thành `secrets.h` trong thư mục gốc và điền Wi-Fi cùng API key. Khi thử camera riêng, sao chép `test_camera/secrets.example.h` thành `test_camera/secrets.h` và điền Wi-Fi. Các file `secrets.h` được `.gitignore` loại khỏi Git. Nếu chưa tạo chúng, sketch dùng cấu hình rỗng trong file mẫu. Các cấu hình model, giọng đọc và phần cứng vẫn nằm ở đầu `.ino`.

| Cấu hình | Ý nghĩa |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | Wi-Fi trong `secrets.h` |
| `OPENROUTER_API_KEY` | API key tài khoản OpenRouter trong `secrets.h` |
| `OPENROUTER_VLM_MODEL` | ID model nhận ảnh; ví dụ `google/gemini-2.5-flash`, hoặc model vision bạn chọn |
| `OPENROUTER_TTS_MODEL` | ID model speech/TTS hỗ trợ tiếng Việt và đầu ra PCM |
| `OPENROUTER_TTS_VOICE` | Tên giọng hợp lệ của đúng model TTS đã chọn |
| `TTS_PCM_SAMPLE_RATE` | Tần số PCM của model TTS; mặc định 24000 Hz, cần đối chiếu model thực tế |
| `TTS_PCM_CHANNELS` | Số kênh PCM; mặc định mono (1), cần đúng với đầu ra model |

Đã chọn VLM `google/gemini-2.5-flash`, TTS `microsoft/mai-voice-2.1-flash`, giọng `vi-VN-Harper:MAI-Voice-2.1-Flash`. Model TTS này trả PCM 24 kHz mono theo [tài liệu OpenRouter](https://openrouter.ai/microsoft/mai-voice-2.1-flash). Key trong `secrets.h` được dùng chung cho hai lần gọi API. Có thể xem model và giọng đọc tại [OpenRouter Models](https://openrouter.ai/models). Chọn VLM có đầu vào image/Base64, và model TTS có đầu ra speech, đọc được tiếng Việt.

Hai URL đã điền sẵn:

```cpp
constexpr char OPENROUTER_VLM_URL[] = "https://openrouter.ai/api/v1/chat/completions";
constexpr char OPENROUTER_TTS_URL[] = "https://openrouter.ai/api/v1/audio/speech";
```

Đã bỏ cấu hình máy chủ trung gian `API_URL`, `DEVICE_TOKEN`, `API_ROOT_CA` và `ALLOW_HTTP_FOR_LOCAL_TEST`. `TLS_ROOT_CA` là cấu hình nâng cao tùy chọn: **để trống** thì chương trình dùng CA bundle tích hợp trong Arduino-ESP32 3.3.12 để xác thực HTTPS. Không cần tự dán CA trong cấu hình thông thường. Thiết bị đồng bộ giờ NTP trước khi gọi HTTPS.

## GPIO và nạp chương trình

Cấu hình chân loa theo dây nối thực tế: **BCLK=41 (tím), WS/LRC=40 (xám), DOUT/DIN=42 (xanh dương)**. Cấu hình này đã cập nhật trong cả sketch chính và `test_loa/test_loa.ino`. MAX98357A và ESP32 dùng chung GND; hai cực loa nối OUT+ và OUT−. Nút HC-6136 đã thử thành công: VCC=3V3, GND chung, OUT=GPIO47; nhấn kéo xuống LOW. Âm lượng mặc định 65%, Serial `+`/`-` chỉnh từng 5%.

Sketch chính đã chọn **CAMERA_PIN_PROFILE=1** với pinout OV3660 bạn đã thử: XCLK=15, SDA=4, SCL=5, D0…D7=11/9/8/10/12/18/17/16, VSYNC=6, HREF=7, PCLK=13; PWDN/RESET=-1. `CAMERA_VFLIP=true` sửa ảnh ngược trên/dưới. Chân I2S 41/40/42 và nút 47 không trùng camera. Profile 0 dành cho cấu hình tay khi đổi bo.

**VLM/TTS cần PSRAM.** Khi chưa nhận PSRAM, camera dùng ảnh QVGA trong RAM nội bộ để kiểm tra; yêu cầu phân tích/đọc sẽ báo cần bật OPI PSRAM.

Trong Arduino IDE:

1. Mở `sketch_oct7a.ino`, dùng **esp32 by Espressif Systems 3.3.12**.
2. Chọn **ESP32S3 Dev Module**, **Flash Size: 16MB**, **PSRAM: OPI PSRAM**.
3. Chọn **Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)**. Bật USB CDC On Boot nếu dùng USB tích hợp; nếu dùng cổng UART qua chip USB, chọn cấu hình Serial phù hợp.
4. Điền Wi-Fi/API key trong `secrets.h` và kiểm tra cấu hình ở đầu `.ino`, Verify rồi Upload qua đúng cổng.
5. Mở Serial Monitor **115200 baud**.

Các thư viện WiFi, HTTPClient, ESP_I2S, esp_camera, cJSON và mbedTLS có trong core nêu trên; không cần thêm file mã hoặc ArduinoJson.

## Thử thiết bị

| Lệnh Serial | Chức năng |
|---|---|
| `h` | Hướng dẫn |
| `s` | Trạng thái |
| `b` | Thử loa bằng bíp, không cần mạng/API |
| `p` | Thử chụp JPEG và in kích thước, cần cấu hình camera |
| `c` | Chụp → OpenRouter VLM → OpenRouter TTS → loa |
| `t` | Thử câu tiếng Việt qua OpenRouter TTS, không gọi VLM |
| `+` / `-` | Tăng/giảm âm lượng 5%, trong khoảng 5–100% |

Nhấn HC-6136 GPIO47 thực hiện cùng luồng với `c`. Nút được chống dội, giữ nút chỉ tạo một yêu cầu. Mỗi lần xử lý một yêu cầu, không xếp hàng các lần nhấn trong lúc chờ API/phát. Khi lỗi, phát ba bíp trầm và ghi nguyên nhân lên Serial; không phát lại mô tả cũ.

Bản sửa Wi-Fi hiện tại in `[FIRMWARE] OV3660-FAST-AUDIO-03` khi khởi động và khi gửi `s`. Sau khi sửa file từ bên ngoài Arduino IDE, hãy mở lại sketch và Upload đúng cổng; xác nhận mã này để tránh thử nhầm firmware cũ.

Wi-Fi được cấu hình một lần khi khởi động. Nếu nhấn nút sớm, chương trình chờ lượt kết nối hiện tại tối đa 20 giây; không gọi lại `WiFi.begin()` khi STA đang kết nối. Khi cần thử lại, dùng cấu hình đã lưu với khoảng cách tối thiểu 30 giây. Nên chờ Serial báo `WiFi=connected` trước khi thử `t` hoặc nhấn nút.

Sau khi Wi-Fi kết nối, Serial in `[WEB] http://<IP>/`. Mở trên máy cùng mạng để chụp ảnh hoặc bật/dừng realtime MJPEG (cổng 81), cùng giao diện đã thử trong sketch camera. Mỗi lần chỉ xem một phiên realtime. Nhấn nút hoặc gửi `c`/`t` sẽ dừng realtime để xử lý; cần bấm **Bật realtime** để xem lại. Trong lúc gọi API/phát, trang điều khiển cổng 80 tạm chờ cho đến khi xử lý xong. Xem log Serial để theo dõi tiến trình. Xem realtime/chụp JPEG không gọi OpenRouter; `c` gọi VLM và TTS, `t` gọi riêng TTS và có thể tính phí theo tài khoản.

Trạng thái chính: `CAPTURING` → `WAITING_VLM` → `WAITING_TTS` → `STREAMING_AUDIO` → `SPEAKING`. Với WAV hoặc tắt streaming, dùng `DOWNLOADING_AUDIO` trước khi phát. Mô tả của VLM được in lên Serial để kiểm tra. API key không được in.

## Định dạng và giới hạn

VLM được gọi bằng JSON chứa model, prompt tiếng Việt và JPEG Base64 trong `image_url.url`. Firmware đọc `choices[0].message.content`, từ chối JSON lỗi, mô tả rỗng, bị cắt bởi giới hạn token hoặc lớn hơn 2048 byte. JPEG tối đa 512 KiB; JSON phản hồi tối đa 32 KiB. Base64 được tạo trong PSRAM, không tạo một chuỗi ảnh lớn trong heap nội bộ.

TTS được gọi bằng JSON chứa `model`, `voice`, `input` và `response_format: "pcm"`. Theo tài liệu OpenRouter, PCM được trả dưới dạng byte âm thanh, không phải JSON. Firmware xử lý PCM signed 16-bit little-endian, đọc `rate` và `channels` từ Content-Type của API, dùng cấu hình 24000 Hz mono khi phản hồi không có thông tin này; nếu nhà cung cấp trả WAV hợp lệ thì đọc thông tin định dạng từ WAV. MP3 và JSON không được phát. Nếu tiếng quá nhanh/chậm, kiểm tra sample rate của model; đổi model TTS thì cần đối chiếu lại định dạng PCM và giọng đọc.

Giới hạn âm thanh 30 giây và 2 MiB; hỗ trợ HTTP Content-Length hoặc chunked. Mặc định `STREAM_TTS_PCM=true`: với Content-Type `audio/pcm`, HTTPClient gỡ khung HTTP chunked và chuyển từng khối PCM sang I2S trong lúc tải, không chờ cả câu. Buffer stereo 1024 frame nằm trên heap; giữ byte lẻ giữa các khối TCP, kiểm tra giới hạn thời lượng và số byte. WAV và application/octet-stream vẫn được tải đầy đủ vào PSRAM rồi phát. Nếu mạng ngắt trong khi stream, có thể đã đọc một phần câu; chương trình dừng và báo lỗi. Đặt `STREAM_TTS_PCM=false` để quay về chế độ tải đủ rồi phát nếu Wi-Fi làm giọng bị gián đoạn. Timeout kết nối 10 giây, chờ đọc HTTP 60 giây; giới hạn tải 90 giây được kiểm tra khi nhận dữ liệu. Đây không phải deadline tuyệt đối cho toàn bộ luồng gồm Wi-Fi, NTP, hai API và phát loa.

## Xác minh

Bản tích hợp OV3660 + nút GPIO47 + I2S + web realtime được kiểm tra biên dịch với Arduino-ESP32 3.3.12 cho S3 N16R8. Camera, realtime và giọng nói ghi sẵn đã được bạn thử thành công qua các sketch riêng. Đã kiểm tra các thao tác giao diện realtime/chụp và cấu hình chân không trùng. Chưa xác minh luồng VLM → TTS hoàn chỉnh trên phần cứng và chưa gọi API suy luận tính phí trong lần chỉnh sửa này. Định dạng PCM có tham số rate/channels được đối chiếu theo [hướng dẫn TTS của OpenRouter](https://openrouter.ai/docs/guides/overview/multimodal/tts).

`sketch_oct7a_beep_original.ino.txt` giữ mã thử loa ban đầu và không tham gia biên dịch.

## Thử riêng camera OV3660

Mở **`test_camera/test_camera.ino`**, điền Wi-Fi trong `test_camera/secrets.h` rồi nạp riêng sketch này. Không cần API, loa hoặc nút nhấn để thử camera.

- Arduino IDE: ESP32S3 Dev Module, Flash 16MB, PSRAM **OPI PSRAM** cho N16R8; cấu hình Serial/USB theo cổng đang dùng.
- Mở Serial Monitor **115200 baud**, chờ `[CAMERA OK]`, `[CHUP OK]` và địa chỉ `http://<IP>/`.
- Mở địa chỉ đó trên máy tính/điện thoại cùng mạng, bấm **Bật realtime** để xem MJPEG liên tục, **Dừng realtime** để dừng. **Chụp ảnh mới** dừng video và lấy ảnh tĩnh. Chỉ mở một phiên realtime mỗi lần. Lệnh Serial `s` in lại IP/trạng thái, `c` thử chụp và in kích thước JPEG.
- Mặc định dùng VGA 640×480 với PSRAM. Khi chưa nhận PSRAM, sketch thử ảnh QVGA 320×240 trong RAM nội bộ và in thông báo; vẫn nên bật đúng PSRAM cho bo N16R8.
- Pinout hiện tại đã được bạn thử thành công với OV3660: XCLK=15, SDA=4, SCL=5, D0…D7=11/9/8/10/12/18/17/16, VSYNC=6, HREF=7, PCLK=13.
- Đã sửa ảnh ngược trên/dưới bằng `CAMERA_VFLIP=true`; `CAMERA_HMIRROR=false`. Nếu đổi cách lắp camera, chỉnh hai giá trị này ở đầu file.
- Realtime chạy bằng HTTP MJPEG trên `http://<IP>:81/stream`, trang điều khiển trên cổng 80. Giới hạn gửi khoảng 10 khung/giây; tốc độ thực tế phụ thuộc camera và Wi-Fi. Vẫn xem được QVGA khi chưa nhận PSRAM. Có mutex bảo vệ buffer khi chụp và stream cùng dùng camera.

Cách gửi MJPEG dựa trên [CameraWebServer của Espressif](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/app_httpd.cpp). Camera đã chụp JPEG thành công theo log bạn gửi; bản sửa chiều ảnh và realtime cần nạp lại để kiểm tra thực tế. Nếu giao diện cũ còn hiện, tải lại trang và bỏ cache. Bản thử camera không thay đổi sketch thử loa/nút đã chạy thành công.

## Thử một câu nói qua loa

Mở và nạp riêng `test_giong_noi/test_giong_noi.ino`. Câu tiếng Việt **“Xin chào. Hệ thống hỗ trợ người khiếm thị đã sẵn sàng.”** dài khoảng 4,4 giây đã được nhúng trong flash của một file `.ino`; không cần Wi-Fi, API TTS hay PSRAM. Giọng mẫu tiếng Việt được tạo trước bằng Google Translate TTS ([gTTS](https://gtts.readthedocs.io/en/latest/)), chuyển sang PCM16 mono 16 kHz rồi phát cùng tín hiệu ở hai kênh I2S. ESP32 phát dữ liệu nhúng sẵn, không gọi dịch vụ này khi chạy.

- Giữ dây loa đã thử được: BCLK=41, WS/LRC=40, DIN=42, MAX98357A VIN=5V và chung GND với ESP32.
- HC-6136 OUT=GPIO47, VCC=3V3, GND=GND. Nhấn nút để phát một lần, nhả rồi nhấn lại.
- Serial Monitor 115200: `p` phát câu nói, `+`/`-` thay đổi âm lượng từng 5%, `s` xem trạng thái. Âm lượng ban đầu 65%.
- Dùng ESP32S3 Dev Module, Flash 16MB và cấu hình USB/Serial theo cổng đang dùng. Sketch không cần PSRAM; không cần cài thêm thư viện ngoài core Arduino-ESP32 3.3.12.

Đây là câu ghi sẵn để kiểm tra chất lượng phát lời nói. Thay câu đọc trong hệ thống VLM sẽ dùng TTS; sửa dòng chữ trong sketch này không thay đổi âm thanh nhúng.

## Giảm thời gian chờ phản hồi

Bản `OV3660-FAST-AUDIO-03` yêu cầu VLM mô tả trong 1–2 câu, tối đa 25 từ, giới hạn đầu ra 192 token và giữ tắt reasoning. Câu mô tả ngắn giúp giảm lượng văn bản cần tổng hợp và thời gian đọc. Model và giọng TTS hiện có được giữ nguyên. PCM được phát trong lúc tải theo [định dạng byte âm thanh của OpenRouter](https://openrouter.ai/docs/guides/overview/multimodal/tts); mức giảm thời gian chờ thực tế còn tùy thời điểm dịch vụ gửi dữ liệu và Wi-Fi.

Serial in `[TIME] capture_encode`, `VLM`, `TTS_headers`, `TTS_first_audio`, `TTS_stream_total` và `request_total` (đơn vị ms). `TTS_first_audio` tính từ khi bắt đầu yêu cầu TTS đến lúc chuẩn bị ghi khối âm thanh đầu tiên; `TTS_stream_total` gồm thời gian tải và phát chồng lên nhau, `request_total` gồm toàn bộ chu trình đến hết âm báo kết thúc. Không cộng các số đo chồng lấp này với nhau. Chưa đo thời gian phản hồi của bản streaming trên phần cứng thực tế.
