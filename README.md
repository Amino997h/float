# Float Browser 🌊🍾

**Float** is an ultra-lightweight, high-performance web browser written in native **C++** using the Windows Win32 API and the Microsoft WebView2 COM SDK.

It provides the sleek visual aesthetics and speed of modern browsers (Chrome/Edge), while consuming **only ~70 to ~150 MB of RAM** (compared to ~1,200 MB on Chrome/Edge with identical pages), saving more than **85%** of system resources!

---

## 📥 كيفية التثبيت والتشغيل (How to Install & Run)

### 🟢 1. التشغيل المباشر (للمستخدمين - بدون برمجة أو أدوات)
لا تحتاج إلى تثبيت أي برامج أو أدوات برمجية، فالبرنامج جاهز ومترجم مسبقاً:

1. قم بتحميل المستودع بالضغط على الزر الأخضر **`< > Code`** ثم **Download ZIP** (أو عبر Git):
   ```bash
   git clone https://github.com/Amino997h/float.git
   ```
2. فك ضغط المجلد في أي مكان تريده على جهازك.
3. شغل **`Float.exe`** مباشرة بنقرة مزدوجة!
4. *(اختياري)* انقر بزر الفأرة الأيمن على `Float.exe` واختر **Send to -> Desktop (create shortcut)** لإنشاء اختصار أنيق على سطح المكتب.

---

### 💻 2. البناء والتجميع من المصدر (للمطورين - Build from Source)

إذا كنت ترغب في تعديل الكود البرمجي وإعادة تجميع المتصفح بنفسك:

#### المتطلبات (Prerequisites):
- نظام تشغيل: **Windows 10 / 11** (64-bit).
- مترجم C++: **MinGW-w64** (يدعم C++17 مثل GCC 10 فأحدث).
- **Edge WebView2 Runtime** (مثبت مسبقاً وتلقائياً على كافة أجهزة Windows 10 و 11 الحديثة).

#### خطوات التجميع (Build Steps):
1. افتح موجه الأوامر (CMD) أو PowerShell داخل مجلد المشروع.
2. قم بتشغيل سكريبت البناء:
   ```cmd
   build.bat
   ```
3. سيقوم السكريبت بالآتي تلقائياً:
   - تضمين أيقونة المتصفح وملف الـ Manifest (دعم كامل لشاشات الـ 4K و Per-Monitor DPI V2).
   - تجميع الكود المصدري بأقصى خيارات التحسين السريع (`g++ -O3 -std=c++17 -static`).
   - ربط مكتبة `WebView2Loader.dll` لإنتاج ملف `Float.exe` مستقل تماماً ولا يحتاج إلى تثبيت.

---

## ✨ المميزات الرئيسية (Features)

- **خفة وسرعة استثنائية (Ultra-Lightweight)**: استهلاك RAM يتراوح بين 70MB إلى 150MB مقارنة بـ 1.2GB في المتصفحات الأخرى لنفس المواقع.
- **واجهة عصرية مستوحاة من كروم (Chrome UI)**:
  - أشرطة تبويب دائرية ناعمة وقابلة للسحب والإغلاق والتكرار (Duplicate Tab).
  - قائمة سياق عند النقر بالزر الأيمن على أي رابط: "فتح الرابط في علامة تبويب جديدة" (Open link in new tab).
  - شريط عناوين ذكي وكبسولي (Capsule Omnibox) يدعم التصفح والبحث المباشر.
- **ترجمة فورية بضغطة زر (Instant Translate)**: زر "ترجمة" مدمج في شريط الأدوات لترجمة الصفحة الحالية للغة العربية فورياً عبر Google Translate.
- **صفحة سجل تصفح مخصصة (Float History)**: سجل تصفح سريع وواضح للبحث في المواقع السابقة وإدارتها ومسحها بسهولة.
- **أداة قياس الأداء (Benchmark Script)**: يتضمن ملف `compare_resources.ps1` لمقارنة استهلاك الذاكرة في الوقت الفعلي بين Float و Chrome و Edge و Firefox.

---

## 📂 هيكل المشروع (Project Structure)

```text
├── Float.exe              # البرنامج التنفيذي الجاهز والمترجم
├── main.cpp               # الكود المصدري الأساسي (C++ Win32 & WebView2 Controllers)
├── chrome_ui.html         # الواجهة الرسومية العلوية (شريط التبويبات وأدوات التصفح)
├── history.html           # صفحة سجل التصفح المخصصة (Float History Dashboard)
├── resource.rc            # ملف الموارد (تضمين الأيقونات)
├── manifest.xml           # إعدادات DPI Awareness و Windows Styling
├── app.ico / app.png      # شعار وأيقونة متصفح Float
├── WebView2Loader.dll     # مكتبة ربط محرك WebView2
├── compare_resources.ps1  # سكريبت قياس ومقارنة استهلاك الرام
└── build.bat              # سكريبت البناء والتجميع بنقرة واحدة
```

---

## 📜 الترخيص (License)
هذا المشروع مفتوح المصدر تحت ترخيص [MIT License](LICENSE).
